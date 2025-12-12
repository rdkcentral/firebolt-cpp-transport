/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 Sky UK
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
 */

#include "firebolt/gateway.h"
#include "firebolt/logger.h"
#include "firebolt/transport_version.h"
#include "firebolt/types.h"
#include "transport.h"
#include <assert.h>
#include <chrono>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>

namespace Firebolt::Transport
{

// Runtime configuration used by client/server watchdog and provider wait
static unsigned runtime_waitTime_ms = 3000;

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

    Firebolt::Error send(const std::string& method, const nlohmann::json& parameters)
    {
        MessageID id = transport_.getNextMessageID();
        {
            std::lock_guard lck(invokes_mtx);
            invokes.insert(id);
        }
        return transport_.send(method, parameters, id);
    }

    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters)
    {
        MessageID id = transport_.getNextMessageID();
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

    using EventList = std::list<CallbackDataEvent>;

    EventList eventList;
    mutable std::mutex eventMap_mtx;

public:
    virtual ~Server()
    {
        {
            std::lock_guard lck(eventMap_mtx);
            eventList.clear();
        }
    }

    Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb)
    {
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

        std::lock_guard lck(eventMap_mtx);
        for (auto& callback : eventList)
        {
            if (callback.eventName == key)
            {
                callback.lambda(callback.usercb, params);
            }
        }
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

public:
    GatewayImpl()
        : client(*this),
          server()
    {
    }

    ~GatewayImpl() = default;

    virtual Firebolt::Error connect(const Firebolt::Config& cfg, ConnectionChangeCallback onConnectionChange) override
    {
        assert(onConnectionChange != nullptr);

        Firebolt::Logger::setLogLevel(cfg.log.level);
        Firebolt::Logger::setFormat(cfg.log.format.ts, cfg.log.format.location, cfg.log.format.function,
                                    cfg.log.format.thread);

        connectionChangeListener = onConnectionChange;

        runtime_waitTime_ms = cfg.waitTime_ms;
        std::string url = cfg.wsUrl;
        if (url.find("?") == std::string::npos)
        {
            url += "?";
        }
        else
        {
            url += "&";
        }
        url += "RPCv2=true";

        std::optional<unsigned> transportLoggingInclude = cfg.log.transportInclude;
        std::optional<unsigned> transportLoggingExclude = cfg.log.transportExclude;

        FIREBOLT_LOG_NOTICE("Transport", "Version: %s", Version::String);
        FIREBOLT_LOG_INFO("Gateway", "Connecting to url = %s", url.c_str());
        Firebolt::Error status = transport.connect(
            url, [this](const nlohmann::json& message) { this->onMessage(message); },
            [this](const bool connected, Firebolt::Error error) { this->onConnectionChange(connected, error); },
            transportLoggingInclude, transportLoggingExclude);

        if (status != Firebolt::Error::None)
        {
            return status;
        }

        return status;
    }

    virtual Firebolt::Error disconnect() override { return transport.disconnect(); }

    Firebolt::Error send(const std::string& method, const nlohmann::json& parameters) override
    {
        return client.send(method, parameters);
    }

    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters) override
    {
        auto future = client.request(method, parameters);

        if (future.wait_for(std::chrono::milliseconds(runtime_waitTime_ms)) == std::future_status::timeout)
        {
            FIREBOLT_LOG_WARNING("Gateway", "Request timed out, method: %s", method.c_str());
            std::promise<Result<nlohmann::json>> p;
            p.set_value(Result<nlohmann::json>{Error::Timedout});
            return p.get_future();
        }
        return future;
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

        nlohmann::json params;
        params["listen"] = true;
        auto future = request(event, params);
        auto result = future.get();

        if (!result)
        {
            status = result.error();
        }

        if (status != Firebolt::Error::None)
        {
            server.unsubscribe(event, usercb);
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

        nlohmann::json params;
        params["listen"] = false;
        auto future = request(event, params);
        auto result = future.get();

        if (!result)
        {
            status = result.error();
        }

        return status;
    }

private:
    void onMessage(const nlohmann::json& message)
    {
        if (message.contains("id") && (message.contains("result") || message.contains("error")))
        {
            client.response(message);
            return;
        }
        if (message.contains("method"))
        {
            if (message.contains("id"))
            {
                FIREBOLT_LOG_ERROR("Gateway", "Invalid payload received (id is present): %s", message.dump().c_str());
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
