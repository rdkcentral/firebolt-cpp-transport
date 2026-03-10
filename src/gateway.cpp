/**
 * Copyright 2025 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "firebolt/gateway.h"
#include "firebolt/logger.h"
#include "firebolt/transport_version.h"
#include "firebolt/types.h"
#include "transport.h"
#include "utils.h"
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <thread>

namespace Firebolt::Transport
{

// Runtime configuration used by client/server watchdog and provider wait
static unsigned runtime_waitTime_ms = 3000;
static unsigned watchdog_interval_ms = 500;

using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
using MessageID = uint32_t;

IGateway::~IGateway() = default;

class IClientTransport
{
public:
    virtual ~IClientTransport() = default;
    virtual MessageID getNextMessageID() = 0;
    virtual Firebolt::Error send(const std::string& method, const nlohmann::json& parameters, MessageID id) = 0;
};

class Client
{
    struct Caller
    {
        Caller(MessageID id_)
            : id(id_)
        {
        }
        const MessageID id;
        std::promise<Result<nlohmann::json>> promise;
        Timestamp timestamp = std::chrono::steady_clock::now();
    };

    std::map<MessageID, std::shared_ptr<Caller>> queue;
    mutable std::mutex queue_mtx;
    std::set<MessageID> invokes;
    mutable std::mutex invokes_mtx;

    IClientTransport& transport_;

public:
    Client(IClientTransport& transport)
        : transport_(transport)
    {
    }

    void checkPromises()
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        auto now = std::chrono::steady_clock::now();
        for (auto it = queue.begin(); it != queue.end();)
        {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second->timestamp).count() >
                runtime_waitTime_ms)
            {
                FIREBOLT_LOG_WARNING("Gateway", "Request timed out, id: %u", it->second->id);
                it->second->promise.set_value(Result<nlohmann::json>(Firebolt::Error::Timedout));
                it = queue.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    Firebolt::Error send(const std::string& method, const nlohmann::json& parameters)
    {
        MessageID id = transport_.getNextMessageID();
        {
            std::lock_guard lck(invokes_mtx);
            invokes.insert(id);
        }
        return transport_.send(method, parameters, id);
    }

    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters,
                                                std::optional<MessageID> idOpt)
    {
        MessageID id;
        if (idOpt.has_value())
        {
            id = idOpt.value();
        }
        else
        {
            id = transport_.getNextMessageID();
        }
        std::shared_ptr<Caller> c = std::make_shared<Caller>(id);
        auto future = c->promise.get_future();

        {
            std::lock_guard lck(queue_mtx);
            queue[id] = c;
        }

        Firebolt::Error result = transport_.send(method, parameters, id);
        if (result != Firebolt::Error::None)
        {
            c->promise.set_value(Result<nlohmann::json>{result});
            std::lock_guard lck(queue_mtx);
            queue.erase(id);
        }

        return future;
    }

    void response(const nlohmann::json& message)
    {
        MessageID id = message["id"];
        {
            std::lock_guard lck(invokes_mtx);
            if (invokes.find(id) != invokes.end())
            {
                invokes.erase(id);
                return;
            }
        }
        try
        {
            std::shared_ptr<Caller> c;
            std::lock_guard lck(queue_mtx);
            c = queue.at(id);
            queue.erase(id);

            if (!message.contains("error"))
            {
                c->promise.set_value(Result<nlohmann::json>{message["result"]});
            }
            else
            {
                c->promise.set_value(Result<nlohmann::json>{static_cast<Firebolt::Error>(message["error"]["code"])});
            }
        }
        catch (const std::out_of_range& e)
        {
            FIREBOLT_LOG_INFO("Gateway", "No receiver for a message, id: %u", id);
        }
    }
};

class Server
{
    struct CallbackDataEvent
    {
        std::string eventName;
        const EventCallback lambda;
        void* usercb;
    };

    struct QueuedNotification
    {
        std::vector<CallbackDataEvent> callbacks;
        nlohmann::json params;
    };

    using EventList = std::list<CallbackDataEvent>;

    EventList eventList;
    mutable std::mutex eventMap_mtx;

    std::queue<QueuedNotification> notificationQueue_;
    mutable std::mutex notificationQueueMutex_;
    std::condition_variable notificationQueueCv_;
    std::thread notificationWorkerThread_;
    std::atomic<bool> stopNotificationWorker_{false};

public:
    Server() = default;

    virtual ~Server()
    {
        stopNotificationWorker();
        {
            std::lock_guard lck(eventMap_mtx);
            eventList.clear();
        }
    }

    void stopNotificationWorker()
    {
        stopNotificationWorker_ = true;
        notificationQueueCv_.notify_all();

        if (notificationWorkerThread_.joinable())
        {
            notificationWorkerThread_.join();
        }
    }

    void ensureNotificationWorkerStarted()
    {
        if (!notificationWorkerThread_.joinable())
        {
            stopNotificationWorker_ = false;
            notificationWorkerThread_ = std::thread(&Server::processQueuedNotifications, this);
        }
    }

    void processQueuedNotifications()
    {
        while (true)
        {
            QueuedNotification notification;
            {
                std::unique_lock<std::mutex> lock(notificationQueueMutex_);
                notificationQueueCv_.wait(lock,
                                          [this] { return stopNotificationWorker_ || !notificationQueue_.empty(); });

                if (notificationQueue_.empty())
                {
                    if (stopNotificationWorker_)
                    {
                        return;
                    }
                    continue;
                }

                notification = std::move(notificationQueue_.front());
                notificationQueue_.pop();
            }

            for (auto& callback : notification.callbacks)
            {
                callback.lambda(callback.usercb, notification.params);
            }
        }
    }

    Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb)
    {
        ensureNotificationWorkerStarted();

        CallbackDataEvent callbackData = {event, callback, usercb};

        std::lock_guard lck(eventMap_mtx);
        auto eventIt = std::find_if(eventList.begin(), eventList.end(), [&event, usercb](const CallbackDataEvent& e)
                                    { return e.eventName == event && e.usercb == usercb; });

        if (eventIt != eventList.end())
        {
            return Firebolt::Error::General;
        }

        eventList.push_back(callbackData);
        return Firebolt::Error::None;
    }

    Firebolt::Error unsubscribe(const std::string& event, void* usercb)
    {
        std::lock_guard lck(eventMap_mtx);

        auto it = std::find_if(eventList.begin(), eventList.end(), [&event, usercb](const CallbackDataEvent& e)
                               { return e.eventName == event && e.usercb == usercb; });

        if (it == eventList.end())
        {
            return Firebolt::Error::General;
        }

        eventList.erase(it);
        return Firebolt::Error::None;
    }

    void notify(const std::string& method, const nlohmann::json& parameters)
    {
        if (stopNotificationWorker_)
        {
            FIREBOLT_LOG_WARNING("Gateway", "Received a notification while stopping the notification worker. Ignoring");
            return;
        }
        std::string key = method;
        nlohmann::json params;
        if (parameters.size() == 1 && parameters.contains("value"))
        {
            const auto firstValue = parameters.begin().value();
            if (!firstValue.is_object())
            {
                params = firstValue;
            }
            else
            {
                params = parameters;
            }
        }
        else
        {
            params = parameters;
        }

        std::vector<CallbackDataEvent> callbacks;
        {
            std::lock_guard lck(eventMap_mtx);
            for (auto& callback : eventList)
            {
                if (callback.eventName == key)
                {
                    callbacks.push_back(callback);
                }
            }
        }

        if (callbacks.empty())
        {
            FIREBOLT_LOG_WARNING("Gateway", "No subscribers for event: %s", method.c_str());
            return;
        }

        {
            std::lock_guard<std::mutex> lock(notificationQueueMutex_);
            notificationQueue_.push(QueuedNotification{std::move(callbacks), params});
        }
        notificationQueueCv_.notify_one();
    }

    bool isAnySubscriber(const std::string& method)
    {
        std::string key = method;
        size_t dotPos = key.find('.');
        if (dotPos != std::string::npos)
        {
            std::transform(key.begin(), key.begin() + dotPos, key.begin(),
                           [](unsigned char c) { return std::tolower(c); });
        }
        std::lock_guard lck(eventMap_mtx);

        for (auto& callback : eventList)
        {
            if (callback.eventName == key)
            {
                return true;
            }
        }
        return false;
    }
};

class GatewayImpl : public IGateway, private IClientTransport
{
private:
    ConnectionChangeCallback connectionChangeListener;
    Transport transport;
    Client client;
    Server server;
    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning;
    bool legacyRPCv1;

    std::map<MessageID, std::string> rpcv1_eventMap;
    std::mutex rpcv1_eventMap_mtx;

public:
    GatewayImpl()
        : client(*this),
          server(),
          watchdogRunning(false),
          legacyRPCv1(false)
    {
    }

    ~GatewayImpl()
    {
        if (watchdogRunning)
        {
            watchdogRunning = false;
            if (watchdogThread.joinable())
            {
                watchdogThread.join();
            }
        }
    }

    virtual Firebolt::Error connect(const Firebolt::Config& cfg, ConnectionChangeCallback onConnectionChange) override
    {
        assert(onConnectionChange != nullptr);

        Firebolt::Logger::setLogLevel(cfg.log.level);
        Firebolt::Logger::setFormat(cfg.log.format.ts, cfg.log.format.location, cfg.log.format.function,
                                    cfg.log.format.thread);

        connectionChangeListener = onConnectionChange;

        runtime_waitTime_ms = cfg.waitTime_ms;
        legacyRPCv1 = cfg.legacyRPCv1;

        std::string url = buildGatewayUrl(cfg.wsUrl, legacyRPCv1);

        std::optional<unsigned> transportLoggingInclude = cfg.log.transportInclude;
        std::optional<unsigned> transportLoggingExclude = cfg.log.transportExclude;

        FIREBOLT_LOG_NOTICE("Transport", "Version: %s", Version::String);
        if (legacyRPCv1)
        {
            FIREBOLT_LOG_NOTICE("Transport", "Legacy RPCv1");
        }

        FIREBOLT_LOG_NOTICE("Gateway", "Connecting to url = %s", url.c_str());
        Firebolt::Error status = transport.connect(
            url, [this](const nlohmann::json& message) { this->onMessage(message); },
            [this](const bool connected, Firebolt::Error error) { this->onConnectionChange(connected, error); },
            transportLoggingInclude, transportLoggingExclude);

        if (status != Firebolt::Error::None)
        {
            return status;
        }

        if (!watchdogRunning.exchange(true))
        {
            watchdogThread = std::thread(
                [this]()
                {
                    while (watchdogRunning)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(watchdog_interval_ms));
                        client.checkPromises();
                    }
                });
        }

        return status;
    }

    virtual Firebolt::Error disconnect() override
    {
        Firebolt::Error status = transport.disconnect();
        if (status != Firebolt::Error::None)
        {
            return status;
        }
        if (watchdogRunning.exchange(false))
        {
            if (watchdogThread.joinable())
            {
                watchdogThread.join();
            }
        }
        server.stopNotificationWorker();
        return Error::None;
    }

    Firebolt::Error send(const std::string& method, const nlohmann::json& parameters) override
    {
        return client.send(method, parameters);
    }

    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters) override
    {
        return request(method, parameters, std::nullopt);
    }

    Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb) override
    {
        bool alreadySubscribed = server.isAnySubscriber(event);
        Firebolt::Error status = server.subscribe(event, callback, usercb);
        if (status != Firebolt::Error::None)
        {
            return status;
        }

        if (alreadySubscribed)
        {
            return Firebolt::Error::None;
        }

        MessageID id = transport.getNextMessageID();

        if (legacyRPCv1)
        {
            std::lock_guard<std::mutex> lock(rpcv1_eventMap_mtx);
            rpcv1_eventMap[id] = event;
        }

        nlohmann::json params;
        params["listen"] = true;

        auto result = request(event, params, id).get();

        if (!result)
        {
            status = result.error();
        }

        if (status != Firebolt::Error::None)
        {
            server.unsubscribe(event, usercb);
            if (legacyRPCv1)
            {
                std::lock_guard<std::mutex> lock(rpcv1_eventMap_mtx);
                rpcv1_eventMap.erase(id);
            }
        }
        return status;
    }

    Firebolt::Error unsubscribe(const std::string& event, void* usercb) override
    {
        Firebolt::Error status = server.unsubscribe(event, usercb);
        if (status != Firebolt::Error::None)
        {
            return status;
        }

        if (server.isAnySubscriber(event))
        {
            return Firebolt::Error::None;
        }

        if (legacyRPCv1)
        {
            std::lock_guard<std::mutex> lock(rpcv1_eventMap_mtx);
            for (auto it = rpcv1_eventMap.begin(); it != rpcv1_eventMap.end();)
            {
                if (it->second == event)
                {
                    it = rpcv1_eventMap.erase(it);
                    break;
                }
                else
                {
                    ++it;
                }
            }
        }

        nlohmann::json params;
        params["listen"] = false;
        auto result = request(event, params).get();

        if (!result)
        {
            status = result.error();
        }

        return status;
    }

private:
    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters,
                                                std::optional<MessageID> id)
    {
        return client.request(method, parameters, id);
    }

    void onMessage(const nlohmann::json& message)
    {
        if (message.contains("id") && (message.contains("result") || message.contains("error")))
        {
            if (legacyRPCv1)
            {
                if (message.contains("result") && !message["result"].empty() &&
                    (!message["result"].is_object() || !message["result"].contains("listening")))
                {
                    MessageID id = message["id"];
                    std::string eventName;
                    {
                        std::lock_guard<std::mutex> lock(rpcv1_eventMap_mtx);
                        auto it = rpcv1_eventMap.find(id);
                        if (it != rpcv1_eventMap.end())
                        {
                            eventName = it->second;
                        }
                    }
                    if (!eventName.empty())
                    {
                        server.notify(eventName, message["result"]);
                        return;
                    }
                }
            }
            client.response(message);
            return;
        }
        if (message.contains("method"))
        {
            if (message.contains("id"))
            {
                FIREBOLT_LOG_ERROR("Gateway", "Invalid payload received (id is present): %s", message.dump().c_str());
                return;
            }
            else
            {
                if (message.contains("params") == false)
                {
                    FIREBOLT_LOG_ERROR("Gateway", "Invalid notification-payload received: %s", message.dump().c_str());
                    return;
                }
                server.notify(message["method"], message["params"]);
            }
            return;
        }
        FIREBOLT_LOG_ERROR("Gateway", "Invalid payload received: %s", message.dump().c_str());
    }

    void onConnectionChange(const bool connected, Firebolt::Error error) { connectionChangeListener(connected, error); }

    MessageID getNextMessageID() override { return transport.getNextMessageID(); }

    Firebolt::Error send(const std::string& method, const nlohmann::json& parameters, MessageID id) override
    {
        return transport.send(method, parameters, id);
    }
};

IGateway& GetGatewayInstance()
{
    static GatewayImpl instance;
    return instance;
}

} // namespace Firebolt::Transport
