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

#pragma once

#include "firebolt/config.h"
#include "firebolt/transport_export.h"
#include "firebolt/types.h"
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include <string>

namespace Firebolt::Transport
{
using EventCallback = std::function<void(void* usercb, const nlohmann::json& params)>;
using ConnectionChangeCallback = std::function<void(const bool connected, const Firebolt::Error error)>;

class IGateway
{
public:
    virtual ~IGateway();

    virtual Firebolt::Error connect(const Firebolt::Config& config, ConnectionChangeCallback onConnectionChange) = 0;
    virtual Firebolt::Error disconnect() = 0;

    virtual Firebolt::Error send(const std::string& method, const nlohmann::json& parameters) = 0;
    virtual std::future<Firebolt::Result<nlohmann::json>> request(const std::string& method,
                                                                  const nlohmann::json& parameters) = 0;
    virtual Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb) = 0;
    virtual Firebolt::Error unsubscribe(const std::string& event, void* usercb) = 0;
};

FIREBOLTTRANSPORT_EXPORT IGateway& GetGatewayInstance();
} // namespace Firebolt::Transport
