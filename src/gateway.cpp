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
#include "ack_observer.h"
#include "active_clock.h"
#include "firebolt/logger.h"
#include "firebolt/transport_version.h"
#include "firebolt/types.h"
#include "gateway_testing.h"
#include "transport.h"
#include "utils.h"
#include <any>
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
#include <vector>

namespace Firebolt::Transport
{

// Runtime configuration used by client/server watchdog and provider wait
static unsigned runtime_waitTime_ms = 3000;
static unsigned watchdog_interval_ms = 500;
constexpr std::chrono::milliseconds kAckTimeout{50};
constexpr std::chrono::milliseconds kAckPollSlice{10};
constexpr const char* kResumeEventName = "<transport.resume>";

using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
using MessageID = uint32_t;

static long long elapsedMs(Timestamp since)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count();
}

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
        Caller(MessageID id_, std::string method_, std::chrono::milliseconds activeAtEnqueue_)
            : id(id_),
              method(std::move(method_)),
              activeAtEnqueue(activeAtEnqueue_)
        {
        }
        const MessageID id;
        const std::string method;
        std::promise<Result<nlohmann::json>> promise;
        const std::chrono::milliseconds activeAtEnqueue;
    };

    std::map<MessageID, std::shared_ptr<Caller>> queue;
    mutable std::mutex queue_mtx;
    std::set<MessageID> invokes;
    mutable std::mutex invokes_mtx;

    IClientTransport& transport_;
    ActiveClock& clock_;

public:
    Client(IClientTransport& transport, ActiveClock& clock)
        : transport_(transport),
          clock_(clock)
    {
    }

    void checkPromises()
    {
        std::vector<std::shared_ptr<Caller>> timedOut;
        std::size_t queueSizeSnapshot = 0;
        {
            std::lock_guard<std::mutex> lock(queue_mtx);
            // Reserve upfront (before any erases) so push_back() cannot
            // reallocate mid-loop. If reserve() throws here, no entries have
            // been erased yet, so all promises remain fulfillable.
            timedOut.reserve(queue.size());
            queueSizeSnapshot = queue.size();
            const std::chrono::milliseconds activeNow = clock_.activeNow();
            const std::chrono::milliseconds graceUntil = clock_.graceUntil();
            const std::chrono::milliseconds waitTime{runtime_waitTime_ms};
            for (auto it = queue.begin(); it != queue.end();)
            {
                // A grace window opened by a resume extends every deadline that would end inside it.
                if (activeNow > std::max(it->second->activeAtEnqueue + waitTime, graceUntil))
                {
                    // Capture the Caller pointer under the lock. timedOut.push_back() may allocate;
                    // if it throws, the entry remains in the queue because we erase only after
                    // the push, so a later watchdog iteration can retry.
                    timedOut.push_back(it->second);
                    it = queue.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        // Log after releasing the lock to avoid blocking I/O under the mutex.
        if (queueSizeSnapshot > 0)
        {
            FIREBOLT_LOG_DEBUG("Gateway", "[watchdog] pending request queue size=%zu", queueSizeSnapshot);
        }
        // Log and fulfill promises outside the critical section to minimize lock
        // contention and avoid blocking other gateway operations.
        for (auto& caller : timedOut)
        {
            FIREBOLT_LOG_WARNING("Gateway", "Request timed out, id=%u method='%s'", caller->id, caller->method.c_str());
            caller->promise.set_value(Result<nlohmann::json>(Firebolt::Error::Timedout));
        }
    }

    void cancelAll()
    {
        // Move all pending callers out of the map under the mutex, then fulfill
        // their promises after releasing the lock.  Fulfilling under the lock can
        // wake waiting threads and adds avoidable contention; it also risks
        // deadlock if a future continuation ever calls back into the gateway.
        std::map<MessageID, std::shared_ptr<Caller>> toCancel;
        {
            std::lock_guard<std::mutex> lck(queue_mtx);
            toCancel = std::move(queue);
        }
        for (auto& [id, caller] : toCancel)
        {
            FIREBOLT_LOG_WARNING("Gateway", "[disconnect] cancelling pending request id=%u method='%s'", id,
                                 caller->method.c_str());
            caller->promise.set_value(Result<nlohmann::json>(Firebolt::Error::NotConnected));
        }
    }

    // Firebolt::Error send(const std::string& method, const nlohmann::json& parameters)
    // {
    //     MessageID id = transport_.getNextMessageID();
    //     {
    //         std::lock_guard lck(invokes_mtx);
    //         invokes.insert(id);
    //     }
    //     FIREBOLT_LOG_DEBUG("Gateway", "[send] upstream produce method='%s' id=%u", method.c_str(), id);
    //     return transport_.send(method, parameters, id);
    // }

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
        std::shared_ptr<Caller> c = std::make_shared<Caller>(id, method, clock_.activeNow());
        auto future = c->promise.get_future();

        std::size_t pendingAfterEnqueue = 0;
        {
            std::lock_guard lck(queue_mtx);
            queue[id] = c;
            pendingAfterEnqueue = queue.size();
        }
        FIREBOLT_LOG_DEBUG("Gateway", "[request] queued method='%s' id=%u, pending=%zu", method.c_str(), id,
                           pendingAfterEnqueue);

        Firebolt::Error result = transport_.send(method, parameters, id);
        if (result != Firebolt::Error::None)
        {
            FIREBOLT_LOG_WARNING("Gateway", "[request] transport send failed for method='%s' id=%u status=%d",
                                 method.c_str(), id, static_cast<int>(result));
            // Erase from queue under the mutex BEFORE fulfilling the promise.
            // cancelAll() moves the entire map out under the lock first, so exactly one
            // of these paths will see this entry:
            //   (a) we erase successfully (count > 0) → we call set_value
            //   (b) cancelAll() already moved it out → erase returns 0 → we skip
            // This prevents double set_value() → std::future_error → std::terminate.
            bool ownedByUs = false;
            {
                std::lock_guard<std::mutex> lck(queue_mtx);
                ownedByUs = queue.erase(id) > 0;
            }
            if (ownedByUs)
            {
                c->promise.set_value(Result<nlohmann::json>{result});
            }
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
                FIREBOLT_LOG_DEBUG("Gateway", "[response] dropping self-originated invoke ack id=%u", id);
                invokes.erase(id);
                return;
            }
        }
        // Grab and erase the caller under the mutex, then fulfill the promise
        // after releasing it.  Holding the mutex across set_value() / logging
        // adds unnecessary contention and risks deadlock if a continuation
        // calls back into the gateway.
        std::shared_ptr<Caller> c;
        size_t remaining = 0;
        {
            std::lock_guard lck(queue_mtx);
            auto it = queue.find(id);
            if (it == queue.end())
            {
                FIREBOLT_LOG_INFO("Gateway", "No receiver for a message, id: %u", id);
                return;
            }
            c = it->second;
            queue.erase(it);
            remaining = queue.size();
        }

        FIREBOLT_LOG_DEBUG("Gateway", "[response] matched id=%u method='%s', remaining_pending=%zu", id,
                           c->method.c_str(), remaining);

        if (!message.contains("error"))
        {
            FIREBOLT_LOG_DEBUG("Gateway", "[response] success id=%u method='%s'", id, c->method.c_str());
            c->promise.set_value(Result<nlohmann::json>{message["result"]});
        }
        else
        {
            // Defensively extract error fields: a malformed gateway payload
            // (missing/wrong-type keys) would otherwise throw and crash the
            // process since the exception would escape the calling thread.
            int32_t code = static_cast<int32_t>(Firebolt::Error::General);
            std::string msg = "unknown error";
            try
            {
                if (message["error"].contains("code") && message["error"]["code"].is_number())
                    code = message["error"]["code"].get<int32_t>();
                if (message["error"].contains("message") && message["error"]["message"].is_string())
                    msg = message["error"]["message"].get<std::string>();
            }
            catch (const std::exception& e)
            {
                FIREBOLT_LOG_WARNING("Gateway", "[response] malformed error payload id=%u: %s", id, e.what());
            }
            Firebolt::ErrorInfo errorInfo(code, msg);
            FIREBOLT_LOG_WARNING("Gateway", "[response] error id=%u method='%s' code=%d message='%s'", id,
                                 c->method.c_str(), errorInfo.error(), errorInfo.message().c_str());
            c->promise.set_value(Result<nlohmann::json>{static_cast<Firebolt::Error>(code), errorInfo});
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
    std::mutex workerLifecycle_mtx_;
    std::mutex workerStop_mtx_;
    bool workerJoining_ = false;

public:
    Server() = default;

#ifdef FIREBOLT_TRANSPORT_TESTING
    bool testStopRequested() const { return stopNotificationWorker_; }
#endif

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
        FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] stop requested");
        std::lock_guard<std::mutex> stopLock(workerStop_mtx_);
        std::thread worker;
        {
            std::lock_guard<std::mutex> lifecycleLock(workerLifecycle_mtx_);
            workerJoining_ = true;
            {
                std::lock_guard<std::mutex> queueLock(notificationQueueMutex_);
                stopNotificationWorker_ = true;
            }
            worker = std::move(notificationWorkerThread_);
        }
        notificationQueueCv_.notify_all();

        // Callbacks may subscribe while draining; never hold their lifecycle lock across join.
        if (worker.joinable())
        {
            worker.join();
            FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] joined");
        }
        {
            std::lock_guard<std::mutex> lifecycleLock(workerLifecycle_mtx_);
            workerJoining_ = false;
        }
    }

    void ensureNotificationWorkerStarted()
    {
        std::lock_guard<std::mutex> lifecycleLock(workerLifecycle_mtx_);
        if (!workerJoining_ && !notificationWorkerThread_.joinable())
        {
            stopNotificationWorker_ = false;
            notificationWorkerThread_ = std::thread(&Server::processQueuedNotifications, this);
            FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] started");
        }
    }

    // Queues an internal (non-JSON-RPC) callback to run on the notification worker.
    void post(const std::string& name, EventCallback lambda)
    {
        ensureNotificationWorkerStarted();
        {
            std::lock_guard<std::mutex> lock(notificationQueueMutex_);
            QueuedNotification item;
            item.callbacks.push_back(CallbackDataEvent{name, std::move(lambda), nullptr});
            notificationQueue_.push(std::move(item));
        }
        notificationQueueCv_.notify_one();
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
                        FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] exiting (stop flag set, queue drained)");
                        return;
                    }
                    continue;
                }

                notification = std::move(notificationQueue_.front());
                notificationQueue_.pop();
                FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] dequeued notification callbacks=%zu remaining=%zu",
                                   notification.callbacks.size(), notificationQueue_.size());
            }

            for (auto& callback : notification.callbacks)
            {
                try
                {
                    callback.lambda(callback.usercb, notification.params);
                }
                catch (const std::bad_any_cast& e)
                {
                    FIREBOLT_LOG_ERROR("Gateway",
                                       "[notification-worker] bad_any_cast dispatching event='%s': %s - "
                                       "notification type does not match the registered callback signature",
                                       callback.eventName.c_str(), e.what());
                }
                catch (const std::exception& e)
                {
                    FIREBOLT_LOG_ERROR("Gateway", "[notification-worker] exception dispatching event='%s': %s",
                                       callback.eventName.c_str(), e.what());
                }
                catch (...)
                {
                    FIREBOLT_LOG_ERROR("Gateway", "[notification-worker] unknown exception dispatching event='%s'",
                                       callback.eventName.c_str());
                }
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
            FIREBOLT_LOG_WARNING("Gateway", "[subscribe] duplicate subscriber ignored for event='%s'", event.c_str());
            return Firebolt::Error::General;
        }

        eventList.push_back(callbackData);
        FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] local subscriber added event='%s', total_subscribers=%zu",
                           event.c_str(), eventList.size());
        return Firebolt::Error::None;
    }

    Firebolt::Error unsubscribe(const std::string& event, void* usercb)
    {
        std::lock_guard lck(eventMap_mtx);

        auto it = std::find_if(eventList.begin(), eventList.end(), [&event, usercb](const CallbackDataEvent& e)
                               { return e.eventName == event && e.usercb == usercb; });

        if (it == eventList.end())
        {
            FIREBOLT_LOG_WARNING("Gateway", "[unsubscribe] local subscriber not found for event='%s'", event.c_str());
            return Firebolt::Error::General;
        }

        eventList.erase(it);
        FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] local subscriber removed event='%s', remaining=%zu", event.c_str(),
                           eventList.size());
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
            FIREBOLT_LOG_DEBUG("Gateway", "[notify] event='%s' queued, queue_depth=%zu", method.c_str(),
                               notificationQueue_.size());
        }
        notificationQueueCv_.notify_one();
    }

    bool isAnySubscriber(const std::string& method)
    {
        std::lock_guard lck(eventMap_mtx);

        for (auto& callback : eventList)
        {
            if (callback.eventName == method)
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
    std::mutex connectionChangeListener_mtx;
    ActiveClock clock_;
    std::mutex resume_mtx_;
    ResumeCallback resumeListener_;
    std::chrono::milliseconds pendingResume_{0};
    bool resumeQueued_ = false;
    Transport transport;
    Client client;
    Server server;
    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning;
    std::condition_variable watchdogCv;
    std::mutex watchdogMtx;
    bool legacyRPCv1;

    std::map<MessageID, std::string> rpcv1_eventMap;
    std::mutex rpcv1_eventMap_mtx;

    std::mutex connectionLog_mtx;
    bool hasLastConnectionLog{false};
    bool lastConnectionState{false};
    bool connectionStarted{false};
    Firebolt::Error lastConnectionError{Firebolt::Error::None};
    std::chrono::steady_clock::time_point lastConnectionLogTs{};
    size_t suppressedConnectionNoticeCount{0};
    std::mutex cleanup_mtx;

public:
    GatewayImpl()
        : client(*this, clock_),
          server(),
          watchdogRunning(false),
          legacyRPCv1(false)
    {
    }

    ~GatewayImpl()
    {
        bool needsDisconnection = false;
        {
            std::lock_guard<std::mutex> lock(connectionLog_mtx);
            needsDisconnection = connectionStarted;
        }
        if (needsDisconnection)
        {
            disconnect();
        }
    }

    virtual Firebolt::Error connect(const Firebolt::Config& cfg, ConnectionChangeCallback onConnectionChange) override
    {
        assert(onConnectionChange != nullptr);

        const Firebolt::LogLevel logLevel = Firebolt::Logger::resolveLogLevelFromEnvironment(cfg.log.level);

        Firebolt::Logger::setLogLevel(logLevel);
        Firebolt::Logger::setFormat(cfg.log.format.ts, cfg.log.format.location, cfg.log.format.function,
                                    cfg.log.format.thread);

        ConnectionChangeCallback previousConnectionChangeListener;
        {
            std::lock_guard<std::mutex> lock(connectionChangeListener_mtx);
            previousConnectionChangeListener = connectionChangeListener;
            connectionChangeListener = std::move(onConnectionChange);
        }

        runtime_waitTime_ms = cfg.waitTime_ms;
        legacyRPCv1 = cfg.legacyRPCv1;
        watchdog_interval_ms = cfg.watchdogCycle_ms;

        const std::chrono::milliseconds gapThreshold =
            ActiveClock::effectiveGapThreshold(cfg.stopGapThreshold_ms, cfg.watchdogCycle_ms);
        if (gapThreshold.count() != static_cast<long long>(cfg.stopGapThreshold_ms))
        {
            FIREBOLT_LOG_WARNING("Gateway", "[connect] stopGapThreshold_ms=%u clamped to %lld (4 x watchdogCycle_ms=%u)",
                                 cfg.stopGapThreshold_ms, static_cast<long long>(gapThreshold.count()),
                                 cfg.watchdogCycle_ms);
        }
        clock_.setGapThreshold(gapThreshold);
        clock_.setResumeGrace(std::chrono::milliseconds(cfg.resumeGrace_ms));
        clock_.reset();

        FIREBOLT_LOG_NOTICE("Gateway", "[connect] config waitTime_ms=%u watchdog_interval_ms=%u headers=%zu",
                            runtime_waitTime_ms, watchdog_interval_ms, cfg.headers.size());
        FIREBOLT_LOG_NOTICE("Gateway", "[connect] log level resolved=%d (cfg=%d), transport masks include=%s exclude=%s",
                            static_cast<int>(logLevel), static_cast<int>(cfg.log.level),
                            cfg.log.transportInclude.has_value() ? "set" : "unset",
                            cfg.log.transportExclude.has_value() ? "set" : "unset");

        std::string url = buildGatewayUrl(cfg.wsUrl, legacyRPCv1);

        std::optional<unsigned> transportLoggingInclude = cfg.log.transportInclude;
        std::optional<unsigned> transportLoggingExclude = cfg.log.transportExclude;

        FIREBOLT_LOG_NOTICE("Transport", "%s", Version::Banner);
        if (legacyRPCv1)
        {
            FIREBOLT_LOG_NOTICE("Transport", "Legacy RPCv1");
        }

        // Redact URL before logging: may contain credentials (userinfo) or tokens (query/fragment).
        std::string safeConnectUrl = url;
        {
            const size_t schemeEnd = safeConnectUrl.find("://");
            if (schemeEnd != std::string::npos)
            {
                const size_t authStart = schemeEnd + 3;
                const size_t authEnd = safeConnectUrl.find_first_of("/?#", authStart);
                const size_t limit = (authEnd != std::string::npos) ? authEnd : safeConnectUrl.size();
                const size_t atPos = safeConnectUrl.find('@', authStart);
                if (atPos != std::string::npos && atPos < limit)
                    safeConnectUrl = safeConnectUrl.substr(0, authStart) + safeConnectUrl.substr(atPos + 1);
            }
            const size_t qPos = safeConnectUrl.find('?');
            if (qPos != std::string::npos)
                safeConnectUrl = safeConnectUrl.substr(0, qPos);
            const size_t fPos = safeConnectUrl.find('#');
            if (fPos != std::string::npos)
                safeConnectUrl = safeConnectUrl.substr(0, fPos);
        }
        FIREBOLT_LOG_NOTICE("Gateway", "Connecting to url = %s", safeConnectUrl.c_str());
        Firebolt::Error status = transport.connect(
            std::move(url), [this](const nlohmann::json& message) { this->onMessage(message); },
            [this](const bool connected, Firebolt::Error error) { this->onConnectionChange(connected, error); },
            transportLoggingInclude, transportLoggingExclude, cfg.headers);

        if (status != Firebolt::Error::None)
        {
            std::lock_guard<std::mutex> lock(connectionChangeListener_mtx);
            connectionChangeListener = std::move(previousConnectionChangeListener);
            FIREBOLT_LOG_ERROR("Gateway", "[connect] transport connect failed status=%d", static_cast<int>(status));
            return status;
        }
        else
        {
            // Same lock as cleanupInternalState() so this doesn't run at the same time as a disconnect
            std::lock_guard<std::mutex> cleanupLock(cleanup_mtx);
            {
                std::lock_guard<std::mutex> connectionLock(connectionLog_mtx);
                connectionStarted = true;
            }

            if (!watchdogRunning.exchange(true))
            {
                FIREBOLT_LOG_DEBUG("Gateway", "[watchdog] starting thread (interval=%u ms)", watchdog_interval_ms);
                watchdogThread = std::thread(
                    [this]()
                    {
                        std::unique_lock<std::mutex> lock(watchdogMtx);
                        while (watchdogRunning)
                        {
                            if (watchdogCv.wait_for(lock, std::chrono::milliseconds(watchdog_interval_ms),
                                                    [this] { return !watchdogRunning; }))
                            {
                                break;
                            }
                            lock.unlock();
                            watchdogTick();
                            lock.lock();
                        }
                    });
                FIREBOLT_LOG_DEBUG("Gateway", "[watchdog] thread started");
            }
        }

        return status;
    }

    virtual Firebolt::Error disconnect() override
    {
        FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] transport.disconnect() start");
        auto t0_disc = std::chrono::steady_clock::now();
        Firebolt::Error status = transport.disconnect();
        FIREBOLT_LOG_INFO("Gateway", "[disconnect] transport.disconnect() done in %lld ms, status=%d",
                          static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     std::chrono::steady_clock::now() - t0_disc)
                                                     .count()),
                          static_cast<int>(status));
        if (status != Firebolt::Error::None)
        {
            return status;
        }
        cleanupInternalState();
        return Error::None;
    }

    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters) override
    {
        FIREBOLT_LOG_DEBUG("Gateway", "[request] request from helper layer method='%s'", method.c_str());
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
            FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] upstream subscribe skipped; already subscribed event='%s'",
                               event.c_str());
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

        FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] waiting for subscribe ACK for '%s'...", event.c_str());
        auto t0_sub = std::chrono::steady_clock::now();
        auto future_sub = request(event, params, id);
        if (!waitForAck(future_sub, id, true))
        {
            FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] ACK for '%s' timed out after %lld ms", event.c_str(),
                               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now() - t0_sub)
                                                          .count()));
            status = Firebolt::Error::Timedout;
        }
        else
        {
            FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] ACK for '%s' received in %lld ms", event.c_str(),
                               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now() - t0_sub)
                                                          .count()));
            auto result = future_sub.get();
            if (!result)
            {
                status = result.error();
            }
        }

        if (status != Firebolt::Error::None)
        {
            FIREBOLT_LOG_WARNING("Gateway", "[subscribe] failed for event='%s' status=%d", event.c_str(),
                                 static_cast<int>(status));
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
            FIREBOLT_LOG_DEBUG("Gateway",
                               "[unsubscribe] retaining upstream subscription for event='%s' (other listeners remain)",
                               event.c_str());
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
        const MessageID id = transport.getNextMessageID();
        auto future_unsub = request(event, params, id);
        long long unsub_ms = 0;
        if (waitForAck(future_unsub, id, false))
        {
            unsub_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_unsub).count();
            FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] ACK received after %lld ms", unsub_ms);
            auto result = future_unsub.get();
            if (!result)
            {
                status = result.error();
            }
        }
        else
        {
            unsub_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_unsub).count();
            FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] ACK timed out after %lld ms (server unresponsive)", unsub_ms);
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
            std::string responseIdLog;
            if (message["id"].is_number_unsigned())
            {
                responseIdLog = std::to_string(message["id"].get<MessageID>());
            }
            else if (message["id"].is_number_integer())
            {
                responseIdLog = std::to_string(message["id"].get<long long>());
            }
            else
            {
                responseIdLog = message["id"].dump();
            }
            FIREBOLT_LOG_DEBUG("Gateway", "[onMessage] classified as response id=%s", responseIdLog.c_str());

            if (legacyRPCv1)
            {
                if (message.contains("result") && !message["result"].empty() &&
                    (!message["result"].is_object() || !message["result"].contains("listening")))
                {
                    std::optional<MessageID> id;
                    if (message["id"].is_number_unsigned())
                    {
                        id = message["id"].get<MessageID>();
                    }
                    else if (message["id"].is_number_integer() && (message["id"].get<long long>() >= 0))
                    {
                        id = static_cast<MessageID>(message["id"].get<long long>());
                    }

                    std::string eventName;
                    if (id.has_value())
                    {
                        std::lock_guard<std::mutex> lock(rpcv1_eventMap_mtx);
                        auto it = rpcv1_eventMap.find(id.value());
                        if (it != rpcv1_eventMap.end())
                        {
                            eventName = it->second;
                        }
                    }
                    if (!eventName.empty())
                    {
                        FIREBOLT_LOG_DEBUG("Gateway", "[onMessage] legacy response mapped to event='%s'",
                                           eventName.c_str());
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
                FIREBOLT_LOG_DEBUG("Gateway", "[onMessage] classified as notification method='%s'",
                                   message["method"].get<std::string>().c_str());
                server.notify(message["method"], message["params"]);
            }
            return;
        }
        FIREBOLT_LOG_ERROR("Gateway", "Invalid payload received: %s", message.dump().c_str());
    }

    void onConnectionChange(const bool connected, Firebolt::Error error)
    {
        constexpr auto kConnectionNoticeMinInterval = std::chrono::seconds(5);
        const auto now = std::chrono::steady_clock::now();

        bool emitNotice = false;
        size_t suppressedCount = 0;
        {
            std::lock_guard<std::mutex> lock(connectionLog_mtx);
            const bool sameState = hasLastConnectionLog && (connected == lastConnectionState) &&
                                   (error == lastConnectionError);
            const bool windowElapsed = hasLastConnectionLog &&
                                       ((now - lastConnectionLogTs) >= kConnectionNoticeMinInterval);

            emitNotice = !sameState || !hasLastConnectionLog || windowElapsed;
            if (emitNotice)
            {
                suppressedCount = suppressedConnectionNoticeCount;
                suppressedConnectionNoticeCount = 0;
                hasLastConnectionLog = true;
                lastConnectionState = connected;
                lastConnectionError = error;
                lastConnectionLogTs = now;
            }
            else
            {
                ++suppressedConnectionNoticeCount;
            }
        }

        // Disconnection can also happen from the server, it's necessary to cleanup if this ever happens.
        if (!connected)
        {
            cleanupInternalState();
        }

        if (emitNotice)
        {
            FIREBOLT_LOG_NOTICE("Gateway", "[connection] state=%s error=%d suppressed_repeats=%zu",
                                connected ? "connected" : "disconnected", static_cast<int>(error), suppressedCount);
        }
        ConnectionChangeCallback listener;
        {
            std::lock_guard<std::mutex> lock(connectionChangeListener_mtx);
            listener = connectionChangeListener;
        }

        if (listener)
        {
            listener(connected, error);
        }
        else
        {
            FIREBOLT_LOG_WARNING("Gateway", "[connection] no connectionChangeListener installed");
        }
    }

    MessageID getNextMessageID() override { return transport.getNextMessageID(); }

    Firebolt::Error send(const std::string& method, const nlohmann::json& parameters, MessageID id) override
    {
        FIREBOLT_LOG_DEBUG("Gateway", "[transport-send] method='%s' id=%u", method.c_str(), id);
        return transport.send(method, parameters, id);
    }

    std::optional<std::string> getResponseHeader(const std::string& headerName) override
    {
        return transport.getResponseHeader(headerName);
    }

public:
    void setResumeListener(ResumeCallback listener) override
    {
        std::lock_guard<std::mutex> lock(resume_mtx_);
        resumeListener_ = std::move(listener);
    }

#ifdef FIREBOLT_TRANSPORT_TESTING
    void testSetTimeSource(TimeSource source) { clock_.setTimeSource(std::move(source)); }
    void testWatchdogTick() { watchdogTick(); }
    std::chrono::milliseconds testGapThreshold() const { return clock_.gapThreshold(); }
    void testStopNotificationWorker() { server.stopNotificationWorker(); }
    bool testNotificationWorkerStopRequested() const { return server.testStopRequested(); }
#endif

private:
    // Waits for a subscribe/unsubscribe acknowledgement, counting only active time so a
    // process stop during the wait does not consume the budget.
    bool waitForAck(std::future<Firebolt::Result<nlohmann::json>>& future, [[maybe_unused]] MessageID id,
                    [[maybe_unused]] bool listen)
    {
#ifdef FIREBOLT_TRANSPORT_ACK_OBSERVER
        const int observer = Testing::ackObserverFd();
        const auto startedNs = Testing::ackObserverNow();
#endif
        const std::chrono::milliseconds operationBudget = listen ? std::chrono::milliseconds(runtime_waitTime_ms)
                                                                 : kAckTimeout;
        const std::chrono::milliseconds start = clock_.activeNow();
        const std::chrono::milliseconds deadline = start + operationBudget;
#ifdef FIREBOLT_TRANSPORT_ACK_OBSERVER
        const std::chrono::milliseconds observedBudget = std::max(operationBudget, clock_.graceUntil() - start);
        if (observer >= 0)
        {
            Testing::observeAck(observer, "entered", id, listen, observedBudget.count(), startedNs, false);
        }
#endif
        while (future.wait_for(kAckPollSlice) != std::future_status::ready)
        {
            const std::chrono::milliseconds activeNow = clock_.activeNow();
            if (activeNow >= std::max(deadline, clock_.graceUntil()))
            {
                const bool ready = future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
#ifdef FIREBOLT_TRANSPORT_ACK_OBSERVER
                if (observer >= 0)
                {
                    Testing::observeAck(observer, "exited", id, listen, observedBudget.count(), startedNs, ready);
                }
#endif
                return ready;
            }
        }
#ifdef FIREBOLT_TRANSPORT_ACK_OBSERVER
        if (observer >= 0)
        {
            Testing::observeAck(observer, "exited", id, listen, observedBudget.count(), startedNs, true);
        }
#endif
        return true;
    }

    // Accumulates stopped time and queues at most one resume dispatch; further gaps detected
    // before it runs are coalesced into it.
    void dispatchResumeIfStopped()
    {
        std::chrono::milliseconds stopped{0};
        bool shouldPost = false;
        {
            std::lock_guard<std::mutex> lock(resume_mtx_);
            stopped = clock_.takeStoppedTime();
            if (stopped <= std::chrono::milliseconds(0))
            {
                return;
            }
            pendingResume_ += stopped;
            if (!resumeQueued_ && resumeListener_)
            {
                resumeQueued_ = true;
                shouldPost = true;
            }
        }
        FIREBOLT_LOG_NOTICE("Gateway", "[resume] process stop detected, stoppedFor=%lld ms",
                            static_cast<long long>(stopped.count()));
        if (shouldPost)
        {
            server.post(kResumeEventName,
                        [this](void* /*usercb*/, const nlohmann::json& /*params*/) { deliverResume(); });
        }
    }

    void deliverResume()
    {
        ResumeCallback listener;
        std::chrono::milliseconds stoppedFor{0};
        {
            std::lock_guard<std::mutex> lock(resume_mtx_);
            pendingResume_ += clock_.takeStoppedTime();
            listener = resumeListener_;
            stoppedFor = pendingResume_;
            pendingResume_ = std::chrono::milliseconds(0);
            resumeQueued_ = false;
        }
        if (listener && stoppedFor > std::chrono::milliseconds(0))
        {
            FIREBOLT_LOG_DEBUG("Gateway", "[resume] invoking listener, stoppedFor=%lld ms",
                               static_cast<long long>(stoppedFor.count()));
            listener(stoppedFor);
        }
    }

    void watchdogTick()
    {
        try
        {
            client.checkPromises();
            dispatchResumeIfStopped();
        }
        catch (const std::exception& e)
        {
            FIREBOLT_LOG_ERROR("Gateway", "[watchdog] checkPromises() threw: %s", e.what());
        }
        catch (...)
        {
            FIREBOLT_LOG_ERROR("Gateway", "[watchdog] checkPromises() threw unknown exception");
        }
    }

    void cleanupInternalState()
    {
        std::lock_guard<std::mutex> lock(cleanup_mtx);
        if (watchdogRunning.exchange(false))
        {
            watchdogCv.notify_all();
            FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] waiting for watchdog thread join...");
            auto t0_wdog = std::chrono::steady_clock::now();
            if (watchdogThread.joinable())
            {
                watchdogThread.join();
            }
            FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] watchdog joined in %lld ms",
                               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now() - t0_wdog)
                                                          .count()));
        }
        client.cancelAll();
        FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] stopping notification worker...");
        auto t0_nw = std::chrono::steady_clock::now();
        server.stopNotificationWorker();
        FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] notification worker stopped in %lld ms",
                           static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now() - t0_nw)
                                                      .count()));
        {
            std::lock_guard<std::mutex> lock(connectionLog_mtx);
            connectionStarted = false;
        }
    }
};

IGateway& GetGatewayInstance()
{
    static GatewayImpl instance;
    return instance;
}

#ifdef FIREBOLT_TRANSPORT_TESTING
void testSetGatewayTimeSource(TimeSource source)
{
    static_cast<GatewayImpl&>(GetGatewayInstance()).testSetTimeSource(std::move(source));
}

void testRunGatewayWatchdogTick()
{
    static_cast<GatewayImpl&>(GetGatewayInstance()).testWatchdogTick();
}

std::chrono::milliseconds testGatewayGapThreshold()
{
    return static_cast<GatewayImpl&>(GetGatewayInstance()).testGapThreshold();
}

void testStopGatewayNotificationWorker()
{
    static_cast<GatewayImpl&>(GetGatewayInstance()).testStopNotificationWorker();
}

bool testGatewayNotificationWorkerStopRequested()
{
    return static_cast<GatewayImpl&>(GetGatewayInstance()).testNotificationWorkerStopRequested();
}
#endif

} // namespace Firebolt::Transport
