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
#include "firebolt/logger.h"
#include "firebolt/types.h"
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <map>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef ENABLE_SYSLOG
#include <syslog.h>
#endif

namespace Firebolt
{
/* static */ LogLevel Logger::_logLevel = LogLevel::Error;
/* static */ bool Logger::formatter_addTs = true;
/* static */ bool Logger::formatter_addThreadId = true;
/* static */ bool Logger::formatter_addLocation = false;
/* static */ bool Logger::formatter_addFunction = true;

// clang-format off
std::map<Firebolt::LogLevel, const char*> _logLevelNames = {
    {LogLevel::Error, "Error"},
    {LogLevel::Warning, "Warning"},
    {LogLevel::Notice, "Notice"},
    {LogLevel::Info, "Info"},
    {LogLevel::Debug, "Debug"},
};
// clang-format on

#ifdef ENABLE_SYSLOG
// clang-format off
std::map<Firebolt::LogLevel, int> _logLevel2SysLog = {
    {LogLevel::Error, LOG_ERR},
    {LogLevel::Warning, LOG_WARNING},
    {LogLevel::Notice, LOG_NOTICE},
    {LogLevel::Info, LOG_INFO},
    {LogLevel::Debug, LOG_DEBUG},
};
// clang-format on
#endif

void Logger::setLogLevel(LogLevel logLevel)
{
    if (logLevel < LogLevel::MaxLevel)
    {
        _logLevel = logLevel;
    }
}

void Logger::setFormat(bool addTs, bool addLocation, bool addFunction, bool addThreadId)
{
    formatter_addTs = addTs;
    formatter_addLocation = addLocation;
    formatter_addFunction = addFunction;
    formatter_addThreadId = addThreadId;
}

void Logger::log(LogLevel logLevel, const std::string& module, const std::string file, const std::string function,
                 const uint16_t line, const char* format, ...)
{
    if (logLevel > _logLevel)
    {
        return;
    }

    auto now = std::chrono::system_clock::now();

    va_list arg;
    char msg[Logger::MaxBufSize];
    va_start(arg, format);
    int length = vsnprintf(msg, Logger::MaxBufSize, format, arg);
    va_end(arg);

    uint32_t position = (length >= Logger::MaxBufSize) ? (Logger::MaxBufSize - 1) : length;
    msg[position] = '\0';
    if (msg[position - 1] == '\n')
    {
        msg[position - 1] = '\0';
    }

    std::string time;
    if (formatter_addTs)
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_r(&t, &tm);
        char timeBuf[16];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03ld", tm.tm_hour, tm.tm_min, tm.tm_sec,
                 static_cast<long>(ms.count()));
        time = timeBuf;
    }

    const std::string levelName = _logLevelNames[logLevel];

    std::string fileName;
    if (formatter_addLocation)
    {
        fileName = strrchr(file.c_str(), '/');
        if (fileName.empty())
        {
            fileName = file;
        }
        else
        {
            fileName = fileName.substr(1);
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    char formattedMsg[Logger::MaxBufSize] = {0};
    size_t len = 0;
    if (formatter_addTs)
    {
        len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "%s: ", time.c_str());
    }
    len +=
        snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "[Firebolt|%s|%s]", module.c_str(), levelName.c_str());
    if (formatter_addLocation || formatter_addFunction)
    {
        if (formatter_addLocation && formatter_addFunction)
        {
            len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "[%s:%d,%s]", fileName.c_str(), line,
                            function.c_str());
        }
        else if (formatter_addLocation)
        {
            len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "[%s:%d]", fileName.c_str(), line);
        }
        else
        {
            len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "[%s()]", function.c_str());
        }
    }
    if (formatter_addThreadId)
    {
        len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "<tid:%u>", ::gettid());
    }
    len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, ": %s", msg);
#pragma GCC diagnostic pop

#ifdef ENABLE_SYSLOG
    syslog(_logLevel2SysLog[logLevel], "%s", formattedMsg);
#else
    fprintf(stderr, "%s\n", formattedMsg);
    fflush(stderr);
#endif
}
} // namespace Firebolt
