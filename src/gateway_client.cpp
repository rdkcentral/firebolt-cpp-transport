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

#include "gateway_client.h"
#include "firebolt/logger.h"
#include <exception>
#include <vector>

namespace Firebolt::Transport
{

namespace
{
// Extracts code and message from a JSON-RPC error object. A malformed gateway payload (missing or wrong-type
// keys) falls back to defaults instead of throwing, because an exception would escape the calling thread and
// crash the process.
Firebolt::ErrorInfo parseErrorInfo(const nlohmann::json& error, MessageID id)
{
    int32_t code = static_cast<int32_t>(Firebolt::Error::General);
    std::string message = "unknown error";
    try
    {
        if (error.contains("code") && error["code"].is_number())
            code = error["code"].get<int32_t>();
        if (error.contains("message") && error["message"].is_string())
            message = error["message"].get<std::string>();
    }
    catch (const std::exception& e)
    {
        FIREBOLT_LOG_WARNING("Gateway", "[response] malformed error payload id=%u: %s", id, e.what());
    }
    return Firebolt::ErrorInfo(code, message);
}
} // namespace

Client::Client(IClientTransport& transport)
    : transport_(transport)
{
}

std::unique_ptr<Client::Caller> Client::takeCaller(MessageID id, std::size_t* remaining)
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    auto it = queue_.find(id);
    if (it == queue_.end())
    {
        return nullptr;
    }
    auto caller = std::move(it->second);
    queue_.erase(it);
    if (remaining)
    {
        *remaining = queue_.size();
    }
    return caller;
}

void Client::checkPromises(std::chrono::milliseconds timeout)
{
    std::vector<std::unique_ptr<Caller>> timedOut;
    std::size_t queueSizeSnapshot = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        // Reserve upfront so push_back() cannot throw mid-loop after an entry has already been moved out of
        // the queue: either every expired caller is collected, or none has been removed.
        timedOut.reserve(queue_.size());
        queueSizeSnapshot = queue_.size();
        const auto now = std::chrono::steady_clock::now();
        for (auto it = queue_.begin(); it != queue_.end();)
        {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second->timestamp) > timeout)
            {
                timedOut.push_back(std::move(it->second));
                it = queue_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    // Log and fulfill promises outside the critical section to avoid blocking I/O under the mutex and to
    // keep other gateway operations from waiting on it.
    if (queueSizeSnapshot > 0)
    {
        FIREBOLT_LOG_DEBUG("Gateway", "[watchdog] pending request queue size=%zu", queueSizeSnapshot);
    }
    for (auto& caller : timedOut)
    {
        FIREBOLT_LOG_WARNING("Gateway", "Request timed out, id=%u method='%s'", caller->id, caller->method.c_str());
        caller->promise.set_value(Result<nlohmann::json>(Firebolt::Error::Timedout));
    }
}

void Client::cancelAll()
{
    // Move all pending callers out under the mutex, then fulfill their promises after releasing it.
    // Fulfilling under the lock wakes waiting threads while still holding it, and would deadlock if a future
    // continuation ever called back into the gateway.
    decltype(queue_) toCancel;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        toCancel = std::move(queue_);
    }
    for (auto& [id, caller] : toCancel)
    {
        FIREBOLT_LOG_WARNING("Gateway", "[disconnect] cancelling pending request id=%u method='%s'", id,
                             caller->method.c_str());
        caller->promise.set_value(Result<nlohmann::json>(Firebolt::Error::NotConnected));
    }
}

std::future<Result<nlohmann::json>> Client::request(const std::string& method, const nlohmann::json& parameters,
                                                    std::optional<MessageID> idOpt)
{
    const MessageID id = idOpt ? *idOpt : transport_.getNextMessageID();
    auto caller = std::make_unique<Caller>(id, method);
    auto future = caller->promise.get_future();

    std::size_t pendingAfterEnqueue = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_[id] = std::move(caller);
        pendingAfterEnqueue = queue_.size();
    }
    FIREBOLT_LOG_DEBUG("Gateway", "[request] queued method='%s' id=%u, pending=%zu", method.c_str(), id,
                       pendingAfterEnqueue);

    const Firebolt::Error status = transport_.send(method, parameters, id);
    if (status != Firebolt::Error::None)
    {
        FIREBOLT_LOG_WARNING("Gateway", "[request] transport send failed for method='%s' id=%u status=%d",
                             method.c_str(), id, static_cast<int>(status));
        // Take the entry back before fulfilling: if cancelAll(), checkPromises() or response() already took
        // it, takeCaller() returns nullptr and we must not touch the promise a second time. A double
        // set_value() throws std::future_error.
        if (auto owned = takeCaller(id))
        {
            owned->promise.set_value(Result<nlohmann::json>{status});
        }
    }

    return future;
}

void Client::response(const nlohmann::json& message)
{
    const MessageID id = message["id"];
    std::size_t remaining = 0;
    // The caller is removed under the mutex and its promise fulfilled after the lock is released, so waking
    // the waiting thread never happens while holding it.
    auto caller = takeCaller(id, &remaining);
    if (!caller)
    {
        FIREBOLT_LOG_INFO("Gateway", "No receiver for a message, id: %u", id);
        return;
    }

    FIREBOLT_LOG_DEBUG("Gateway", "[response] matched id=%u method='%s', remaining_pending=%zu", id,
                       caller->method.c_str(), remaining);

    if (!message.contains("error"))
    {
        FIREBOLT_LOG_DEBUG("Gateway", "[response] success id=%u method='%s'", id, caller->method.c_str());
        caller->promise.set_value(Result<nlohmann::json>{message["result"]});
        return;
    }

    const Firebolt::ErrorInfo errorInfo = parseErrorInfo(message["error"], id);
    FIREBOLT_LOG_WARNING("Gateway", "[response] error id=%u method='%s' code=%d message='%s'", id,
                         caller->method.c_str(), errorInfo.error(), errorInfo.message().c_str());
    caller->promise.set_value(Result<nlohmann::json>{static_cast<Firebolt::Error>(errorInfo.error()), errorInfo});
}

} // namespace Firebolt::Transport
