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

#include "firebolt/config.h"
#include "firebolt/transport_export.h"
#include "firebolt/types.h"
#include <chrono>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Firebolt::Transport
{
using EventCallback = std::function<void(void* usercb, const nlohmann::json& params)>;
using ConnectionChangeCallback = std::function<void(const bool connected, const Firebolt::Error error)>;
using ResumeCallback = std::function<void(std::chrono::milliseconds stoppedFor)>;

class FIREBOLTTRANSPORT_EXPORT IGateway
{
public:
    virtual ~IGateway();

    // NOTE: onConnectionChange is invoked once with the final result on the
    // connect() calling thread.  All subsequent callbacks (disconnect, watchdog
    // reconnect) fire on the websocketpp IO thread.  Callers must be thread-safe.
    virtual Firebolt::Error connect(const Firebolt::Config& config, ConnectionChangeCallback onConnectionChange) = 0;
    virtual Firebolt::Error disconnect() = 0;

    virtual Firebolt::Error send(const std::string& method, const nlohmann::json& parameters) = 0;
    virtual std::future<Firebolt::Result<nlohmann::json>> request(const std::string& method,
                                                                  const nlohmann::json& parameters) = 0;
    virtual Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb) = 0;
    virtual Firebolt::Error unsubscribe(const std::string& event, void* usercb) = 0;

    /**
     * @brief Retrieve a response header by name from the server after connection.
     * @param headerName The name of the header to retrieve (case-insensitive).
     * @return The header value if present, otherwise std::nullopt.
     */
    virtual std::optional<std::string> getResponseHeader(const std::string& headerName) = 0;

    /**
     * @brief Register a listener invoked after the process resumes from a stop.
     *
     * While connected, the transport detects intervals in which the process did
     * not run (e.g. container hibernation) and invokes the listener once per
     * detected resume with the total stopped duration since the last dispatch.
     * The listener runs on the notification worker thread, like event callbacks,
     * and may issue requests. Setting a listener replaces the previous one;
     * nullptr clears it.
     */
    virtual void setResumeListener(ResumeCallback /*listener*/) {}
};

FIREBOLTTRANSPORT_EXPORT IGateway& GetGatewayInstance();
} // namespace Firebolt::Transport
