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

#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>

inline uint16_t reserveUnusedLoopbackPort()
{
    websocketpp::lib::asio::io_service io;
    websocketpp::lib::asio::ip::tcp::acceptor acceptor(io);
    websocketpp::lib::asio::error_code ec;

    acceptor.open(websocketpp::lib::asio::ip::tcp::v4(), ec);
    if (ec)
    {
        return 0;
    }
    acceptor.bind(websocketpp::lib::asio::ip::tcp::endpoint(websocketpp::lib::asio::ip::address::from_string(
                                                                "127.0.0.1"),
                                                            0),
                  ec);
    if (ec)
    {
        return 0;
    }
    acceptor.listen(websocketpp::lib::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        return 0;
    }

    const uint16_t port = acceptor.local_endpoint(ec).port();
    acceptor.close(ec);
    return ec ? 0 : port;
}

inline bool waitForAtomicAtLeast(std::atomic<int>& value, int target, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (value.load() >= target)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return value.load() >= target;
}