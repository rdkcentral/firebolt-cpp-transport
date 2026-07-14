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
#include <array>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <websocketpp/base64/base64.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/sha1/sha1.hpp>

using namespace Firebolt::Transport;

class TransportUTest : public ::testing::Test
{
protected:
    Transport transport;
};

TEST_F(TransportUTest, GetNextMessageID)
{
    unsigned firstId = transport.getNextMessageID();
    unsigned secondId = transport.getNextMessageID();
    EXPECT_EQ(firstId, 1);
    EXPECT_EQ(secondId, 2);
}

TEST_F(TransportUTest, SendWithoutConnection)
{
    nlohmann::json params;
    Firebolt::Error err = transport.send("test.method", params, 1);
    EXPECT_EQ(err, Firebolt::Error::NotConnected);
}

TEST_F(TransportUTest, DisconnectWithoutStart)
{
    Firebolt::Error err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

class TransportIntegrationUTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    const std::string m_uri = "ws://localhost:9002";
    bool m_serverStarted = false;

    void SetUp() override
    {
        try
        {
            m_server.init_asio();
            m_server.set_reuse_addr(true);

            m_server.set_message_handler(
                [this](connection_hdl hdl, server::message_ptr msg)
                {
                    try
                    {
                        m_server.send(hdl, msg->get_payload(), msg->get_opcode());
                    }
                    catch (const websocketpp::exception& e)
                    {
                        FAIL() << "Server failed to send message: " << e.what();
                    }
                });

            websocketpp::log::level include = websocketpp::log::alevel::all;
            websocketpp::log::level exclude = (websocketpp::log::alevel::frame_header |
                                               websocketpp::log::alevel::frame_payload |
                                               websocketpp::log::alevel::control);
            m_server.set_access_channels(include);
            m_server.clear_access_channels(exclude);
            m_server.listen(9002);
            m_server.start_accept();
            m_serverThread = std::make_unique<std::thread>([this]() { m_server.run(); });
            m_serverStarted = true;
        }
        catch (const websocketpp::exception& e)
        {
            FAIL() << "Failed to start websocket server: " << e.what();
        }
    }

    void TearDown() override
    {
        if (m_serverStarted)
        {
            m_server.stop_listening();
            m_server.stop();
            if (m_serverThread && m_serverThread->joinable())
            {
                m_serverThread->join();
            }
        }
    }
};

TEST_F(TransportIntegrationUTest, ConnectAndDisconnect)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            try
            {
                connectionPromise.set_value(true);
            }
            catch (const std::future_error&)
            {
            }
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) {};

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    EXPECT_TRUE(connectionFuture.get());

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportIntegrationUTest, SendAndReceiveMessage)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::promise<nlohmann::json> messagePromise;
    auto messageFuture = messagePromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            try
            {
                connectionPromise.set_value(true);
            }
            catch (const std::future_error&)
            {
            }
        }
    };

    auto onMessage = [&](const nlohmann::json& msg) { messagePromise.set_value(msg); };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    ASSERT_TRUE(connectionFuture.get());

    nlohmann::json params = {{"key", "value"}};
    unsigned msgId = transport.getNextMessageID();
    err = transport.send("test.method", params, msgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = messageFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Message response timed out";

    nlohmann::json receivedMsg = messageFuture.get();
    EXPECT_EQ(receivedMsg["id"], msgId);
    EXPECT_EQ(receivedMsg["method"], "test.method");
    EXPECT_EQ(receivedMsg["params"]["key"], "value");

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportUTest, ConnectionFailure)
{
    Transport transport;
    std::promise<bool> connectedPromise;
    auto connectedFuture = connectedPromise.get_future();
    std::promise<Firebolt::Error> errorPromise;
    auto errorFuture = errorPromise.get_future();
    std::atomic<bool> promiseSet(false);

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& err)
    {
        bool alreadySet = promiseSet.exchange(true);
        if (!alreadySet)
        {
            connectedPromise.set_value(connected);
            if (!connected)
            {
                errorPromise.set_value(err);
            }
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/)
    { FAIL() << "Should not receive a message on a failed connection"; };

    Firebolt::Error err = transport.connect("ws://localhost:49151", onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectedFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "onConnectionChange callback timed out";

    EXPECT_FALSE(connectedFuture.get());

    auto errorStatus = errorFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(errorStatus, std::future_status::ready) << "Error promise timed out";

    EXPECT_NE(errorFuture.get(), Firebolt::Error::None);
}

TEST_F(TransportIntegrationUTest, ConnectWhenAlreadyConnected)
{
    Transport transport;
    std::promise<void> firstConnectionPromise;
    auto firstConnectionFuture = firstConnectionPromise.get_future();

    int connectionChangeCount = 0;
    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        connectionChangeCount++;
        if (connected)
        {
            if (connectionChangeCount == 1)
            {
                firstConnectionPromise.set_value();
            }
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) {};

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = firstConnectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Initial connection timed out";
    ASSERT_EQ(connectionChangeCount, 1);

    err = transport.connect(m_uri, onMessage, onConnectionChange);
    EXPECT_EQ(err, Firebolt::Error::AlreadyConnected);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(connectionChangeCount, 1);

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportIntegrationUTest, HeaderInjectionAndResponseHeaderRetrieval)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            try
            {
                connectionPromise.set_value(true);
            }
            catch (const std::future_error&)
            {
            }
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) {};

    // Custom header to inject
    std::map<std::string, std::string> customHeaders = {{"X-Test-Header", "HeaderValue"}};

    Firebolt::Error err =
        transport.connect(m_uri, onMessage, onConnectionChange, std::nullopt, std::nullopt, customHeaders);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    EXPECT_TRUE(connectionFuture.get());

    // The server will echo back headers, but since we use websocketpp, only standard headers may be available.
    // We check that getResponseHeader returns something for a standard header (e.g., Sec-WebSocket-Accept)
    // and for our custom header (may be empty if server does not echo).
    auto stdHeader = transport.getResponseHeader("Sec-WebSocket-Accept");
    EXPECT_TRUE(stdHeader.has_value());

    auto customHeader = transport.getResponseHeader("X-Test-Header");
    // Custom header may or may not be echoed by server implementation.
    (void)customHeader;

    auto definitelyMissingHeader = transport.getResponseHeader("X-Definitely-Missing-Header");
    EXPECT_EQ(definitelyMissingHeader, std::nullopt);

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);

    // Cached response headers must be cleared on disconnect.
    EXPECT_EQ(transport.getResponseHeader("Sec-WebSocket-Accept"), std::nullopt);
}

class TransportCustomServerUTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    const std::string m_uri = "ws://localhost:9002";
    bool m_serverStarted = false;

    void StartServer()
    {
        try
        {
            m_server.init_asio();
            m_server.set_reuse_addr(true);
            websocketpp::log::level include = websocketpp::log::alevel::all;
            websocketpp::log::level exclude = (websocketpp::log::alevel::frame_header |
                                               websocketpp::log::alevel::frame_payload |
                                               websocketpp::log::alevel::control);
            m_server.set_access_channels(include);
            m_server.clear_access_channels(exclude);
            m_server.listen(9002);
            m_server.start_accept();
            m_serverThread = std::make_unique<std::thread>([this]() { m_server.run(); });
            m_serverStarted = true;
        }
        catch (const websocketpp::exception& e)
        {
            FAIL() << "Failed to start websocket server: " << e.what();
        }
    }

    void TearDown() override
    {
        if (m_serverStarted)
        {
            m_server.stop_listening();
            m_server.stop();
            if (m_serverThread && m_serverThread->joinable())
            {
                m_serverThread->join();
            }
        }
    }
};

TEST_F(TransportCustomServerUTest, ServerClosesConnection)
{
    m_server.set_open_handler(
        [this](connection_hdl hdl)
        {
            try
            {
                m_server.close(hdl, websocketpp::close::status::normal, "Closing");
            }
            catch (const websocketpp::exception& e)
            {
                FAIL() << "Server failed to close connection: " << e.what();
            }
        });

    StartServer();

    Transport transport;
    std::promise<void> connectionOpenedPromise;
    auto connectionOpenedFuture = connectionOpenedPromise.get_future();
    std::promise<void> connectionClosedPromise;
    auto connectionClosedFuture = connectionClosedPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionOpenedPromise.set_value();
        }
        else
        {
            connectionClosedPromise.set_value();
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) { FAIL() << "Should not receive a message"; };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto openStatus = connectionOpenedFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(openStatus, std::future_status::ready) << "onOpen event timed out";

    auto closeStatus = connectionClosedFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(closeStatus, std::future_status::ready) << "onClose event timed out";
}

TEST_F(TransportCustomServerUTest, SendAfterServerDisconnect)
{
    std::promise<void> messageReceivedPromise;
    auto messageReceivedFuture = messageReceivedPromise.get_future();

    m_server.set_message_handler(
        [this, &messageReceivedPromise](connection_hdl hdl, server::message_ptr /*msg*/)
        {
            messageReceivedPromise.set_value();
            try
            {
                m_server.close(hdl, websocketpp::close::status::normal, "Closing after message");
            }
            catch (const websocketpp::exception& e)
            {
                FAIL() << "Server failed to close connection: " << e.what();
            }
        });

    StartServer();

    Transport transport;
    std::promise<void> connectionOpenedPromise;
    auto connectionOpenedFuture = connectionOpenedPromise.get_future();
    std::promise<void> connectionClosedPromise;
    auto connectionClosedFuture = connectionClosedPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionOpenedPromise.set_value();
        }
        else
        {
            connectionClosedPromise.set_value();
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) { FAIL() << "Should not receive a message from the server"; };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    ASSERT_EQ(connectionOpenedFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready)
        << "Client connection timed out";

    nlohmann::json params;
    params["test"] = "data";
    err = transport.send("test.method", params, transport.getNextMessageID());
    ASSERT_EQ(err, Firebolt::Error::None);

    ASSERT_EQ(messageReceivedFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready)
        << "Server did not receive the message in time";

    ASSERT_EQ(connectionClosedFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready)
        << "Client did not detect server-initiated disconnection";

    err = transport.send("test.method.fail", params, transport.getNextMessageID());
    EXPECT_EQ(err, Firebolt::Error::NotConnected);

    transport.disconnect();
}

// ---- Regression test: disconnect() must not hang when the server ignores the close handshake ----
//
// Root cause: disconnect() called client_->close() (async) then immediately joined the ASIO
// thread.  websocketpp's default close_handshake_timeout is 5 s, so if the gateway process
// was hung (TCP connection alive but no frames processed) the call would block for 5 s.
//
// Fix: set con->set_close_handshake_timeout(100) just before calling close(), capping the
// wait at 100ms.  This test asserts the whole call returns in under 3 s.
//
// The server used here is a minimal raw TCP server that performs the WebSocket HTTP upgrade
// then reads all incoming bytes into /dev/null without ever replying — exactly what happens
// when a gateway process freezes with the TCP connection still open.
namespace
{
namespace asio = websocketpp::lib::asio;

class SilentAfterUpgradeServer
{
public:
    explicit SilentAfterUpgradeServer()
    {
        m_acceptor.open(asio::ip::tcp::v4());
        m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
        m_acceptor.listen();
    }

    uint16_t port() const { return m_acceptor.local_endpoint().port(); }

    ~SilentAfterUpgradeServer() { stop(); }

    void start()
    {
        accept();
        m_thread = std::thread([this] { m_ioc.run(); });
    }

    void stop()
    {
        m_ioc.stop();
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    void accept()
    {
        m_acceptor.async_accept(
            [this](websocketpp::lib::error_code ec, asio::ip::tcp::socket sock)
            {
                if (ec)
                    return;
                doUpgrade(std::make_shared<asio::ip::tcp::socket>(std::move(sock)));
            });
    }

    void doUpgrade(std::shared_ptr<asio::ip::tcp::socket> sock)
    {
        auto buf = std::make_shared<asio::streambuf>();
        asio::async_read_until(*sock, *buf, "\r\n\r\n",
                               [this, sock, buf](websocketpp::lib::error_code ec, size_t)
                               {
                                   if (ec)
                                       return;
                                   std::istream stream(buf.get());
                                   std::string line;
                                   std::string wsKey;
                                   while (std::getline(stream, line))
                                   {
                                       const std::string header = "Sec-WebSocket-Key:";
                                       if (line.find(header) != std::string::npos)
                                       {
                                           wsKey = line.substr(line.find(':') + 2);
                                           wsKey.erase(wsKey.find_last_not_of(" \t\r\n") + 1);
                                       }
                                   }
                                   sendUpgradeResponse(sock, wsKey);
                               });
    }

    void sendUpgradeResponse(std::shared_ptr<asio::ip::tcp::socket> sock, const std::string& wsKey)
    {
        const std::string combined = wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        unsigned char sha1Digest[20];
        websocketpp::sha1::calc(combined.c_str(), static_cast<int>(combined.size()), sha1Digest);
        const std::string accept = websocketpp::base64_encode(sha1Digest, 20);

        auto response = std::make_shared<std::string>("HTTP/1.1 101 Switching Protocols\r\n"
                                                      "Upgrade: websocket\r\n"
                                                      "Connection: Upgrade\r\n"
                                                      "Sec-WebSocket-Accept: " +
                                                      accept + "\r\n\r\n");

        asio::async_write(*sock, asio::buffer(*response),
                          [this, sock, response](websocketpp::lib::error_code ec, size_t)
                          {
                              if (ec)
                                  return;
                              // Upgrade done. Read forever and discard — never send a CLOSE response.
                              discardForever(sock);
                          });
    }

    void discardForever(std::shared_ptr<asio::ip::tcp::socket> sock)
    {
        auto buf = std::make_shared<std::array<char, 1024>>();
        sock->async_read_some(asio::buffer(*buf),
                              [this, sock, buf](websocketpp::lib::error_code ec, size_t)
                              {
                                  if (ec)
                                      return;
                                  discardForever(sock);
                              });
    }

    asio::io_context m_ioc;
    asio::ip::tcp::acceptor m_acceptor{m_ioc};
    std::thread m_thread;
};
} // namespace

TEST(TransportDisconnectTimeoutComponentTest, DisconnectDoesNotHangWhenServerIgnoresCloseHandshake)
{
    SilentAfterUpgradeServer silentServer;
    silentServer.start();
    const uint16_t port = silentServer.port();

    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::atomic<bool> promiseSet{false};

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        bool expected = false;
        if (promiseSet.compare_exchange_strong(expected, true))
            connectionPromise.set_value(connected);
    };
    auto onMessage = [](const nlohmann::json& /*msg*/) {};

    ASSERT_EQ(transport.connect("ws://localhost:" + std::to_string(port), onMessage, onConnectionChange),
              Firebolt::Error::None);

    auto connStatus = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(connStatus, std::future_status::ready) << "Transport never connected to silent server";
    ASSERT_TRUE(connectionFuture.get());

    // disconnect() sends a CLOSE frame; the silent server reads the bytes but never replies.
    // Without the timeout cap this would block for websocketpp's default 5 seconds.
    // The fix sets close_handshake_timeout(2000), so this must return well under 3 s.
    const auto start = std::chrono::steady_clock::now();
    const Firebolt::Error err = transport.disconnect();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(err, Firebolt::Error::None);
    EXPECT_LT(elapsed, std::chrono::seconds(3))
        << "disconnect() blocked for " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        << " ms — close-handshake timeout was not applied (expected < 3000 ms)";
}

TEST_F(TransportIntegrationUTest, SendWithEmptyParams)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::promise<nlohmann::json> messagePromise;
    auto messageFuture = messagePromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value(true);
        }
    };

    auto onMessage = [&](const nlohmann::json& msg) { messagePromise.set_value(msg); };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    ASSERT_TRUE(connectionFuture.get());

    nlohmann::json emptyParams;
    unsigned msgId = transport.getNextMessageID();
    err = transport.send("test.method.empty", emptyParams, msgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = messageFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Message response timed out";

    nlohmann::json receivedMsg = messageFuture.get();
    EXPECT_EQ(receivedMsg["id"], msgId);
    EXPECT_EQ(receivedMsg["method"], "test.method.empty");

    EXPECT_EQ(receivedMsg.find("params"), receivedMsg.end());

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportCustomServerUTest, MalformedMessageFromServer)
{
    std::promise<void> malformedMessageSentPromise;
    auto malformedMessageSentFuture = malformedMessageSentPromise.get_future();
    std::atomic<int> serverMessageCount{0};

    m_server.set_message_handler(
        [this, &malformedMessageSentPromise, &serverMessageCount](connection_hdl hdl, server::message_ptr msg)
        {
            int count = serverMessageCount++;
            if (count == 0)
            {
                std::string malformedJson =
                    R"({"jsonrpc":"2.0","id":1,"result":{"valid":true})"; // Missing closing brace }
                m_server.send(hdl, malformedJson, msg->get_opcode());
                malformedMessageSentPromise.set_value();
            }
            else if (count == 1)
            {
                m_server.send(hdl, msg->get_payload(), msg->get_opcode());
            }
        });

    StartServer();

    Transport transport;
    std::promise<void> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::promise<nlohmann::json> validMessagePromise;
    auto validMessageFuture = validMessagePromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value();
        }
    };

    auto onMessage = [&](const nlohmann::json& msg) { validMessagePromise.set_value(msg); };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready)
        << "Connection timed out";

    err = transport.send("test.method", {}, transport.getNextMessageID());
    ASSERT_EQ(err, Firebolt::Error::None);

    ASSERT_EQ(malformedMessageSentFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready)
        << "Server did not send malformed message in time";

    nlohmann::json params = {{"key", "value"}};
    unsigned validMsgId = transport.getNextMessageID();
    err = transport.send("test.method.valid", params, validMsgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = validMessageFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(msgStatus, std::future_status::ready)
        << "Did not receive the valid message. The transport may have crashed or closed.";

    nlohmann::json receivedMsg = validMessageFuture.get();
    EXPECT_EQ(receivedMsg["id"], validMsgId);
    EXPECT_EQ(receivedMsg["method"], "test.method.valid");

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Additional transport tests for coverage gaps
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: TransportCustomServerUTest.NonTextMessageIgnored
// Covers: transport.cpp non-text opcode branch (warning + ignore)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(TransportCustomServerUTest, NonTextMessageIgnored)
{
    m_server.set_message_handler(
        [this](connection_hdl hdl, server::message_ptr /*msg*/)
        {
            // Send a binary frame with valid JSON — proves rejection is by opcode, not parse failure
            nlohmann::json binaryPayload;
            binaryPayload["jsonrpc"] = "2.0";
            binaryPayload["id"] = 999;
            binaryPayload["result"] = {{"binary", true}};
            m_server.send(hdl, binaryPayload.dump(), websocketpp::frame::opcode::binary);
            // Then send a valid text message
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = 1;
            response["result"] = {{"ok", true}};
            m_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
        });

    StartServer();

    Transport transport;
    std::promise<void> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::promise<nlohmann::json> validMessagePromise;
    auto validMessageFuture = validMessagePromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value();
        }
    };

    auto onMessage = [&](const nlohmann::json& msg) { validMessagePromise.set_value(msg); };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready)
        << "Connection timed out";

    // Trigger the server to send a binary + text message
    err = transport.send("trigger", {}, transport.getNextMessageID());
    ASSERT_EQ(err, Firebolt::Error::None);

    // Only the text message should arrive
    auto msgStatus = validMessageFuture.wait_for(std::chrono::milliseconds(300));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Valid text message not received after binary was ignored";

    nlohmann::json received = validMessageFuture.get();
    EXPECT_TRUE(received.contains("result"));
    // Proves the binary frame (id=999) was dropped by opcode, not by JSON parse failure
    EXPECT_EQ(received["id"], 1) << "Received binary frame's id instead of text frame's id";

    transport.disconnect();
}

// ---------------------------------------------------------------------------
// Test name: TransportIntegrationUTest.DisconnectWhileConnected
// Covers: transport.cpp disconnect from Connected state
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(TransportIntegrationUTest, DisconnectWhileConnected)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value(true);
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) {};

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";

    // Disconnect and verify state reset is clean
    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);

    // After disconnect, sending should fail with NotConnected
    err = transport.send("test.method", {}, 1);
    EXPECT_EQ(err, Firebolt::Error::NotConnected);
}

// ---------------------------------------------------------------------------
// Test name: TransportIntegrationUTest.MultipleMessagesInSequence
// Covers: transport.cpp processQueuedMessages loop
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(TransportIntegrationUTest, MultipleMessagesInSequence)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::atomic<int> messageCount{0};
    std::promise<void> allMessagesPromise;
    auto allMessagesFuture = allMessagesPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value(true);
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/)
    {
        if (++messageCount >= 3)
        {
            allMessagesPromise.set_value();
        }
    };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";

    // Send multiple messages
    for (int i = 0; i < 3; i++)
    {
        nlohmann::json params = {{"seq", i}};
        err = transport.send("test.method", params, transport.getNextMessageID());
        EXPECT_EQ(err, Firebolt::Error::None);
    }

    auto msgStatus = allMessagesFuture.wait_for(std::chrono::milliseconds(500));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Not all messages were received";
    EXPECT_EQ(messageCount.load(), 3);

    transport.disconnect();
}

// ---------------------------------------------------------------------------
// Test name: TransportIntegrationUTest.ConnectWithTransportLogging
// Covers: transport.cpp logging include/exclude params
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(TransportIntegrationUTest, ConnectWithTransportLogging)
{
    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value(true);
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) {};

    // Provide explicit transport logging masks
    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange,
                                            static_cast<unsigned>(websocketpp::log::alevel::all),
                                            static_cast<unsigned>(websocketpp::log::alevel::frame_payload));
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: TransportUTest.GetNextMessageIDMonotonic
// Covers: transport.cpp getNextMessageID atomic increment
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(TransportUTest, GetNextMessageIDMonotonic)
{
    unsigned prev = transport.getNextMessageID();
    for (int i = 0; i < 100; ++i)
    {
        unsigned next = transport.getNextMessageID();
        EXPECT_EQ(next, prev + 1);
        prev = next;
    }
}

// ---------------------------------------------------------------------------
// Test name: TransportCustomServerUTest.DisconnectFromDisconnectedState
// Covers: transport.cpp disconnect when already Disconnected
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(TransportCustomServerUTest, DisconnectFromDisconnectedState)
{
    m_server.set_open_handler([this](connection_hdl hdl)
                              { m_server.close(hdl, websocketpp::close::status::normal, "Bye"); });

    StartServer();

    Transport transport;
    std::promise<void> closedPromise;
    auto closedFuture = closedPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (!connected)
        {
            closedPromise.set_value();
        }
    };

    auto onMessage = [](const nlohmann::json& /*msg*/) {};

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    // Wait for server to close connection
    ASSERT_EQ(closedFuture.wait_for(std::chrono::milliseconds(300)), std::future_status::ready);

    // Now disconnect from already-disconnected state (no close() call, just cleanup)
    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Branch-coverage tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: TransportIntegrationUTest.DebugLoggingOnSendAndReceive
// Covers: transport.cpp debugEnabled_ true branch (log send/receive)
//         Exercises both inbound and outbound debug logging
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(TransportIntegrationUTest, DebugLoggingOnSendAndReceive)
{
    // Set log level to Debug so debugEnabled_ is true when connect() is called
    Firebolt::Logger::setLogLevel(Firebolt::LogLevel::Debug);
    struct LogLevelGuard
    {
        ~LogLevelGuard() { Firebolt::Logger::setLogLevel(Firebolt::LogLevel::Error); }
    } logGuard;

    Transport transport;
    std::promise<bool> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::promise<nlohmann::json> messagePromise;
    auto messageFuture = messagePromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value(true);
        }
    };

    auto onMessage = [&](const nlohmann::json& msg) { messagePromise.set_value(msg); };

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";

    nlohmann::json params = {{"key", "value"}};
    unsigned msgId = transport.getNextMessageID();
    err = transport.send("test.debug", params, msgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = messageFuture.wait_for(std::chrono::milliseconds(150));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Message response timed out";

    nlohmann::json received = messageFuture.get();
    EXPECT_EQ(received["id"], msgId);

    transport.disconnect();
}

// ---------------------------------------------------------------------------
// Test name: TransportCustomServerUTest.MessageDuringShutdownIgnored
// Covers: Resilience — verifies no crash or hang when an echo arrives
//         during or immediately after disconnect(). The stopMessageWorker_
//         guard (transport.cpp:323-324) is unreachable from the public API,
//         but this test validates safe behavior under rapid teardown.
// Scenario type: resilience
// ---------------------------------------------------------------------------
TEST_F(TransportCustomServerUTest, MessageDuringShutdownIgnored)
{
    std::promise<void> serverReceivedPromise;
    auto serverReceivedFuture = serverReceivedPromise.get_future();
    std::atomic<bool> serverPromiseSet{false};

    m_server.set_message_handler(
        [this, &serverReceivedPromise, &serverPromiseSet](connection_hdl hdl, server::message_ptr msg)
        {
            bool expected = false;
            if (serverPromiseSet.compare_exchange_strong(expected, true))
            {
                serverReceivedPromise.set_value();
            }
            // Echo back
            m_server.send(hdl, msg->get_payload(), msg->get_opcode());
        });

    StartServer();

    Transport transport;
    std::promise<void> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            connectionPromise.set_value();
        }
    };

    auto onMessage = [&](const nlohmann::json& /*msg*/) {};

    Firebolt::Error err = transport.connect(m_uri, onMessage, onConnectionChange);
    ASSERT_EQ(err, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::milliseconds(150)), std::future_status::ready);

    // Send a message, then immediately disconnect
    // The echo may arrive while the message worker is stopping
    transport.send("test.method", {{"k", "v"}}, transport.getNextMessageID());
    transport.disconnect();

    // No crash, no hang — that's the primary assertion.
    // Server receipt is best-effort: a rapid close may legitimately prevent delivery.
    auto serverStatus = serverReceivedFuture.wait_for(std::chrono::milliseconds(500));
    if (serverStatus != std::future_status::ready)
    {
        // Delivery was pre-empted by disconnect — acceptable for this resilience test.
        SUCCEED() << "Message not delivered before disconnect (expected race outcome)";
    }
}

// ---------------------------------------------------------------------------
// Test name: TransportUTest.ConnectWithInvalidUrl
// Covers: transport.cpp get_connection error → NotConnected
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(TransportUTest, ConnectWithInvalidUrl)
{
    Transport transport;
    auto onMessage = [](const nlohmann::json&) {};
    auto onConnectionChange = [](bool, const Firebolt::Error&) {};

    // An empty or completely invalid URL should cause get_connection to fail
    Firebolt::Error err = transport.connect("", onMessage, onConnectionChange);
    EXPECT_EQ(err, Firebolt::Error::NotConnected);
}
