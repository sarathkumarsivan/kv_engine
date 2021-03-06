/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2020 Couchbase, Inc
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

#include "vb_commit.h"

#include <utility>

namespace VB {
Commit::Commit(Collections::VB::Manifest& manifest,
               vbucket_state vbs,
               std::function<bool(const std::system_error&)> sysErrorCallback)
    : collections(manifest),
      proposedVBState(std::move(vbs)),
      sysErrorCallback(std::move(sysErrorCallback)) {
}
} // namespace VB