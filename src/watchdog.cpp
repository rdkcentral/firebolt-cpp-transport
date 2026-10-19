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

#include "watchdog.h"
#include "firebolt/logger.h"
#include <exception>

namespace Firebolt::Transport
{

Watchdog::~Watchdog()
{
    stop();
}

void Watchdog::start(std::chrono::milliseconds interval, std::function<void()> task)
{
    if (running_.exchange(true))
    {
        return;
    }
    FIREBOLT_LOG_DEBUG("Gateway", "[watchdog] starting thread (interval=%lld ms)",
                       static_cast<long long>(interval.count()));
    thread_ = std::thread([this, interval, task = std::move(task)]() { run(interval, task); });
    FIREBOLT_LOG_DEBUG("Gateway", "[watchdog] thread started");
}

void Watchdog::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }
    cv_.notify_all();
    FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] waiting for watchdog thread join...");
    const auto begin = std::chrono::steady_clock::now();
    if (thread_.joinable())
    {
        thread_.join();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    FIREBOLT_LOG_DEBUG("Gateway", "[disconnect] watchdog joined in %lld ms", static_cast<long long>(elapsed.count()));
}

void Watchdog::run(std::chrono::milliseconds interval, const std::function<void()>& task)
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_)
    {
        if (cv_.wait_for(lock, interval, [this] { return !running_; }))
        {
            break;
        }
        lock.unlock();
        try
        {
            task();
        }
        catch (const std::exception& e)
        {
            FIREBOLT_LOG_ERROR("Gateway", "[watchdog] task threw: %s", e.what());
        }
        catch (...)
        {
            FIREBOLT_LOG_ERROR("Gateway", "[watchdog] task threw unknown exception");
        }
        lock.lock();
    }
}

} // namespace Firebolt::Transport
