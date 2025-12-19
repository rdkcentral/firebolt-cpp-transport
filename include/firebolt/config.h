/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 Sky UK
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

#pragma once

#include "firebolt/types.h"
#include <optional>
#include <string>

namespace Firebolt
{
/**
 * @brief Configuration used to establish a connection to the gateway transport.
 */
struct Config
{
    /** WebSocket URL to connect to. */
    std::string wsUrl = "ws://127.0.0.1:9998";

    /** Timeout for RPC responses in milliseconds. Default: 3000. */
    unsigned waitTime_ms = 3000;

    /**
     * @brief Log format settings used by the Firebolt logger.
     */
    struct LogFormat
    {
        /** Include timestamps in log output. */
        bool ts = true;
        /** Include source location in log output. */
        bool location = false;
        /** Include function name in log output. */
        bool function = true;
        /** Include thread id in log output. */
        bool thread = true;
    };

    /**
     * @brief Log level and transport-log masks.
     *
     * `transportInclude` / `transportExclude` correspond to bitmasks used by
     * the transport implementation to filter log categories.
     */
    struct LogSettings
    {
        /** Log level */
        LogLevel level = LogLevel::Info;
        /** Optional include mask for transport logging. */
        std::optional<unsigned> transportInclude;
        /** Optional exclude mask for transport logging. */
        std::optional<unsigned> transportExclude;
        /** Log format sub-structure. */
        LogFormat format;
    } log;

    /** Watchdog polling cycle in milliseconds. Default: 500. */
    unsigned watchdogCycle_ms = 500;
};

} // namespace Firebolt
