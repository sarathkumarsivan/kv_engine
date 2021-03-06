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
#include "hdrhistogram.h"
#include "thread_gate.h"

#include <folly/portability/GTest.h>
#include <cmath>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <utility>

static std::vector<std::pair<uint64_t, uint64_t>> getValuesOnePerBucket(
        HdrHistogram& histo) {
    std::vector<std::pair<uint64_t, uint64_t>> values;
    auto iter = histo.makeLinearIterator(/* valueUnitsPerBucket */ 1);
    while (auto pair = iter.getNextValueAndCount()) {
        values.push_back(*pair);
    }
    return values;
}

/*
 * Unit tests for the HdrHistogram
 */

// Test can add minimum value (0)
TEST(HdrHistogramTest, addMin) {
    HdrHistogram histogram{0, 255, 3};
    histogram.addValue(0);
    EXPECT_EQ(1, histogram.getValueCount());
    EXPECT_EQ(0, histogram.getValueAtPercentile(100.0));
    EXPECT_EQ(0, histogram.getMinValue());
}

// Test can add maximum value (255)
TEST(HdrHistogramTest, addMax) {
    HdrHistogram histogram{0, 255, 3};
    histogram.addValue(255);
    EXPECT_EQ(1, histogram.getValueCount());
    EXPECT_EQ(255, histogram.getValueAtPercentile(100.0));
    EXPECT_EQ(255, histogram.getMaxValue());
}

// Test the bias of +1 used by the underlying hdr_histogram data structure
// does not affect the overall behaviour.
TEST(HdrHistogramTest, biasTest) {
    HdrHistogram histogram{0, 255, 3};

    double sum = 0;
    for (int ii = 0; ii < 256; ii++) {
        histogram.addValue(ii);
        sum += ii;
    }

    EXPECT_EQ(0, histogram.getValueAtPercentile(0.1));
    EXPECT_EQ(2, histogram.getValueAtPercentile(1.0));
    EXPECT_EQ(127, histogram.getValueAtPercentile(50.0));
    EXPECT_EQ(229, histogram.getValueAtPercentile(90.0));
    EXPECT_EQ(242, histogram.getValueAtPercentile(95.0));
    EXPECT_EQ(255, histogram.getValueAtPercentile(100.0));

    EXPECT_EQ(sum / 256, histogram.getMean());
}

// Test the linear iterator
TEST(HdrHistogramTest, linearIteratorTest) {
    HdrHistogram histogram{0, 255, 3};

    for (int ii = 0; ii < 256; ii++) {
        histogram.addValue(ii);
    }

    uint64_t valueCount = 0;
    auto values = getValuesOnePerBucket(histogram);
    EXPECT_EQ(256, values.size());
    for (auto& result : values) {
        EXPECT_EQ(valueCount++, result.first);
    }
}

// Test the linear iterator using base two
TEST(HdrHistogramTest, logIteratorBaseTwoTest) {
    const uint64_t initBucketWidth = 1;
    const uint64_t numOfValues = 256;
    const uint64_t minValue = 0;
    HdrHistogram histogram{minValue, numOfValues - 1, 3};

    for (uint64_t ii = minValue; ii < numOfValues; ii++) {
        histogram.addValue(ii);
    }

    // Need to create the iterator after we have added the data
    const double iteratorBase = 2.0;
    HdrHistogram::Iterator iter{
            histogram.makeLogIterator(initBucketWidth, iteratorBase)};

    uint64_t countSum = 0;
    uint64_t bucketIndex = 0;
    while (auto result = iter.getNextValueAndCount()) {
        // Check that the values of the buckets increase exponentially
        EXPECT_EQ(pow(iteratorBase, bucketIndex) - 1, result->first);
        // Check that the width of the bucket is the same number as the count
        // as we added values in a linear matter
        EXPECT_EQ((iter.value_iterated_to - iter.value_iterated_from),
                  result->second);
        bucketIndex++;
        countSum += result->second;
    }
    // check we count as many counts as we added
    EXPECT_EQ(numOfValues, countSum);
    // check the iterator has the same number of values we added
    EXPECT_EQ(numOfValues, iter.total_count);
}

// Test the linear iterator using base five
TEST(HdrHistogramTest, logIteratorBaseFiveTest) {
    const uint64_t initBucketWidth = 1;
    const uint64_t numOfValues = 625;
    const uint64_t minValue = 0;
    HdrHistogram histogram{minValue, numOfValues - 1, 3};

    for (uint64_t ii = minValue; ii < numOfValues; ii++) {
        histogram.addValue(ii);
    }

    // Need to create the iterator after we have added the data
    const double iteratorBase = 5.0;
    HdrHistogram::Iterator iter{
            histogram.makeLogIterator(initBucketWidth, iteratorBase)};

    uint64_t countSum = 0;
    uint64_t bucketIndex = 0;
    while (auto result = iter.getNextValueAndCount()) {
        // Check that the values of the buckets increase exponentially
        EXPECT_EQ(pow(iteratorBase, bucketIndex) - 1, result->first);
        // Check that the width of the bucket is the same number as the count
        // as we added values in a linear matter
        EXPECT_EQ((iter.value_iterated_to - iter.value_iterated_from),
                  result->second);
        bucketIndex++;
        countSum += result->second;
    }
    // check we count as many counts as we added
    EXPECT_EQ(numOfValues, countSum);
    // check the iterator has the same number of values we added
    EXPECT_EQ(numOfValues, iter.total_count);
}

// Test the addValueAndCount method
TEST(HdrHistogramTest, addValueAndCountTest) {
    HdrHistogram histogram{0, 255, 3};

    histogram.addValueAndCount(0, 100);

    auto values = getValuesOnePerBucket(histogram);
    EXPECT_EQ(1, values.size());
    for (auto& result : values) {
        EXPECT_EQ(0, result.first);
        EXPECT_EQ(100, result.second);
    }
}

#define LOG_NORMAL_MEAN 0
#define LOG_NORMAL_STD 2.0
#define LOG_NORMAL_SCALE_UP_MULT 35000
#define LOG_NORMAL_MIN 50000
static std::vector<uint64_t> valuesToAdd(10000);
static bool initialised = false;
// static function to return a log normal value scaled by
// LOG_NORMAL_SCALE_UP_MULT. It creates an array of 10000 static values that
// using std::lognormal_distribution and returners them in an incrementing
// linear fashion so that they can be used for the meanTest
static uint64_t GetNextLogNormalValue() {
    static unsigned int i = 0;

    if (!initialised) {
        // create a log normal distribution and random number generator
        // so we can add random values in a log normal distribution which is a
        // better representation of a production environment
        std::random_device randomDevice;
        std::mt19937 randomNumGen(randomDevice());
        std::lognormal_distribution<long double> distribution(LOG_NORMAL_MEAN,
                                                              LOG_NORMAL_STD);
        // We have denormalize the log normal distribution with a min
        // changing from 0 to 50000ns the max should remain at inf and set
        // the mean to about 84000ns.
        // Percentile values will vary as we use a random number generator to
        // seed a X value when getting values from the distribution. However,
        // the values below should give an idea of the distribution which
        // modelled around an "ADD" op from stats.log p50:~84000ns |
        // p90:~489000ns |p99:3424000ns |p99.9:20185000ns | p99.99:41418000ns
        for (auto& currVal : valuesToAdd) {
            auto valToAdd = static_cast<uint64_t>(
                    LOG_NORMAL_MIN + std::round(distribution(randomNumGen) *
                                                LOG_NORMAL_SCALE_UP_MULT));
            currVal = valToAdd;
        }
        initialised = true;
    }

    if (i >= valuesToAdd.size()) {
        i = 0;
    }

    return valuesToAdd[i++];
}

// Test the getMean method
TEST(HdrHistogramTest, meanTest) {
    HdrHistogram histogram{0, 60000000, 3};
    uint64_t sum = 0;
    uint64_t total_count = 0;

    for (uint64_t i = 0; i < 1000000; i++) {
        uint64_t count = GetNextLogNormalValue();
        uint64_t value = GetNextLogNormalValue();

        // only add random values inside the histograms range
        // otherwise we sill skew the mean
        if (value <= histogram.getMaxTrackableValue()) {
            histogram.addValueAndCount(value, count);
            // store values so we can calculate the real mean
            sum += value * count;
            total_count += count;
        }
    }

    // calculate the mean
    double_t avg = (sum / static_cast<double_t>(total_count));

    uint64_t meanDiff = std::abs(avg - histogram.getMean());
    double_t errorPer = (meanDiff / avg) * 100.0;

    // check that the error percentage is less than 0.05%
    EXPECT_GT(0.05, std::abs(errorPer));
}

void addValuesThread(HdrHistogram& histo,
                     ThreadGate& tg,
                     unsigned int iterations,
                     unsigned int size) {
    // wait for all threads to be ready to start
    tg.threadUp();
    for (unsigned int iteration = 0; iteration < iterations; iteration++) {
        for (unsigned int value = 0; value < size; value++) {
            histo.addValue(value);
        }
    }
}
// Test to check that no counts to HdrHistogram are dropped due to
// incorrect memory order when using parallel writing threads
TEST(HdrHistogramTest, addValueParallel) {
    // we want to perform a large amount of addValues so we increase the
    // probability of dropping a count
    unsigned int numOfAddIterations = 5000;
    unsigned int maxVal = 2;
    HdrHistogram histogram{0, maxVal, 3};

    // Create two threads and get them to add values to a small
    // histogram so there is a high contention on it's counts.
    std::vector<std::thread> threads(2);
    ThreadGate tg(threads.size());
    for (auto& t : threads) {
        t = std::thread(addValuesThread,
                        std::ref(histogram),
                        std::ref(tg),
                        numOfAddIterations,
                        maxVal);
    }
    // wait for all the threads to finish
    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(numOfAddIterations * maxVal * threads.size(),
              histogram.getValueCount());
    ASSERT_EQ(maxVal - 1, histogram.getMaxValue());
    ASSERT_EQ(0, histogram.getMinValue());

    uint64_t valueCount = 0;
    auto values = getValuesOnePerBucket(histogram);
    EXPECT_EQ(maxVal, values.size());
    for (auto& result : values) {
        ASSERT_EQ(valueCount++, result.first);
        ASSERT_EQ(threads.size() * numOfAddIterations, result.second);
    }
}

// Test that when histogram is empty getValueAtPercentile returns 0.
TEST(HdrHistogramTest, percentileWhenEmptyTest) {
    HdrHistogram histogram{0, 255, 3};
    ASSERT_EQ(0, histogram.getValueCount());
    EXPECT_EQ(0, histogram.getValueAtPercentile(0.0));
    EXPECT_EQ(0, histogram.getValueAtPercentile(50.0));
    EXPECT_EQ(0, histogram.getValueAtPercentile(100.0));
}

// Test the aggregation operator method
TEST(HdrHistogramTest, aggregationTest) {
    const uint16_t numberOfValues = 15;
    HdrHistogram histogramOne{0, numberOfValues, 3};
    HdrHistogram histogramTwo{0, numberOfValues, 3};

    for (int i = 0; i < numberOfValues; i++) {
        histogramOne.addValue(i);
        histogramTwo.addValue(i);
    }
    // Do aggregation
    histogramOne += histogramTwo;

    auto histoOneValues = getValuesOnePerBucket(histogramOne);
    EXPECT_EQ(numberOfValues, histoOneValues.size());

    auto histoTwoValues = getValuesOnePerBucket(histogramTwo);
    EXPECT_EQ(numberOfValues, histoTwoValues.size());

    EXPECT_NE(histoOneValues, histoTwoValues);

    uint64_t valueCount = 0;
    for (int i = 0; i < numberOfValues; i++) {
        // check values are the same for both histograms
        EXPECT_EQ(valueCount, histoTwoValues[i].first);
        EXPECT_EQ(valueCount, histoOneValues[i].first);
        // check that the counts for each value is twice as much as
        // in a bucket in histogram one as it is in histogram two
        EXPECT_EQ(histoOneValues[i].second, histoTwoValues[i].second * 2);
        ++valueCount;
    }

    // Check the totals of each histogram
    EXPECT_EQ(numberOfValues * 2, histogramOne.getValueCount());
    EXPECT_EQ(numberOfValues, histogramTwo.getValueCount());
}

// Test the aggregation operator method
TEST(HdrHistogramTest, aggregationTestEmptyLhr) {
    const uint16_t numberOfValues = 200;
    HdrHistogram histogramOne{0, 15, 3};
    HdrHistogram histogramTwo{0, numberOfValues, 3};

    for (int i = 0; i < numberOfValues; i++) {
        histogramTwo.addValue(i);
    }
    // Do aggregation
    histogramOne += histogramTwo;

    auto histoOneValues = getValuesOnePerBucket(histogramOne);
    EXPECT_EQ(numberOfValues, histoOneValues.size());

    auto histoTwoValues = getValuesOnePerBucket(histogramTwo);
    EXPECT_EQ(numberOfValues, histoTwoValues.size());

    EXPECT_EQ(histoTwoValues, histoOneValues);

    // Check the totals of each histogram
    EXPECT_EQ(numberOfValues, histogramOne.getValueCount());
    EXPECT_EQ(numberOfValues, histogramTwo.getValueCount());
}

// Test the aggregation operator method
TEST(HdrHistogramTest, aggregationTestEmptyRhs) {
    HdrHistogram histogramOne{0, 1, 3};
    HdrHistogram histogramTwo{0, 1, 1};

    for (int i = 0; i < 200; i++) {
        histogramOne.addValue(i);
    }
    // Do aggregation
    histogramOne += histogramTwo;

    // make sure the histogram has expanded in size for all 200 values
    auto values = getValuesOnePerBucket(histogramOne);
    EXPECT_EQ(200, values.size());
    uint64_t valueCount = 0;
    for (auto& result : values) {
        EXPECT_EQ(valueCount++, result.first);
        EXPECT_EQ(1, result.second);
    }

    // Check the totals of each histogram
    EXPECT_EQ(200, histogramOne.getValueCount());
    EXPECT_EQ(0, histogramTwo.getValueCount());
}

TEST(HdrHistogramTest, int32MaxSizeTest) {
    // Histogram type doesn't really matter for this but we first saw this with
    // a percentiles histogram so that's what we'll use here
    HdrHistogram histogram{
            0, 255, 1, HdrHistogram::Iterator::IterMode::Percentiles};

    // Add int32_t max counts
    uint64_t limit = std::numeric_limits<int32_t>::max();
    histogram.addValueAndCount(0, limit);

    // And test that returned values are correct
    EXPECT_EQ(limit, histogram.getValueCount());
    EXPECT_EQ(0, histogram.getValueAtPercentile(100.0));
    EXPECT_EQ(0, histogram.getMinValue());

    { // iter read lock scope
        HdrHistogram::Iterator iter{histogram.getHistogramsIterator()};
        auto res = iter.getNextBucketLowHighAndCount();
        EXPECT_TRUE(res);
        // The 3rd field [2] of the returned tuple is the count
        EXPECT_EQ(limit, std::get<2>(*res));
    }

    // Add 1 more count (previously this would overflow the total_count field
    // in the iterator)
    histogram.addValue(0);
    limit++;

    // And test that returned values are correct
    EXPECT_EQ(limit, histogram.getValueCount());
    EXPECT_EQ(0, histogram.getValueAtPercentile(100.0));
    EXPECT_EQ(0, histogram.getMinValue());

    { // iter2 read lock scope
        HdrHistogram::Iterator iter2{histogram.getHistogramsIterator()};
        auto res2 = iter2.getNextBucketLowHighAndCount();
        EXPECT_TRUE(res2);
        // The 3rd field [2] of the returned tuple is the count
        EXPECT_EQ(limit, std::get<2>(*res2));
    }
}

TEST(HdrHistogramTest, int64MaxSizeTest) {
#ifdef UNDEFINED_SANITIZER
    // UBSan reports an underflow in this test when manipulating numbers close
    // to uin64_t. Given we don't ever expect to have 2^64 samples I think it's
    // ok to just skip the check under UBSan.
    GTEST_SKIP();
#endif
    // Histogram type doesn't really matter for this but we first saw this with
    // a percentiles histogram so that's what we'll use here
    HdrHistogram histogram{
            0, 255, 1, HdrHistogram::Iterator::IterMode::Percentiles};

    // Add int64_t max counts
    uint64_t limit = std::numeric_limits<int64_t>::max();
    histogram.addValueAndCount(0, limit);

    // And test that returned values are correct
    EXPECT_EQ(limit, histogram.getValueCount());
    EXPECT_EQ(0, histogram.getValueAtPercentile(100.0));
    EXPECT_EQ(0, histogram.getMinValue());

    HdrHistogram::Iterator iter{histogram.getHistogramsIterator()};
    auto res = iter.getNextBucketLowHighAndCount();
    EXPECT_TRUE(res);
    // The 3rd field [2] of the returned tuple is the count
    EXPECT_EQ(limit, std::get<2>(*res));

    // Testing any higher than this gives us garbage results back but
    // unfortunately with no way of knowing that they're garbage.
}

static std::vector<std::optional<std::pair<uint64_t, double>>> getAllValues(
        const HdrHistogram& histo, HdrHistogram::Iterator& iter) {
    std::vector<std::optional<std::pair<uint64_t, double>>> values;
    while (auto pair = iter.getNextValueAndPercentile()) {
        if (!pair.has_value())
            break;
        values.push_back(pair);
    }
    return values;
}

void resetThread(HdrHistogram& histo, ThreadGate& tg) {
    EXPECT_EQ(histo.getValueCount(), 10);
    tg.threadUp();
    histo.reset();
    EXPECT_EQ(histo.getValueCount(), 0);
}

/**
 * Test to check that if you create an iterator on a HdrHistogram object, then
 * call reset() on it in another thread and use the iterator that the iterator
 * doesn't end up in an infinite loop.
 */
TEST(HdrHistogramTest, ResetItoratorInfLoop) {
    Hdr2sfMicroSecHistogram histogram;
    for (int i = 0; i < 10; i++) {
        histogram.addValue(i);
    }
    {
        auto iter = histogram.getHistogramsIterator();
        auto values = getAllValues(histogram, iter);
        EXPECT_EQ(values.size(), 20);
    }

    std::thread thread;
    ThreadGate tg(2);
    { // Scope that holds read lock for iterator
        auto iter2 = histogram.getHistogramsIterator();
        /**
         * Create thread this should start running resetThread() at some point
         * int time will be blocked at HdrHistogram::reset() till this scope is
         * exited and iter2 is destroyed (releasing the read lock).
         * We will also create a ThreadGat to ensure the reset thread is runing
         * and is about to try and get an exclusive lock before reading values
         * from the histogram.
         */
        thread = std::thread(resetThread, std::ref(histogram), std::ref(tg));
        tg.threadUp();
        auto values = getAllValues(histogram, iter2);
        EXPECT_EQ(values.size(), 20);
    } // iter2 read lock released

    // wait for resetThread() to complete
    thread.join();
    EXPECT_EQ(histogram.getValueCount(), 0);
}
