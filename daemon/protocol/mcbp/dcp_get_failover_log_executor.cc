/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "executors.h"

#include "dcp_add_failover_log.h"
#include "engine_wrapper.h"

#include <daemon/cookie.h>

void dcp_get_failover_log_executor(Cookie& cookie) {
    auto ret = cookie.swapAiostat(cb::engine_errc::success);

    if (ret == cb::engine_errc::success) {
        auto& req = cookie.getRequest();
        ret = dcpGetFailoverLog(
                cookie,
                req.getOpaque(),
                req.getVBucket(),
                [c = std::ref(cookie)](
                        const std::vector<vbucket_failover_t>& vec) {
                    return add_failover_log(vec, c);
                });
    }

    if (ret != cb::engine_errc::success) {
        handle_executor_status(cookie, ret);
    }
}
