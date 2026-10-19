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
#include <atomic>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

using namespace Firebolt::Transport;
using namespace std::chrono_literals;

namespace
{
bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
} // namespace

TEST(WatchdogUTest, RunsTaskPeriodicallyUntilStopped)
{
    std::atomic<int> ticks{0};
    Watchdog watchdog;
    watchdog.start(5ms, [&ticks] { ++ticks; });

    EXPECT_TRUE(waitFor([&ticks] { return ticks >= 3; }));

    watchdog.stop();
    const int afterStop = ticks;
    std::this_thread::sleep_for(30ms);
    EXPECT_EQ(ticks, afterStop);
}

TEST(WatchdogUTest, StopIsPromptEvenWithLongInterval)
{
    Watchdog watchdog;
    watchdog.start(1h, [] {});

    const auto begin = std::chrono::steady_clock::now();
    watchdog.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 1s);
}

TEST(WatchdogUTest, SecondStartWhileRunningIsIgnored)
{
    std::atomic<int> first{0};
    std::atomic<int> second{0};
    Watchdog watchdog;
    watchdog.start(5ms, [&first] { ++first; });
    watchdog.start(5ms, [&second] { ++second; });

    EXPECT_TRUE(waitFor([&first] { return first >= 2; }));
    watchdog.stop();
    EXPECT_EQ(second, 0);
}

TEST(WatchdogUTest, CanBeRestartedAfterStop)
{
    std::atomic<int> ticks{0};
    Watchdog watchdog;
    watchdog.start(5ms, [&ticks] { ++ticks; });
    EXPECT_TRUE(waitFor([&ticks] { return ticks >= 1; }));
    watchdog.stop();

    ticks = 0;
    watchdog.start(5ms, [&ticks] { ++ticks; });
    EXPECT_TRUE(waitFor([&ticks] { return ticks >= 1; }));
}

TEST(WatchdogUTest, TaskExceptionDoesNotStopWatchdog)
{
    std::atomic<int> ticks{0};
    Watchdog watchdog;
    watchdog.start(5ms,
                   [&ticks]
                   {
                       if (++ticks % 2 == 0)
                           throw std::runtime_error("boom");
                       throw 42;
                   });

    EXPECT_TRUE(waitFor([&ticks] { return ticks >= 4; }));
}

TEST(WatchdogUTest, DestructorJoinsRunningThread)
{
    std::atomic<int> ticks{0};
    {
        Watchdog watchdog;
        watchdog.start(5ms, [&ticks] { ++ticks; });
        EXPECT_TRUE(waitFor([&ticks] { return ticks >= 1; }));
    }
    const int afterDestroy = ticks;
    std::this_thread::sleep_for(30ms);
    EXPECT_EQ(ticks, afterDestroy);
}
