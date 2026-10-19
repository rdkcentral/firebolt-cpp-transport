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

#include "firebolt/types.h"
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace Firebolt::Transport
{

// Rate-limits the "connection state changed" notice: a repeat of the same state is only reported again after
// `minInterval`, together with how many repeats were swallowed in between.
class ConnectionNoticeThrottle
{
public:
    using Clock = std::chrono::steady_clock;

    explicit ConnectionNoticeThrottle(Clock::duration minInterval = std::chrono::seconds(5))
        : minInterval_(minInterval)
    {
    }

    // Returns the number of suppressed repeats if this change should be logged, nullopt if it should not.
    std::optional<std::size_t> record(bool connected, Firebolt::Error error, Clock::time_point now = Clock::now())
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool emit = !hasLast_ || connected != lastConnected_ || error != lastError_ ||
                          (now - lastTimestamp_) >= minInterval_;
        if (!emit)
        {
            ++suppressed_;
            return std::nullopt;
        }
        hasLast_ = true;
        lastConnected_ = connected;
        lastError_ = error;
        lastTimestamp_ = now;
        return std::exchange(suppressed_, 0);
    }

private:
    const Clock::duration minInterval_;
    std::mutex mutex_;
    bool hasLast_{false};
    bool lastConnected_{false};
    Firebolt::Error lastError_{Firebolt::Error::None};
    Clock::time_point lastTimestamp_{};
    std::size_t suppressed_{0};
};

} // namespace Firebolt::Transport
