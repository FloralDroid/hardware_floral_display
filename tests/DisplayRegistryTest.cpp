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

#include "floral/display/DisplayRegistry.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace floral::display {
namespace {

TEST(DisplayRegistryTest, CreatesExactlyOnePermanentPrimaryDisplay) {
    DisplayConfig config;
    config.id = 99;
    config.port = 42;
    DisplayRegistry registry(config, {});

    const auto ids = registry.ConnectedDisplayIds();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], DisplayRegistry::kPrimaryDisplayId);

    const std::shared_ptr<Display> primary = registry.Get(DisplayRegistry::kPrimaryDisplayId);
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->id(), DisplayRegistry::kPrimaryDisplayId);
    EXPECT_EQ(primary->port(), 0);
    EXPECT_EQ(registry.Get(99), nullptr);
}

TEST(DisplayRegistryTest, EmitsHotplugEventsForExternalDisplayLifetime) {
    std::vector<std::pair<hwc2_display_t, bool>> hotplugEvents;
    DisplayRegistry registry(DisplayConfig{}, {});
    registry.SetHotplugCallback([&hotplugEvents](hwc2_display_t display, bool connected) {
        hotplugEvents.emplace_back(display, connected);
    });
    DisplayConfig external;
    external.id = 2;
    external.port = 1;
    external.width = 1280;
    external.height = 720;
    external.name = "Floral External Display 2";

    ASSERT_EQ(registry.ConnectExternal(external), DisplayTopologyResult::kSuccess);
    EXPECT_EQ(registry.ConnectedDisplayIds(),
              (std::vector<hwc2_display_t>{DisplayRegistry::kPrimaryDisplayId, 2}));
    const std::shared_ptr<Display> retainedDisplay = registry.Get(2);
    ASSERT_NE(retainedDisplay, nullptr);
    EXPECT_EQ(retainedDisplay->port(), 1);

    ASSERT_EQ(registry.Disconnect(2), DisplayTopologyResult::kSuccess);
    EXPECT_EQ(registry.Get(2), nullptr);
    EXPECT_EQ(registry.ConnectedDisplayIds(),
              (std::vector<hwc2_display_t>{DisplayRegistry::kPrimaryDisplayId}));

    // An in-flight HWC call can safely finish after registry removal.
    EXPECT_EQ(retainedDisplay->id(), 2u);
    EXPECT_EQ(hotplugEvents,
              (std::vector<std::pair<hwc2_display_t, bool>>{
                      {DisplayRegistry::kPrimaryDisplayId, true}, {2, true}, {2, false}}));
}

TEST(DisplayRegistryTest, PublishesCurrentSnapshotWhenHotplugCallbackIsInstalled) {
    DisplayRegistry registry(DisplayConfig{}, {});
    DisplayConfig external;
    external.id = 2;
    external.port = 1;
    external.width = 1280;
    external.height = 720;
    external.name = "Floral External Display 2";
    ASSERT_EQ(registry.ConnectExternal(external), DisplayTopologyResult::kSuccess);

    std::vector<std::pair<hwc2_display_t, bool>> hotplugEvents;
    registry.SetHotplugCallback([&hotplugEvents](hwc2_display_t display, bool connected) {
        hotplugEvents.emplace_back(display, connected);
    });
    EXPECT_EQ(hotplugEvents, (std::vector<std::pair<hwc2_display_t, bool>>{
                                     {DisplayRegistry::kPrimaryDisplayId, true}, {2, true}}));

    registry.SetHotplugCallback({});
    ASSERT_EQ(registry.Disconnect(2), DisplayTopologyResult::kSuccess);
    EXPECT_EQ(hotplugEvents, (std::vector<std::pair<hwc2_display_t, bool>>{
                                     {DisplayRegistry::kPrimaryDisplayId, true}, {2, true}}));
}

TEST(DisplayRegistryTest, RejectsPrimaryDisconnectWithoutHotplugEvent) {
    size_t hotplugEventCount = 0;
    DisplayRegistry registry(DisplayConfig{}, {});
    registry.SetHotplugCallback([&hotplugEventCount](hwc2_display_t display, bool connected) {
        (void)display;
        (void)connected;
        ++hotplugEventCount;
    });
    hotplugEventCount = 0;

    EXPECT_EQ(registry.Disconnect(DisplayRegistry::kPrimaryDisplayId),
              DisplayTopologyResult::kPrimaryDisplayProtected);
    EXPECT_EQ(hotplugEventCount, 0u);
    EXPECT_NE(registry.Get(DisplayRegistry::kPrimaryDisplayId), nullptr);
}

TEST(DisplayRegistryTest, ReplacesExternalSnapshotWithOrderedHotplugEvents) {
    std::vector<std::pair<hwc2_display_t, bool>> hotplugEvents;
    std::vector<bool> displayPresentDuringEvent;
    DisplayRegistry* registryPointer = nullptr;
    DisplayRegistry registry(DisplayConfig{}, {});
    registryPointer = &registry;
    registry.SetHotplugCallback([&hotplugEvents, &displayPresentDuringEvent, &registryPointer](
                                        hwc2_display_t display, bool connected) {
        hotplugEvents.emplace_back(display, connected);
        displayPresentDuringEvent.push_back(registryPointer != nullptr &&
                                            registryPointer->Get(display) != nullptr);
    });
    hotplugEvents.clear();
    displayPresentDuringEvent.clear();
    DisplayConfig first;
    first.id = 2;
    first.port = 1;
    first.width = 1280;
    first.height = 720;
    first.name = "Floral External Display 2";
    DisplayConfig second = first;
    second.id = 3;
    second.port = 2;
    second.name = "Floral External Display 3";

    ASSERT_EQ(registry.ReplaceExternalDisplays({first, second}), DisplayTopologyResult::kSuccess);
    const std::shared_ptr<Display> retainedFirst = registry.Get(2);
    ASSERT_NE(retainedFirst, nullptr);
    EXPECT_EQ(hotplugEvents, (std::vector<std::pair<hwc2_display_t, bool>>{{2, true}, {3, true}}));

    first.width = 1920;
    DisplayConfig fourth = second;
    fourth.id = 4;
    fourth.name = "Floral External Display 4";
    ASSERT_EQ(registry.ReplaceExternalDisplays({first, fourth}), DisplayTopologyResult::kSuccess);
    EXPECT_EQ(registry.ConnectedDisplayIds(),
              (std::vector<hwc2_display_t>{DisplayRegistry::kPrimaryDisplayId, 2, 4}));
    EXPECT_EQ(hotplugEvents,
              (std::vector<std::pair<hwc2_display_t, bool>>{
                      {2, true}, {3, true}, {2, false}, {3, false}, {2, true}, {4, true}}));
    EXPECT_EQ(displayPresentDuringEvent, (std::vector<bool>{true, true, false, false, true, true}));

    // The old display remains valid until an in-flight HWC call releases it.
    EXPECT_EQ(retainedFirst->id(), 2u);
}

}  // namespace
}  // namespace floral::display
