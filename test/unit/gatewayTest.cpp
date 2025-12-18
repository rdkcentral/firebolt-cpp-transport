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
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace Firebolt::Transport;

auto dummy_onConnectionChange = [](bool /*connected*/, const Firebolt::Error& /*err*/) {};

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

    IGateway& connectAndWait()
    {
        startServer();
        IGateway& gateway = GetGatewayInstance();
        auto connectionFuture = m_connectionPromise.get_future();

        auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/) {
            if (connected) {
                try {
                    m_connectionPromise.set_value(true);
                } catch (const std::future_error& e) {
                    // Promise might be already set, ignore
                }
            }
        };

        Firebolt::Error err = gateway.connect(getTestConfig(), onConnectionChange);
        EXPECT_EQ(err, Firebolt::Error::None);

        auto status = connectionFuture.wait_for(std::chrono::seconds(2));
        EXPECT_EQ(status, std::future_status::ready) << "Connection timed out";
        if (status == std::future_status::ready) {
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

    auto onConnectionChange = [&](bool connected, const Firebolt::Error& /*err*/) {
        try {
            m_connectionPromise.set_value(connected);
        } catch (const std::future_error& e) {
        }
    };

    Firebolt::Error err = gateway.connect(getTestConfig(), onConnectionChange);
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

    auto onEvent = [](void* usercb, const nlohmann::json& params) {
        static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params);
    };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    m_onMessageAction = [](server* s, connection_hdl hdl) {
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

    auto responseStatus = responseFuture.wait_for(std::chrono::milliseconds(50));
    std::cout << "Response wait status: " << static_cast<int>(responseStatus) << std::endl;
    ASSERT_EQ(responseStatus, std::future_status::timeout) << "Request should have timed out";
}

TEST_F(GatewayTest, RequestWithError)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg) {
        try {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["error"]["code"] = -32601;
            response["error"]["message"] = "Method not found";
            m_server.send(hdl, response.dump(), msg->get_opcode());
        } catch (...) {
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
    startServer();
    IGateway& gateway = GetGatewayInstance();
    gateway.connect(getTestConfig(), dummy_onConnectionChange);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
    startServer();
    IGateway& gateway = GetGatewayInstance();
    gateway.connect(getTestConfig(), dummy_onConnectionChange);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

    EXPECT_EQ(eventFuture1.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    ASSERT_EQ(eventFuture2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(eventFuture2.get()["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise2);
}
