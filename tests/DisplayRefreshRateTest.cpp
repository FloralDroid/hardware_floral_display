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

#include "floral/display/Display.h"

#include <gtest/gtest.h>
#include <time.h>

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace floral::display {
namespace {

int64_t MonotonicNanos() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

DisplayConfig MultiRefreshConfig(uint32_t initialRefreshRateHz) {
    DisplayConfig config;
    config.supported_refresh_rates_hz = {15, 30, 60};
    config.vsync_period_nanos = RefreshRateToVsyncPeriodNanos(initialRefreshRateHz);
    return config;
}

hwc2_config_t FindConfigByRefreshRate(Display* display, uint32_t refreshRateHz) {
    uint32_t count = 0;
    if (display->GetDisplayConfigs(&count, nullptr) != HWC2_ERROR_NONE) {
        return std::numeric_limits<hwc2_config_t>::max();
    }
    std::vector<hwc2_config_t> configs(count);
    if (display->GetDisplayConfigs(&count, configs.data()) != HWC2_ERROR_NONE) {
        return std::numeric_limits<hwc2_config_t>::max();
    }

    const int64_t expectedPeriod = RefreshRateToVsyncPeriodNanos(refreshRateHz);
    for (const hwc2_config_t config : configs) {
        int32_t period = 0;
        if (display->GetDisplayAttribute(config, HWC2_ATTRIBUTE_VSYNC_PERIOD, &period) ==
                    HWC2_ERROR_NONE &&
            period == expectedPeriod) {
            return config;
        }
    }
    return std::numeric_limits<hwc2_config_t>::max();
}

TEST(DisplayRefreshRateTest, ExposesSameResolutionRefreshConfigsInOneGroup) {
    Display display(MultiRefreshConfig(60), {}, CreatePassthroughFrameSink());
    uint32_t count = 0;
    ASSERT_EQ(display.GetDisplayConfigs(&count, nullptr), HWC2_ERROR_NONE);
    ASSERT_EQ(count, 3u);

    std::vector<hwc2_config_t> configs(count);
    ASSERT_EQ(display.GetDisplayConfigs(&count, configs.data()), HWC2_ERROR_NONE);
    ASSERT_EQ(count, 3u);

    const int64_t expectedPeriods[] = {
            RefreshRateToVsyncPeriodNanos(15),
            RefreshRateToVsyncPeriodNanos(30),
            RefreshRateToVsyncPeriodNanos(60),
    };
    for (size_t index = 0; index < configs.size(); ++index) {
        int32_t width = 0;
        int32_t height = 0;
        int32_t period = 0;
        int32_t group = -1;
        EXPECT_EQ(display.GetDisplayAttribute(configs[index], HWC2_ATTRIBUTE_WIDTH, &width),
                  HWC2_ERROR_NONE);
        EXPECT_EQ(display.GetDisplayAttribute(configs[index], HWC2_ATTRIBUTE_HEIGHT, &height),
                  HWC2_ERROR_NONE);
        EXPECT_EQ(display.GetDisplayAttribute(configs[index], HWC2_ATTRIBUTE_VSYNC_PERIOD, &period),
                  HWC2_ERROR_NONE);
        EXPECT_EQ(display.GetDisplayAttribute(configs[index], HWC2_ATTRIBUTE_CONFIG_GROUP, &group),
                  HWC2_ERROR_NONE);
        EXPECT_EQ(width, 1920);
        EXPECT_EQ(height, 1080);
        EXPECT_EQ(period, expectedPeriods[index]);
        EXPECT_EQ(group, 0);
    }
}

TEST(DisplayRefreshRateTest, ImmediateConfigSwitchUpdatesCurrentPeriod) {
    Display display(MultiRefreshConfig(60), {}, CreatePassthroughFrameSink());
    const hwc2_config_t config30 = FindConfigByRefreshRate(&display, 30);
    ASSERT_NE(config30, std::numeric_limits<hwc2_config_t>::max());

    ASSERT_EQ(display.SetActiveConfig(config30), HWC2_ERROR_NONE);

    hwc2_config_t activeConfig = 0;
    hwc2_vsync_period_t currentPeriod = 0;
    EXPECT_EQ(display.GetActiveConfig(&activeConfig), HWC2_ERROR_NONE);
    EXPECT_EQ(display.GetDisplayVsyncPeriod(&currentPeriod), HWC2_ERROR_NONE);
    EXPECT_EQ(activeConfig, config30);
    EXPECT_EQ(currentPeriod, RefreshRateToVsyncPeriodNanos(30));
}

TEST(DisplayRefreshRateTest, ConstrainedSwitchKeepsOldPeriodUntilDesiredTime) {
    Display display(MultiRefreshConfig(60), {}, CreatePassthroughFrameSink());
    const hwc2_config_t config30 = FindConfigByRefreshRate(&display, 30);
    ASSERT_NE(config30, std::numeric_limits<hwc2_config_t>::max());

    hwc_vsync_period_change_constraints_t constraints{};
    constraints.desiredTimeNanos = MonotonicNanos() + 50'000'000;
    constraints.seamlessRequired = true;
    hwc_vsync_period_change_timeline_t timeline{};
    ASSERT_EQ(display.SetActiveConfigWithConstraints(config30, &constraints, &timeline),
              HWC2_ERROR_NONE);
    EXPECT_GE(timeline.newVsyncAppliedTimeNanos, constraints.desiredTimeNanos);
    EXPECT_FALSE(timeline.refreshRequired);

    hwc2_config_t activeConfig = 0;
    hwc2_vsync_period_t currentPeriod = 0;
    ASSERT_EQ(display.GetActiveConfig(&activeConfig), HWC2_ERROR_NONE);
    ASSERT_EQ(display.GetDisplayVsyncPeriod(&currentPeriod), HWC2_ERROR_NONE);
    EXPECT_EQ(activeConfig, config30);
    EXPECT_EQ(currentPeriod, RefreshRateToVsyncPeriodNanos(60));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ASSERT_EQ(display.GetDisplayVsyncPeriod(&currentPeriod), HWC2_ERROR_NONE);
    } while (currentPeriod != RefreshRateToVsyncPeriodNanos(30) &&
             std::chrono::steady_clock::now() < deadline);
    EXPECT_EQ(currentPeriod, RefreshRateToVsyncPeriodNanos(30));
}

TEST(DisplayRefreshRateTest, VsyncCallbackUsesNewPeriodAfterImmediateSwitch) {
    std::mutex callbackMutex;
    std::condition_variable callbackCondition;
    int64_t observedPeriod = 0;
    Display display(
            MultiRefreshConfig(60),
            [&](hwc2_display_t, int64_t, int64_t periodNanos) {
                {
                    std::lock_guard lock(callbackMutex);
                    observedPeriod = periodNanos;
                }
                callbackCondition.notify_all();
            },
            CreatePassthroughFrameSink());

    ASSERT_EQ(display.SetVsyncEnabled(HWC2_VSYNC_ENABLE), HWC2_ERROR_NONE);
    {
        std::unique_lock lock(callbackMutex);
        ASSERT_TRUE(callbackCondition.wait_for(lock, std::chrono::milliseconds(500), [&] {
            return observedPeriod == RefreshRateToVsyncPeriodNanos(60);
        }));
    }

    const hwc2_config_t config30 = FindConfigByRefreshRate(&display, 30);
    ASSERT_NE(config30, std::numeric_limits<hwc2_config_t>::max());
    ASSERT_EQ(display.SetActiveConfig(config30), HWC2_ERROR_NONE);
    {
        std::unique_lock lock(callbackMutex);
        ASSERT_TRUE(callbackCondition.wait_for(lock, std::chrono::milliseconds(500), [&] {
            return observedPeriod == RefreshRateToVsyncPeriodNanos(30);
        }));
    }
    EXPECT_EQ(display.SetVsyncEnabled(HWC2_VSYNC_DISABLE), HWC2_ERROR_NONE);
}

}  // namespace
}  // namespace floral::display
