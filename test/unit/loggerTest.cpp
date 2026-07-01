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
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>
#include <string>
#include <unistd.h>

using namespace Firebolt;

// ---------------------------------------------------------------------------
// File: test/unit/loggerTest.cpp
// Covers: src/logger.cpp — format flag combinations and level filtering
// ---------------------------------------------------------------------------

class LoggerFormatUTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        unsetenv("FIREBOLT_TRANSPORT_LOG_LEVEL");
        Logger::resolveLogLevelFromEnvironment(LogLevel::Debug);
        Logger::setLogLevel(LogLevel::Debug);
    }

    void TearDown() override
    {
        // Restore defaults
        unsetenv("FIREBOLT_TRANSPORT_LOG_LEVEL");
        unsetenv("FIREBOLT_TRANSPORT_LOG_FILE");
        Logger::resolveLogLevelFromEnvironment(LogLevel::Error);
        Logger::setFormat(true, false, true, true);
        Logger::setLogLevel(LogLevel::Error);
    }

    // Captures stderr output from a Logger::log call via the FIREBOLT_LOG_ERROR macro style
    std::string captureLogCall(LogLevel level, const std::string& module, const char* msg)
    {
        // Redirect stderr to a pipe
        fflush(stderr);
        int pipefd[2] = {-1, -1};
        if (pipe(pipefd) != 0)
        {
            ADD_FAILURE() << "pipe() failed while capturing logger output";
            return std::string();
        }
        int savedStderr = dup(STDERR_FILENO);
        if (savedStderr < 0)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            ADD_FAILURE() << "dup(STDERR_FILENO) failed while capturing logger output";
            return std::string();
        }
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
// Covers: logger.cpp format branch (addLocation=true, addFunction=true)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationTrue_FunctionTrue)
{
    Logger::setFormat(false, true, true, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "hello");
    // Expect [filename:line,function] pattern
    EXPECT_NE(output.find("[FireboltNative|Test|Error]"), std::string::npos);
    // Should contain file:line,function format (e.g. [loggerTest.cpp:72,captureLog])
    std::regex pattern(R"(\[.*\.cpp:\d+,\w+\])");
    EXPECT_TRUE(std::regex_search(output, pattern)) << "Output: " << output;
    // Should NOT contain timestamp (disabled)
    // Timestamp format is HH:MM:SS.mmm:
    std::regex tsPattern(R"(\d{2}:\d{2}:\d{2}\.\d{3}:)");
    EXPECT_FALSE(std::regex_search(output, tsPattern)) << "Timestamp should not be present. Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.ResolveLogLevelFromEnvironment
// Covers: logger.cpp env log-level parser used by resolveLogLevelFromEnvironment
// Scenario type: success + edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, ResolveLogLevelFromEnvironment)
{
    unsetenv("FIREBOLT_TRANSPORT_LOG_LEVEL");
    EXPECT_EQ(Logger::resolveLogLevelFromEnvironment(LogLevel::Warning), LogLevel::Warning);

    setenv("FIREBOLT_TRANSPORT_LOG_LEVEL", "debug", 1);
    EXPECT_EQ(Logger::resolveLogLevelFromEnvironment(LogLevel::Error), LogLevel::Debug);

    setenv("FIREBOLT_TRANSPORT_LOG_LEVEL", "bogus", 1);
    EXPECT_EQ(Logger::resolveLogLevelFromEnvironment(LogLevel::Notice), LogLevel::Notice);

    setenv("FIREBOLT_TRANSPORT_LOG_LEVEL", "off", 1);
    EXPECT_EQ(Logger::resolveLogLevelFromEnvironment(LogLevel::Info), LogLevel::Info);
    EXPECT_FALSE(Logger::isLogLevelEnabled(LogLevel::Error));
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LogLevelOffDisablesAllLogging
// Covers: logger.cpp env parser and runtime suppression when level is set to off
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LogLevelOffDisablesAllLogging)
{
    Logger::setFormat(false, false, false, false);
    Logger::setLogLevel(LogLevel::Debug);

    setenv("FIREBOLT_TRANSPORT_LOG_LEVEL", "off", 1);
    Logger::setLogLevel(Logger::resolveLogLevelFromEnvironment(LogLevel::Debug));

    // No output should be emitted at any level while disabled.
    std::string output = captureLogCall(LogLevel::Error, "Test", "should not appear");
    EXPECT_TRUE(output.empty()) << "Expected no output when log level is off. Output: " << output;

    // Restoring env should re-enable logging.
    unsetenv("FIREBOLT_TRANSPORT_LOG_LEVEL");
    Logger::setLogLevel(Logger::resolveLogLevelFromEnvironment(LogLevel::Debug));
    output = captureLogCall(LogLevel::Error, "Test", "should appear");
    EXPECT_NE(output.find("[FireboltNative|Test|Error]"), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LogFileSink
// Covers: logger.cpp env logfile sink + stderr fallback bypass when file sink works
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LogFileSink)
{
    Logger::setFormat(false, false, false, false);
    Logger::setLogLevel(LogLevel::Error);

    char pathTemplate[] = "/tmp/firebolt-transport-logger-test-XXXXXX";
    int fd = mkstemp(pathTemplate);
    ASSERT_GE(fd, 0);
    close(fd);

    setenv("FIREBOLT_TRANSPORT_LOG_FILE", pathTemplate, 1);

    // With file sink configured, this should not go to stderr.
    const std::string stderrOutput = captureLogCall(LogLevel::Error, "Test", "file sink payload");
    EXPECT_TRUE(stderrOutput.empty()) << "Expected file sink to bypass stderr. Output: " << stderrOutput;

    std::ifstream in(pathTemplate);
    ASSERT_TRUE(in.good()) << "Could not open log file: " << pathTemplate;
    std::string fileContents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(fileContents.find("[FireboltNative|Test|Error]"), std::string::npos) << fileContents;
    EXPECT_NE(fileContents.find("file sink payload"), std::string::npos) << fileContents;

    std::remove(pathTemplate);
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LogFileSinkFallbackToStderrOnOpenFailure
// Covers: logger.cpp fallback path when FIREBOLT_TRANSPORT_LOG_FILE cannot be opened
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LogFileSinkFallbackToStderrOnOpenFailure)
{
    Logger::setFormat(false, false, false, false);
    Logger::setLogLevel(LogLevel::Error);

    // A directory path is not writable as a regular file with fopen("a").
    setenv("FIREBOLT_TRANSPORT_LOG_FILE", "/tmp", 1);

    const std::string stderrOutput = captureLogCall(LogLevel::Error, "Test", "fallback payload");

    EXPECT_NE(stderrOutput.find("[FireboltNative|Test|Error]"), std::string::npos) << stderrOutput;
    EXPECT_NE(stderrOutput.find("fallback payload"), std::string::npos) << stderrOutput;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationFalse_FunctionTrue
// Covers: logger.cpp format branch (addFunction only → [func()])
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
// Covers: logger.cpp format branch (addLocation only → [file:line])
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
// Covers: logger.cpp format branch (no location/function → no [] block)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationFalse_FunctionFalse)
{
    Logger::setFormat(false, false, false, false);

    std::string output = captureLogCall(LogLevel::Error, "Test", "msg");
    // Should contain the module/level tag but no location/function brackets after it
    EXPECT_NE(output.find("[FireboltNative|Test|Error]"), std::string::npos);
    // No file:line or function()
    EXPECT_EQ(output.find(".cpp:"), std::string::npos) << "Output: " << output;
    std::regex funcPattern(R"(\[\w+\(\)\])");
    EXPECT_FALSE(std::regex_search(output, funcPattern)) << "Output: " << output;
    // No thread id
    EXPECT_EQ(output.find("<tid:"), std::string::npos) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.ThreadIdEnabled
// Covers: logger.cpp formatter_addThreadId true branch
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
// Covers: logger.cpp formatter_addThreadId false branch
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
// Covers: logger.cpp formatter_addTs timestamp formatting branch
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
// Covers: logger.cpp all format branches active simultaneously
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
    EXPECT_NE(output.find("[FireboltNative|Test|Error]"), std::string::npos);
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
// Covers: logger.cpp logLevel filtering early return
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
// Covers: logger.cpp setLogLevel MaxLevel → Debug mapping
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
// Covers: logger.cpp setLogLevel out-of-range guard
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
// Covers: logger.cpp message truncation at MaxBufSize
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, MessageTruncation)
{
    Logger::setFormat(false, false, false, false);
    // Generate a message larger than MaxBufSize (1024)
    std::string longMsg(2000, 'X');
    std::string output = captureLogCall(LogLevel::Error, "Test", longMsg.c_str());
    EXPECT_NE(output.find("[FireboltNative|Test|Error]"), std::string::npos);
    // The raw message is truncated to MaxBufSize-1 chars, and the formatted
    // output buffer (also MaxBufSize) further limits it because the prefix
    // consumes space.  Verify truncation actually occurred:
    size_t xCount = std::count(output.begin(), output.end(), 'X');
    EXPECT_GT(xCount, 0u) << "Truncated output should still contain message characters";
    EXPECT_LT(xCount, longMsg.size()) << "Message should have been truncated to fit MaxBufSize";
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.MessageWithTrailingNewline
// Covers: logger.cpp trailing newline removal
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, MessageWithTrailingNewline)
{
    Logger::setFormat(false, false, false, false);

    // The logger strips trailing newlines from the message
    std::string output = captureLogCall(LogLevel::Error, "Test", "trailing newline\n");
    // The output should contain the message text
    size_t msgPos = output.find("trailing newline");
    ASSERT_NE(msgPos, std::string::npos);
    // After stripping the user's \n, only fprintf's \n should remain:
    // the text "trailing newline" should be followed by exactly "\n" (end of output)
    std::string afterMsg = output.substr(msgPos + sizeof("trailing newline") - 1);
    EXPECT_EQ(afterMsg, "\n") << "Expected exactly one trailing newline. Got: [" << afterMsg << "]";
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LogLevelNames
// Covers: logger.cpp _logLevelNames map entries
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
// Test name: LoggerFormatUTest.LocationWithoutSlashInPath
// Covers: src/logger.cpp (std::filesystem::path::filename() with no directory)
// Regression: previously assigned nullptr to std::string (UB / crash)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, LocationWithoutSlashInPath)
{
    Logger::setFormat(false, true, false, false);

    // Redirect stderr
    fflush(stderr);
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0)
    {
        ADD_FAILURE() << "pipe() failed while capturing logger output";
        return;
    }
    int savedStderr = dup(STDERR_FILENO);
    if (savedStderr < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        ADD_FAILURE() << "dup(STDERR_FILENO) failed while capturing logger output";
        return;
    }
    dup2(pipefd[1], STDERR_FILENO);

    // Pass a file path WITHOUT a slash — previously caused nullptr UB
    Logger::log(LogLevel::Error, "Test", "noSlashFile.cpp", "testFunc", 42, "msg");

    fflush(stderr);
    dup2(savedStderr, STDERR_FILENO);
    close(savedStderr);
    close(pipefd[1]);

    char buf[2048] = {0};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    std::string output(buf, n > 0 ? n : 0);
    // Should use the bare filename as-is
    EXPECT_NE(output.find("noSlashFile.cpp:42"), std::string::npos) << "Output: " << output;
}

// ---------------------------------------------------------------------------
// Branch-coverage tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationWithSlashInPath
// Covers: logger.cpp (std::filesystem::path::filename() strips directory)
// When __FILE__ contains '/', the filename is extracted from the path.
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
    if (savedStderr < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        ADD_FAILURE() << "dup(STDERR_FILENO) failed while capturing logger output";
        return;
    }
    dup2(pipefd[1], STDERR_FILENO);

    // Pass a file path with slashes so the logger strips the directory and keeps only the filename
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

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.EmptyMessageDoesNotCrash
// Covers: logger.cpp empty formatted message handling
// Scenario type: negative / stability
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, EmptyMessageDoesNotCrash)
{
    EXPECT_EXIT(
        {
            Logger::setFormat(false, false, false, false);
            Logger::setLogLevel(LogLevel::Error);
            Logger::log(LogLevel::Error, "Test", "f.cpp", "fn", 1, "%s", "");
            _exit(0);
        },
        ::testing::ExitedWithCode(0), ".*");
}

// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.MalformedFormatDoesNotCrash
// Covers: logger.cpp behavior when vsnprintf reports formatting error
// Scenario type: negative / stability
// ---------------------------------------------------------------------------
TEST_F(LoggerFormatUTest, MalformedFormatDoesNotCrash)
{
    EXPECT_EXIT(
        {
            Logger::setFormat(false, false, false, false);
            Logger::setLogLevel(LogLevel::Error);
            const char* malformedFormat = "%";
            Logger::log(LogLevel::Error, "Test", "f.cpp", "fn", 1, malformedFormat);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), ".*");
}
