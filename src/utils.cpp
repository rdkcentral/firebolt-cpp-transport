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

#include "utils.h"

namespace Firebolt::Transport
{
std::string buildGatewayUrl(std::string url, bool legacyRPCv1)
{
    const size_t schemePos = url.find("://");
    const size_t hostStart = (schemePos == std::string::npos) ? 0 : schemePos + 3;
    const size_t pathPos = url.find('/', hostStart);

    if (pathPos == std::string::npos)
    {
        const size_t queryPos = url.find('?', hostStart);
        if (queryPos == std::string::npos)
        {
            url += '/';
        }
        else
        {
            url.insert(queryPos, "/");
        }
    }

    if (!legacyRPCv1)
    {
        if (url.find('?', hostStart) == std::string::npos)
        {
            url += "?";
        }
        else
        {
            url += "&";
        }
        url += "RPCv2=true";
    }

    return url;
}
} // namespace Firebolt::Transport
