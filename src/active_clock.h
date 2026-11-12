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

#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <utility>

namespace Firebolt::Transport
{

using TimePoint = std::chrono::steady_clock::time_point;
using TimeSource = std::function<TimePoint()>;

/**
 * @brief Monotonic clock that separates active time from stopped time.
 *
 * Each sample() compares the time source with the previous sample. An interval
 * up to the gap threshold counts as active time; a longer interval means the
 * process was not running (SIGSTOP, cgroup freeze, memcr) and is accumulated as
 * stopped time instead. Timeouts are measured against activeNow().
 *
 * The time source is injectable so tests can simulate a process stop
 * deterministically. A null source selects std::chrono::steady_clock.
 */
class ActiveClock
{
public:
    static constexpr unsigned kWatchdogCyclesPerGap = 4;

    explicit ActiveClock(TimeSource source = nullptr) { setTimeSource(std::move(source)); }

    ActiveClock(const ActiveClock&) = delete;
    ActiveClock& operator=(const ActiveClock&) = delete;
    ActiveClock(ActiveClock&&) = delete;
    ActiveClock& operator=(ActiveClock&&) = delete;

    // max(configured, 4 x watchdog cycle): a gap must clearly exceed scheduling jitter.
    static std::chrono::milliseconds effectiveGapThreshold(unsigned stopGapThreshold_ms, unsigned watchdogCycle_ms)
    {
        const unsigned long long floor = static_cast<unsigned long long>(watchdogCycle_ms) * kWatchdogCyclesPerGap;
        const unsigned long long effective = std::max<unsigned long long>(stopGapThreshold_ms, floor);
        return std::chrono::milliseconds(effective);
    }

    void setTimeSource(TimeSource source)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        source_ = source ? std::move(source) : TimeSource(&std::chrono::steady_clock::now);
        last_ = source_();
        pendingStopped_ = Duration::zero();
    }

    void setGapThreshold(std::chrono::milliseconds threshold)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        gapThreshold_ = threshold;
    }

    std::chrono::milliseconds gapThreshold() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return gapThreshold_;
    }

    void setResumeGrace(std::chrono::milliseconds grace)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        resumeGrace_ = grace;
    }

    // Active-time point at which the current post-resume grace window ends; 0 if none was opened.
    std::chrono::milliseconds graceUntil() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return std::chrono::duration_cast<std::chrono::milliseconds>(graceUntil_);
    }

    // Samples, then returns the active time left in the grace window (0 outside it).
    std::chrono::milliseconds graceRemaining()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sampleLocked();
        if (graceUntil_ <= active_)
        {
            return std::chrono::milliseconds(0);
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(graceUntil_ - active_);
    }

    // Re-baselines without accounting the elapsed interval and closes any grace window, e.g. on (re)connect.
    void reset()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        last_ = source_();
        pendingStopped_ = Duration::zero();
        graceUntil_ = Duration::zero();
    }

    TimePoint now() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return source_();
    }

    void sample()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sampleLocked();
    }

    // Samples, then returns total active time since construction.
    std::chrono::milliseconds activeNow()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sampleLocked();
        return std::chrono::duration_cast<std::chrono::milliseconds>(active_);
    }

    // Returns stopped time accumulated since the previous call and clears it.
    std::chrono::milliseconds takeStoppedTime()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto stopped = std::chrono::duration_cast<std::chrono::milliseconds>(pendingStopped_);
        pendingStopped_ = Duration::zero();
        return stopped;
    }

private:
    using Duration = std::chrono::steady_clock::duration;
    static constexpr std::chrono::milliseconds kDefaultGapThreshold{2000};

    void sampleLocked()
    {
        const TimePoint current = source_();
        const Duration delta = current - last_;
        last_ = current;
        if (delta <= Duration::zero())
        {
            return;
        }
        if (delta <= gapThreshold_)
        {
            active_ += delta;
        }
        else
        {
            pendingStopped_ += delta;
            if (resumeGrace_ > std::chrono::milliseconds(0))
            {
                graceUntil_ = active_ + resumeGrace_;
            }
        }
    }

    mutable std::mutex mtx_;
    TimeSource source_;
    TimePoint last_{};
    Duration active_{Duration::zero()};
    Duration pendingStopped_{Duration::zero()};
    std::chrono::milliseconds gapThreshold_{kDefaultGapThreshold};
    std::chrono::milliseconds resumeGrace_{0};
    Duration graceUntil_{Duration::zero()};
};

} // namespace Firebolt::Transport
