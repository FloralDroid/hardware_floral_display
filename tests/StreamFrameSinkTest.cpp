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

#include "floral/display/StreamFrameSink.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace floral::display {
namespace {

class FakeClientTargetResolver final : public ClientTargetResolver {
  public:
    bool Resolve(const native_handle_t* buffer, uint32_t displayWidth, uint32_t displayHeight,
                 ResolvedClientTarget* outTarget) override {
        ++resolve_count;
        if (!resolve_success || buffer == nullptr || outTarget == nullptr) {
            return false;
        }
        outTarget->identity = identity;
        outTarget->descriptor.width = displayWidth;
        outTarget->descriptor.height = displayHeight;
        outTarget->descriptor.layers = 1;
        outTarget->descriptor.format = 1;
        outTarget->descriptor.usage = 2;
        outTarget->descriptor.stride = displayWidth;
        outTarget->descriptor.protected_content = protected_content;
        return true;
    }

    bool resolve_success = true;
    bool protected_content = false;
    uint64_t identity = 41;
    uint64_t resolve_count = 0;
};

class FakeFrameConsumer final : public FrameConsumerEndpoint {
  public:
    FrameConsumerStreamState GetStreamState(DisplayId displayId) override {
        last_state_display_id = displayId;
        return state;
    }

    FrameConsumerStatus RegisterBuffer(const StreamBufferRegistration& registration) override {
        ++register_count;
        last_registration_display_id = registration.display_id;
        last_registration_generation = registration.generation;
        last_registration_buffer_id = registration.buffer_id;
        last_registration_handle = registration.buffer;
        return register_status;
    }

    StreamFrameResult SubmitFrame(StreamFrameRequest request) override {
        ++submit_count;
        submitted_buffer_ids.push_back(request.buffer_id);
        last_source_sequence = request.source_sequence;
        last_frame_submit_time_nanos = request.frame_submit_time_nanos;
        last_presentation_time_nanos = request.presentation_time_nanos;
        last_dataspace = request.dataspace;

        StreamFrameResult result;
        result.status = submit_status;
        if (result.status == FrameConsumerStatus::kAccepted) {
            result.release_fence = std::move(request.acquire_fence);
        }
        return result;
    }

    FrameConsumerStreamState state;
    FrameConsumerStatus register_status = FrameConsumerStatus::kAccepted;
    FrameConsumerStatus submit_status = FrameConsumerStatus::kAccepted;
    DisplayId last_state_display_id = 0;
    DisplayId last_registration_display_id = 0;
    uint32_t last_registration_generation = 0;
    uint64_t last_registration_buffer_id = 0;
    const native_handle_t* last_registration_handle = nullptr;
    uint64_t last_source_sequence = 0;
    int64_t last_frame_submit_time_nanos = 0;
    int64_t last_presentation_time_nanos = 0;
    int32_t last_dataspace = 0;
    uint64_t register_count = 0;
    uint64_t submit_count = 0;
    std::vector<uint64_t> submitted_buffer_ids;
};

FrameSubmission MakeSubmission(native_handle_t* buffer, int acquireFence, uint64_t sequence) {
    FrameSubmission submission;
    submission.display_id = 5;
    submission.width = 1280;
    submission.height = 720;
    submission.buffer = buffer;
    submission.acquire_fence.reset(acquireFence);
    submission.dataspace = 142;
    submission.damage = {{1, 2, 300, 400}};
    submission.sequence = sequence;
    submission.submission_time_nanos = 456789 + static_cast<int64_t>(sequence);
    return submission;
}

TEST(StreamFrameSinkTest, ReturnsAcquireFenceWhenStreamIsInactive) {
    auto consumer = std::make_shared<FakeFrameConsumer>();
    auto resolver = std::make_shared<FakeClientTargetResolver>();
    std::unique_ptr<FrameSink> sink = CreateStreamFrameSink(consumer, resolver);
    ASSERT_NE(sink, nullptr);

    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    native_handle_t buffer{};
    FrameSinkResult result = sink->Submit(MakeSubmission(&buffer, pipeFds[0], 1));

    EXPECT_EQ(result.present_fence.get(), pipeFds[0]);
    EXPECT_EQ(consumer->register_count, 0u);
    EXPECT_EQ(consumer->submit_count, 0u);
    EXPECT_EQ(resolver->resolve_count, 0u);
    const FrameSinkStats stats = sink->GetStats();
    EXPECT_EQ(stats.submitted_frames, 1u);
    EXPECT_EQ(stats.returned_present_fences, 1u);
    close(pipeFds[1]);
}

TEST(StreamFrameSinkTest, RegistersBufferOnceAndReturnsConsumerReleaseFence) {
    auto consumer = std::make_shared<FakeFrameConsumer>();
    consumer->state.generation = 7;
    consumer->state.accepting_frames = true;
    auto resolver = std::make_shared<FakeClientTargetResolver>();
    std::unique_ptr<FrameSink> sink = CreateStreamFrameSink(consumer, resolver);
    ASSERT_NE(sink, nullptr);

    native_handle_t buffer{};
    int firstPipe[2];
    int secondPipe[2];
    ASSERT_EQ(pipe(firstPipe), 0);
    ASSERT_EQ(pipe(secondPipe), 0);
    FrameSinkResult first = sink->Submit(MakeSubmission(&buffer, firstPipe[0], 11));

    EXPECT_TRUE(first.present_fence.ok());
    EXPECT_NE(first.present_fence.get(), firstPipe[0]);
    errno = 0;
    EXPECT_EQ(fcntl(firstPipe[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);

    FrameSinkResult second = sink->Submit(MakeSubmission(&buffer, secondPipe[0], 12));

    EXPECT_TRUE(second.present_fence.ok());
    EXPECT_NE(second.present_fence.get(), secondPipe[0]);
    errno = 0;
    EXPECT_EQ(fcntl(secondPipe[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(consumer->register_count, 1u);
    EXPECT_EQ(consumer->submit_count, 2u);
    ASSERT_EQ(consumer->submitted_buffer_ids.size(), 2u);
    EXPECT_EQ(consumer->submitted_buffer_ids[0], consumer->submitted_buffer_ids[1]);
    EXPECT_EQ(consumer->last_registration_generation, 7u);
    EXPECT_EQ(consumer->last_source_sequence, 12u);
    EXPECT_EQ(consumer->last_frame_submit_time_nanos, 456801);
    EXPECT_EQ(consumer->last_presentation_time_nanos, 456801);
    EXPECT_EQ(consumer->last_dataspace, 142);
    close(firstPipe[1]);
    close(secondPipe[1]);
}

TEST(StreamFrameSinkTest, ReregistersBufferAfterGenerationChange) {
    auto consumer = std::make_shared<FakeFrameConsumer>();
    consumer->state.generation = 3;
    consumer->state.accepting_frames = true;
    auto resolver = std::make_shared<FakeClientTargetResolver>();
    std::unique_ptr<FrameSink> sink = CreateStreamFrameSink(consumer, resolver);
    ASSERT_NE(sink, nullptr);

    native_handle_t buffer{};
    FrameSinkResult first = sink->Submit(MakeSubmission(&buffer, -1, 1));
    consumer->state.generation = 4;
    FrameSinkResult second = sink->Submit(MakeSubmission(&buffer, -1, 2));

    EXPECT_FALSE(first.present_fence.ok());
    EXPECT_FALSE(second.present_fence.ok());
    EXPECT_EQ(consumer->register_count, 2u);
    ASSERT_EQ(consumer->submitted_buffer_ids.size(), 2u);
    EXPECT_NE(consumer->submitted_buffer_ids[0], consumer->submitted_buffer_ids[1]);
    EXPECT_EQ(consumer->last_registration_generation, 4u);
}

TEST(StreamFrameSinkTest, ReturnsOriginalAcquireFenceWhenConsumerDropsFrame) {
    auto consumer = std::make_shared<FakeFrameConsumer>();
    consumer->state.generation = 9;
    consumer->state.accepting_frames = true;
    consumer->submit_status = FrameConsumerStatus::kDropped;
    auto resolver = std::make_shared<FakeClientTargetResolver>();
    std::unique_ptr<FrameSink> sink = CreateStreamFrameSink(consumer, resolver);
    ASSERT_NE(sink, nullptr);

    int pipeFds[2];
    ASSERT_EQ(pipe(pipeFds), 0);
    native_handle_t buffer{};
    FrameSinkResult result = sink->Submit(MakeSubmission(&buffer, pipeFds[0], 17));

    EXPECT_EQ(result.present_fence.get(), pipeFds[0]);
    EXPECT_EQ(consumer->register_count, 1u);
    EXPECT_EQ(consumer->submit_count, 1u);
    close(pipeFds[1]);
}

TEST(StreamFrameSinkTest, ReregistersBufferAfterConsumerReportsItUnknown) {
    auto consumer = std::make_shared<FakeFrameConsumer>();
    consumer->state.generation = 9;
    consumer->state.accepting_frames = true;
    consumer->submit_status = FrameConsumerStatus::kBufferUnknown;
    auto resolver = std::make_shared<FakeClientTargetResolver>();
    std::unique_ptr<FrameSink> sink = CreateStreamFrameSink(consumer, resolver);
    ASSERT_NE(sink, nullptr);

    native_handle_t buffer{};
    sink->Submit(MakeSubmission(&buffer, -1, 1));
    consumer->submit_status = FrameConsumerStatus::kAccepted;
    sink->Submit(MakeSubmission(&buffer, -1, 2));

    EXPECT_EQ(consumer->register_count, 2u);
    ASSERT_EQ(consumer->submitted_buffer_ids.size(), 2u);
    EXPECT_NE(consumer->submitted_buffer_ids[0], consumer->submitted_buffer_ids[1]);
}

TEST(StreamFrameSinkTest, DoesNotExportProtectedClientTarget) {
    auto consumer = std::make_shared<FakeFrameConsumer>();
    consumer->state.generation = 1;
    consumer->state.accepting_frames = true;
    auto resolver = std::make_shared<FakeClientTargetResolver>();
    resolver->protected_content = true;
    std::unique_ptr<FrameSink> sink = CreateStreamFrameSink(consumer, resolver);
    ASSERT_NE(sink, nullptr);

    native_handle_t buffer{};
    FrameSinkResult result = sink->Submit(MakeSubmission(&buffer, -1, 1));

    EXPECT_FALSE(result.present_fence.ok());
    EXPECT_EQ(consumer->register_count, 0u);
    EXPECT_EQ(consumer->submit_count, 0u);
}

}  // namespace
}  // namespace floral::display
