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

#include "gateway_client.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace Firebolt::Transport
{

// RPCv1 has no notification messages: events arrive as responses to the original "listen" request. This maps
// the id of that request back to the event it subscribed to.
class LegacyEventRegistry
{
public:
    void add(MessageID id, const std::string& event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_[id] = event;
    }

    void remove(MessageID id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.erase(id);
    }

    // Forgets the (first) subscription request made for `event`.
    void removeEvent(const std::string& event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it =
            std::find_if(events_.begin(), events_.end(), [&event](const auto& entry) { return entry.second == event; });
        if (it != events_.end())
        {
            events_.erase(it);
        }
    }

    std::optional<std::string> find(MessageID id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = events_.find(id);
        if (it == events_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

private:
    mutable std::mutex mutex_;
    std::map<MessageID, std::string> events_;
};

} // namespace Firebolt::Transport
