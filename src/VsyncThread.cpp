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

#include "floral/display/VsyncThread.h"

#include <pthread.h>
#include <time.h>

#include <chrono>
#include <utility>

namespace floral::display {
namespace {

int64_t MonotonicNanos() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

}  // namespace

VsyncThread::VsyncThread(int64_t periodNanos, Callback callback)
    : period_nanos_(periodNanos),
      callback_(std::move(callback)),
      thread_(&VsyncThread::ThreadMain, this) {}

VsyncThread::~VsyncThread() {
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        ++generation_;
    }
    condition_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void VsyncThread::SetEnabled(bool enabled) {
    {
        std::lock_guard lock(mutex_);
        if (enabled_ == enabled) {
            return;
        }
        enabled_ = enabled;
        ++generation_;
    }
    condition_.notify_all();
}

void VsyncThread::SetPeriod(int64_t periodNanos) {
    {
        std::lock_guard lock(mutex_);
        if (period_nanos_ == periodNanos) {
            return;
        }
        period_nanos_ = periodNanos;
        ++generation_;
    }
    condition_.notify_all();
}

void VsyncThread::ThreadMain() {
    pthread_setname_np(pthread_self(), "floral-vsync");

    std::unique_lock lock(mutex_);
    while (!stopped_) {
        condition_.wait(lock, [this] { return stopped_ || enabled_; });
        if (stopped_) {
            break;
        }

        uint64_t observedGeneration = generation_;
        auto nextDeadline = std::chrono::steady_clock::now();
        while (!stopped_ && enabled_ && observedGeneration == generation_) {
            const int64_t periodNanos = period_nanos_;
            nextDeadline += std::chrono::nanoseconds(periodNanos);

            const bool interrupted =
                    condition_.wait_until(lock, nextDeadline, [this, observedGeneration] {
                        return stopped_ || !enabled_ || generation_ != observedGeneration;
                    });
            if (interrupted) {
                break;
            }

            const Callback callback = callback_;
            lock.unlock();
            if (callback) {
                callback(MonotonicNanos(), periodNanos);
            }
            lock.lock();

            // Resume from the current monotonic time after a long scheduler
            // stall instead of emitting a burst of stale VSync callbacks.
            const auto now = std::chrono::steady_clock::now();
            if (nextDeadline + std::chrono::nanoseconds(periodNanos) < now) {
                nextDeadline = now;
            }
        }
    }
}

}  // namespace floral::display
