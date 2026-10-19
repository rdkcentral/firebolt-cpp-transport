/**
 * Copyright 2026 Comcast Cable Communications Management, LLC
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

#pragma once

#include "firebolt/gateway.h"
#include "firebolt/types.h"
#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace Firebolt::Transport
{

// Keeps the local event subscribers and delivers incoming notifications to them on a dedicated worker thread,
// so a slow or re-entrant callback never blocks the transport's receive path.
class Server
{
public:
    Server() = default;
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb);
    Firebolt::Error unsubscribe(const std::string& event, void* usercb);
    void notify(const std::string& method, const nlohmann::json& parameters);
    bool isAnySubscriber(const std::string& method) const;

    // Stops and joins the worker after it has drained the queued notifications. A later subscribe() restarts it.
    void stopNotificationWorker();

private:
    struct CallbackDataEvent
    {
        std::string eventName;
        EventCallback callback;
        void* usercb;
    };

    struct QueuedNotification
    {
        std::vector<CallbackDataEvent> callbacks;
        nlohmann::json params;
    };

    void ensureNotificationWorkerStarted();
    void processQueuedNotifications();

    std::list<CallbackDataEvent> eventList_;
    mutable std::mutex eventListMutex_;

    std::queue<QueuedNotification> notificationQueue_;
    std::mutex notificationQueueMutex_;
    std::condition_variable notificationQueueCv_;
    std::thread notificationWorkerThread_;
    std::atomic<bool> stopNotificationWorker_{false};
};

} // namespace Firebolt::Transport
