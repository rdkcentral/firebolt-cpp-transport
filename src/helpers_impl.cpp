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

#include "helpers_impl.h"
#include "firebolt/gateway.h"

namespace Firebolt::Helpers
{

SubscriptionManager::SubscriptionManager(IHelper& helper, void* owner)
    : helper_(helper),
      owner_(owner)
{
}
SubscriptionManager::~SubscriptionManager()
{
    unsubscribeAll();
}

Result<void> SubscriptionManager::unsubscribe(SubscriptionId id)
{
    return helper_.unsubscribe(id);
}

void SubscriptionManager::unsubscribeAll()
{
    helper_.unsubscribeAll(owner_);
}

IHelper& GetHelperInstance()
{
    static HelperImpl instance(Firebolt::Transport::GetGatewayInstance());
    return instance;
}

} // namespace Firebolt::Helpers
