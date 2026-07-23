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

#include "floral/display/Edid.h"

#include <gtest/gtest.h>

namespace floral::display {
namespace {

TEST(EdidTest, PrimaryIdentityIsStableAndValid) {
    const EdidBlock first = BuildEdid(0, "Floral Main");
    const EdidBlock second = BuildEdid(0, "Floral Main");

    EXPECT_EQ(first, second);
    EXPECT_TRUE(HasValidEdidChecksum(first));
    EXPECT_EQ(first[126], 0);
}

TEST(EdidTest, PortsHaveDistinctProductAndSerialIdentity) {
    const EdidBlock primary = BuildEdid(0, "Floral Main");
    const EdidBlock external = BuildEdid(1, "Floral Ext 1");

    EXPECT_TRUE(HasValidEdidChecksum(primary));
    EXPECT_TRUE(HasValidEdidChecksum(external));
    EXPECT_NE(primary, external);
    EXPECT_NE(primary[10], external[10]);
    EXPECT_NE(primary[12], external[12]);
}

}  // namespace
}  // namespace floral::display
