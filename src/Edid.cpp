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

#include <algorithm>
#include <numeric>

namespace floral::display {
namespace {

constexpr std::array<uint8_t, 8> kEdidHeader = {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
};

constexpr uint16_t EncodeManufacturer(char first, char second, char third) {
    return static_cast<uint16_t>(((first - 'A' + 1) << 10) | ((second - 'A' + 1) << 5) |
                                 (third - 'A' + 1));
}

void WriteTextDescriptor(EdidBlock* edid, size_t offset, uint8_t type, std::string_view text) {
    auto& data = *edid;
    data[offset + 0] = 0x00;
    data[offset + 1] = 0x00;
    data[offset + 2] = 0x00;
    data[offset + 3] = type;
    data[offset + 4] = 0x00;
    std::fill(data.begin() + offset + 5, data.begin() + offset + 18, ' ');

    const size_t length = std::min<size_t>(text.size(), 12);
    std::copy_n(text.begin(), length, data.begin() + offset + 5);
    data[offset + 5 + length] = '\n';
}

void Write1080p60Timing(EdidBlock* edid, size_t offset) {
    // CTA-861 1920x1080p60 detailed timing: 148.5 MHz pixel clock,
    // 2200x1125 total pixels, positive HSync and VSync.
    constexpr std::array<uint8_t, 18> timing = {
            0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58,
            0x2c, 0x45, 0x00, 0xfd, 0x1e, 0x11, 0x00, 0x00, 0x1e,
    };
    std::copy(timing.begin(), timing.end(), edid->begin() + offset);
}

}  // namespace

EdidBlock BuildEdid(uint8_t port, std::string_view displayName) {
    EdidBlock edid{};
    std::copy(kEdidHeader.begin(), kEdidHeader.end(), edid.begin());

    const uint16_t manufacturer = EncodeManufacturer('F', 'L', 'R');
    edid[8] = static_cast<uint8_t>(manufacturer >> 8);
    edid[9] = static_cast<uint8_t>(manufacturer & 0xff);

    // Product and serial values are little-endian in EDID. Port zero is the
    // permanent primary connector; later ports retain distinct identities.
    const uint16_t product = static_cast<uint16_t>(0x1000u + port);
    const uint32_t serial = 0x464c0000u + static_cast<uint32_t>(port) + 1u;
    edid[10] = static_cast<uint8_t>(product & 0xff);
    edid[11] = static_cast<uint8_t>(product >> 8);
    edid[12] = static_cast<uint8_t>(serial & 0xff);
    edid[13] = static_cast<uint8_t>((serial >> 8) & 0xff);
    edid[14] = static_cast<uint8_t>((serial >> 16) & 0xff);
    edid[15] = static_cast<uint8_t>((serial >> 24) & 0xff);

    edid[16] = 1;   // Manufacture week.
    edid[17] = 36;  // 2026 - 1990.
    edid[18] = 1;
    edid[19] = 4;
    edid[20] = 0x80;  // Digital input.
    edid[21] = 51;    // Approximate 23-inch 16:9 panel dimensions.
    edid[22] = 29;
    edid[23] = 120;   // Gamma 2.2.
    edid[24] = 0x02;  // Preferred timing is in the first descriptor.

    // Unused standard timing slots use the EDID-defined 0x01,0x01 marker.
    for (size_t offset = 38; offset < 54; offset += 2) {
        edid[offset] = 0x01;
        edid[offset + 1] = 0x01;
    }

    Write1080p60Timing(&edid, 54);
    WriteTextDescriptor(&edid, 72, 0xfc, displayName);
    WriteTextDescriptor(&edid, 90, 0xff, port == 0 ? "FLR-PRIMARY" : "FLR-DISPLAY");

    // Monitor range descriptor: 24-60 Hz vertical, 30-140 kHz horizontal,
    // 300 MHz maximum pixel clock. Phase one advertises only 60 Hz through HWC.
    constexpr std::array<uint8_t, 18> range = {
            0x00, 0x00, 0x00, 0xfd, 0x00, 24,   60,   30,   140,
            30,   0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    };
    std::copy(range.begin(), range.end(), edid.begin() + 108);

    edid[126] = 0;  // No extension blocks.
    const uint8_t sum = std::accumulate(edid.begin(), edid.begin() + 127, static_cast<uint8_t>(0));
    edid[127] = static_cast<uint8_t>(0u - sum);
    return edid;
}

bool HasValidEdidChecksum(const EdidBlock& edid) {
    const uint8_t sum = std::accumulate(edid.begin(), edid.end(), static_cast<uint8_t>(0));
    return std::equal(kEdidHeader.begin(), kEdidHeader.end(), edid.begin()) && sum == 0;
}

}  // namespace floral::display
