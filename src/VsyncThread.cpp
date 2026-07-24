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

#include <algorithm>
#include <chrono>
#include <optional>
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
        ++wake_generation_;
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
        ++wake_generation_;
        ++period_generation_;
    }
    condition_.notify_all();
}

void VsyncThread::SetPeriod(int64_t periodNanos) {
    {
        std::lock_guard lock(mutex_);
        if (period_nanos_ == periodNanos && !period_change_pending_) {
            return;
        }
        period_nanos_ = periodNanos;
        period_change_pending_ = false;
        period_applied_callback_ = {};
        ++wake_generation_;
        ++period_generation_;
    }
    condition_.notify_all();
}

void VsyncThread::SetPeriodAt(int64_t periodNanos, int64_t applyTimeNanos,
                              PeriodAppliedCallback callback) {
    {
        std::lock_guard lock(mutex_);
        period_change_pending_ = true;
        pending_period_nanos_ = periodNanos;
        pending_apply_time_nanos_ = applyTimeNanos;
        period_applied_callback_ = std::move(callback);
        ++wake_generation_;
    }
    condition_.notify_all();
}

void VsyncThread::ThreadMain() {
    pthread_setname_np(pthread_self(), "floral-vsync");

    std::unique_lock lock(mutex_);
    uint64_t observedPeriodGeneration = period_generation_;
    std::optional<std::chrono::steady_clock::time_point> nextVsync;
    while (!stopped_) {
        if (observedPeriodGeneration != period_generation_) {
            nextVsync.reset();
            observedPeriodGeneration = period_generation_;
        }

        const int64_t nowNanos = MonotonicNanos();
        if (period_change_pending_ && nowNanos >= pending_apply_time_nanos_) {
            period_nanos_ = pending_period_nanos_;
            period_change_pending_ = false;
            PeriodAppliedCallback appliedCallback = std::move(period_applied_callback_);
            period_applied_callback_ = {};
            ++period_generation_;
            ++wake_generation_;

            const int64_t appliedPeriod = period_nanos_;
            lock.unlock();
            if (appliedCallback) {
                appliedCallback(appliedPeriod);
            }
            lock.lock();
            continue;
        }

        const auto steadyNow = std::chrono::steady_clock::now();
        if (!enabled_) {
            nextVsync.reset();
        } else if (!nextVsync.has_value()) {
            nextVsync = steadyNow + std::chrono::nanoseconds(period_nanos_);
        }

        std::optional<std::chrono::steady_clock::time_point> wakeTime = nextVsync;
        if (period_change_pending_) {
            const int64_t delayNanos = std::max<int64_t>(0, pending_apply_time_nanos_ - nowNanos);
            const auto periodChangeTime = steadyNow + std::chrono::nanoseconds(delayNanos);
            if (!wakeTime.has_value() || periodChangeTime < *wakeTime) {
                wakeTime = periodChangeTime;
            }
        }

        const uint64_t observedWakeGeneration = wake_generation_;
        if (!wakeTime.has_value()) {
            condition_.wait(lock, [this, observedWakeGeneration] {
                return stopped_ || wake_generation_ != observedWakeGeneration;
            });
            continue;
        }

        const bool interrupted =
                condition_.wait_until(lock, *wakeTime, [this, observedWakeGeneration] {
                    return stopped_ || wake_generation_ != observedWakeGeneration;
                });
        if (interrupted || stopped_) {
            continue;
        }

        if (period_change_pending_ && MonotonicNanos() >= pending_apply_time_nanos_) {
            continue;
        }

        if (nextVsync.has_value() && std::chrono::steady_clock::now() >= *nextVsync) {
            const int64_t periodNanos = period_nanos_;
            const Callback callback = callback_;
            nextVsync = *nextVsync + std::chrono::nanoseconds(periodNanos);

            lock.unlock();
            if (callback) {
                callback(MonotonicNanos(), periodNanos);
            }
            lock.lock();

            // Resume from the current monotonic time after a long scheduler
            // stall instead of emitting a burst of stale VSync callbacks.
            const auto now = std::chrono::steady_clock::now();
            if (*nextVsync + std::chrono::nanoseconds(periodNanos) < now) {
                nextVsync = now + std::chrono::nanoseconds(periodNanos);
            }
        }
    }
}

}  // namespace floral::display
