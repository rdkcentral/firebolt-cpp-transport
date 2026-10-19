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

#include "connection_notice_throttle.h"
#include <gtest/gtest.h>

using namespace Firebolt::Transport;
using Firebolt::Error;

class ConnectionNoticeThrottleUTest : public ::testing::Test
{
protected:
    using Clock = ConnectionNoticeThrottle::Clock;
    ConnectionNoticeThrottle throttle{std::chrono::seconds(5)};
    const Clock::time_point t0 = Clock::now();
};

TEST_F(ConnectionNoticeThrottleUTest, FirstChangeIsAlwaysReported)
{
    auto suppressed = throttle.record(true, Error::None, t0);
    ASSERT_TRUE(suppressed.has_value());
    EXPECT_EQ(*suppressed, 0u);
}

TEST_F(ConnectionNoticeThrottleUTest, RepeatsWithinIntervalAreSuppressedAndCounted)
{
    throttle.record(false, Error::NotConnected, t0);
    EXPECT_FALSE(throttle.record(false, Error::NotConnected, t0 + std::chrono::seconds(1)));
    EXPECT_FALSE(throttle.record(false, Error::NotConnected, t0 + std::chrono::seconds(4)));

    auto suppressed = throttle.record(false, Error::NotConnected, t0 + std::chrono::seconds(5));
    ASSERT_TRUE(suppressed.has_value());
    EXPECT_EQ(*suppressed, 2u);
}

TEST_F(ConnectionNoticeThrottleUTest, CounterResetsAfterReport)
{
    throttle.record(true, Error::None, t0);
    EXPECT_FALSE(throttle.record(true, Error::None, t0 + std::chrono::seconds(1)));
    throttle.record(true, Error::None, t0 + std::chrono::seconds(6));

    auto suppressed = throttle.record(true, Error::None, t0 + std::chrono::seconds(12));
    ASSERT_TRUE(suppressed.has_value());
    EXPECT_EQ(*suppressed, 0u);
}

TEST_F(ConnectionNoticeThrottleUTest, StateOrErrorChangeIsReportedImmediately)
{
    throttle.record(true, Error::None, t0);
    EXPECT_TRUE(throttle.record(false, Error::None, t0 + std::chrono::milliseconds(1)));
    EXPECT_TRUE(throttle.record(false, Error::NotConnected, t0 + std::chrono::milliseconds(2)));
}
