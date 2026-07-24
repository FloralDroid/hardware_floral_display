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

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <cerrno>

#include <memory>
#include <utility>

namespace floral::display {
namespace {

class RecordingFrameSink final : public FrameSink {
  public:
    const char* Name() const override { return "recording"; }

    FrameSinkResult Submit(FrameSubmission submission) override {
        last_display_id = submission.display_id;
        last_width = submission.width;
        last_height = submission.height;
        last_buffer = submission.buffer;
        last_dataspace = submission.dataspace;
        last_damage = std::move(submission.damage);
        last_sequence = submission.sequence;
        last_submission_time_nanos = submission.submission_time_nanos;
        ++submission_count;

        ++stats.submitted_frames;
        stats.last_sequence = last_sequence;
        stats.last_submission_time_nanos = last_submission_time_nanos;
        stats.last_dataspace = last_dataspace;
        stats.last_damage_rect_count = static_cast<uint32_t>(last_damage.size());
        if (last_buffer != nullptr) {
            ++stats.frames_with_buffer;
        }
        if (submission.acquire_fence.ok()) {
            ++stats.frames_with_acquire_fence;
            ++stats.returned_present_fences;
        }
        FrameSinkResult result;
        result.present_fence = std::move(submission.acquire_fence);
        return result;
    }

    FrameSinkStats GetStats() const override { return stats; }

    DisplayId last_display_id = 0;
    uint32_t last_width = 0;
    uint32_t last_height = 0;
    const native_handle_t* last_buffer = nullptr;
    int32_t last_dataspace = 0;
    std::vector<DamageRect> last_damage;
    uint64_t last_sequence = 0;
    int64_t last_submission_time_nanos = 0;
    uint64_t submission_count = 0;
    FrameSinkStats stats;
};

void Validate(Display* display) {
    uint32_t typeCount = 0;
    uint32_t requestCount = 0;
    ASSERT_EQ(display->ValidateDisplay(&typeCount, &requestCount), HWC2_ERROR_NONE);
    ASSERT_EQ(typeCount, 0u);
    ASSERT_EQ(requestCount, 0u);
}

TEST(DisplayFrameSinkTest, PresentSubmitsClientTargetMetadataAndReturnsFence) {
    auto sink = std::make_unique<RecordingFrameSink>();
    RecordingFrameSink* recording = sink.get();
    DisplayConfig config;
    config.id = 5;
    config.width = 1280;
    config.height = 720;
    Display display(config, {}, std::move(sink));

    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    native_handle_t buffer{};
    buffer.version = sizeof(native_handle_t);
    const hwc_rect_t rects[] = {{1, 2, 300, 400}, {8, 9, 10, 11}};
    const hwc_region_t damage = {2, rects};

    Validate(&display);
    ASSERT_EQ(display.SetClientTarget(&buffer, pipeFds[0], 142, damage), HWC2_ERROR_NONE);

    int32_t presentFence = -1;
    ASSERT_EQ(display.PresentDisplay(&presentFence), HWC2_ERROR_NONE);

    EXPECT_EQ(presentFence, pipeFds[0]);
    EXPECT_NE(fcntl(presentFence, F_GETFD), -1);
    EXPECT_EQ(recording->submission_count, 1u);
    EXPECT_EQ(recording->last_display_id, 5u);
    EXPECT_EQ(recording->last_width, 1280u);
    EXPECT_EQ(recording->last_height, 720u);
    EXPECT_EQ(recording->last_buffer, &buffer);
    EXPECT_EQ(recording->last_dataspace, 142);
    ASSERT_EQ(recording->last_damage.size(), 2u);
    EXPECT_EQ(recording->last_damage[0].left, 1);
    EXPECT_EQ(recording->last_damage[0].bottom, 400);
    EXPECT_EQ(recording->last_sequence, 1u);
    EXPECT_GT(recording->last_submission_time_nanos, 0);

    close(presentFence);
    close(pipeFds[1]);

    Validate(&display);
    const hwc_region_t noDamage = {0, nullptr};
    ASSERT_EQ(display.SetClientTarget(nullptr, -1, 0, noDamage), HWC2_ERROR_NONE);
    ASSERT_EQ(display.PresentDisplay(&presentFence), HWC2_ERROR_NONE);
    EXPECT_EQ(presentFence, -1);
    EXPECT_EQ(recording->submission_count, 2u);
    EXPECT_EQ(recording->last_sequence, 2u);
}

TEST(DisplayFrameSinkTest, ReplacingClientTargetClosesPreviousAcquireFence) {
    auto sink = std::make_unique<RecordingFrameSink>();
    Display display(DisplayConfig{}, {}, std::move(sink));
    int firstPipe[2];
    int secondPipe[2];
    ASSERT_EQ(pipe(firstPipe), 0);
    ASSERT_EQ(pipe(secondPipe), 0);
    const hwc_region_t noDamage = {0, nullptr};

    ASSERT_EQ(display.SetClientTarget(nullptr, firstPipe[0], 0, noDamage), HWC2_ERROR_NONE);
    ASSERT_EQ(display.SetClientTarget(nullptr, secondPipe[0], 0, noDamage), HWC2_ERROR_NONE);

    errno = 0;
    EXPECT_EQ(fcntl(firstPipe[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_NE(fcntl(secondPipe[0], F_GETFD), -1);

    close(firstPipe[1]);
    close(secondPipe[1]);
}

TEST(DisplayFrameSinkTest, InvalidDamageClosesIncomingAcquireFence) {
    auto sink = std::make_unique<RecordingFrameSink>();
    Display display(DisplayConfig{}, {}, std::move(sink));
    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    const hwc_region_t invalidDamage = {1, nullptr};

    EXPECT_EQ(display.SetClientTarget(nullptr, pipeFds[0], 0, invalidDamage),
              HWC2_ERROR_BAD_PARAMETER);

    errno = 0;
    EXPECT_EQ(fcntl(pipeFds[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    close(pipeFds[1]);
}

}  // namespace
}  // namespace floral::display
