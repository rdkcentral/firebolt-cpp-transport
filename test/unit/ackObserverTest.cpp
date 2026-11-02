/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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
 */

#include "ack_observer.h"

#ifdef FIREBOLT_TRANSPORT_ACK_OBSERVER

#include <array>
#include <gtest/gtest.h>

TEST(AckObserverUTest, NonblockingRecordsRetainOperationBudgetAndOutcome)
{
    int channel[2];
    ASSERT_EQ(::pipe2(channel, O_NONBLOCK | O_CLOEXEC), 0);
    const auto start = Firebolt::Transport::Testing::ackObserverNow();
    Firebolt::Transport::Testing::observeAck(channel[1], "entered", 7, false, 50, start, false);
    Firebolt::Transport::Testing::observeAck(channel[1], "exited", 7, false, 50, start, true);
    std::array<char, 4096> data{};
    const auto count = ::read(channel[0], data.data(), data.size());
    ::close(channel[0]);
    ::close(channel[1]);
    ASSERT_GT(count, 0);
    const std::string records(data.data(), static_cast<size_t>(count));
    const auto split = records.find('\n');
    const auto entered = nlohmann::json::parse(records.substr(0, split));
    const auto exited = nlohmann::json::parse(records.substr(split + 1));
    EXPECT_EQ(entered["phase"], "entered");
    EXPECT_EQ(entered["operation_id"], 7);
    EXPECT_EQ(entered["listen"], false);
    EXPECT_EQ(entered["budget_ms"], 50);
    EXPECT_EQ(entered["started_ns"], start);
    EXPECT_EQ(exited["phase"], "exited");
    EXPECT_EQ(exited["started_ns"], start);
    EXPECT_EQ(exited["ready"], true);
    EXPECT_GE(exited["monotonic_ns"].get<long long>(), entered["monotonic_ns"].get<long long>());
}

TEST(AckObserverUTest, InvalidBlockingAndFullChannelsFailExplicitly)
{
    EXPECT_THROW(Firebolt::Transport::Testing::observeAck(-1, "entered", 7, true, 50, 0, false), std::runtime_error);
    int channel[2];
    ASSERT_EQ(::pipe(channel), 0);
    EXPECT_THROW(Firebolt::Transport::Testing::observeAck(channel[1], "entered", 7, true, 50, 0, false),
                 std::runtime_error);
    ASSERT_EQ(::fcntl(channel[1], F_SETFL, O_NONBLOCK), 0);
    std::array<char, 4096> data{};
    while (::write(channel[1], data.data(), data.size()) > 0)
    {
    }
    EXPECT_THROW(Firebolt::Transport::Testing::observeAck(channel[1], "entered", 7, true, 50, 0, false),
                 std::runtime_error);
    ::close(channel[0]);
    ::close(channel[1]);
}

#endif
