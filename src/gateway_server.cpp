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

#include "gateway_server.h"
#include "firebolt/logger.h"
#include <algorithm>
#include <any>
#include <cctype>
#include <iterator>

namespace Firebolt::Transport
{

namespace
{
// A notification of the form {"value": <non-object>} is delivered to subscribers as the bare value; anything
// else (including {"value": {...}}) is delivered unchanged.
nlohmann::json unwrapNotificationParams(const nlohmann::json& parameters)
{
    if (parameters.size() == 1 && parameters.contains("value") && !parameters.at("value").is_object())
    {
        return parameters.at("value");
    }
    return parameters;
}
} // namespace

Server::~Server()
{
    stopNotificationWorker();
}

void Server::stopNotificationWorker()
{
    FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] stop requested");
    stopNotificationWorker_ = true;
    notificationQueueCv_.notify_all();

    if (notificationWorkerThread_.joinable())
    {
        notificationWorkerThread_.join();
        FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] joined");
    }
}

void Server::ensureNotificationWorkerStarted()
{
    if (!notificationWorkerThread_.joinable())
    {
        stopNotificationWorker_ = false;
        notificationWorkerThread_ = std::thread(&Server::processQueuedNotifications, this);
        FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] started");
    }
}

void Server::processQueuedNotifications()
{
    while (true)
    {
        QueuedNotification notification;
        {
            std::unique_lock<std::mutex> lock(notificationQueueMutex_);
            notificationQueueCv_.wait(lock, [this] { return stopNotificationWorker_ || !notificationQueue_.empty(); });

            // The wait only returns with an empty queue when a stop was requested, so the worker exits
            // once everything queued before the stop has been delivered.
            if (notificationQueue_.empty())
            {
                FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] exiting (stop flag set, queue drained)");
                return;
            }

            notification = std::move(notificationQueue_.front());
            notificationQueue_.pop();
            FIREBOLT_LOG_DEBUG("Gateway", "[notification-worker] dequeued notification callbacks=%zu remaining=%zu",
                               notification.callbacks.size(), notificationQueue_.size());
        }

        for (auto& subscriber : notification.callbacks)
        {
            try
            {
                subscriber.callback(subscriber.usercb, notification.params);
            }
            catch (const std::bad_any_cast& e)
            {
                FIREBOLT_LOG_ERROR("Gateway",
                                   "[notification-worker] bad_any_cast dispatching event='%s': %s - "
                                   "notification type does not match the registered callback signature",
                                   subscriber.eventName.c_str(), e.what());
            }
            catch (const std::exception& e)
            {
                FIREBOLT_LOG_ERROR("Gateway", "[notification-worker] exception dispatching event='%s': %s",
                                   subscriber.eventName.c_str(), e.what());
            }
            catch (...)
            {
                FIREBOLT_LOG_ERROR("Gateway", "[notification-worker] unknown exception dispatching event='%s'",
                                   subscriber.eventName.c_str());
            }
        }
    }
}

Firebolt::Error Server::subscribe(const std::string& event, EventCallback callback, void* usercb)
{
    ensureNotificationWorkerStarted();

    std::lock_guard<std::mutex> lock(eventListMutex_);
    const bool duplicate = std::any_of(eventList_.begin(), eventList_.end(), [&](const CallbackDataEvent& e)
                                       { return e.eventName == event && e.usercb == usercb; });
    if (duplicate)
    {
        FIREBOLT_LOG_WARNING("Gateway", "[subscribe] duplicate subscriber ignored for event='%s'", event.c_str());
        return Firebolt::Error::General;
    }

    eventList_.push_back({event, std::move(callback), usercb});
    FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] local subscriber added event='%s', total_subscribers=%zu", event.c_str(),
                       eventList_.size());
    return Firebolt::Error::None;
}

Firebolt::Error Server::unsubscribe(const std::string& event, void* usercb)
{
    std::lock_guard<std::mutex> lock(eventListMutex_);
    auto it = std::find_if(eventList_.begin(), eventList_.end(),
                           [&](const CallbackDataEvent& e) { return e.eventName == event && e.usercb == usercb; });
    if (it == eventList_.end())
    {
        FIREBOLT_LOG_WARNING("Gateway", "[unsubscribe] local subscriber not found for event='%s'", event.c_str());
        return Firebolt::Error::General;
    }

    eventList_.erase(it);
    FIREBOLT_LOG_DEBUG("Gateway", "[unsubscribe] local subscriber removed event='%s', remaining=%zu", event.c_str(),
                       eventList_.size());
    return Firebolt::Error::None;
}

void Server::notify(const std::string& method, const nlohmann::json& parameters)
{
    if (stopNotificationWorker_)
    {
        FIREBOLT_LOG_WARNING("Gateway", "Received a notification while stopping the notification worker. Ignoring");
        return;
    }

    // Snapshot the matching subscribers so the callbacks run without holding eventListMutex_.
    std::vector<CallbackDataEvent> callbacks;
    {
        std::lock_guard<std::mutex> lock(eventListMutex_);
        std::copy_if(eventList_.begin(), eventList_.end(), std::back_inserter(callbacks),
                     [&method](const CallbackDataEvent& e) { return e.eventName == method; });
    }

    if (callbacks.empty())
    {
        FIREBOLT_LOG_WARNING("Gateway", "No subscribers for event: %s", method.c_str());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(notificationQueueMutex_);
        notificationQueue_.push(QueuedNotification{std::move(callbacks), unwrapNotificationParams(parameters)});
        FIREBOLT_LOG_DEBUG("Gateway", "[notify] event='%s' queued, queue_depth=%zu", method.c_str(),
                           notificationQueue_.size());
    }
    notificationQueueCv_.notify_one();
}

bool Server::isAnySubscriber(const std::string& method) const
{
    // The module part of "Module.event" (before the first '.') is matched case-insensitively.
    std::string key = method;
    const size_t dotPos = key.find('.');
    if (dotPos != std::string::npos)
    {
        std::transform(key.begin(), key.begin() + dotPos, key.begin(), [](unsigned char c) { return std::tolower(c); });
    }

    std::lock_guard<std::mutex> lock(eventListMutex_);
    return std::any_of(eventList_.begin(), eventList_.end(),
                       [&key](const CallbackDataEvent& e) { return e.eventName == key; });
}

} // namespace Firebolt::Transport
