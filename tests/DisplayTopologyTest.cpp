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

#include "floral/display/DisplayTopology.h"

#include <gtest/gtest.h>

namespace floral::display {
namespace {

TEST(DisplayTopologyTest, PreservesPermanentInternalPrimaryIdentity) {
    DisplayConfig config;
    config.id = 99;
    config.port = 42;
    config.connection_type = ConnectionType::kExternal;

    DisplayTopology topology(config);
    const auto displays = topology.ConnectedDisplays();

    ASSERT_EQ(displays.size(), 1u);
    EXPECT_EQ(displays[0].id, DisplayTopology::kPrimaryDisplayId);
    EXPECT_EQ(displays[0].port, 0);
    EXPECT_EQ(displays[0].connection_type, ConnectionType::kInternal);
    EXPECT_FALSE(topology.Get(99).has_value());
}

TEST(DisplayTopologyTest, ConnectsExternalDisplaysWithoutRenumberingPrimary) {
    DisplayTopology topology(DisplayConfig{});
    DisplayConfig external;
    external.id = 7;
    external.port = 3;
    external.width = 1280;
    external.height = 720;
    external.connection_type = ConnectionType::kInternal;
    external.name = "Floral External Display 7";

    EXPECT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kSuccess);

    const auto displays = topology.ConnectedDisplays();
    ASSERT_EQ(displays.size(), 2u);
    EXPECT_EQ(displays[0].id, DisplayTopology::kPrimaryDisplayId);
    EXPECT_EQ(displays[0].port, 0);
    EXPECT_EQ(displays[1].id, 7u);
    EXPECT_EQ(displays[1].port, 3);
    EXPECT_EQ(displays[1].connection_type, ConnectionType::kExternal);
}

TEST(DisplayTopologyTest, RejectsReservedIdentityAndDuplicatePort) {
    DisplayTopology topology(DisplayConfig{});
    DisplayConfig external;
    external.id = 2;
    external.port = 1;
    ASSERT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kSuccess);

    external.id = DisplayTopology::kPrimaryDisplayId;
    external.port = 2;
    EXPECT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kPrimaryDisplayProtected);

    external.id = 3;
    external.port = 1;
    EXPECT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kPortInUse);

    external.id = 2;
    external.port = 2;
    EXPECT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kAlreadyConnected);

    external.id = 0;
    EXPECT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kInvalidConfig);
}

TEST(DisplayTopologyTest, DisconnectsOnlyExternalDisplays) {
    DisplayTopology topology(DisplayConfig{});
    DisplayConfig external;
    external.id = 8;
    external.port = 4;
    ASSERT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kSuccess);

    EXPECT_EQ(topology.Disconnect(DisplayTopology::kPrimaryDisplayId),
              DisplayTopologyResult::kPrimaryDisplayProtected);
    EXPECT_EQ(topology.Disconnect(99), DisplayTopologyResult::kNotFound);
    EXPECT_EQ(topology.Disconnect(8), DisplayTopologyResult::kSuccess);
    EXPECT_FALSE(topology.Get(8).has_value());

    const auto displays = topology.ConnectedDisplays();
    ASSERT_EQ(displays.size(), 1u);
    EXPECT_EQ(displays[0].id, DisplayTopology::kPrimaryDisplayId);
}

TEST(DisplayTopologyTest, ReplacesExternalSnapshotAndReportsConfigurationChanges) {
    DisplayTopology topology(DisplayConfig{});
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

    DisplayTopologyChanges changes;
    ASSERT_EQ(topology.ReplaceExternalDisplays({first, second}, &changes),
              DisplayTopologyResult::kSuccess);
    EXPECT_TRUE(changes.disconnected_display_ids.empty());
    ASSERT_EQ(changes.connected_displays.size(), 2u);
    EXPECT_EQ(changes.connected_displays[0].id, 2u);
    EXPECT_EQ(changes.connected_displays[1].id, 3u);

    ASSERT_EQ(topology.ReplaceExternalDisplays({first, second}, &changes),
              DisplayTopologyResult::kSuccess);
    EXPECT_TRUE(changes.disconnected_display_ids.empty());
    EXPECT_TRUE(changes.connected_displays.empty());

    first.width = 1920;
    first.port = 2;
    second.port = 1;
    ASSERT_EQ(topology.ReplaceExternalDisplays({first, second}, &changes),
              DisplayTopologyResult::kSuccess);
    EXPECT_EQ(changes.disconnected_display_ids, (std::vector<DisplayId>{2, 3}));
    ASSERT_EQ(changes.connected_displays.size(), 2u);
    EXPECT_EQ(changes.connected_displays[0].port, 2);
    EXPECT_EQ(changes.connected_displays[1].port, 1);
}

TEST(DisplayTopologyTest, RejectsInvalidReplacementWithoutChangingCurrentDisplays) {
    DisplayTopology topology(DisplayConfig{});
    DisplayConfig external;
    external.id = 2;
    external.port = 1;
    ASSERT_EQ(topology.ConnectExternal(external), DisplayTopologyResult::kSuccess);

    DisplayTopologyChanges changes;
    DisplayConfig duplicatePort = external;
    duplicatePort.id = 3;
    EXPECT_EQ(topology.ReplaceExternalDisplays({external, duplicatePort}, &changes),
              DisplayTopologyResult::kPortInUse);
    EXPECT_TRUE(topology.Get(2).has_value());
    EXPECT_FALSE(topology.Get(3).has_value());
}

}  // namespace
}  // namespace floral::display
