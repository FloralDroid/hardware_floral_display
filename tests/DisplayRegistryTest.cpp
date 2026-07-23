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

    Display* primary = registry.Get(DisplayRegistry::kPrimaryDisplayId);
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->id(), DisplayRegistry::kPrimaryDisplayId);
    EXPECT_EQ(primary->port(), 0);
    EXPECT_EQ(registry.Get(99), nullptr);
}

}  // namespace
}  // namespace floral::display
