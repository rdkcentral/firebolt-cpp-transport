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

#include "active_clock.h"
#include "unit/testClock.h"
#include <chrono>
#include <gtest/gtest.h>

using namespace Firebolt::Transport;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.DefaultSourceIsSteadyClock
// Covers: active_clock.h ActiveClock::now() with no injected source
// Scenario type: success
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, DefaultSourceIsSteadyClock)
{
    ActiveClock clock;
    const TimePoint before = std::chrono::steady_clock::now();
    const TimePoint observed = clock.now();
    const TimePoint after = std::chrono::steady_clock::now();
    EXPECT_LE(before, observed);
    EXPECT_LE(observed, after);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.InjectedSourceIsUsed
// Covers: active_clock.h ActiveClock(TimeSource), ActiveClock::now()
// Scenario type: success
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, InjectedSourceIsUsed)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    EXPECT_EQ(clock.now(), manual.now());
    manual.advance(5000ms);
    EXPECT_EQ(clock.now(), manual.now());
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.NullSourceRestoresSteadyClock
// Covers: active_clock.h ActiveClock::setTimeSource(nullptr)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, NullSourceRestoresSteadyClock)
{
    Testing::ManualClock manual;
    manual.advance(std::chrono::hours(24));
    ActiveClock clock(manual.source());
    clock.setTimeSource(nullptr);
    const TimePoint before = std::chrono::steady_clock::now();
    const TimePoint observed = clock.now();
    const TimePoint after = std::chrono::steady_clock::now();
    EXPECT_LE(before, observed);
    EXPECT_LE(observed, after);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.JitterBelowThresholdCountsAsActive
// Covers: ActiveClock::sample(), ActiveClock::activeNow()
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Jitter below threshold counts as active time
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, JitterBelowThresholdCountsAsActive)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);

    manual.advance(1500ms);
    clock.sample();

    EXPECT_EQ(clock.activeNow(), 1500ms);
    EXPECT_EQ(clock.takeStoppedTime(), 0ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.GapAboveThresholdCountsAsStopped
// Covers: ActiveClock::sample(), ActiveClock::activeNow(), ActiveClock::takeStoppedTime()
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Gap above threshold counts as stopped time
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, GapAboveThresholdCountsAsStopped)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);

    manual.advance(2500ms);

    EXPECT_EQ(clock.activeNow(), 0ms);
    EXPECT_EQ(clock.takeStoppedTime(), 2500ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.TakeStoppedTimeCoalescesAndResets
// Covers: ActiveClock::takeStoppedTime()
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, TakeStoppedTimeCoalescesAndResets)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);

    manual.advance(3000ms);
    clock.sample();
    manual.advance(100ms);
    clock.sample();
    manual.advance(4000ms);
    clock.sample();

    EXPECT_EQ(clock.takeStoppedTime(), 7000ms);
    EXPECT_EQ(clock.takeStoppedTime(), 0ms);
    EXPECT_EQ(clock.activeNow(), 100ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.ConfiguredThresholdIsHonored
// Covers: ActiveClock::effectiveGapThreshold(), ActiveClock::setGapThreshold()
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Configured threshold is honored
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, ConfiguredThresholdIsHonored)
{
    EXPECT_EQ(ActiveClock::effectiveGapThreshold(10000, 500), 10000ms);

    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(ActiveClock::effectiveGapThreshold(10000, 500));

    manual.advance(5000ms);

    EXPECT_EQ(clock.activeNow(), 5000ms);
    EXPECT_EQ(clock.takeStoppedTime(), 0ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.TooSmallThresholdIsClamped
// Covers: ActiveClock::effectiveGapThreshold()
// Scenario type: edge case
// Spec: hibernation-resilience § Requirement: Gap threshold distinguishes a stop from jitter
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, TooSmallThresholdIsClamped)
{
    EXPECT_EQ(ActiveClock::effectiveGapThreshold(100, 500), 2000ms);
    EXPECT_EQ(ActiveClock::effectiveGapThreshold(0, 1000), 4000ms);
    EXPECT_EQ(ActiveClock::effectiveGapThreshold(2000, 500), 2000ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.ResetDiscardsElapsedTime
// Covers: ActiveClock::reset()
// Scenario type: edge case
// Rationale: time spent disconnected (no samples) must not be reported as a stop.
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, ResetDiscardsElapsedTime)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);

    manual.advance(60000ms);
    clock.reset();
    manual.advance(500ms);

    EXPECT_EQ(clock.activeNow(), 500ms);
    EXPECT_EQ(clock.takeStoppedTime(), 0ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.SetTimeSourceRebaselines
// Covers: ActiveClock::setTimeSource()
// Scenario type: edge case
// Rationale: swapping sources (test hook) must not register a spurious gap.
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, SetTimeSourceRebaselines)
{
    Testing::ManualClock manual;
    manual.advance(std::chrono::hours(1));
    ActiveClock clock;
    clock.setGapThreshold(2000ms);

    clock.setTimeSource(manual.source());

    EXPECT_EQ(clock.takeStoppedTime(), 0ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.GraceWindowOpensOnResume
// Covers: ActiveClock::setResumeGrace(), ActiveClock::graceRemaining()
// Scenario type: success
// Spec: hibernation-resilience § Requirement: Post-resume grace period tolerates traffic held after thaw
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, GraceWindowOpensOnResume)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);
    clock.setResumeGrace(5000ms);

    EXPECT_EQ(clock.graceRemaining(), 0ms) << "no grace before any stop";

    manual.advance(1000ms);
    clock.sample();
    manual.advance(10000ms);

    EXPECT_EQ(clock.graceRemaining(), 5000ms);
    manual.advance(1500ms);
    EXPECT_EQ(clock.graceRemaining(), 3500ms);
    manual.advance(1500ms);
    clock.sample();
    manual.advance(1500ms);
    clock.sample();
    manual.advance(1500ms);
    EXPECT_EQ(clock.graceRemaining(), 0ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.GraceDisabledWhenZero
// Covers: ActiveClock::setResumeGrace(0)
// Scenario type: edge case
// Spec: hibernation-resilience § Scenario: Grace disabled
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, GraceDisabledWhenZero)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);
    clock.setResumeGrace(0ms);

    manual.advance(10000ms);

    EXPECT_EQ(clock.graceRemaining(), 0ms);
    EXPECT_EQ(clock.graceUntil(), 0ms);
}

// ---------------------------------------------------------------------------
// Test name: ActiveClockUTest.ResetClosesGraceWindow
// Covers: ActiveClock::reset()
// Scenario type: edge case
// Rationale: a grace window from a previous connection must not extend deadlines after reconnect.
// ---------------------------------------------------------------------------
TEST(ActiveClockUTest, ResetClosesGraceWindow)
{
    Testing::ManualClock manual;
    ActiveClock clock(manual.source());
    clock.setGapThreshold(2000ms);
    clock.setResumeGrace(5000ms);
    manual.advance(10000ms);
    ASSERT_EQ(clock.graceRemaining(), 5000ms);

    clock.reset();

    EXPECT_EQ(clock.graceRemaining(), 0ms);
    EXPECT_LE(clock.graceUntil(), clock.activeNow());
}
