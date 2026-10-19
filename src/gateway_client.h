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

#include "firebolt/types.h"
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Firebolt::Transport
{
using MessageID = uint32_t;

// The part of the transport that Client needs: id allocation and sending.
class IClientTransport
{
public:
    virtual ~IClientTransport() = default;
    virtual MessageID getNextMessageID() = 0;
    virtual Firebolt::Error send(const std::string& method, const nlohmann::json& parameters, MessageID id) = 0;
};

// Correlates outgoing requests with their responses. Every pending request owns a promise that is fulfilled
// exactly once: by response(), by checkPromises() on timeout, by cancelAll() on disconnect, or by request()
// itself when the transport fails to send.
class Client
{
public:
    explicit Client(IClientTransport& transport);

    std::future<Result<nlohmann::json>> request(const std::string& method, const nlohmann::json& parameters,
                                                std::optional<MessageID> id = std::nullopt);
    void response(const nlohmann::json& message);
    void checkPromises(std::chrono::milliseconds timeout);
    void cancelAll();

private:
    struct Caller
    {
        Caller(MessageID id_, std::string method_)
            : id(id_),
              method(std::move(method_))
        {
        }
        const MessageID id;
        const std::string method;
        std::promise<Result<nlohmann::json>> promise;
        const std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    };

    // Removes and returns the pending caller for `id`, or nullptr if it is no longer pending. Whoever gets a
    // non-null result is the only party allowed to fulfill its promise.
    std::unique_ptr<Caller> takeCaller(MessageID id, std::size_t* remaining = nullptr);

    std::map<MessageID, std::unique_ptr<Caller>> queue_;
    mutable std::mutex queueMutex_;
    IClientTransport& transport_;
};

} // namespace Firebolt::Transport
