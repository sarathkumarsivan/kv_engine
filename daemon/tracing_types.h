/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc
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

#pragma once

#include "task.h"

#include <phosphor/phosphor.h>
#include <phosphor/tools/export.h>
#include <platform/uuid.h>

#include <map>
#include <mutex>

class Connection;

/**
 * DumpContext holds all the information required for an ongoing
 * trace dump
 */
struct DumpContext {
    explicit DumpContext(std::string content)
        : content(std::move(content)),
          last_touch(std::chrono::steady_clock::now()) {
    }

    const std::string content;
    const std::chrono::steady_clock::time_point last_touch;
};

/**
 * Aggregate object to hold a map of dumps and a mutex protecting them
 */
struct TraceDumps {
    std::map<cb::uuid::uuid_t, DumpContext> dumps;
    std::mutex mutex;
};

/**
 * StaleTraceDumpRemover is a periodic task that removes old dumps
 */
class StaleTraceDumpRemover : public PeriodicTask {
public:
    StaleTraceDumpRemover(TraceDumps& traceDumps,
                          std::chrono::seconds period,
                          std::chrono::seconds max_age)
        : PeriodicTask(period), traceDumps(traceDumps), max_age(max_age) {
    }

    Status periodicExecute() override;

private:
    TraceDumps& traceDumps;
    const std::chrono::seconds max_age;
};
