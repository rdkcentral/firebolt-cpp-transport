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

#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace Firebolt::Transport::Testing
{

// Loopback WebSocket server standing in for App Gateway in deterministic tests.
// Requests are answered with an empty result unless their method is held; held
// requests are recorded and answered only on release(). Binds an ephemeral port.
class ScriptedGateway
{
public:
    using Server = websocketpp::server<websocketpp::config::asio>;
    using Handle = websocketpp::connection_hdl;

    ScriptedGateway() = default;
    ScriptedGateway(const ScriptedGateway&) = delete;
    ScriptedGateway& operator=(const ScriptedGateway&) = delete;

    ~ScriptedGateway() { stop(); }

    bool start()
    {
        try
        {
            server_.init_asio();
            server_.set_reuse_addr(true);
            server_.clear_access_channels(websocketpp::log::alevel::all);
            server_.clear_error_channels(websocketpp::log::elevel::all);
            server_.set_open_handler([this](Handle hdl) { onOpen(hdl); });
            server_.set_message_handler([this](Handle hdl, Server::message_ptr msg) { onMessage(hdl, msg); });

            websocketpp::lib::asio::ip::tcp::endpoint endpoint(websocketpp::lib::asio::ip::address_v4::loopback(), 0);
            server_.listen(endpoint);
            websocketpp::lib::asio::error_code ec;
            auto local = server_.get_local_endpoint(ec);
            if (ec)
            {
                return false;
            }
            port_ = local.port();
            server_.start_accept();
            thread_ = std::thread([this]() { server_.run(); });
            started_ = true;
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    void stop()
    {
        if (!started_)
        {
            return;
        }
        started_ = false;
        server_.stop_listening();
        server_.stop();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    std::string url() const { return "ws://127.0.0.1:" + std::to_string(port_); }

    void holdMethod(const std::string& method)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        heldMethods_.insert(method);
    }

    // Answers every held request for `method` and stops holding it.
    void release(const std::string& method)
    {
        std::vector<Pending> toSend;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            heldMethods_.erase(method);
            auto it = pending_.begin();
            while (it != pending_.end())
            {
                if (it->method == method)
                {
                    toSend.push_back(*it);
                    it = pending_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        for (const Pending& p : toSend)
        {
            reply(p);
        }
    }

    void releaseAll()
    {
        std::vector<Pending> toSend;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            heldMethods_.clear();
            toSend.swap(pending_);
        }
        for (const Pending& p : toSend)
        {
            reply(p);
        }
    }

    // Blocks until at least `count` requests for `method` have reached the server.
    bool waitForRequest(const std::string& method, std::size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return cv_.wait_for(lock, timeout, [&]() { return received_[method] >= count; });
    }

    std::size_t receivedCount(const std::string& method)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return received_[method];
    }

    // Sends a JSON-RPC notification to every connected client.
    void notify(const std::string& method, const nlohmann::json& params)
    {
        nlohmann::json msg;
        msg["jsonrpc"] = "2.0";
        msg["method"] = method;
        msg["params"] = params;
        std::vector<Handle> clients;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            clients = clients_;
        }
        for (const Handle& hdl : clients)
        {
            websocketpp::lib::error_code ec;
            server_.send(hdl, msg.dump(), websocketpp::frame::opcode::text, ec);
        }
    }

private:
    struct Pending
    {
        Handle hdl;
        nlohmann::json id;
        std::string method;
    };

    void onOpen(Handle hdl)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        clients_.push_back(hdl);
    }

    void onMessage(Handle hdl, Server::message_ptr msg)
    {
        nlohmann::json request = nlohmann::json::parse(msg->get_payload(), nullptr, false);
        if (request.is_discarded() || !request.contains("id") || !request.contains("method"))
        {
            return;
        }
        Pending p{hdl, request["id"], request["method"].get<std::string>()};
        bool held = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            held = heldMethods_.count(p.method) > 0;
            if (held)
            {
                pending_.push_back(p);
            }
            ++received_[p.method];
        }
        cv_.notify_all();
        if (!held)
        {
            reply(p);
        }
    }

    void reply(const Pending& p)
    {
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["id"] = p.id;
        response["result"] = nlohmann::json::object();
        websocketpp::lib::error_code ec;
        server_.send(p.hdl, response.dump(), websocketpp::frame::opcode::text, ec);
    }

    Server server_;
    std::thread thread_;
    bool started_ = false;
    uint16_t port_ = 0;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::set<std::string> heldMethods_;
    std::vector<Pending> pending_;
    std::map<std::string, std::size_t> received_;
    std::vector<Handle> clients_;
};

} // namespace Firebolt::Transport::Testing
