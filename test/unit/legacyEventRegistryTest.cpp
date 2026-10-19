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

#include "legacy_event_registry.h"
#include <gtest/gtest.h>

using namespace Firebolt::Transport;

TEST(LegacyEventRegistryUTest, FindReturnsRegisteredEvent)
{
    LegacyEventRegistry registry;
    registry.add(7, "device.onNameChanged");
    ASSERT_TRUE(registry.find(7).has_value());
    EXPECT_EQ(*registry.find(7), "device.onNameChanged");
    EXPECT_FALSE(registry.find(8).has_value());
}

TEST(LegacyEventRegistryUTest, RemoveById)
{
    LegacyEventRegistry registry;
    registry.add(1, "a.b");
    registry.remove(1);
    registry.remove(99); // unknown id is a no-op
    EXPECT_FALSE(registry.find(1).has_value());
}

TEST(LegacyEventRegistryUTest, RemoveEventRemovesOnlyOneMatchingEntry)
{
    LegacyEventRegistry registry;
    registry.add(1, "a.b");
    registry.add(2, "a.b");
    registry.add(3, "c.d");

    registry.removeEvent("a.b");

    EXPECT_FALSE(registry.find(1).has_value());
    EXPECT_TRUE(registry.find(2).has_value());
    EXPECT_TRUE(registry.find(3).has_value());

    registry.removeEvent("missing"); // no-op
    EXPECT_TRUE(registry.find(3).has_value());
}
