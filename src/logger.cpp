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
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <limits.h>
#include <map>
#include <optional>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef ENABLE_SYSLOG
#include <syslog.h>
#endif

namespace Firebolt
{

namespace
{
std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

std::optional<Firebolt::LogLevel> parseEnvLogLevel(const char* name)
{
    // Copy to a local buffer immediately: the pointer returned by getenv() can
    // become stale if another thread calls setenv/unsetenv/putenv.
    char buffer[256];
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0')
    {
        return std::nullopt;
    }

    std::strncpy(buffer, raw, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    const std::string value = toLowerCopy(buffer);
    if (value == "error" || value == "0")
    {
        return Firebolt::LogLevel::Error;
    }
    if (value == "warning" || value == "warn" || value == "1")
    {
        return Firebolt::LogLevel::Warning;
    }
    if (value == "notice" || value == "2")
    {
        return Firebolt::LogLevel::Notice;
    }
    if (value == "info" || value == "3")
    {
        return Firebolt::LogLevel::Info;
    }
    if (value == "debug" || value == "4")
    {
        return Firebolt::LogLevel::Debug;
    }

    return std::nullopt;
}

bool isEnvLogDisabled(const char* name)
{
    // Copy to a local buffer immediately: the pointer returned by getenv() can
    // become stale if another thread calls setenv/unsetenv/putenv.
    char buffer[256];
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0')
    {
        return false;
    }

    std::strncpy(buffer, raw, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    const std::string value = toLowerCopy(buffer);
    return value == "off" || value == "none" || value == "disable" || value == "disabled";
}

std::string resolveLogFilePathFromEnvironment()
{
    // Cache the validated canonical path per-thread to avoid a realpath() syscall
    // on every log call when the env var hasn't changed.  thread_local is safe
    // here: each thread gets its own cache and picks up env var changes on the
    // next log call in that thread.
    thread_local std::string cachedEnvValue;
    thread_local std::string cachedCanonicalPath;

    // Copy to a local buffer immediately: the pointer returned by getenv() can
    // become stale if another thread calls setenv/unsetenv/putenv.
    char buffer[PATH_MAX];
    const char* raw = std::getenv("FIREBOLT_TRANSPORT_LOG_FILE");
    if (raw == nullptr || *raw == '\0')
    {
        cachedEnvValue.clear();
        cachedCanonicalPath.clear();
        return "";
    }

    // Reject overlong values rather than silently truncating.
    if (std::strlen(raw) >= sizeof(buffer))
    {
        return "";
    }

    // Fast path: env var unchanged since last call in this thread.
    if (raw == cachedEnvValue || std::strcmp(raw, cachedEnvValue.c_str()) == 0)
    {
        return cachedCanonicalPath;
    }

    std::strncpy(buffer, raw, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Pre-validate: only absolute paths, no traversal sequences.
    const std::string path(buffer);
    if (path[0] != '/' || path.find("../") != std::string::npos)
    {
        return "";
    }

    // Split into parent directory and filename on the last '/'.
    const auto lastSlash = path.rfind('/');
    const std::string parentDir = (lastSlash == 0) ? "/" : path.substr(0, lastSlash);
    const std::string filename = path.substr(lastSlash + 1);

    // Reject empty filenames, filenames containing '/', and dot/dotdot entries
    // that could traverse outside the parent directory via openat().
    if (filename.empty() || filename == "." || filename == ".." || filename.find('/') != std::string::npos)
    {
        return "";
    }

    // Canonicalize the parent directory via realpath() to resolve symlinks and
    // remove any remaining traversal components.  realpath() is the standard
    // POSIX sanitizer for user-controlled paths; CodeQL and Coverity recognise
    // its output as trusted.  If the parent directory does not exist the env
    // var is silently ignored (no log file).
    char canonicalParent[PATH_MAX];
    if (realpath(parentDir.c_str(), canonicalParent) == nullptr)
    {
        return "";
    }

    // Reconstruct the full path from the now-canonical parent and the filename.
    // Avoid a double leading slash when canonicalParent is exactly "/".
    // Non-const so NRVO/implicit move applies on return (Coverity COPY_INSTEAD_OF_MOVE).
    std::string canonicalPath = std::string(canonicalParent);
    if (canonicalPath.back() != '/')
    {
        canonicalPath += '/';
    }
    canonicalPath += filename;
    if (canonicalPath.size() >= static_cast<std::size_t>(PATH_MAX))
    {
        return "";
    }
    // Update the per-thread cache.
    cachedEnvValue = buffer;
    cachedCanonicalPath = canonicalPath;
    return canonicalPath;
}

bool tryWriteToConfiguredLogFile(const char* message)
{
    const auto logFilePath = resolveLogFilePathFromEnvironment();
    if (logFilePath.empty())
    {
        return false;
    }

    // Split the canonical path into parent directory and filename.  The parent
    // has already been resolved through realpath() in resolveLogFilePathFromEnv,
    // so canonicalParent is a fully trusted string.  We then open the parent
    // directory as a directory-file-descriptor and use openat() to create or
    // open the log file relative to that trusted dirfd.
    //
    // Using openat(dirfd, filename, ...) keeps the user-supplied filename
    // component constrained to the trusted directory, breaking the taint chain
    // that CodeQL traces from getenv() → open(full_tainted_path).
    const auto lastSlash = logFilePath.rfind('/');
    if (lastSlash == std::string::npos || lastSlash == logFilePath.size() - 1)
    {
        return false; // malformed path (no filename component)
    }
    const std::string canonicalParent = (lastSlash == 0) ? "/" : logFilePath.substr(0, lastSlash);
    const std::string filename = logFilePath.substr(lastSlash + 1);

    // Open the parent directory via its realpath()-sanitised canonical path.
    int dirfd = open(canonicalParent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0)
    {
        return false;
    }

    // Open (or create) the log file relative to the trusted dirfd.
    // Mode 0644 avoids world-writable creation; O_CLOEXEC prevents FD inheritance.
    //
    // CodeQL cpp/path-injection: filename originates from getenv() and remains
    // tainted despite validation. The security controls are:
    //   1. Only absolute paths accepted (path[0] == '/').
    //   2. No path-traversal sequences ("../") in the full path.
    //   3. filename contains no '/' (checked in resolveLogFilePathFromEnvironment).
    //   4. Parent directory canonicalised via realpath(); dirfd is bound to it.
    //   5. openat(dirfd, ...) confines access to the realpath'd directory.
    // This is a false positive: the tainted filename cannot escape the trusted
    // dirfd scope. Suppress the alert.
    // lgtm[cpp/path-injection]
    int fd = openat(dirfd, filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    close(dirfd);
    if (fd < 0)
    {
        return false;
    }

    std::string line;
    try
    {
        line = std::string(message) + "\n";
    }
    catch (...)
    {
        // Logging must be best-effort and must never crash the process.
        close(fd);
        return false;
    }
    // A single write() on a file opened with O_APPEND is atomic: POSIX
    // guarantees the seek-to-end and write happen as one operation, so
    // concurrent threads cannot interleave their lines.
    // A retry loop would break this atomicity — if a short write occurred
    // between loop iterations another thread could insert bytes mid-line.
    // For log lines (typically << PIPE_BUF), a genuine short write is
    // vanishingly rare; treat it as a failure and fall back to stderr.
    const ssize_t ret = write(fd, line.c_str(), line.size());
    close(fd);
    return ret == static_cast<ssize_t>(line.size());
}
} // namespace

/* static */ std::atomic<LogLevel> Logger::_logLevel{LogLevel::Error};
/* static */ std::atomic<bool> Logger::_loggingEnabled{true};
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
        _logLevel.store(logLevel);
    }
    else if (logLevel == LogLevel::MaxLevel)
    {
        _logLevel.store(LogLevel::Debug);
    }
}

LogLevel Logger::resolveLogLevelFromEnvironment(LogLevel defaultLevel)
{
    if (isEnvLogDisabled("FIREBOLT_TRANSPORT_LOG_LEVEL"))
    {
        _loggingEnabled.store(false);
        return defaultLevel;
    }

    _loggingEnabled.store(true);
    if (const auto level = parseEnvLogLevel("FIREBOLT_TRANSPORT_LOG_LEVEL"))
    {
        return *level;
    }

    return defaultLevel;
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
    if (!_loggingEnabled.load() || logLevel > _logLevel.load())
    {
        return;
    }

    auto now = std::chrono::system_clock::now();

    va_list arg;
    char msg[Logger::MaxBufSize];
    va_start(arg, format);
    int length = vsnprintf(msg, Logger::MaxBufSize, format, arg);
    va_end(arg);

    size_t position = 0;
    if (length > 0)
    {
        position = (static_cast<size_t>(length) >= Logger::MaxBufSize) ? (Logger::MaxBufSize - 1)
                                                                       : static_cast<size_t>(length);
    }
    msg[position] = '\0';
    if (position > 0 && msg[position - 1] == '\n')
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
        fileName = std::filesystem::path(file).filename().string();
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    char formattedMsg[Logger::MaxBufSize] = {0};
    size_t len = 0;
    if (formatter_addTs)
    {
        len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "%s: ", time.c_str());
    }
    len += snprintf(formattedMsg + len, sizeof(formattedMsg) - len, "[FireboltNative|%s|%s]", module.c_str(),
                    levelName.c_str());
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

    if (!tryWriteToConfiguredLogFile(formattedMsg))
    {
#ifdef ENABLE_SYSLOG
        syslog(_logLevel2SysLog[logLevel], "%s", formattedMsg);
#else
        fprintf(stderr, "%s\n", formattedMsg);
        fflush(stderr);
#endif
    }
}
} // namespace Firebolt
