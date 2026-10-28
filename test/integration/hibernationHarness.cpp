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

// End-to-end freeze harness: a real SIGSTOP/SIGCONT of a process using the transport,
// standing in for a Dobby/memcr container freeze.
//
// The parent forks *before* any thread exists, then runs the scripted gateway; the child
// connects, sends a request and reports the outcome through a pipe. While the request is
// outstanding the parent stops the child for longer than waitTime_ms, continues it, and
// holds the reply past waitTime_ms (but within the resume grace) before answering.
//
// Scenarios:
//   resilient  - resumeGrace_ms > 0: the request must succeed and one resume be reported.
//   control    - resumeGrace_ms = 0: the same script must end in Timedout, proving the
//                harness can observe the failure it guards against.

#include "unit/scriptedGateway.h"

#include <firebolt/gateway.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{

using namespace std::chrono_literals;
using Firebolt::Transport::Testing::ScriptedGateway;

constexpr const char* kMethod = "harness.op";
constexpr unsigned kWaitTime_ms = 1000;
constexpr unsigned kWatchdogCycle_ms = 100;
constexpr unsigned kStopGapThreshold_ms = 500;
constexpr std::chrono::milliseconds kStopDuration{2500};
constexpr std::chrono::milliseconds kHoldAfterThaw{1500};
constexpr std::chrono::milliseconds kReportTimeout{20000};

struct Scenario
{
    const char* name;
    unsigned resumeGrace_ms;
    bool expectSuccess;
};

struct Report
{
    bool ok = false;
    int error = -1;
    int resumes = -1;
};

bool writeAll(int fd, const std::string& data)
{
    const char* p = data.data();
    std::size_t left = data.size();
    while (left > 0)
    {
        ssize_t n = ::write(fd, p, left);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        if (n <= 0)
        {
            return false;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

// Reads one '\n'-terminated line, giving up after the timeout.
bool readLine(int fd, std::string& line, std::chrono::milliseconds timeout)
{
    line.clear();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    char c = 0;
    while (true)
    {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left <= 0ms)
        {
            return false;
        }
        pollfd pfd{fd, POLLIN, 0};
        int rc = ::poll(&pfd, 1, static_cast<int>(left.count()));
        if (rc < 0 && errno == EINTR)
        {
            continue;
        }
        if (rc <= 0)
        {
            return false;
        }
        ssize_t n = ::read(fd, &c, 1);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        if (n <= 0)
        {
            return false;
        }
        if (c == '\n')
        {
            return true;
        }
        line.push_back(c);
    }
}

[[noreturn]] void runChild(const Scenario& scenario, int urlFd, int reportFd)
{
    std::string url;
    if (!readLine(urlFd, url, kReportTimeout))
    {
        ::_exit(10);
    }

    Firebolt::Config cfg;
    cfg.wsUrl = url;
    cfg.waitTime_ms = kWaitTime_ms;
    cfg.watchdogCycle_ms = kWatchdogCycle_ms;
    cfg.stopGapThreshold_ms = kStopGapThreshold_ms;
    cfg.resumeGrace_ms = scenario.resumeGrace_ms;
    cfg.log.level = Firebolt::LogLevel::Notice;

    Firebolt::Transport::IGateway& gateway = Firebolt::Transport::GetGatewayInstance();
    auto connected = std::make_shared<std::promise<void>>();
    auto connectedFuture = connected->get_future();
    gateway.connect(cfg,
                    [connected](bool isConnected, const Firebolt::Error& /*err*/)
                    {
                        if (isConnected)
                        {
                            try
                            {
                                connected->set_value();
                            }
                            catch (const std::future_error&)
                            {
                            }
                        }
                    });
    if (connectedFuture.wait_for(5s) != std::future_status::ready)
    {
        ::_exit(11);
    }

    std::atomic<int> resumes{0};
    gateway.setResumeListener([&resumes](std::chrono::milliseconds /*stoppedFor*/) { ++resumes; });

    auto future = gateway.request(kMethod, nlohmann::json::object());
    if (future.wait_for(kReportTimeout) != std::future_status::ready)
    {
        ::_exit(12);
    }
    Firebolt::Result<nlohmann::json> result = future.get();

    // Resume is delivered on the notification worker; allow it to land.
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (resumes.load() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(10ms);
    }

    Report report;
    report.ok = static_cast<bool>(result);
    report.error = static_cast<int>(result.error());
    report.resumes = resumes.load();
    const std::string line = std::to_string(report.ok ? 1 : 0) + " " + std::to_string(report.error) + " " +
                             std::to_string(report.resumes) + "\n";
    const bool written = writeAll(reportFd, line);

    gateway.setResumeListener(nullptr);
    gateway.disconnect();
    ::_exit(written ? 0 : 13);
}

bool parseReport(const std::string& line, Report& report)
{
    int ok = 0;
    if (std::sscanf(line.c_str(), "%d %d %d", &ok, &report.error, &report.resumes) != 3)
    {
        return false;
    }
    report.ok = ok != 0;
    return true;
}

bool runScenario(const Scenario& scenario)
{
    std::printf("[ RUN      ] %s\n", scenario.name);
    std::fflush(stdout);

    int urlPipe[2];
    int reportPipe[2];
    if (::pipe(urlPipe) != 0 || ::pipe(reportPipe) != 0)
    {
        std::perror("pipe");
        return false;
    }

    // Fork while single-threaded: the child must not inherit locks held by other threads.
    pid_t child = ::fork();
    if (child < 0)
    {
        std::perror("fork");
        return false;
    }
    if (child == 0)
    {
        ::close(urlPipe[1]);
        ::close(reportPipe[0]);
        runChild(scenario, urlPipe[0], reportPipe[1]);
    }
    ::close(urlPipe[0]);
    ::close(reportPipe[1]);

    bool pass = false;
    std::string failure;
    {
        ScriptedGateway server;
        server.holdMethod(kMethod);
        if (!server.start())
        {
            failure = "server failed to start";
        }
        else if (!writeAll(urlPipe[1], server.url() + "\n"))
        {
            failure = "could not send url to child";
        }
        else if (!server.waitForRequest(kMethod, 1, 10s))
        {
            failure = "child never sent the request";
        }
        else
        {
            ::kill(child, SIGSTOP);
            std::this_thread::sleep_for(kStopDuration);
            ::kill(child, SIGCONT);
            std::this_thread::sleep_for(kHoldAfterThaw);
            server.release(kMethod);

            std::string line;
            Report report;
            if (!readLine(reportPipe[0], line, kReportTimeout))
            {
                failure = "no report from child";
            }
            else if (!parseReport(line, report))
            {
                failure = "malformed report: " + line;
            }
            else if (scenario.expectSuccess)
            {
                pass = report.ok && report.resumes == 1;
                if (!pass)
                {
                    failure = "expected success with one resume; got ok=" + std::to_string(report.ok) +
                              " error=" + std::to_string(report.error) + " resumes=" + std::to_string(report.resumes);
                }
            }
            else
            {
                pass = !report.ok && report.error == static_cast<int>(Firebolt::Error::Timedout);
                if (!pass)
                {
                    failure = "expected Timedout; got ok=" + std::to_string(report.ok) +
                              " error=" + std::to_string(report.error);
                }
            }
        }
    }
    ::close(urlPipe[1]);
    ::close(reportPipe[0]);

    int status = 0;
    if (::waitpid(child, &status, 0) != child)
    {
        pass = false;
        failure += " (waitpid failed)";
    }
    else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        pass = false;
        failure += " (child exit status " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")";
    }

    if (pass)
    {
        std::printf("[       OK ] %s\n", scenario.name);
    }
    else
    {
        std::printf("[  FAILED  ] %s: %s\n", scenario.name, failure.c_str());
    }
    std::fflush(stdout);
    return pass;
}

} // namespace

int main()
{
    const Scenario scenarios[] = {
        {"resilient: request survives SIGSTOP and late reply within grace", 4000, true},
        {"control: same freeze without grace times out", 0, false},
    };

    bool allPassed = true;
    for (const Scenario& scenario : scenarios)
    {
        allPassed = runScenario(scenario) && allPassed;
    }
    return allPassed ? 0 : 1;
}
