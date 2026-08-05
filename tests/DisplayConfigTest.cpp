/*
 * Copyright 2026 FloralDroid
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "floral/display/DisplayConfig.h"

#include <gtest/gtest.h>

namespace floral::display {
namespace {

TEST(DisplayConfigTest, LimitsAdvertisedRefreshRatesAndPreservesExactMaximum) {
    const std::vector<uint32_t> supported = {60, 15, 30, 0, 30};

    EXPECT_EQ(LimitRefreshRates(supported, 1), (std::vector<uint32_t>{1}));
    EXPECT_EQ(LimitRefreshRates(supported, 15), (std::vector<uint32_t>{15}));
    EXPECT_EQ(LimitRefreshRates(supported, 24), (std::vector<uint32_t>{15, 24}));
    EXPECT_EQ(LimitRefreshRates(supported, 30), (std::vector<uint32_t>{15, 30}));
    EXPECT_EQ(LimitRefreshRates(supported, 45), (std::vector<uint32_t>{15, 30, 45}));
    EXPECT_EQ(LimitRefreshRates(supported, 60), (std::vector<uint32_t>{15, 30, 60}));
    EXPECT_EQ(LimitRefreshRates(supported, 0), std::vector<uint32_t>{});
}

TEST(DisplayConfigTest, ConvertsRefreshRateToRoundedVsyncPeriod) {
    EXPECT_EQ(RefreshRateToVsyncPeriodNanos(15), 66'666'667);
    EXPECT_EQ(RefreshRateToVsyncPeriodNanos(30), 33'333'333);
    EXPECT_EQ(RefreshRateToVsyncPeriodNanos(60), 16'666'667);
    EXPECT_EQ(RefreshRateToVsyncPeriodNanos(0), 0);
}

}  // namespace
}  // namespace floral::display
