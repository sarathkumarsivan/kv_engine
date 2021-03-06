/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2018 Couchbase, Inc
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

#include "dcp/active_stream.h"
#include "globaltask.h"
#include "vb_ready_queue.h"

#include <memcached/engine_common.h>

#include <queue>
#include <string>
#include <unordered_set>

class ActiveStream;
class DcpProducer;
class Stream;
template <class E>
class StreamContainer;

class ActiveStreamCheckpointProcessorTask : public GlobalTask {
public:
    ActiveStreamCheckpointProcessorTask(EventuallyPersistentEngine& e,
                                        std::shared_ptr<DcpProducer> p);

    std::string getDescription() override {
        return description;
    }

    std::chrono::microseconds maxExpectedDuration() override {
        // Empirical evidence from perf runs suggests this task runs under
        // 210ms 99.9999% of the time.
        return std::chrono::milliseconds(210);
    }

    bool run() override;
    void schedule(std::shared_ptr<ActiveStream> stream);
    void wakeup();

    /* Clears the queues and resets the producer reference */
    void cancelTask();

    /* Returns the number of unique streams waiting to be processed */
    size_t queueSize() {
        return queue.size();
    }

    /// Outputs statistics related to this task via the given callback.
    void addStats(const std::string& name,
                  const AddStatFn& add_stat,
                  const void* c) const;

private:
    std::shared_ptr<StreamContainer<std::shared_ptr<Stream>>> queuePop();

    bool queueEmpty() {
        return queue.empty();
    }

    /// Human-readable description of this task.
    const std::string description;

    /*
     * Maintain a queue of unique vbucket ids for which stream should be
     * processed.
     * There's no need to have the same stream in the queue more than once
     *
     * The streams are kept in the 'streams map' of the producer object. We
     * should not hold a shared reference (even a weak ref) to the stream object
     * here because 'streams map' is the actual owner. If we hold a weak ref
     * here and the streams map replaces the stream for the vbucket id with a
     * new one, then we would end up not updating it here as we append to the
     * queue only if there is no entry for the vbucket in the queue.
     */
    VBReadyQueue queue;

    std::atomic<bool> notified;
    const size_t iterationsBeforeYield;

    const std::weak_ptr<DcpProducer> producerPtr;
};
