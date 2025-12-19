#include "transport.h"
#include <chrono>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace Firebolt::Transport;

class TransportTest : public ::testing::Test
{
protected:
    Transport transport;
};

TEST_F(TransportTest, GetNextMessageID)
{
    unsigned firstId = transport.getNextMessageID();
    unsigned secondId = transport.getNextMessageID();
    EXPECT_EQ(firstId, 1);
    EXPECT_EQ(secondId, 2);
}

TEST_F(TransportTest, SendWithoutConnection)
{
    nlohmann::json params;
    Firebolt::Error err = transport.send("test.method", params, 1);
    EXPECT_EQ(err, Firebolt::Error::NotConnected);
}

TEST_F(TransportTest, DisconnectWithoutStart)
{
    Firebolt::Error err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

class TransportIntegrationTest : public ::testing::Test
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

TEST_F(TransportIntegrationTest, ConnectAndDisconnect)
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

    auto status = connectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    EXPECT_TRUE(connectionFuture.get());

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportIntegrationTest, SendAndReceiveMessage)
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

    auto status = connectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    ASSERT_TRUE(connectionFuture.get());

    nlohmann::json params = {{"key", "value"}};
    unsigned msgId = transport.getNextMessageID();
    err = transport.send("test.method", params, msgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = messageFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Message response timed out";

    nlohmann::json receivedMsg = messageFuture.get();
    EXPECT_EQ(receivedMsg["id"], msgId);
    EXPECT_EQ(receivedMsg["method"], "test.method");
    EXPECT_EQ(receivedMsg["params"]["key"], "value");

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportTest, ConnectionFailure)
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

    auto status = connectedFuture.wait_for(std::chrono::seconds(3));
    ASSERT_EQ(status, std::future_status::ready) << "onConnectionChange callback timed out";

    EXPECT_FALSE(connectedFuture.get());

    auto errorStatus = errorFuture.wait_for(std::chrono::seconds(1));
    ASSERT_EQ(errorStatus, std::future_status::ready) << "Error promise timed out";

    EXPECT_NE(errorFuture.get(), Firebolt::Error::None);
}

TEST_F(TransportIntegrationTest, ConnectWhenAlreadyConnected)
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

    auto status = firstConnectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Initial connection timed out";
    ASSERT_EQ(connectionChangeCount, 1);

    err = transport.connect(m_uri, onMessage, onConnectionChange);
    EXPECT_EQ(err, Firebolt::Error::AlreadyConnected);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(connectionChangeCount, 1);

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

class TransportCustomServerTest : public ::testing::Test
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

TEST_F(TransportCustomServerTest, ServerClosesConnection)
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

    auto openStatus = connectionOpenedFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(openStatus, std::future_status::ready) << "onOpen event timed out";

    auto closeStatus = connectionClosedFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(closeStatus, std::future_status::ready) << "onClose event timed out";
}

TEST_F(TransportCustomServerTest, SendAfterServerDisconnect)
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

    ASSERT_EQ(connectionOpenedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready)
        << "Client connection timed out";

    nlohmann::json params;
    params["test"] = "data";
    err = transport.send("test.method", params, transport.getNextMessageID());
    ASSERT_EQ(err, Firebolt::Error::None);

    ASSERT_EQ(messageReceivedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready)
        << "Server did not receive the message in time";

    ASSERT_EQ(connectionClosedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready)
        << "Client did not detect server-initiated disconnection";

    err = transport.send("test.method.fail", params, transport.getNextMessageID());
    EXPECT_EQ(err, Firebolt::Error::NotConnected);

    transport.disconnect();
}

TEST_F(TransportIntegrationTest, SendWithEmptyParams)
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

    auto status = connectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    ASSERT_TRUE(connectionFuture.get());

    nlohmann::json emptyParams;
    unsigned msgId = transport.getNextMessageID();
    err = transport.send("test.method.empty", emptyParams, msgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = messageFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(msgStatus, std::future_status::ready) << "Message response timed out";

    nlohmann::json receivedMsg = messageFuture.get();
    EXPECT_EQ(receivedMsg["id"], msgId);
    EXPECT_EQ(receivedMsg["method"], "test.method.empty");

    EXPECT_EQ(receivedMsg.find("params"), receivedMsg.end());

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(TransportCustomServerTest, MalformedMessageFromServer)
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
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready) << "Connection timed out";

    err = transport.send("test.method", {}, transport.getNextMessageID());
    ASSERT_EQ(err, Firebolt::Error::None);

    ASSERT_EQ(malformedMessageSentFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready)
        << "Server did not send malformed message in time";

    nlohmann::json params = {{"key", "value"}};
    unsigned validMsgId = transport.getNextMessageID();
    err = transport.send("test.method.valid", params, validMsgId);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto msgStatus = validMessageFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(msgStatus, std::future_status::ready)
        << "Did not receive the valid message. The transport may have crashed or closed.";

    nlohmann::json receivedMsg = validMessageFuture.get();
    EXPECT_EQ(receivedMsg["id"], validMsgId);
    EXPECT_EQ(receivedMsg["method"], "test.method.valid");

    err = transport.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}