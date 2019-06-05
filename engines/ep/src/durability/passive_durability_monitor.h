/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2019 Couchbase, Inc.
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

#include "durability_monitor.h"
#include "ep_types.h"
#include <folly/SynchronizedPtr.h>
#include <vector>

class VBucket;
class StoredDocKey;

/*
 * The DurabilityMonitor for Replica VBuckets.
 *
 * The PassiveDurabilityMonitor (PDM) is responsible for ack'ing received
 * Prepares back to the Active. The logic in the PDM ensures that Prepares are
 * ack'ed in seqno-order, which is fundamental for achieving:
 * - In-Order Commit at Active
 * - Consistency at failure scenarios
 */
class PassiveDurabilityMonitor : public DurabilityMonitor {
public:
    PassiveDurabilityMonitor(VBucket& vb);

    /**
     * Construct a PassiveDM for the given vBucket, with the specified
     * outstanding prepares as the initial state of the tracked SyncWrites. Used
     * by warmup to restore the state as it was before restart.
     * @param vb VBucket which owns this Durability Monitor.
     * @param outstandingPrepares In-flight prepares which the DM should take
     *        responsibility for.
     *        These must be ordered by ascending seqno, otherwise
     *        std::invalid_argument will be thrown.
     */
    PassiveDurabilityMonitor(VBucket& vb,
                             std::vector<queued_item>&& outstandingPrepares);

    ~PassiveDurabilityMonitor();

    void addStats(const AddStatFn& addStat, const void* cookie) const override;

    int64_t getHighPreparedSeqno() const override;

    int64_t getHighCompletedSeqno() const override;

    /**
     * Add a pending Prepare for tracking into the PDM.
     *
     * @param item the queued_item
     */
    void addSyncWrite(queued_item item);

    enum class Resolution : uint8_t { Commit, Abort };

    /**
     * Complete the given Prepare, i.e. remove it from tracking.
     *
     * @param key The key of the Prepare to be removed
     * @param res The type of resolution, Commit/Abort
     */
    void completeSyncWrite(const StoredDocKey& key, Resolution res);

    static std::string to_string(Resolution res);

    size_t getNumTracked() const override;

    size_t getNumAccepted() const override;
    size_t getNumCommitted() const override;
    size_t getNumAborted() const override;

    /**
     * Notify this PDM that the snapshot-end mutation has been received for the
     * owning VBucket.
     * The snapshot-end seqno is used for the correct implementation of the HPS
     * move-logic.
     *
     * @param snapEnd The snapshot-end seqno
     */
    void notifySnapshotEndReceived(uint64_t snapEnd);

    void notifyLocalPersistence() override;

protected:
    void toOStream(std::ostream& os) const override;

    // The VBucket owning this DurabilityMonitor instance
    VBucket& vb;

    /// PassiveDM state. Guarded by folly::Synchronized to manage concurrent
    /// access. Uses unique_ptr for pimpl.
    struct State;
    folly::SynchronizedPtr<std::unique_ptr<State>> state;

    // Necessary for implementing ADM(PDM&&)
    friend class ActiveDurabilityMonitor;
};
