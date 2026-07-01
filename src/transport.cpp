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

#include "transport.h"
#include "firebolt/logger.h"
#include "firebolt/types.h"
#include <assert.h>
#include <chrono>
#include <memory>

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
    : client_(std::make_unique<client>()),
      connectionStatus_(TransportState::NotStarted),
      id_counter_(0),
      debugEnabled_(false)
{
}

Transport::~Transport()
{
    disconnect();
    stopMessageWorker();
}

void Transport::start()
{
    FIREBOLT_LOG_DEBUG("Transport", "[start] initializing websocket client (state=%d)",
                       static_cast<int>(connectionStatus_.load()));
    client_->clear_access_channels(websocketpp::log::alevel::all);
    client_->clear_error_channels(websocketpp::log::elevel::all);

    client_->init_asio();
    client_->start_perpetual();

    connectionThread_.reset(new websocketpp::lib::thread(&client::run, client_.get()));
    FIREBOLT_LOG_DEBUG("Transport", "[start] connection thread started");
    startMessageWorker();
    connectionStatus_ = TransportState::Disconnected;
    FIREBOLT_LOG_DEBUG("Transport", "[start] transport state transitioned to %d",
                       static_cast<int>(connectionStatus_.load()));
}

void Transport::startMessageWorker()
{
    if (messageWorkerThread_.joinable())
    {
        FIREBOLT_LOG_DEBUG("Transport", "[message-worker] start requested but worker already running");
        return;
    }
    stopMessageWorker_ = false;
    messageWorkerThread_ = std::thread(&Transport::processQueuedMessages, this);
    FIREBOLT_LOG_DEBUG("Transport", "[message-worker] started");
}

void Transport::stopMessageWorker()
{
    FIREBOLT_LOG_DEBUG("Transport", "[message-worker] stop requested");
    stopMessageWorker_ = true;
    messageQueueCv_.notify_all();

    if (messageWorkerThread_.joinable())
    {
        messageWorkerThread_.join();
        FIREBOLT_LOG_DEBUG("Transport", "[message-worker] joined");
    }
}

void Transport::processQueuedMessages()
{
    while (true)
    {
        std::string payload;
        {
            std::unique_lock<std::mutex> lock(messageQueueMutex_);
            messageQueueCv_.wait(lock, [this] { return stopMessageWorker_ || !messageQueue_.empty(); });

            if (messageQueue_.empty())
            {
                if (stopMessageWorker_)
                {
                    FIREBOLT_LOG_DEBUG("Transport", "[message-worker] exiting (stop flag set, queue drained)");
                    return;
                }
                continue;
            }

            payload = std::move(messageQueue_.front());
            messageQueue_.pop();
            FIREBOLT_LOG_DEBUG("Transport", "[message-worker] dequeued payload bytes=%zu, remaining_queue=%zu",
                               payload.size(), messageQueue_.size());
        }
        try
        {
            nlohmann::json jsonMsg = nlohmann::json::parse(payload);
            if (debugEnabled_)
            {
                FIREBOLT_LOG_DEBUG("Transport", "Received: %s", jsonMsg.dump().c_str());
            }
            messageReceiver_(jsonMsg);
        }
        catch (const std::exception&)
        {
            FIREBOLT_LOG_ERROR("Transport", "Cannot parse payload: '%s'", payload.c_str());
        }
    }
}

Firebolt::Error Transport::connect(std::string url, MessageCallback onMessage, ConnectionCallback onConnectionChange,
                                   std::optional<unsigned> transportLoggingInclude,
                                   std::optional<unsigned> transportLoggingExclude,
                                   const std::map<std::string, std::string>& headers)
{
    if (connectionStatus_ == TransportState::Connected)
    {
        FIREBOLT_LOG_WARNING("Transport", "Connect called when already connected. Ignoring");
        return Firebolt::Error::AlreadyConnected;
    }

    FIREBOLT_LOG_DEBUG("Transport", "[connect] requested url=%s, current_state=%d, header_count=%zu", url.c_str(),
                       static_cast<int>(connectionStatus_.load()), headers.size());

    if (connectionStatus_ == TransportState::NotStarted)
    {
        debugEnabled_ = Logger::isLogLevelEnabled(Firebolt::LogLevel::Debug);
        FIREBOLT_LOG_DEBUG("Transport", "[connect] debugEnabled=%s", debugEnabled_ ? "true" : "false");
        start();
    }

    assert(onMessage != nullptr);
    assert(onConnectionChange != nullptr);

    messageReceiver_ = onMessage;
    connectionReceiver_ = onConnectionChange;

    websocketpp::log::level include = websocketpp::log::alevel::none;
    websocketpp::log::level exclude = websocketpp::log::alevel::none;
    if (debugEnabled_)
    {
        include = websocketpp::log::alevel::all & ~websocketpp::log::alevel::frame_payload;
        exclude = (websocketpp::log::alevel::frame_header | websocketpp::log::alevel::control);
    }
    if (transportLoggingInclude.has_value())
    {
        include = static_cast<websocketpp::log::level>(transportLoggingInclude.value());
    }
    if (transportLoggingExclude.has_value())
    {
        exclude = static_cast<websocketpp::log::level>(transportLoggingExclude.value());
    }
    if (!(include & websocketpp::log::alevel::frame_payload))
    {
        exclude |= websocketpp::log::alevel::frame_payload;
    }
    setLogging(include, exclude);
    FIREBOLT_LOG_DEBUG("Transport", "[connect] websocket logging masks include=0x%x exclude=0x%x",
                       static_cast<unsigned>(include), static_cast<unsigned>(exclude));

    websocketpp::lib::error_code ec;
    client::connection_ptr con = client_->get_connection(url, ec);

    if (ec)
    {
        FIREBOLT_LOG_ERROR("Transport", "Could not create connection because: %s", ec.message().c_str());
        return Firebolt::Error::NotConnected;
    }

    if (!con)
    {
        FIREBOLT_LOG_ERROR("Transport", "Connection pointer is null after get_connection.");
        return Firebolt::Error::NotConnected;
    }

    // Inject custom headers before connecting
    for (const auto& header : headers)
    {
        con->replace_header(header.first, header.second);
        FIREBOLT_LOG_DEBUG("Transport", "[connect] injected header '%s'", header.first.c_str());
    }

    connectionHandle_ = con->get_handle();

    con->set_open_handler(
        websocketpp::lib::bind(&Transport::onOpen, this, client_.get(), websocketpp::lib::placeholders::_1));
    con->set_fail_handler(
        websocketpp::lib::bind(&Transport::onFail, this, client_.get(), websocketpp::lib::placeholders::_1));
    con->set_close_handler(
        websocketpp::lib::bind(&Transport::onClose, this, client_.get(), websocketpp::lib::placeholders::_1));

    con->set_message_handler(websocketpp::lib::bind(&Transport::onMessage, this, websocketpp::lib::placeholders::_1,
                                                    websocketpp::lib::placeholders::_2));

    client_->connect(con);
    FIREBOLT_LOG_DEBUG("Transport", "[connect] connect() dispatched to websocket client");

    return Firebolt::Error::None;
}

Firebolt::Error Transport::disconnect()
{
    FIREBOLT_LOG_DEBUG("Transport", "[disconnect] requested, state=%d", static_cast<int>(connectionStatus_.load()));
    if (connectionStatus_ == TransportState::NotStarted)
    {
        return Firebolt::Error::None;
    }
    client_->stop_perpetual();

    if (connectionStatus_ == TransportState::Connected)
    {
        // Shorten the close-handshake timeout so that join() below does not block
        // for the full websocketpp default (5 s) if the gateway is unresponsive.
        // get_con_from_hdl() throws bad_weak_ptr when the connection has already
        // been torn down at the network level, in which case close() will fail
        // gracefully via its error_code path.
        try
        {
            auto con = client_->get_con_from_hdl(connectionHandle_);
            con->set_close_handshake_timeout(100);
        }
        catch (const std::bad_weak_ptr& ex)
        {
            FIREBOLT_LOG_WARNING("Transport", "Could not set close handshake timeout: %s", ex.what());
        }

        websocketpp::lib::error_code ec;
        FIREBOLT_LOG_DEBUG("Transport", "[disconnect] close() start (handshake timeout=100ms)");
        client_->close(connectionHandle_, websocketpp::close::status::going_away, "", ec);
        if (ec)
        {
            FIREBOLT_LOG_ERROR("Transport", "Error closing connection: %s", ec.message().c_str());
        }
    }

    FIREBOLT_LOG_DEBUG("Transport", "[disconnect] waiting for connectionThread join (close handshake in progress)...");
    auto t0_ct = std::chrono::steady_clock::now();
    if (connectionThread_ && connectionThread_->joinable())
    {
        connectionThread_->join();
    }
    FIREBOLT_LOG_INFO("Transport", "[disconnect] connectionThread joined in %ld ms",
                      static_cast<long>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_ct)
                              .count()));

    FIREBOLT_LOG_DEBUG("Transport", "[disconnect] stopping message worker...");
    auto t0_mw = std::chrono::steady_clock::now();
    stopMessageWorker();
    FIREBOLT_LOG_DEBUG("Transport", "[disconnect] message worker stopped in %ld ms",
                       static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - t0_mw)
                                             .count()));

    client_ = std::make_unique<client>();
    connectionStatus_ = TransportState::NotStarted;
    FIREBOLT_LOG_DEBUG("Transport", "[disconnect] transport reset complete, state=%d",
                       static_cast<int>(connectionStatus_.load()));
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
        FIREBOLT_LOG_WARNING("Transport", "[send] rejected method='%s' id=%u (state=%d)", method.c_str(), id,
                             static_cast<int>(connectionStatus_.load()));
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

    client_->send(connectionHandle_, to_string(msg), websocketpp::frame::opcode::text, ec);
    if (ec)
    {
        FIREBOLT_LOG_ERROR("Transport", "Error sending message :%s", ec.message().c_str());
        return mapError(ec);
    }

    return Firebolt::Error::None;
}

void Transport::setLogging(websocketpp::log::level include, websocketpp::log::level exclude)
{
    FIREBOLT_LOG_DEBUG("Transport", "[setLogging] include=0x%x exclude=0x%x", static_cast<unsigned>(include),
                       static_cast<unsigned>(exclude));
    client_->set_access_channels(include);
    client_->clear_access_channels(exclude);
}

void Transport::onMessage(websocketpp::connection_hdl /* hdl */,
                          websocketpp::client<websocketpp::config::asio_client>::message_ptr msg)
{
    if (msg->get_opcode() != websocketpp::frame::opcode::text)
    {
        FIREBOLT_LOG_WARNING("Transport", "Received a non-text message. Ignoring");
        return;
    }
    if (stopMessageWorker_)
    {
        FIREBOLT_LOG_WARNING("Transport", "Received a message while stopping the message worker. Ignoring");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(messageQueueMutex_);
        messageQueue_.push(msg->get_payload());
        FIREBOLT_LOG_DEBUG("Transport", "[onMessage] enqueued payload bytes=%zu, queue_depth=%zu",
                           msg->get_payload().size(), messageQueue_.size());
    }
    messageQueueCv_.notify_one();
}

void Transport::onOpen(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl)
{
    connectionStatus_ = TransportState::Connected;

    client::connection_ptr con = c->get_con_from_hdl(hdl);
    // Populate responseHeaders_ from the connection's response headers
    {
        std::lock_guard<std::mutex> lock(responseHeadersMutex_);
        responseHeaders_.clear();
        if (con)
        {
            const auto& headers = con->get_response().get_headers();
            for (const auto& header : headers)
            {
                responseHeaders_[header.first] = header.second;
            }
            FIREBOLT_LOG_DEBUG("Transport", "[onOpen] stored response headers=%zu", responseHeaders_.size());
        }
    }
    FIREBOLT_LOG_NOTICE("Transport", "Connection opened (state=%d)", static_cast<int>(connectionStatus_.load()));
    connectionReceiver_(true, Firebolt::Error::None);
}

void Transport::onClose(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl)
{
    connectionStatus_ = TransportState::Disconnected;
    Firebolt::Error mappedError = Firebolt::Error::General;
    try
    {
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        if (con)
        {
            mappedError = mapError(con->get_ec());
            FIREBOLT_LOG_WARNING("Transport", "Connection closed: ws_ec=%d ('%s'), mapped_error=%d", con->get_ec().value(),
                                 con->get_ec().message().c_str(), static_cast<int>(mappedError));
        }
        else
        {
            FIREBOLT_LOG_WARNING("Transport", "Connection closed: connection handle resolved to null");
        }
    }
    catch (const std::exception& ex)
    {
        FIREBOLT_LOG_WARNING("Transport", "Connection closed: failed to resolve handle (%s)", ex.what());
    }
    connectionReceiver_(false, mappedError);
}

void Transport::onFail(websocketpp::client<websocketpp::config::asio_client>* c, websocketpp::connection_hdl hdl)
{
    connectionStatus_ = TransportState::Disconnected;
    Firebolt::Error mappedError = Firebolt::Error::General;
    try
    {
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        if (con)
        {
            mappedError = mapError(con->get_ec());
            FIREBOLT_LOG_ERROR("Transport", "Connection failed: ws_ec=%d ('%s'), mapped_error=%d", con->get_ec().value(),
                               con->get_ec().message().c_str(), static_cast<int>(mappedError));
        }
        else
        {
            FIREBOLT_LOG_ERROR("Transport", "Connection failed: connection handle resolved to null");
        }
    }
    catch (const std::exception& ex)
    {
        FIREBOLT_LOG_ERROR("Transport", "Connection failed: failed to resolve handle (%s)", ex.what());
    }
    connectionReceiver_(false, mappedError);
}

std::optional<std::string> Transport::getResponseHeader(const std::string& headerName)
{
    std::lock_guard<std::mutex> lock(responseHeadersMutex_);
    auto it = responseHeaders_.find(headerName);
    if (it != responseHeaders_.end())
    {
        return it->second;
    }
    return std::nullopt;
}
} // namespace Firebolt::Transport
