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
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

namespace Firebolt::Transport
{
using MessageCallback = std::function<void(const nlohmann::json& message)>;
using ConnectionCallback = std::function<void(const bool connected, Firebolt::Error error)>;

class Transport
{
public:
    Transport();
    Transport(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport& operator=(Transport&&) = delete;
    virtual ~Transport();

    Firebolt::Error connect(std::string url, MessageCallback onMessage, ConnectionCallback onConnectionChange,
                            std::optional<unsigned> transportLoggingInclude = std::nullopt,
                            std::optional<unsigned> transportLoggingExclude = std::nullopt);
    Firebolt::Error disconnect();
    unsigned getNextMessageID();
    Firebolt::Error send(const std::string& method, const nlohmann::json& params, const unsigned id);

private:
    void start();
    void setLogging(websocketpp::log::level include, websocketpp::log::level exclude = 0);
    void onMessage(websocketpp::connection_hdl hdl,
                   websocketpp::client<websocketpp::config::asio_client>::message_ptr msg);
    void onOpen(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl);
    void onClose(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl);
    void onFail(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl);

private:
    enum class TransportState;

    std::atomic<TransportState> connectionStatus_;
    std::atomic<unsigned> id_counter_ = 0;
    bool debugEnabled_ = false;
    MessageCallback messageReceiver_;
    ConnectionCallback connectionReceiver_;
    websocketpp::client<websocketpp::config::asio_client> client_;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> connectionThread_;
    websocketpp::connection_hdl connectionHandle_;
};
} // namespace Firebolt::Transport
