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

#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace floral::display {

class VsyncThread {
  public:
    using Callback = std::function<void(int64_t timestampNanos, int64_t periodNanos)>;
    using PeriodAppliedCallback = std::function<void(int64_t periodNanos)>;

    VsyncThread(int64_t periodNanos, Callback callback);
    ~VsyncThread();

    VsyncThread(const VsyncThread&) = delete;
    VsyncThread& operator=(const VsyncThread&) = delete;

    void SetEnabled(bool enabled);
    void SetPeriod(int64_t periodNanos);
    void SetPeriodAt(int64_t periodNanos, int64_t applyTimeNanos, PeriodAppliedCallback callback);

  private:
    void ThreadMain();

    std::mutex mutex_;
    std::condition_variable condition_;
    int64_t period_nanos_;
    Callback callback_;
    bool period_change_pending_ = false;
    int64_t pending_period_nanos_ = 0;
    int64_t pending_apply_time_nanos_ = 0;
    PeriodAppliedCallback period_applied_callback_;
    bool enabled_ = false;
    bool stopped_ = false;
    uint64_t wake_generation_ = 0;
    uint64_t period_generation_ = 0;
    std::thread thread_;
};

}  // namespace floral::display
