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
#include "utils.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace Firebolt::Transport;

TEST(GatewayUrlUtilsTest, VerifyUrls)
{
    bool legacyRPCv1 = false;
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437", legacyRPCv1), "ws://127.0.0.1:3437/?RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437?sessionId=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/?sessionId=abc&RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437/json?session=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/json?session=abc&RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://localhost:9003", legacyRPCv1), "ws://localhost:9003/?RPCv2=true");
}

TEST(GatewayUrlUtilsTest, VerifyUrls_LegacyMode)
{
    bool legacyRPCv1 = true;
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437", legacyRPCv1), "ws://127.0.0.1:3437/");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437?sessionId=abc", legacyRPCv1), "ws://127.0.0.1:3437/?sessionId=abc");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437/json?session=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/json?session=abc");
    EXPECT_EQ(buildGatewayUrl("ws://localhost:9003", legacyRPCv1), "ws://localhost:9003/");
}

class GatewayTest : public ::testing::Test
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
        cfg.log.level = Firebolt::LogLevel::Error;
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

TEST_F(GatewayTest, ConnectAndDisconnect)
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

TEST_F(GatewayTest, Request)
{
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_TRUE(result);
}

TEST_F(GatewayTest, Send)
{
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    Firebolt::Error err = gateway.send("test.method", params);
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayTest, SubscribeUnsubscribe)
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

TEST_F(GatewayTest, RequestTimeout)
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

TEST_F(GatewayTest, RequestWithError)
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
}

TEST_F(GatewayTest, MultipleSubscriptions)
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

TEST_F(GatewayTest, UnsubscribeSpecific)
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

TEST_F(GatewayTest, InvalidNotification)
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

TEST_F(GatewayTest, LegacyRPCv1Event)
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

TEST_F(GatewayTest, UnsubscribeFromCallbackDoesNotDeadlock)
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
