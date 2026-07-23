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

}  // namespace
}  // namespace floral::display
