/**
 * Copyright 2026 Comcast Cable Communications Management, LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef FIREBOLT_TRANSPORT_ACK_OBSERVER

#include "firebolt/logger.h"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <time.h>
#include <unistd.h>

namespace Firebolt::Transport::Testing
{

inline void ackObserverFailure(const char* message)
{
    FIREBOLT_LOG_ERROR("Gateway", "[test ACK observer] %s", message);
    throw std::runtime_error(message);
}

inline int ackObserverFd()
{
    const char* value = std::getenv("FIREBOLT_ACK_OBSERVER_FD");
    if (nullptr == value)
    {
        return -1;
    }
    int fd = -1;
    const auto parsed = std::from_chars(value, value + std::strlen(value), fd);
    if (parsed.ec != std::errc{} || parsed.ptr != value + std::strlen(value) || fd < 0)
    {
        ackObserverFailure("invalid ACK observer descriptor");
    }
    return fd;
}

inline long long ackObserverNow()
{
    timespec sample{};
    if (0 != ::clock_gettime(CLOCK_MONOTONIC, &sample))
    {
        ackObserverFailure("cannot sample ACK observer CLOCK_MONOTONIC");
    }
    return static_cast<long long>(sample.tv_sec) * 1000000000LL + sample.tv_nsec;
}

inline void observeAck(int fd, const char* phase, uint32_t id, bool listen, long long budgetMs, long long startedNs,
                       bool ready)
{
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || 0 == (flags & O_NONBLOCK))
    {
        ackObserverFailure("ACK observer requires a valid nonblocking descriptor");
    }
    const std::string record = nlohmann::json{{"schema_version", 1},
                                              {"phase", phase},
                                              {"operation_id", id},
                                              {"listen", listen},
                                              {"budget_ms", budgetMs},
                                              {"started_ns", startedNs},
                                              {"monotonic_ns", ackObserverNow()},
                                              {"ready", ready}}
                                   .dump() +
                               "\n";
    // One bounded, nonblocking write: a full/broken channel invalidates proof, never extends the wait.
    if (record.size() > PIPE_BUF || ::write(fd, record.data(), record.size()) != static_cast<ssize_t>(record.size()))
    {
        ackObserverFailure("cannot write complete ACK observer record");
    }
}

} // namespace Firebolt::Transport::Testing

#endif
