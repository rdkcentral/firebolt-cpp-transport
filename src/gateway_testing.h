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

#ifdef FIREBOLT_TRANSPORT_TESTING

#include "active_clock.h"

namespace Firebolt::Transport
{

// Test hooks operating on the GetGatewayInstance() singleton.

/** Replace the gateway's time source; nullptr restores std::chrono::steady_clock. */
void testSetGatewayTimeSource(TimeSource source);

/** Run one watchdog iteration synchronously on the calling thread. */
void testRunGatewayWatchdogTick();

/** Effective stop-gap threshold applied by the last connect(). */
std::chrono::milliseconds testGatewayGapThreshold();

/** Stop the notification worker without disconnecting the transport. */
void testStopGatewayNotificationWorker();

/** Whether notification-worker shutdown has been requested. */
bool testGatewayNotificationWorkerStopRequested();

void testDisconnectBeforeGatewayConnectFinalization();

} // namespace Firebolt::Transport

#endif
