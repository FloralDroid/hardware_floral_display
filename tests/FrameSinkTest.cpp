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

#include "floral/display/FrameSink.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

namespace floral::display {
namespace {

TEST(FrameSinkTest, PassthroughTransfersAcquireFenceAndRecordsMetadata) {
    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);

    native_handle_t buffer{};
    buffer.version = sizeof(native_handle_t);

    FrameSubmission submission;
    submission.display_id = 7;
    submission.width = 1920;
    submission.height = 1080;
    submission.buffer = &buffer;
    submission.acquire_fence.reset(pipeFds[0]);
    submission.dataspace = 142;
    submission.damage = {{1, 2, 300, 400}, {8, 9, 10, 11}};
    submission.sequence = 23;
    submission.submission_time_nanos = 456789;

    std::unique_ptr<FrameSink> sink = CreatePassthroughFrameSink();
    FrameSinkResult result = sink->Submit(std::move(submission));

    EXPECT_EQ(result.present_fence.get(), pipeFds[0]);
    EXPECT_NE(fcntl(result.present_fence.get(), F_GETFD), -1);

    const FrameSinkStats stats = sink->GetStats();
    EXPECT_EQ(stats.submitted_frames, 1u);
    EXPECT_EQ(stats.frames_with_buffer, 1u);
    EXPECT_EQ(stats.frames_with_acquire_fence, 1u);
    EXPECT_EQ(stats.returned_present_fences, 1u);
    EXPECT_EQ(stats.last_sequence, 23u);
    EXPECT_EQ(stats.last_submission_time_nanos, 456789);
    EXPECT_EQ(stats.last_dataspace, 142);
    EXPECT_EQ(stats.last_damage_rect_count, 2u);

    close(pipeFds[1]);
}

TEST(FrameSinkTest, PassthroughReturnsNoFenceWhenSubmissionHasNone) {
    std::unique_ptr<FrameSink> sink = CreatePassthroughFrameSink();
    FrameSubmission submission;
    submission.sequence = 1;

    FrameSinkResult result = sink->Submit(std::move(submission));

    EXPECT_FALSE(result.present_fence.ok());
    const FrameSinkStats stats = sink->GetStats();
    EXPECT_EQ(stats.submitted_frames, 1u);
    EXPECT_EQ(stats.frames_with_acquire_fence, 0u);
    EXPECT_EQ(stats.returned_present_fences, 0u);
}

}  // namespace
}  // namespace floral::display
