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
#include <algorithm>
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
                Firebolt::ErrorInfo errorInfo(static_cast<int32_t>(message["error"]["code"]),
                                              message["error"]["message"].get<std::string>());
                c->promise.set_value(
                    Result<nlohmann::json>{static_cast<Firebolt::Error>(message["error"]["code"]), errorInfo});
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
    std::mutex watchdogMtx;
    std::condition_variable watchdogCv;
    bool legacyRPCv1;

    std::map<MessageID, std::string> rpcv1_eventMap;
    std::mutex rpcv1_eventMap_mtx;

    // Synchronisation for the initial connection attempt (and retries).
    // connectResultMtx / connectResultCv are only used inside connect().
    std::mutex connectResultMtx;
    std::condition_variable connectResultCv;
    bool connectResultReady{false};
    bool connectResultOk{false};
    Firebolt::Error connectResultError{Firebolt::Error::None};

    // Guards connectionChangeListener against concurrent reads (IO thread) and
    // writes (main thread).  Always acquire connectionListenerMtx BEFORE
    // connectResultMtx to avoid inversion.
    std::mutex connectionListenerMtx;

    // Set to true when disconnect() is called so a retry loop in connect()
    // aborts early instead of sleeping out the full reconnect_delay_ms.
    std::atomic<bool> disconnectRequested_{false};

public:
    GatewayImpl()
        : client(*this),
          server(),
          watchdogRunning(false),
          legacyRPCv1(false),
          disconnectRequested_(false)
    {
    }

    ~GatewayImpl()
    {
        if (watchdogRunning)
        {
            watchdogRunning = false;
            watchdogCv.notify_one();
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

        // Signal the retry-loop condvar when each async open/fail arrives.
        // Do NOT forward to onConnectionChange here — connect() fires it once
        // with the final result after the loop, so callers never see partial
        // failure callbacks from intermediate retry attempts.
        ConnectionChangeCallback previousListener;
        // NOTE: the wrapper intentionally does NOT forward events to the
        // previous listener.  Forwarding only during an AlreadyConnected window
        // requires a flag set after transport.connect() returns, but the IO
        // thread can fire onOpen/onFail between client_->connect() being queued
        // and that flag being set — a race that triggers a spurious callback on
        // the old session.  The AlreadyConnected window is a few microseconds
        // (synchronous return) and the listener is restored before connect()
        // returns, so the theoretical one-event loss is preferable to the race.
        {
            std::lock_guard<std::mutex> listenerLock(connectionListenerMtx);
            previousListener = connectionChangeListener;
            connectionChangeListener = [this](bool connected, Firebolt::Error error)
            {
                {
                    std::lock_guard<std::mutex> lk(connectResultMtx);
                    connectResultReady = true;
                    connectResultOk = connected;
                    connectResultError = error;
                }
                connectResultCv.notify_one();
            };
        }

        runtime_waitTime_ms = cfg.waitTime_ms;
        legacyRPCv1 = cfg.legacyRPCv1;
        disconnectRequested_ = false;

        std::string url = buildGatewayUrl(cfg.wsUrl, legacyRPCv1);

        std::optional<unsigned> transportLoggingInclude = cfg.log.transportInclude;
        std::optional<unsigned> transportLoggingExclude = cfg.log.transportExclude;

        FIREBOLT_LOG_NOTICE("Transport", "Version: %s", Version::String);
        if (legacyRPCv1)
        {
            FIREBOLT_LOG_NOTICE("Transport", "Legacy RPCv1");
        }

        // Cap to avoid unsigned overflow when reconnect_max_attempts is very large.
        constexpr unsigned kMaxRetries = 100u;
        const unsigned maxAttempts = 1 + std::min(cfg.reconnect_max_attempts, kMaxRetries);
        Firebolt::Error status = Firebolt::Error::NotConnected;

        for (unsigned attempt = 1; attempt <= maxAttempts; ++attempt)
        {
            if (disconnectRequested_)
            {
                break;
            }

            if (attempt > 1)
            {
                FIREBOLT_LOG_NOTICE("Gateway", "Reconnect attempt %u/%u in %u ms ...", attempt, maxAttempts,
                                    cfg.reconnect_delay_ms);
                // Sleep in reconnect_delay_ms increments so disconnect() can
                // abort the wait early by setting disconnectRequested_.
                constexpr unsigned kSliceMs = 50;
                for (unsigned elapsed = 0; elapsed < cfg.reconnect_delay_ms && !disconnectRequested_; elapsed += kSliceMs)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(std::min(kSliceMs, cfg.reconnect_delay_ms - elapsed)));
                }
                if (disconnectRequested_)
                    break;
            }

            {
                std::lock_guard<std::mutex> lk(connectResultMtx);
                connectResultReady = false;
                connectResultOk = false;
                connectResultError = Firebolt::Error::None;
            }

            FIREBOLT_LOG_NOTICE("Gateway", "Connecting to url = %s (attempt %u/%u)", url.c_str(), attempt, maxAttempts);

            status = transport.connect(
                url, [this](const nlohmann::json& message) { this->onMessage(message); },
                [this](const bool connected, Firebolt::Error error) { this->onConnectionChange(connected, error); },
                transportLoggingInclude, transportLoggingExclude);

            if (status != Firebolt::Error::None)
            {
                // Synchronous error from transport (e.g. bad URL) — no point retrying.
                break;
            }

            // Wait for the async open/fail callback (with a generous ceiling).
            bool attemptReady = false, attemptOk = false;
            Firebolt::Error attemptError = Firebolt::Error::NotConnected;
            {
                const auto kConnectTimeout = std::chrono::milliseconds(cfg.connect_attempt_timeout_ms);
                // Snapshot result fields while the mutex is held: the IO-thread
                // callback can write to these fields after wait_for releases the
                // lock, so reading them outside the scope is a data race.
                std::unique_lock<std::mutex> lk(connectResultMtx);
                connectResultCv.wait_for(lk, kConnectTimeout,
                                         [this] { return connectResultReady || disconnectRequested_.load(); });
                attemptReady = connectResultReady;
                attemptOk = connectResultOk;
                attemptError = connectResultError;
            }

            if (disconnectRequested_)
            {
                // Abort the in-flight transport connection so the socket is
                // cleaned up, and force a failure status so the post-loop code
                // does not falsely report success, install the user listener, or
                // start the watchdog.
                transport.disconnect();
                status = Firebolt::Error::NotConnected;
                break;
            }

            if (attemptOk)
            {
                status = Firebolt::Error::None;
                break;
            }

            // Connection failed or timed out.  Close the in-flight attempt before
            // the next retry iteration so there are no overlapping websocket handles.
            if (!attemptReady)
            {
                FIREBOLT_LOG_WARNING("Gateway", "Connect attempt %u/%u timed out; aborting", attempt, maxAttempts);
                attemptError = Firebolt::Error::NotConnected;
            }
            transport.disconnect();
            status = (attemptError != Firebolt::Error::None) ? attemptError : Firebolt::Error::NotConnected;
        }

        if (status != Firebolt::Error::None)
        {
            if (status == Firebolt::Error::AlreadyConnected)
            {
                // Transport is already connected — restore the pre-existing listener
                // so the live connection is not disrupted, and do NOT emit a false
                // "disconnected" event to the caller.
                std::lock_guard<std::mutex> lk(connectionListenerMtx);
                connectionChangeListener = previousListener;
                return status;
            }
            // Restore the plain user callback so subsequent events (if any) are
            // forwarded directly without the condvar logic.
            {
                std::lock_guard<std::mutex> lk(connectionListenerMtx);
                connectionChangeListener = onConnectionChange;
            }
            // NOTE: this final result callback fires on the connect() calling
            // thread.  Post-connect state-change callbacks (disconnect, watchdog
            // reconnect) fire on the websocketpp IO thread.  Callers must be
            // prepared to receive callbacks on either thread.
            onConnectionChange(false, status);
            return status;
        }

        // Swap the wrapper out for the plain user callback now that we're connected.
        {
            std::lock_guard<std::mutex> lk(connectionListenerMtx);
            connectionChangeListener = onConnectionChange;
        }
        // See NOTE above about callback thread.
        onConnectionChange(true, Firebolt::Error::None);

        if (!watchdogRunning.exchange(true))
        {
            watchdogThread = std::thread(
                [this]()
                {
                    while (watchdogRunning)
                    {
                        std::unique_lock<std::mutex> lk(watchdogMtx);
                        bool timedOut = !watchdogCv.wait_for(lk, std::chrono::milliseconds(watchdog_interval_ms),
                                                             [this] { return !watchdogRunning.load(); });
                        if (!watchdogRunning)
                            break;
                        if (timedOut)
                            client.checkPromises();
                    }
                });
        }

        return status;
    }

    virtual Firebolt::Error disconnect() override
    {
        disconnectRequested_ = true;
        connectResultCv.notify_all(); // wake any in-progress retry wait
        FIREBOLT_LOG_INFO("Gateway", "[shutdown] transport.disconnect() start");
        Firebolt::Error status = transport.disconnect();
        FIREBOLT_LOG_INFO("Gateway", "[shutdown] transport.disconnect() done, status=%d", static_cast<int>(status));
        if (status != Firebolt::Error::None)
        {
            return status;
        }
        if (watchdogRunning.exchange(false))
        {
            watchdogCv.notify_one();
            FIREBOLT_LOG_INFO("Gateway", "[shutdown] waiting for watchdog join...");
            if (watchdogThread.joinable())
            {
                watchdogThread.join();
            }
            FIREBOLT_LOG_INFO("Gateway", "[shutdown] watchdog joined");
        }
        FIREBOLT_LOG_INFO("Gateway", "[shutdown] stopping notification worker...");
        server.stopNotificationWorker();
        FIREBOLT_LOG_INFO("Gateway", "[shutdown] notification worker stopped");
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

        auto future = request(event, params, id);
        constexpr auto kSubscribeAckTimeout = std::chrono::milliseconds(50);
        if (future.wait_for(kSubscribeAckTimeout) != std::future_status::ready)
        {
            status = Firebolt::Error::Timedout;
        }
        else
        {
            auto result = future.get();
            if (!result)
            {
                status = result.error();
            }
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
        FIREBOLT_LOG_DEBUG("Gateway", "Unsubscribe called for event '%s'", event.c_str());
        Firebolt::Error status = server.unsubscribe(event, usercb);
        if (status != Firebolt::Error::None)
        {
            FIREBOLT_LOG_DEBUG("Gateway", "Unsubscribe failed for event '%s'", event.c_str());
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
        FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] sending unsubscribe for '%s', waiting for ACK (waitTime_ms=%u)...",
                           event.c_str(), runtime_waitTime_ms);
        auto t0_unsub = std::chrono::steady_clock::now();
        auto future_unsub = request(event, params);
        constexpr auto kUnsubscribeAckTimeout = std::chrono::milliseconds(50);
        long unsub_ms = 0;
        if (future_unsub.wait_for(kUnsubscribeAckTimeout) == std::future_status::ready)
        {
            unsub_ms = static_cast<long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_unsub).count());
            FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] ACK received after %ld ms", unsub_ms);
            auto result = future_unsub.get();
            if (!result)
            {
                status = result.error();
            }
        }
        else
        {
            unsub_ms = static_cast<long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_unsub).count());
            FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] ACK timed out after %ld ms (server unresponsive)", unsub_ms);
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

    void onConnectionChange(const bool connected, Firebolt::Error error)
    {
        ConnectionChangeCallback cb;
        {
            std::lock_guard<std::mutex> lk(connectionListenerMtx);
            cb = connectionChangeListener;
        }
        cb(connected, error);
    }

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
