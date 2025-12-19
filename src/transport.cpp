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

#include "transport.h"
#include "firebolt/logger.h"
#include "firebolt/types.h"
#include <assert.h>

namespace Firebolt::Transport
{

using client = websocketpp::client<websocketpp::config::asio_client>;
using message_ptr = websocketpp::config::asio_client::message_type::ptr;

enum class Transport::TransportState
{
    NotStarted,
    Connected,
    Disconnected,
};

static Firebolt::Error mapError(websocketpp::lib::error_code error)
{
    using EV = websocketpp::error::value;
    switch (error.value())
    {
    case EV::open_handshake_timeout:
    case EV::close_handshake_timeout:
        return Firebolt::Error::Timedout;
    case EV::con_creation_failed:
    case EV::unrequested_subprotocol:
    case EV::http_connection_ended:
    case EV::general:
    case EV::invalid_port:
    case EV::rejected:
    default:
        return Firebolt::Error::General;
    }
}

Transport::Transport()
    : connectionStatus_(TransportState::NotStarted),
      id_counter_(0),
      debugEnabled_(false)
{
}

Transport::~Transport()
{
    disconnect();
}

void Transport::start()
{
    client_.clear_access_channels(websocketpp::log::alevel::all);
    client_.clear_error_channels(websocketpp::log::elevel::all);

    client_.init_asio();
    client_.start_perpetual();

    connectionThread_.reset(new websocketpp::lib::thread(&client::run, &client_));
    connectionStatus_ = TransportState::Disconnected;
}

Firebolt::Error Transport::connect(std::string url, MessageCallback onMessage, ConnectionCallback onConnectionChange,
                                   std::optional<unsigned> transportLoggingInclude,
                                   std::optional<unsigned> transportLoggingExclude)
{
    if (connectionStatus_ == TransportState::Connected)
    {
        FIREBOLT_LOG_WARNING("Transport", "Connect called when already connected. Ignoring.");
        return Firebolt::Error::AlreadyConnected;
    }

    if (connectionStatus_ == TransportState::NotStarted)
    {
        start();
    }

    assert(onMessage != nullptr);
    assert(onConnectionChange != nullptr);

    messageReceiver_ = onMessage;
    connectionReceiver_ = onConnectionChange;

    websocketpp::log::level include = websocketpp::log::alevel::all;
    websocketpp::log::level exclude = (websocketpp::log::alevel::frame_header |
                                       websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::control);
    if (transportLoggingInclude.has_value())
    {
        include = static_cast<websocketpp::log::level>(transportLoggingInclude.value());
    }
    if (transportLoggingExclude.has_value())
    {
        exclude = static_cast<websocketpp::log::level>(transportLoggingExclude.value());
    }
    setLogging(include, exclude);

    websocketpp::lib::error_code ec;
    client::connection_ptr con = client_.get_connection(url, ec);

    if (ec)
    {
        FIREBOLT_LOG_ERROR("Transport", "Could not create connection because: %s", ec.message().c_str());
        return Firebolt::Error::NotConnected;
    }

    connectionHandle_ = con->get_handle();

    con->set_open_handler(websocketpp::lib::bind(&Transport::onOpen, this, &client_, websocketpp::lib::placeholders::_1));
    con->set_fail_handler(websocketpp::lib::bind(&Transport::onFail, this, &client_, websocketpp::lib::placeholders::_1));
    con->set_close_handler(
        websocketpp::lib::bind(&Transport::onClose, this, &client_, websocketpp::lib::placeholders::_1));

    con->set_message_handler(websocketpp::lib::bind(&Transport::onMessage, this, websocketpp::lib::placeholders::_1,
                                                    websocketpp::lib::placeholders::_2));

    client_.connect(con);

    debugEnabled_ = Logger::isLogLevelEnabled(Firebolt::LogLevel::Debug);

    return Firebolt::Error::None;
}

Firebolt::Error Transport::disconnect()
{
    if (connectionStatus_ == TransportState::NotStarted)
    {
        return Firebolt::Error::None;
    }
    client_.stop_perpetual();

    if (connectionStatus_ == TransportState::Connected)
    {
        websocketpp::lib::error_code ec;
        client_.close(connectionHandle_, websocketpp::close::status::going_away, "", ec);
        if (ec)
        {
            FIREBOLT_LOG_ERROR("Transport", "Error closing connection: %s", ec.message().c_str());
        }
    }

    if (connectionThread_ && connectionThread_->joinable())
    {
        connectionThread_->join();
    }

    connectionStatus_ = TransportState::NotStarted;
    return Firebolt::Error::None;
}

unsigned Transport::getNextMessageID()
{
    return ++id_counter_;
}

Firebolt::Error Transport::send(const std::string& method, const nlohmann::json& params, const unsigned id)
{
    if (connectionStatus_ != TransportState::Connected)
    {
        return Firebolt::Error::NotConnected;
    }

    nlohmann::json msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["method"] = method;
    if (!params.empty())
    {
        msg["params"] = params;
    }
    if (debugEnabled_)
    {
        FIREBOLT_LOG_DEBUG("Transport", "Send: %s", msg.dump().c_str());
    }

    websocketpp::lib::error_code ec;

    client_.send(connectionHandle_, to_string(msg), websocketpp::frame::opcode::text, ec);
    if (ec)
    {
        FIREBOLT_LOG_ERROR("Transport", "Error sending message :%s", ec.message().c_str());
        return mapError(ec);
    }

    return Firebolt::Error::None;
}

void Transport::setLogging(websocketpp::log::level include, websocketpp::log::level exclude)
{
    client_.set_access_channels(include);
    client_.clear_access_channels(exclude);
}

void Transport::onMessage(websocketpp::connection_hdl /* hdl */,
                          websocketpp::client<websocketpp::config::asio_client>::message_ptr msg)
{
    if (msg->get_opcode() != websocketpp::frame::opcode::text)
    {
        return;
    }
    try
    {
        nlohmann::json jsonMsg = nlohmann::json::parse(msg->get_payload());
        if (debugEnabled_)
        {
            FIREBOLT_LOG_DEBUG("Transport", "Received: %s", jsonMsg.dump().c_str());
        }
        messageReceiver_(jsonMsg);
    }
    catch (const std::exception& e)
    {
        FIREBOLT_LOG_ERROR("Transport", "Cannot parse payload: '%s'", msg->get_payload().c_str());
    }
}

void Transport::onOpen(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl)
{
    connectionStatus_ = TransportState::Connected;

    client::connection_ptr con = c->get_con_from_hdl(hdl);
    connectionReceiver_(true, Firebolt::Error::None);
}

void Transport::onClose(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl)
{
    connectionStatus_ = TransportState::Disconnected;
    client::connection_ptr con = c->get_con_from_hdl(hdl);
    connectionReceiver_(false, mapError(con->get_ec()));
}

void Transport::onFail(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl)
{
    connectionStatus_ = TransportState::Disconnected;
    client::connection_ptr con = c->get_con_from_hdl(hdl);
    connectionReceiver_(false, mapError(con->get_ec()));
}

} // namespace Firebolt::Transport
