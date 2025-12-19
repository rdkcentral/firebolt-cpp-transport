/*
 * Copyright 2023 Comcast Cable Communications Management, LLC
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

#include "firebolt/transport_export.h"
#include "firebolt/types.h"
#include <stdint.h>
#include <string>

namespace Firebolt
{
class FIREBOLTTRANSPORT_EXPORT Logger
{
private:
    static constexpr uint16_t MaxBufSize = 1024;

public:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    ~Logger() = default;

public:
    static void setLogLevel(LogLevel logLevel);
    static void setFormat(bool addTs, bool addLocation, bool addFunction, bool addThreadId);
    static void log(LogLevel logLevel, const std::string& module, const std::string file, const std::string function,
                    const uint16_t line, const char* format, ...) __attribute__((format(printf, 6, 7)));

    static bool isLogLevelEnabled(LogLevel logLevel) { return logLevel <= _logLevel; }

private:
    static LogLevel _logLevel;
    static bool formatter_addTs;
    static bool formatter_addThreadId;
    static bool formatter_addLocation;
    static bool formatter_addFunction;
};
} // namespace Firebolt

#define FIREBOLT_LOG(level, module, ...)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        Firebolt::Logger::log(level, module, __FILE__, __func__, __LINE__, __VA_ARGS__);                               \
    } while (0)
#define FIREBOLT_LOG_ERROR(module, ...)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        FIREBOLT_LOG(Firebolt::LogLevel::Error, module, __VA_ARGS__);                                                  \
    } while (0)
#define FIREBOLT_LOG_WARNING(module, ...)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        FIREBOLT_LOG(Firebolt::LogLevel::Warning, module, __VA_ARGS__);                                                \
    } while (0)
#define FIREBOLT_LOG_NOTICE(module, ...)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        FIREBOLT_LOG(Firebolt::LogLevel::Notice, module, __VA_ARGS__);                                                 \
    } while (0)
#define FIREBOLT_LOG_INFO(module, ...)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        FIREBOLT_LOG(Firebolt::LogLevel::Info, module, __VA_ARGS__);                                                   \
    } while (0)
#define FIREBOLT_LOG_DEBUG(module, ...)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        FIREBOLT_LOG(Firebolt::LogLevel::Debug, module, __VA_ARGS__);                                                  \
    } while (0)
