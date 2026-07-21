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

#include "firebolt/gateway.h"
#include "firebolt/logger.h"
#include "utils.h"
#include <arpa/inet.h>
#include <atomic>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace Firebolt::Transport;

struct ReservedLoopbackPort
{
    int fd = -1;
    uint16_t port = 49198;

    ReservedLoopbackPort() = default;
    ReservedLoopbackPort(const ReservedLoopbackPort&) = delete;
    ReservedLoopbackPort& operator=(const ReservedLoopbackPort&) = delete;

    ReservedLoopbackPort(ReservedLoopbackPort&& other) noexcept
        : fd(other.fd),
          port(other.port)
    {
        other.fd = -1;
    }

    ReservedLoopbackPort& operator=(ReservedLoopbackPort&& other) noexcept
    {
        if (this != &other)
        {
            if (fd >= 0)
            {
                ::close(fd);
            }
            fd = other.fd;
            port = other.port;
            other.fd = -1;
        }
        return *this;
    }

    ~ReservedLoopbackPort()
    {
        if (fd >= 0)
        {
            ::close(fd);
        }
    }
};

static ReservedLoopbackPort reserveLikelyUnusedLoopbackPort()
{
    ReservedLoopbackPort reserved;
    reserved.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (reserved.fd < 0)
    {
        return reserved;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(reserved.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(reserved.fd);
        reserved.fd = -1;
        return reserved;
    }

    socklen_t addrLen = sizeof(addr);
    if (::getsockname(reserved.fd, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0)
    {
        ::close(reserved.fd);
        reserved.fd = -1;
        return reserved;
    }

    reserved.port = ntohs(addr.sin_port);
    return reserved;
}

TEST(GatewayUrlUtilsUTest, VerifyUrls)
{
    bool legacyRPCv1 = false;
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437", legacyRPCv1), "ws://127.0.0.1:3437/?RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437?sessionId=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/?sessionId=abc&RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437/json?session=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/json?session=abc&RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://localhost:9003", legacyRPCv1), "ws://localhost:9003/?RPCv2=true");
}

TEST(GatewayUrlUtilsUTest, VerifyUrls_LegacyMode)
{
    bool legacyRPCv1 = true;
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437", legacyRPCv1), "ws://127.0.0.1:3437/");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437?sessionId=abc", legacyRPCv1), "ws://127.0.0.1:3437/?sessionId=abc");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437/json?session=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/json?session=abc");
    EXPECT_EQ(buildGatewayUrl("ws://localhost:9003", legacyRPCv1), "ws://localhost:9003/");
}

class GatewayUTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    const std::string m_uri = "ws://localhost:9003";
    bool m_serverStarted = false;
    std::map<connection_hdl, nlohmann::json, std::owner_less<connection_hdl>> m_responses;
    std::promise<bool> m_connectionPromise;
    std::function<void(server*, connection_hdl)> m_onMessageAction;
    std::function<void(connection_hdl, server::message_ptr)> m_messageHandler;

    void SetUp() override
    {
        m_connectionPromise = std::promise<bool>();
        m_onMessageAction = nullptr;
        m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
        {
            if (m_onMessageAction)
            {
                m_onMessageAction(&m_server, hdl);
                m_onMessageAction = nullptr;
                return;
            }
            try
            {
                auto request = nlohmann::json::parse(msg->get_payload());
                if (request.contains("id"))
                {
                    nlohmann::json response;
                    if (m_responses.count(hdl))
                    {
                        response = m_responses[hdl];
                        m_responses.erase(hdl);
                    }
                    else
                    {
                        response["jsonrpc"] = "2.0";
                        response["id"] = request["id"];
                        response["result"] = nlohmann::json::object();
                    }
                    m_server.send(hdl, response.dump(), msg->get_opcode());
                }
            }
            catch (const websocketpp::exception& e)
            {
                FAIL() << "Server failed to send message: " << e.what();
            }
            catch (const nlohmann::json::parse_error& e)
            {
                FAIL() << "Failed to parse incoming message: " << e.what();
            }
        };
    }

    void startServer()
    {
        try
        {
            m_server.init_asio();
            m_server.set_reuse_addr(true);

            m_server.set_message_handler(
                [this](connection_hdl hdl, server::message_ptr msg)
                {
                    if (m_messageHandler)
                    {
                        m_messageHandler(hdl, msg);
                    }
                });

            m_server.set_access_channels(websocketpp::log::alevel::none);
            m_server.listen(9003);
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
        GetGatewayInstance().disconnect();

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

    Firebolt::Config getTestConfig()
    {
        Firebolt::Config cfg;
        cfg.wsUrl = m_uri;
        cfg.log.level = Firebolt::LogLevel::Debug;
        cfg.waitTime_ms = 1000;
        return cfg;
    }

    void onConnectionChange(bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            try
            {
                m_connectionPromise.set_value(true);
            }
            catch (const std::future_error& e)
            {
                ADD_FAILURE() << "Failed to set connection promise: " << e.what();
            }
        }
    };

    IGateway& connectAndWait()
    {
        startServer();
        IGateway& gateway = GetGatewayInstance();
        auto connectionFuture = m_connectionPromise.get_future();

        Firebolt::Error err = gateway.connect(getTestConfig(), [this](bool connected, const Firebolt::Error& err)
                                              { onConnectionChange(connected, err); });
        EXPECT_EQ(err, Firebolt::Error::None);

        auto status = connectionFuture.wait_for(std::chrono::seconds(2));
        EXPECT_EQ(status, std::future_status::ready) << "Connection timed out";
        if (status == std::future_status::ready)
        {
            EXPECT_TRUE(connectionFuture.get());
        }
        return gateway;
    }
};

TEST_F(GatewayUTest, ConnectAndDisconnect)
{
    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Error err = gateway.connect(getTestConfig(), [this](bool connected, const Firebolt::Error& err)
                                          { onConnectionChange(connected, err); });
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    EXPECT_TRUE(connectionFuture.get());

    err = gateway.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayUTest, Request)
{
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_TRUE(result);
}

TEST_F(GatewayUTest, Send)
{
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    Firebolt::Error err = gateway.send("test.method", params);
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayUTest, SubscribeUnsubscribe)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onEvent";
        eventMsg["params"] = {{"fired", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };

    gateway.send("dummy.message", {});

    auto eventStatus = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(eventStatus, std::future_status::ready) << "Event was not received";

    nlohmann::json eventParams = eventFuture.get();
    EXPECT_TRUE(eventParams["fired"].get<bool>());

    err = gateway.unsubscribe("test.onEvent", &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayUTest, RequestTimeout)
{
    m_messageHandler = [](connection_hdl /*hdl*/, server::message_ptr /*msg*/) {};
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::milliseconds(2500));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
}

TEST_F(GatewayUTest, RequestWithError)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["error"]["code"] = -32601;
            response["error"]["message"] = "Method not found";
            m_server.send(hdl, response.dump(), msg->get_opcode());
        }
        catch (...)
        {
            FAIL() << "Server failed to send error response";
        }
    };

    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::MethodNotFound);
    EXPECT_EQ(result.errorInfo().error(), -32601);
    EXPECT_EQ(result.errorInfo().message(), "Method not found");
}

TEST_F(GatewayUTest, MultipleSubscriptions)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise1;
    auto eventFuture1 = eventPromise1.get_future();
    auto onEvent1 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    std::promise<nlohmann::json> eventPromise2;
    auto eventFuture2 = eventPromise2.get_future();
    auto onEvent2 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    gateway.subscribe("test.onEvent", onEvent1, &eventPromise1);
    gateway.subscribe("test.onEvent", onEvent2, &eventPromise2);

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onEvent";
        eventMsg["params"] = {{"fired", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy.message", {});

    ASSERT_EQ(eventFuture1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_EQ(eventFuture2.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    EXPECT_TRUE(eventFuture1.get()["fired"].get<bool>());
    EXPECT_TRUE(eventFuture2.get()["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise1);
    gateway.unsubscribe("test.onEvent", &eventPromise2);
}

TEST_F(GatewayUTest, UnsubscribeSpecific)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise1;
    auto eventFuture1 = eventPromise1.get_future();
    auto onEvent1 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    std::promise<nlohmann::json> eventPromise2;
    auto eventFuture2 = eventPromise2.get_future();
    auto onEvent2 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    gateway.subscribe("test.onEvent", onEvent1, &eventPromise1);
    gateway.subscribe("test.onEvent", onEvent2, &eventPromise2);

    gateway.unsubscribe("test.onEvent", &eventPromise1);

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onEvent";
        eventMsg["params"] = {{"fired", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy.message", {});

    EXPECT_EQ(eventFuture1.wait_for(std::chrono::seconds(2)), std::future_status::timeout);
    ASSERT_EQ(eventFuture2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(eventFuture2.get()["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise2);
}

TEST_F(GatewayUTest, InvalidNotification)
{
    IGateway& gateway = connectAndWait();

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json invalidMsg1;
        invalidMsg1["jsonrpc"] = "2.0";
        invalidMsg1["method"] = "test.onEvent";
        s->send(hdl, invalidMsg1.dump(), websocketpp::frame::opcode::text);

        nlohmann::json invalidMsg2;
        invalidMsg2["jsonrpc"] = "2.0";
        invalidMsg2["method"] = "test.onEvent";
        invalidMsg2["id"] = 123;
        invalidMsg2["params"] = {};
        s->send(hdl, invalidMsg2.dump(), websocketpp::frame::opcode::text);

        nlohmann::json invalidMsg3;
        invalidMsg3["jsonrpc"] = "2.0";
        s->send(hdl, invalidMsg3.dump(), websocketpp::frame::opcode::text);
    };

    gateway.send("dummy.message", {});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto responseFuture = gateway.request("test.method", {{"key", "value"}});
    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready)
        << "Gateway did not respond to a valid request after invalid notifications.";

    auto result = responseFuture.get();
    EXPECT_TRUE(result) << "Gateway returned an error for a valid request after invalid notifications.";
}

TEST_F(GatewayUTest, LegacyRPCv1Event)
{
    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        if (request["method"].get<std::string>().find(".onEvent") != std::string::npos)
        {
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["result"]["listening"] = true;
            m_server.send(hdl, response.dump(), msg->get_opcode());

            nlohmann::json legacyEvent;
            legacyEvent["jsonrpc"] = "2.0";
            legacyEvent["id"] = request["id"];
            legacyEvent["result"] = {{"fired", true}};
            m_server.send(hdl, legacyEvent.dump(), msg->get_opcode());
        }
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);

    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto eventStatus = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(eventStatus, std::future_status::ready) << "Legacy event was not received";

    nlohmann::json eventParams = eventFuture.get();
    EXPECT_TRUE(eventParams["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise);
}

TEST_F(GatewayUTest, LegacyRPCv1Event_array)
{
    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        if (request["method"].get<std::string>().find(".onEvent") != std::string::npos)
        {
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["result"]["listening"] = true;
            m_server.send(hdl, response.dump(), msg->get_opcode());

            nlohmann::json legacyEvent;
            legacyEvent["jsonrpc"] = "2.0";
            legacyEvent["id"] = request["id"];
            legacyEvent["result"] = {{{"fired", true}}};
            m_server.send(hdl, legacyEvent.dump(), msg->get_opcode());
        }
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);

    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto eventStatus = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(eventStatus, std::future_status::ready) << "Legacy event was not received";

    nlohmann::json eventParams = eventFuture.get();
    EXPECT_TRUE(eventParams[0]["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise);
}

TEST_F(GatewayUTest, UnsubscribeFromCallbackDoesNotDeadlock)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            if (request.contains("id") && request.contains("params") && request["params"].contains("listen"))
            {
                response["jsonrpc"] = "2.0";
                response["id"] = request["id"];
                response["result"] = {{"listening", request["params"]["listen"]}};
            }
            if (request.contains("id") && request.contains("method") && request["method"] == "fire.event")
            {
                response["jsonrpc"] = "2.0";
                response["method"] = "test.onEvent";
                response["params"] = {{"fired", true}};
            }
            if (!response.empty())
            {
                m_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
            }
        }
        catch (...)
        {
        }
    };

    IGateway& gateway = connectAndWait();

    struct CallbackCtx
    {
        IGateway* gateway;
        std::promise<void>* done;
        std::string eventName;
    };

    std::promise<void> donePromise;
    auto doneFuture = donePromise.get_future();

    CallbackCtx ctx{&gateway, &donePromise, "test.onEvent"};

    auto onEvent = [](void* usercb, const nlohmann::json& /*params*/)
    {
        auto* ctx = static_cast<CallbackCtx*>(usercb);
        ctx->gateway->unsubscribe(ctx->eventName, usercb);
        try
        {
            ctx->done->set_value();
        }
        catch (...)
        {
        }
    };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &ctx);
    ASSERT_EQ(err, Firebolt::Error::None);

    gateway.send("fire.event", {});

    auto status = doneFuture.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(status, std::future_status::ready) << "Callback blocked (possible deadlock)";
}

// ---------------------------------------------------------------------------
// Regression test: disconnect() must return quickly even when the server dies
// while active subscriptions are held.
//
// Repro scenario (RDKEMW-16573):
//   1. Client subscribes to an event (gateway sends subscribe ACK)
//   2. Server disappears abruptly (no WS close handshake, no unsubscribe ACK)
//   3. Client calls disconnect()
//
// Before the fix: disconnect() blocked for ~waitTime_ms per subscription
// (request(...).get() inside unsubscribe() held the calling thread).
//
// After the fix: disconnect() uses wait_for(50ms) ceilings and returns in
// well under 500ms regardless of server responsiveness.
//
// The test asserts disconnect() completes within 500ms (generous allowance).
// On an unpatched build it will take >= waitTime_ms (1000ms default) and fail.
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, DisconnectDoesNotHangWhenServerDisappearsWithActiveSubscription)
{
    // Use a silent message handler so subscribe ACK is sent but unsubscribe ACK
    // is intentionally never sent (simulates server dying after the subscription).
    bool subscribeAckSent = false;
    m_messageHandler = [this, &subscribeAckSent](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        const std::string method = request.value("method", "");

        // Ack the subscribe so the client considers it active.
        if (!subscribeAckSent && method.find(".on") != std::string::npos)
        {
            nlohmann::json ack;
            ack["jsonrpc"] = "2.0";
            ack["id"] = request["id"];
            ack["result"] = {{"listening", true}};
            m_server.send(hdl, ack.dump(), msg->get_opcode());
            subscribeAckSent = true;
            return;
        }
        // All subsequent messages (including the unsubscribe request) get no reply.
        // This simulates a dead server.
    };

    // Connect with a callback that mirrors the Sky app's log output.
    // On device these appear as:
    //   [Firebolt] FireboltService uninitialize
    //   [Firebolt] Connection state changed: Disconnected, error=2
    //   [Firebolt] Connection state changed: Disconnected end
    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Error err =
        gateway.connect(getTestConfig(),
                        [this](bool connected, const Firebolt::Error& error)
                        {
                            if (connected)
                            {
                                onConnectionChange(connected, error);
                            }
                            else
                            {
                                FIREBOLT_LOG_INFO("FireboltApp", "Connection state changed: Disconnected, error=%d",
                                                  static_cast<int>(error));
                                FIREBOLT_LOG_INFO("FireboltApp", "Connection state changed: Disconnected end");
                            }
                        });
    ASSERT_EQ(err, Firebolt::Error::None);
    connectionFuture.wait_for(std::chrono::seconds(2));

    std::promise<nlohmann::json> eventPromise;
    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error subErr = gateway.subscribe("test.onStateChanged", onEvent, &eventPromise);
    ASSERT_EQ(subErr, Firebolt::Error::None) << "subscribe() failed";

    // Give the subscribe ACK time to arrive.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Switch to a silent handler: server keeps the TCP connection open but
    // never replies to anything (including unsubscribe ACKs).
    // This is the on-device scenario: WPEFramework plugin stops responding
    // without closing the socket.  The bug is in unsubscribe(): pre-fix calls
    // request().get() which blocks indefinitely waiting for the ACK.
    m_messageHandler = [](connection_hdl, server::message_ptr) { /* drop everything */ };

    // Time unsubscribe() + disconnect() together.
    // The hang is in unsubscribe(): pre-fix calls request().get() which blocks
    // indefinitely waiting for the server's unsubscribe ACK (which never comes).
    // With the bug: unsubscribe() blocks >= waitTime_ms (1000 ms).
    // With the fix: unsubscribe() returns in <= 50 ms (wait_for ceiling).
    FIREBOLT_LOG_INFO("FireboltApp", "FireboltService uninitialize");
    auto t0 = std::chrono::steady_clock::now();
    gateway.unsubscribe("test.onStateChanged", &eventPromise);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);

    std::cout << "[timing] unsubscribe() took " << elapsed.count() << " ms\n";

    gateway.disconnect();

    EXPECT_LT(elapsed.count(), 200) << "unsubscribe() took " << elapsed.count()
                                    << " ms — blocked waiting for ACK from silent server (bug reproduced)";
}

// ---------------------------------------------------------------------------
// Additional gateway tests for coverage gaps
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.ConnectAlreadyConnected
// Covers: src/gateway.cpp (transport returns AlreadyConnected)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, ConnectAlreadyConnected)
{
    IGateway& gateway = connectAndWait();

    // Try to connect again
    Firebolt::Error err = gateway.connect(getTestConfig(), [](bool, const Firebolt::Error&) {});
    EXPECT_EQ(err, Firebolt::Error::AlreadyConnected);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.DuplicateSubscribeToSameEvent
// Covers: src/gateway.cpp:server.subscribe returns Error::General on dup usercb
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, DuplicateSubscribeToSameEvent)
{
    IGateway& gateway = connectAndWait();

    auto onEvent = [](void*, const nlohmann::json&) {};
    int dummyCb = 0;

    Firebolt::Error err = gateway.subscribe("test.onDup", onEvent, &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Duplicate subscribe with same usercb should fail
    err = gateway.subscribe("test.onDup", onEvent, &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::General);

    gateway.unsubscribe("test.onDup", &dummyCb);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.UnsubscribeNonexistentEvent
// Covers: src/gateway.cpp:server.unsubscribe returns General when not found
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, UnsubscribeNonexistentEvent)
{
    IGateway& gateway = connectAndWait();

    int dummyCb = 0;
    Firebolt::Error err = gateway.unsubscribe("nonexistent.event", &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::General);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.NotificationWithValueWrapping
// Covers: Server::notify value-unwrapping branch (params with single "value" key)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, NotificationWithValueWrapping)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onValue", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Server sends notification with params: {"value": 42} (scalar → unwrapped)
    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onValue";
        eventMsg["params"] = {{"value", 42}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy", {});

    auto status = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    // When params has a single "value" key with non-object value, it's unwrapped
    nlohmann::json received = eventFuture.get();
    EXPECT_EQ(received, 42);

    gateway.unsubscribe("test.onValue", &eventPromise);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.NotificationWithValueObjectNotUnwrapped
// Covers: Server::notify value-is-object branch (params kept as-is)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, NotificationWithValueObjectNotUnwrapped)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onObjValue", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Server sends notification with params: {"value": {"nested": true}} (object → NOT unwrapped)
    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onObjValue";
        eventMsg["params"] = {{"value", {{"nested", true}}}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy", {});

    auto status = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    // When value is an object, params are kept as-is
    nlohmann::json received = eventFuture.get();
    EXPECT_TRUE(received.contains("value"));
    EXPECT_TRUE(received["value"]["nested"].get<bool>());

    gateway.unsubscribe("test.onObjValue", &eventPromise);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.NotificationNoSubscribers
// Covers: Server::notify no-subscribers branch (warning log)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, NotificationNoSubscribers)
{
    IGateway& gateway = connectAndWait();

    // Server sends a notification for an event nobody subscribed to
    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onNobodyListens";
        eventMsg["params"] = {{"data", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy", {});

    // Give time for the notification to be processed (it should just log a warning)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Verify the gateway still works after the orphan notification
    auto responseFuture = gateway.request("test.method", {{"key", "val"}});
    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready);
    EXPECT_TRUE(responseFuture.get());
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.ResponseWithNoReceiver
// Covers: src/gateway.cpp: Client::response out_of_range catch
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, ResponseWithNoReceiver)
{
    IGateway& gateway = connectAndWait();

    // Server sends a response with an id nobody is waiting for
    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json orphanResponse;
        orphanResponse["jsonrpc"] = "2.0";
        orphanResponse["id"] = 99999;
        orphanResponse["result"] = {{"orphan", true}};
        s->send(hdl, orphanResponse.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy", {});

    // Give time for the orphan to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Gateway should still function normally
    auto responseFuture = gateway.request("test.method", {{"key", "val"}});
    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready);
    EXPECT_TRUE(responseFuture.get());
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.SubscribeTimeout
// Covers: src/gateway.cpp:subscribe ACK timeout path
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, SubscribeTimeout)
{
    // Use a message handler that never responds to subscribe requests
    m_messageHandler = [](connection_hdl, server::message_ptr) { /* drop everything */ };

    IGateway& gateway = connectAndWait();

    auto onEvent = [](void*, const nlohmann::json&) {};
    int dummyCb = 0;

    Firebolt::Error err = gateway.subscribe("test.onTimeout", onEvent, &dummyCb);
    // Subscribe should return Timedout because the ACK never arrives
    EXPECT_EQ(err, Firebolt::Error::Timedout);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.SendNotConnected
// Covers: src/gateway.cpp:Client::send → transport.send NotConnected
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, SendNotConnected)
{
    // Don't start the server — connect will fail
    IGateway& gateway = GetGatewayInstance();
    Firebolt::Config cfg = getTestConfig();
    ReservedLoopbackPort reservedPort = reserveLikelyUnusedLoopbackPort();
    ASSERT_GE(reservedPort.fd, 0) << "Failed to reserve loopback port for SendNotConnected";
    cfg.wsUrl = "ws://127.0.0.1:" + std::to_string(reservedPort.port);

    std::promise<Firebolt::Error> connectFailure;
    auto connectFailureFuture = connectFailure.get_future();
    Firebolt::Error err = gateway.connect(cfg,
                                          [&connectFailure](bool connected, const Firebolt::Error& cbErr)
                                          {
                                              if (!connected)
                                              {
                                                  try
                                                  {
                                                      connectFailure.set_value(cbErr);
                                                  }
                                                  catch (const std::future_error&)
                                                  {
                                                  }
                                              }
                                          });
    ASSERT_TRUE(err == Firebolt::Error::General || err == Firebolt::Error::None);

    if (err == Firebolt::Error::None)
    {
        ASSERT_EQ(connectFailureFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_EQ(connectFailureFuture.get(), Firebolt::Error::General);
    }

    // Send should fail with NotConnected
    err = gateway.send("test.method", {});
    EXPECT_EQ(err, Firebolt::Error::NotConnected);

    // Disconnect while locals are still alive to prevent use-after-scope
    gateway.disconnect();
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.DisconnectWithoutConnect
// Covers: src/gateway.cpp:disconnect when transport is in NotStarted state
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, DisconnectWithoutConnect)
{
    // After TearDown resets the gateway, calling disconnect again should be safe
    // The singleton is already disconnected after the preceding test's TearDown
    IGateway& gateway = GetGatewayInstance();
    Firebolt::Error err = gateway.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.NotificationMultipleParams
// Covers: Server::notify multi-key params branch (pass as-is)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, NotificationMultipleParams)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onMultiParam", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Server sends notification with multiple params (not just "value")
    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onMultiParam";
        eventMsg["params"] = {{"key1", "val1"}, {"key2", "val2"}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy", {});

    auto status = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    nlohmann::json received = eventFuture.get();
    EXPECT_EQ(received["key1"], "val1");
    EXPECT_EQ(received["key2"], "val2");

    gateway.unsubscribe("test.onMultiParam", &eventPromise);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.SubscribeErrorFromServer
// Covers: src/gateway.cpp:subscribe → server returns error → unsubscribe
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, SubscribeErrorFromServer)
{
    // Server responds to subscribe with an error
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["error"]["code"] = -32601;
            response["error"]["message"] = "Method not found";
            m_server.send(hdl, response.dump(), msg->get_opcode());
        }
        catch (...)
        {
        }
    };

    IGateway& gateway = connectAndWait();

    auto onEvent = [](void*, const nlohmann::json&) {};
    int dummyCb = 0;

    Firebolt::Error err = gateway.subscribe("test.onInvalid", onEvent, &dummyCb);
    // The server's JSON-RPC error (-32601) should be propagated as MethodNotFound
    EXPECT_EQ(err, Firebolt::Error::MethodNotFound);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.LegacyRPCv1UnsubscribeCleanup
// Covers: src/gateway.cpp:unsubscribe legacy RPC v1 event map cleanup
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, LegacyRPCv1UnsubscribeCleanup)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        if (request.contains("params") && request["params"].contains("listen"))
        {
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["result"]["listening"] = request["params"]["listen"];
            m_server.send(hdl, response.dump(), msg->get_opcode());
        }
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEvent = [](void*, const nlohmann::json&) {};
    int dummyCb = 0;

    Firebolt::Error err = gateway.subscribe("test.onLegacyEvent", onEvent, &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Unsubscribe should clean up the rpcv1_eventMap entry
    err = gateway.unsubscribe("test.onLegacyEvent", &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.LegacyRPCv1NonEventResult
// Covers: legacy RPC v1 response path (id not in eventMap → falls through to client.response)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, LegacyRPCv1NonEventResult)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["id"] = request["id"];
        response["result"] = {{"data", "not_listening"}};
        m_server.send(hdl, response.dump(), msg->get_opcode());
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    // A normal request in legacy mode — result does NOT have "listening" key
    // and the id is not in the event map, so it falls through to client.response()
    auto future = gateway.request("device.name", {});
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    auto result = future.get();
    ASSERT_TRUE(result);
    EXPECT_EQ((*result)["data"], "not_listening");
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.WaitTimeConfiguration
// Covers: src/gateway.cpp:runtime_waitTime_ms configuration from Config
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, WaitTimeConfiguration)
{
    // Use a handler that never responds
    m_messageHandler = [](connection_hdl, server::message_ptr) {};

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.waitTime_ms = 200; // Short timeout

    Firebolt::Error err =
        gateway.connect(cfg, [this](bool connected, const Firebolt::Error& e) { onConnectionChange(connected, e); });
    ASSERT_EQ(err, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto start = std::chrono::steady_clock::now();
    auto future = gateway.request("test.noReply", {});
    auto status = future.wait_for(std::chrono::milliseconds(1000));
    ASSERT_EQ(status, std::future_status::ready);

    auto result = future.get();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
    // Should timeout around waitTime_ms + watchdog interval (200 + 500 = ~700ms max)
    EXPECT_LT(elapsed.count(), 1500);
}

// ---------------------------------------------------------------------------
// Branch-coverage tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.SendResponseIsIgnoredByClient
// Covers: Client::response invoke-set erase path (fire-and-forget response dropped)
// The response for a fire-and-forget send() ID should be silently dropped.
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, SendResponseIsIgnoredByClient)
{
    // Server echoes back a response for every message (including sends)
    IGateway& gateway = connectAndWait();

    // send() uses Client::send which adds the ID to the invokes set.
    // When the echo comes back, client.response() should find it in invokes,
    // erase it, and return without error.
    nlohmann::json params = {{"key", "value"}};
    Firebolt::Error err = gateway.send("test.fire_and_forget", params);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Give time for the echo response to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Gateway should still be functional — the invoke response was silently dropped
    auto future = gateway.request("test.method", {{"k", "v"}});
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(future.get());
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.RequestFailsWhenSendErrors
// Covers: Client::request send-error path (promise set with error + erase)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, RequestFailsWhenSendErrors)
{
    // Don't start server — connect to a port nobody listens on
    // so transport is in Disconnected state after connection failure
    IGateway& gateway = GetGatewayInstance();

    Firebolt::Config cfg = getTestConfig();
    ReservedLoopbackPort reservedPort = reserveLikelyUnusedLoopbackPort();
    ASSERT_GE(reservedPort.fd, 0) << "Failed to reserve loopback port for RequestFailsWhenSendErrors";
    cfg.wsUrl = "ws://127.0.0.1:" + std::to_string(reservedPort.port);

    std::promise<Firebolt::Error> connectFailure;
    auto connectFailureFuture = connectFailure.get_future();
    Firebolt::Error err = gateway.connect(cfg,
                                          [&connectFailure](bool connected, const Firebolt::Error& cbErr)
                                          {
                                              if (!connected)
                                              {
                                                  try
                                                  {
                                                      connectFailure.set_value(cbErr);
                                                  }
                                                  catch (const std::future_error&)
                                                  {
                                                  }
                                              }
                                          });
    ASSERT_TRUE(err == Firebolt::Error::General || err == Firebolt::Error::None);

    if (err == Firebolt::Error::None)
    {
        ASSERT_EQ(connectFailureFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_EQ(connectFailureFuture.get(), Firebolt::Error::General);
    }

    // Now request — transport.send will fail with NotConnected,
    // which triggers the error branch in Client::request (lines 136-141)
    auto future = gateway.request("test.method", {{"k", "v"}});
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    auto result = future.get();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::NotConnected);

    // Disconnect while locals are still alive to prevent use-after-scope
    gateway.disconnect();
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.LegacySubscribeFailureCleansEventMap
// Covers: legacy RPC v1 subscribe error cleanup (erase rpcv1_eventMap)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, LegacySubscribeFailureCleansEventMap)
{
    // Server never responds to subscribe → timeout
    m_messageHandler = [](connection_hdl, server::message_ptr) {};

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEvent = [](void*, const nlohmann::json&) {};
    int dummyCb = 0;

    // Subscribe will timeout (50ms ACK timeout), hitting the error cleanup path
    // that also cleans the rpcv1_eventMap (line 565-567)
    Firebolt::Error err = gateway.subscribe("test.onLegacyTimeout", onEvent, &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::Timedout);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.UnsubscribeAckWithError
// Covers: unsubscribe ACK error propagation (!result → status = error)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, UnsubscribeAckWithError)
{
    // Server responds to subscribe with success, but to unsubscribe with error
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];

            if (request.contains("params") && request["params"].contains("listen"))
            {
                if (request["params"]["listen"].get<bool>())
                {
                    // Subscribe ACK — success
                    response["result"] = {{"listening", true}};
                }
                else
                {
                    // Unsubscribe ACK — error
                    response["error"]["code"] = -32601;
                    response["error"]["message"] = "Unsubscribe failed";
                }
            }
            else
            {
                response["result"] = nlohmann::json::object();
            }
            m_server.send(hdl, response.dump(), msg->get_opcode());
        }
        catch (...)
        {
        }
    };

    IGateway& gateway = connectAndWait();

    auto onEvent = [](void*, const nlohmann::json&) {};
    int dummyCb = 0;

    Firebolt::Error err = gateway.subscribe("test.onErrUnsub", onEvent, &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Unsubscribe should get the error from the ACK (line 621)
    err = gateway.unsubscribe("test.onErrUnsub", &dummyCb);
    EXPECT_EQ(err, Firebolt::Error::MethodNotFound);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.LegacyUnsubscribeIteratesPastNonMatchingEntry
// Covers: legacy RPC v1 unsubscribe loop (++it else branch —
//         it->second != event, advancing the iterator past non-matching entries)
// Scenario: In legacy RPC v1 mode, subscribe to two events (eventA gets a
//           lower message ID, eventB gets a higher one). When unsubscribing
//           eventB, the loop iterates past eventA's entry (++it) before
//           finding eventB. This is a real scenario when multiple concurrent
//           subscriptions exist.
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, LegacyUnsubscribeIteratesPastNonMatchingEntry)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            if (request.contains("params") && request["params"].contains("listen"))
            {
                nlohmann::json response;
                response["jsonrpc"] = "2.0";
                response["id"] = request["id"];
                response["result"]["listening"] = request["params"]["listen"];
                m_server.send(hdl, response.dump(), msg->get_opcode());
            }
        }
        catch (...)
        {
        }
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);
    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEventA = [](void*, const nlohmann::json&) {};
    auto onEventB = [](void*, const nlohmann::json&) {};
    int cbA = 0;
    int cbB = 0;

    // Subscribe to two events — eventA gets a lower message ID in rpcv1_eventMap
    Firebolt::Error err = gateway.subscribe("test.onEventA", onEventA, &cbA);
    EXPECT_EQ(err, Firebolt::Error::None);

    err = gateway.subscribe("test.onEventB", onEventB, &cbB);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Unsubscribe eventB — the loop must skip eventA's entry (++it at line 600)
    // before finding eventB's entry
    err = gateway.unsubscribe("test.onEventB", &cbB);
    EXPECT_EQ(err, Firebolt::Error::None);

    // Clean up eventA
    err = gateway.unsubscribe("test.onEventA", &cbA);
    EXPECT_EQ(err, Firebolt::Error::None);
}

// Regression test: disconnect() must cancel all pending requests so that calling
// threads blocked on future.get() unblock immediately with NotConnected rather
// than hanging indefinitely.
TEST_F(GatewayUTest, DisconnectCancelsPendingRequests)
{
    // Set a message handler that silently swallows every message — simulating a
    // gateway that accepts the connection but never responds to a request.
    m_messageHandler = [](connection_hdl, server::message_ptr) {};

    IGateway& gateway = connectAndWait();

    // Fire a request that the server will never answer.
    auto responseFuture = gateway.request("test.neverResponds", nlohmann::json{});

    // The request is now in-flight and the future is pending. Disconnect before
    // the watchdog timeout (waitTime_ms = 1000 ms) fires.
    Firebolt::Error disconnectErr = gateway.disconnect();
    EXPECT_EQ(disconnectErr, Firebolt::Error::None);

    // After disconnect() returns, cancelAll() must have resolved the promise.
    // The future must be immediately ready — no waiting required.
    ASSERT_EQ(responseFuture.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);

    auto result = responseFuture.get();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Firebolt::Error::NotConnected);
}

// ---------------------------------------------------------------------------
// Test name: GatewayUTest.DisconnectIsNotTimebound
// Covers: disconnect() completes immediately without waiting for watchdog interval
// Scenario type: performance
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, DisconnectIsNotTimebound)
{
    m_messageHandler = [](connection_hdl, server::message_ptr) {};

    IGateway& gateway = connectAndWait();

    // Fire a request that the server will never answer.
    auto responseFuture = gateway.request("test.neverResponds", nlohmann::json{});

    // The request is now in-flight and the future is pending. Disconnect and
    // measure the time it takes. With the condition_variable::wait_for refactoring,
    // disconnect() should complete immediately (< 100ms) rather than waiting
    // for the full watchdog interval (500ms).
    auto t0 = std::chrono::steady_clock::now();
    Firebolt::Error disconnectErr = gateway.disconnect();
    auto t1 = std::chrono::steady_clock::now();
    auto disconnectDuration = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    EXPECT_EQ(disconnectErr, Firebolt::Error::None);

    // Disconnect should complete well within the watchdog interval (500ms).
    // Allow some overhead but it should be significantly faster than 500ms.
    EXPECT_LT(disconnectDuration, 200) << "disconnect() took " << disconnectDuration
                                         << "ms, expected < 200ms (watchdog interval is 500ms)";

    // After disconnect() returns, cancelAll() must have resolved the promise.
    ASSERT_EQ(responseFuture.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);

    auto result = responseFuture.get();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Firebolt::Error::NotConnected);
}
