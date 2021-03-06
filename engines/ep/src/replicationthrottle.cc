/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 Couchbase, Inc
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

#include "replicationthrottle.h"
#include "configuration.h"
#include "stats.h"

ReplicationThrottleEP::ReplicationThrottleEP(const Configuration& config,
                                             EPStats& s)
    : queueCap(config.getReplicationThrottleQueueCap()),
      capPercent(config.getReplicationThrottleCapPcnt()),
      stats(s) {
}

bool ReplicationThrottleEP::persistenceQueueSmallEnough() const {
    size_t queueSize = stats.diskQueueSize.load();
    if (stats.replicationThrottleWriteQueueCap == -1) {
        return true;
    }
    return queueSize < static_cast<size_t>(stats.replicationThrottleWriteQueueCap);
}

bool ReplicationThrottleEP::hasSomeMemory() const {
    auto memoryUsed =
            static_cast<double>(stats.getEstimatedTotalMemoryUsed());
    auto maxSize = static_cast<double>(stats.getMaxDataSize());

    return memoryUsed <= (maxSize * stats.replicationThrottleThreshold);
}

ReplicationThrottleEP::Status ReplicationThrottleEP::getStatus() const {
    return (persistenceQueueSmallEnough() && hasSomeMemory()) ? Status::Process
                                                              : Status::Pause;
}

void ReplicationThrottleEP::adjustWriteQueueCap(size_t totalItems) {
    if (queueCap == -1) {
        stats.replicationThrottleWriteQueueCap.store(-1);
        return;
    }
    auto qcap = static_cast<size_t>(queueCap);
    size_t throttleCap = 0;
    if (capPercent > 0) {
        throttleCap = (static_cast<double>(capPercent) / 100.0) * totalItems;
    }
    stats.replicationThrottleWriteQueueCap.store(throttleCap > qcap ? throttleCap :
                                         qcap);
}

ReplicationThrottleEphe::ReplicationThrottleEphe(const Configuration& config,
                                                 EPStats& s)
    : ReplicationThrottleEP(config, s), config(config) {
}

ReplicationThrottleEP::Status ReplicationThrottleEphe::getStatus() const {
    auto status = ReplicationThrottleEP::getStatus();
    if (status == Status::Pause) {
        if (config.getEphemeralFullPolicy() == "fail_new_data") {
            return Status::Disconnect;
        }
    }
    return status;
}

bool ReplicationThrottleEphe::doDisconnectOnNoMem() const {
    if (config.getEphemeralFullPolicy() == "fail_new_data") {
        return true;
    }
    return false;
}
