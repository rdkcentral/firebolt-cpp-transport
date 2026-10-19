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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace Firebolt::Transport
{

// Runs a task periodically on its own thread until stopped. The thread is always joined by stop() or by the
// destructor, so it can never outlive the owner.
class Watchdog
{
public:
    Watchdog() = default;
    ~Watchdog();
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    // Starts calling `task` every `interval`. Does nothing if already running. Exceptions thrown by `task` are
    // logged and do not stop the watchdog.
    void start(std::chrono::milliseconds interval, std::function<void()> task);

    // Wakes the thread and waits for it to finish. Must not be called from `task`.
    void stop();

private:
    void run(std::chrono::milliseconds interval, const std::function<void()>& task);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex mutex_;
};

} // namespace Firebolt::Transport
