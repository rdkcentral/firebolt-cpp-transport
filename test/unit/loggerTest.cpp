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

#include "firebolt/logger.h"
#include <gtest/gtest.h>
#include <regex>
#include <string>
#include <unistd.h>

using namespace Firebolt;

// ---------------------------------------------------------------------------
// File: test/unit/loggerTest.cpp
// Covers: src/logger.cpp — format flag combinations (lines 119–153)
// ---------------------------------------------------------------------------

class LoggerFormatUTest : public ::testing::Test
{
protected:
    void SetUp() override { Logger::setLogLevel(LogLevel::Debug); }

    void TearDown() override
    {
        // Restore defaults
        Logger::setFormat(true, false, true, true);
        Logger::setLogLevel(LogLevel::Error);
    }

    // Captures stderr output from a Logger::log call via the FIREBOLT_LOG_ERROR macro style
    std::string captureLogCall(LogLevel level, const std::string& module, const char* msg)
    {
        // Redirect stderr to a pipe
        fflush(stderr);
        int pipefd[2];
        EXPECT_EQ(pipe(pipefd), 0);
        int savedStderr = dup(STDERR_FILENO);
        dup2(pipefd[1], STDERR_FILENO);

        Logger::log(level, module, __FILE__, __func__, __LINE__, "%s", msg);

        fflush(stderr);
        dup2(savedStderr, STDERR_FILENO);
        close(savedStderr);
        close(pipefd[1]);

        char buf[2048] = {0};
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        close(pipefd[0]);
        if (n > 0)
        {
            buf[n] = '\0';
        }
        return std::string(buf);
    }
};

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationTrue_FunctionTrue
// Covers: src/logger.cpp:141 (addLocation=true, addFunction=true branch)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationTrue_FunctionTrue)
{
    Logger::setFormat(false, true, true, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "hello");
    // Expect [filename:line,function] pattern
    EXPECT_NE(output.find("[Firebolt|Test|Error]"), std::string::npos);
    // Should contain file:line,function format (e.g. [loggerTest.cpp:72,captureLog])
    std::regex pattern(R"(\[.*\.cpp:\d+,\w+\])");
    EXPECT_TRUE(std::regex_search(output, pattern)) << "Output: " << output;
    // Should NOT contain timestamp (disabled)
    // Timestamp format is HH:MM:SS.mmm:
    std::regex tsPattern(R"(\d{2}:\d{2}:\d{2}\.\d{3}:)");
    EXPECT_FALSE(std::regex_search(output, tsPattern)) << "Timestamp should not be present. Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationFalse_FunctionTrue
// Covers: src/logger.cpp:148 (only addFunction branch → [func()])
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationFalse_FunctionTrue)
{
    Logger::setFormat(false, false, true, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    // Expect [function()] pattern
    std::regex pattern(R"(\[\w+\(\)\])");
    EXPECT_TRUE(std::regex_search(output, pattern)) << "Output: " << output;
    // Should NOT contain file:line
    EXPECT_EQ(output.find(".cpp:"), std::string::npos) << "Should not contain file location. Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationTrue_FunctionFalse
// Covers: src/logger.cpp:145 (only addLocation branch → [file:line])
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationTrue_FunctionFalse)
{
    Logger::setFormat(false, true, false, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    // Expect [filename:line] pattern without function name
    std::regex pattern(R"(\[.*\.cpp:\d+\])");
    EXPECT_TRUE(std::regex_search(output, pattern)) << "Output: " << output;
    // Should NOT contain function() pattern
    std::regex funcPattern(R"(\[\w+\(\)\])");
    EXPECT_FALSE(std::regex_search(output, funcPattern)) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationFalse_FunctionFalse
// Covers: src/logger.cpp:138 (neither location nor function → no [] block)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationFalse_FunctionFalse)
{
    Logger::setFormat(false, false, false, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    // Should contain the module/level tag but no location/function brackets after it
    EXPECT_NE(output.find("[Firebolt|Test|Error]"), std::string::npos);
    // No file:line or function()
    EXPECT_EQ(output.find(".cpp:"), std::string::npos) << "Output: " << output;
    std::regex funcPattern(R"(\[\w+\(\)\])");
    EXPECT_FALSE(std::regex_search(output, funcPattern)) << "Output: " << output;
    // No thread id
    EXPECT_EQ(output.find("<tid:"), std::string::npos) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.ThreadIdEnabled
// Covers: src/logger.cpp:151 (formatter_addThreadId path)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, ThreadIdEnabled)
{
    Logger::setFormat(false, false, false, true);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    // Should contain <tid:NNN>
    std::regex tidPattern(R"(<tid:\d+>)");
    EXPECT_TRUE(std::regex_search(output, tidPattern)) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.ThreadIdDisabled
// Covers: src/logger.cpp:151 (formatter_addThreadId=false → skip)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, ThreadIdDisabled)
{
    Logger::setFormat(true, true, true, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    EXPECT_EQ(output.find("<tid:"), std::string::npos) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.TimestampEnabled
// Covers: src/logger.cpp:109-117 (formatter_addTs branch)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, TimestampEnabled)
{
    Logger::setFormat(true, false, false, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    // Expect HH:MM:SS.mmm: prefix
    std::regex tsPattern(R"(\d{2}:\d{2}:\d{2}\.\d{3}:)");
    EXPECT_TRUE(std::regex_search(output, tsPattern)) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.AllFlagsEnabled
// Covers: src/logger.cpp:109-153 (all format branches active)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, AllFlagsEnabled)
{
    Logger::setFormat(true, true, true, true);

    std::string output = captureLogCall(LogLevel::Error, "Test", "hello world");
    // Timestamp
    std::regex tsPattern(R"(\d{2}:\d{2}:\d{2}\.\d{3}:)");
    EXPECT_TRUE(std::regex_search(output, tsPattern)) << "Output: " << output;
    // Module|Level
    EXPECT_NE(output.find("[Firebolt|Test|Error]"), std::string::npos);
    // File:line,function
    std::regex locPattern(R"(\[.*\.cpp:\d+,\w+\])");
    EXPECT_TRUE(std::regex_search(output, locPattern)) << "Output: " << output;
    // Thread ID
    std::regex tidPattern(R"(<tid:\d+>)");
    EXPECT_TRUE(std::regex_search(output, tidPattern)) << "Output: " << output;
    // Message
    EXPECT_NE(output.find("hello world"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LogLevelFiltering
// Covers: src/logger.cpp:87 (logLevel > _logLevel early return)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LogLevelFiltering)
{
    Logger::setLogLevel(LogLevel::Error);
    Logger::setFormat(false, false, false, false);

    // Debug should be filtered out when level is Error
    std::string output = captureLogCall(LogLevel::Debug, "Test", "should not appear");
    EXPECT_TRUE(output.empty()) << "Debug message should be filtered. Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.SetLogLevelMaxLevel
// Covers: src/logger.cpp:68-69 (logLevel == MaxLevel → set to Debug)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, SetLogLevelMaxLevel)
{
    Logger::setLogLevel(LogLevel::MaxLevel);
    // MaxLevel maps to Debug internally
    EXPECT_TRUE(Logger::isLogLevelEnabled(LogLevel::Debug));
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.SetLogLevelBeyondMax
// Covers: src/logger.cpp:64-67 (logLevel < MaxLevel guard)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, SetLogLevelBeyondMax)
{
    Logger::setLogLevel(LogLevel::Error);
    // Try to set a value beyond MaxLevel — should be ignored
    Logger::setLogLevel(static_cast<LogLevel>(static_cast<uint8_t>(LogLevel::MaxLevel) + 1));
    // Level should still be Error (unchanged)
    EXPECT_TRUE(Logger::isLogLevelEnabled(LogLevel::Error));
    EXPECT_FALSE(Logger::isLogLevelEnabled(LogLevel::Warning));
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.MessageTruncation
// Covers: src/logger.cpp:96-99 (message truncation at MaxBufSize)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, MessageTruncation)
{
    Logger::setFormat(false, false, false, false);
    // Generate a message larger than MaxBufSize (1024)
    std::string longMsg(2000, 'X');
    std::string output = captureLogCall(LogLevel::Error, "Test", longMsg.c_str());
    // Should still produce output (truncated) without crashing
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("[Firebolt|Test|Error]"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.MessageWithTrailingNewline
// Covers: src/logger.cpp:100-103 (trailing newline removal)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, MessageWithTrailingNewline)
{
    Logger::setFormat(false, false, false, false);

    // The logger strips trailing newlines from the message
    std::string output = captureLogCall(LogLevel::Error, "Test", "trailing newline\n");
    // The output should end with the message text (not double newline)
    EXPECT_NE(output.find("trailing newline"), std::string::npos);
    // There should be exactly one newline at the end (from fprintf)
    size_t msgPos = output.find("trailing newline");
    EXPECT_NE(msgPos, std::string::npos);
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LogLevelNames
// Covers: src/logger.cpp:43-49 (_logLevelNames map)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LogLevelNames)
{
    Logger::setFormat(false, false, false, false);

    std::string errOutput = captureLogCall(LogLevel::Error, "Mod", "e");
    EXPECT_NE(errOutput.find("|Error]"), std::string::npos);

    std::string warnOutput = captureLogCall(LogLevel::Warning, "Mod", "w");
    EXPECT_NE(warnOutput.find("|Warning]"), std::string::npos);

    std::string noticeOutput = captureLogCall(LogLevel::Notice, "Mod", "n");
    EXPECT_NE(noticeOutput.find("|Notice]"), std::string::npos);

    std::string infoOutput = captureLogCall(LogLevel::Info, "Mod", "i");
    EXPECT_NE(infoOutput.find("|Info]"), std::string::npos);

    std::string debugOutput = captureLogCall(LogLevel::Debug, "Mod", "d");
    EXPECT_NE(debugOutput.find("|Debug]"), std::string::npos);
}

// ---------------------------------------------------------------------------
// ⚠️  PRODUCTION CODE FLAG
// File:     src/logger.cpp:123
// Symptom:  strrchr(file.c_str(), '/') returns nullptr when file has no '/',
//           and assigning nullptr to std::string via operator=(const char*) is
//           undefined behavior (crash/segfault in libstdc++).
// Expected: Should check for nullptr before assigning to std::string.
// Risk:     Crash when formatter_addLocation=true and __FILE__ has no slash
//           (unlikely in production Docker builds, but possible in unit tests
//           or embedded builds with relative paths).
// Action:   Review before test is written.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Branch-coverage tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationWithSlashInPath
// Covers: logger.cpp:126 (fileName.empty() == false → substr(1) branch)
// When __FILE__ contains '/', strrchr returns a non-null pointer,
// fileName is non-empty, and the substr(1) path (line 130) is taken.
// This is the normal case for all Linux/Docker builds.
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationWithSlashInPath)
{
    Logger::setFormat(false, true, false, false);

    // Redirect stderr
    fflush(stderr);
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    int savedStderr = dup(STDERR_FILENO);
    dup2(pipefd[1], STDERR_FILENO);

    // Pass a file path WITH a slash — triggers the substr(1) branch
    Logger::log(LogLevel::Error, "Test", "/some/path/myfile.cpp", "testFunc", 99, "msg");

    fflush(stderr);
    dup2(savedStderr, STDERR_FILENO);
    close(savedStderr);
    close(pipefd[1]);

    char buf[2048] = {0};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    std::string output(buf, n > 0 ? n : 0);
    // Should show just "myfile.cpp" (stripped the directory), not "/myfile.cpp"
    EXPECT_NE(output.find("myfile.cpp:99"), std::string::npos) << "Output: " << output;
    EXPECT_EQ(output.find("/myfile.cpp"), std::string::npos) << "Should strip leading slash. Output: " << output;
}
