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

#include <log/log.h>
#include <system/graphics.h>
#include <time.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace floral::display {
namespace {

int64_t MonotonicNanos() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

bool IsKnownCompositionType(int32_t type) {
    switch (type) {
        case HWC2_COMPOSITION_CLIENT:
        case HWC2_COMPOSITION_DEVICE:
        case HWC2_COMPOSITION_SOLID_COLOR:
        case HWC2_COMPOSITION_CURSOR:
        case HWC2_COMPOSITION_SIDEBAND:
            return true;
        default:
            return false;
    }
}

}  // namespace

// Construction and mode helpers.

Display::Display(DisplayConfig config, VsyncCallback vsyncCallback,
                 std::unique_ptr<FrameSink> frameSink)
    : config_(std::move(config)),
      modes_(BuildModes(config_)),
      edid_(BuildEdid(config_.port, config_.name)),
      active_config_(FindInitialConfig(modes_, config_.vsync_period_nanos)),
      current_vsync_period_nanos_(modes_[active_config_].vsync_period_nanos),
      frame_sink_(frameSink ? std::move(frameSink) : CreatePassthroughFrameSink()),
      vsync_thread_(current_vsync_period_nanos_,
                    [id = config_.id, callback = std::move(vsyncCallback)](int64_t timestamp,
                                                                           int64_t period) {
                        if (callback) {
                            callback(id, timestamp, period);
                        }
                    }) {}

std::vector<Display::Mode> Display::BuildModes(const DisplayConfig& config) {
    std::vector<int64_t> periods;
    periods.reserve(config.supported_refresh_rates_hz.size() + 1);
    for (const uint32_t refreshRateHz : config.supported_refresh_rates_hz) {
        const int64_t periodNanos = RefreshRateToVsyncPeriodNanos(refreshRateHz);
        if (periodNanos > 0) {
            periods.push_back(periodNanos);
        }
    }
    if (config.vsync_period_nanos > 0) {
        periods.push_back(config.vsync_period_nanos);
    }
    if (periods.empty()) {
        periods.push_back(RefreshRateToVsyncPeriodNanos(60));
    }

    // Slower modes receive lower config IDs so adding a faster mode does not
    // renumber the established low-power configurations.
    std::sort(periods.begin(), periods.end(), std::greater<int64_t>());
    periods.erase(std::unique(periods.begin(), periods.end()), periods.end());

    std::vector<Mode> modes;
    modes.reserve(periods.size());
    for (size_t index = 0; index < periods.size(); ++index) {
        modes.push_back({static_cast<hwc2_config_t>(index), periods[index]});
    }
    return modes;
}

hwc2_config_t Display::FindInitialConfig(const std::vector<Mode>& modes,
                                         int64_t initialVsyncPeriodNanos) {
    const auto found =
            std::find_if(modes.begin(), modes.end(), [initialVsyncPeriodNanos](auto mode) {
                return mode.vsync_period_nanos == initialVsyncPeriodNanos;
            });
    return found == modes.end() ? modes.back().id : found->id;
}

const Display::Mode* Display::FindMode(hwc2_config_t config) const {
    if (config >= modes_.size()) {
        return nullptr;
    }
    return &modes_[config];
}

void Display::ApplyActiveConfigLocked(hwc2_config_t config, const Mode& mode) {
    active_config_ = config;
    current_vsync_period_nanos_ = mode.vsync_period_nanos;
    ++config_change_generation_;
    vsync_thread_.SetPeriod(mode.vsync_period_nanos);
}

void Display::ApplyActiveConfigLocked(hwc2_config_t config, const Mode& mode,
                                      int64_t applyTimeNanos) {
    if (current_vsync_period_nanos_ == mode.vsync_period_nanos) {
        ApplyActiveConfigLocked(config, mode);
        return;
    }

    active_config_ = config;
    const uint64_t generation = ++config_change_generation_;
    vsync_thread_.SetPeriodAt(mode.vsync_period_nanos, applyTimeNanos,
                              [this, generation](int64_t periodNanos) {
                                  std::lock_guard callbackLock(mutex_);
                                  if (generation == config_change_generation_) {
                                      current_vsync_period_nanos_ = periodNanos;
                                  }
                              });
}

FrameSubmission Display::TakeFrameSubmissionLocked() {
    FrameSubmission submission;
    submission.display_id = config_.id;
    submission.width = config_.width;
    submission.height = config_.height;
    submission.buffer = client_target_;
    submission.acquire_fence = std::move(client_target_acquire_fence_);
    submission.dataspace = client_target_dataspace_;
    submission.damage = std::move(client_target_damage_);
    submission.sequence = next_frame_sequence_++;
    submission.submission_time_nanos = MonotonicNanos();

    client_target_ = nullptr;
    client_target_acquire_fence_.reset();
    client_target_dataspace_ = 0;
    client_target_damage_.clear();
    validated_ = false;
    ++present_count_;
    return submission;
}

// Core composition and display queries.

int32_t Display::AcceptDisplayChanges() {
    std::lock_guard lock(mutex_);
    if (!validated_) {
        return HWC2_ERROR_NOT_VALIDATED;
    }
    for (const auto& [layerId, type] : type_changes_) {
        Layer* layer = FindLayerLocked(layerId);
        if (layer != nullptr) {
            layer->SetCompositionType(type);
        }
    }
    type_changes_.clear();
    return HWC2_ERROR_NONE;
}

int32_t Display::CreateLayer(hwc2_layer_t* outLayer) {
    if (outLayer == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    const hwc2_layer_t id = next_layer_id_++;
    layers_.emplace(id, std::make_unique<Layer>(id));
    *outLayer = id;
    validated_ = false;
    return HWC2_ERROR_NONE;
}

int32_t Display::DestroyLayer(hwc2_layer_t layer) {
    std::lock_guard lock(mutex_);
    if (layers_.erase(layer) == 0) {
        return HWC2_ERROR_BAD_LAYER;
    }
    type_changes_.erase(
            std::remove_if(type_changes_.begin(), type_changes_.end(),
                           [layer](const auto& change) { return change.first == layer; }),
            type_changes_.end());
    validated_ = false;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetActiveConfig(hwc2_config_t* outConfig) {
    if (outConfig == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    *outConfig = active_config_;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetChangedCompositionTypes(uint32_t* outNumElements, hwc2_layer_t* outLayers,
                                            int32_t* outTypes) {
    if (outNumElements == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    if (!validated_) {
        return HWC2_ERROR_NOT_VALIDATED;
    }
    if (outLayers == nullptr || outTypes == nullptr) {
        *outNumElements = static_cast<uint32_t>(type_changes_.size());
        return HWC2_ERROR_NONE;
    }

    const uint32_t count =
            std::min<uint32_t>(*outNumElements, static_cast<uint32_t>(type_changes_.size()));
    for (uint32_t index = 0; index < count; ++index) {
        outLayers[index] = type_changes_[index].first;
        outTypes[index] = type_changes_[index].second;
    }
    *outNumElements = count;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetColorModes(uint32_t* outNumModes, int32_t* outModes) {
    if (outNumModes == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (outModes == nullptr) {
        *outNumModes = 1;
        return HWC2_ERROR_NONE;
    }
    if (*outNumModes > 0) {
        outModes[0] = HAL_COLOR_MODE_NATIVE;
        *outNumModes = 1;
    } else {
        *outNumModes = 0;
    }
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayAttribute(hwc2_config_t config, int32_t attribute, int32_t* outValue) {
    if (outValue == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    const Mode* mode = FindMode(config);
    if (mode == nullptr) {
        return HWC2_ERROR_BAD_CONFIG;
    }

    switch (attribute) {
        case HWC2_ATTRIBUTE_WIDTH:
            *outValue = static_cast<int32_t>(config_.width);
            break;
        case HWC2_ATTRIBUTE_HEIGHT:
            *outValue = static_cast<int32_t>(config_.height);
            break;
        case HWC2_ATTRIBUTE_VSYNC_PERIOD:
            *outValue = static_cast<int32_t>(mode->vsync_period_nanos);
            break;
        case HWC2_ATTRIBUTE_DPI_X:
        case HWC2_ATTRIBUTE_DPI_Y:
            *outValue = static_cast<int32_t>(config_.dpi * 1000u);
            break;
        case HWC2_ATTRIBUTE_CONFIG_GROUP:
            *outValue = 0;
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayConfigs(uint32_t* outNumConfigs, hwc2_config_t* outConfigs) {
    if (outNumConfigs == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (outConfigs == nullptr) {
        *outNumConfigs = static_cast<uint32_t>(modes_.size());
        return HWC2_ERROR_NONE;
    }
    const uint32_t count = std::min<uint32_t>(*outNumConfigs, static_cast<uint32_t>(modes_.size()));
    for (uint32_t index = 0; index < count; ++index) {
        outConfigs[index] = modes_[index].id;
    }
    *outNumConfigs = count;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayName(uint32_t* outSize, char* outName) {
    if (outSize == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (outName == nullptr) {
        *outSize = static_cast<uint32_t>(config_.name.size());
        return HWC2_ERROR_NONE;
    }
    const uint32_t count = std::min<uint32_t>(*outSize, static_cast<uint32_t>(config_.name.size()));
    std::memcpy(outName, config_.name.data(), count);
    *outSize = count;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayRequests(int32_t* outDisplayRequests, uint32_t* outNumElements,
                                    hwc2_layer_t* outLayers, int32_t* outLayerRequests) {
    if (outDisplayRequests == nullptr || outNumElements == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    if (!validated_) {
        return HWC2_ERROR_NOT_VALIDATED;
    }
    (void)outLayers;
    (void)outLayerRequests;
    *outDisplayRequests = 0;
    *outNumElements = 0;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayType(int32_t* outType) {
    if (outType == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    *outType = HWC2_DISPLAY_TYPE_PHYSICAL;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDozeSupport(int32_t* outSupport) {
    if (outSupport == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    *outSupport = 0;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetHdrCapabilities(uint32_t* outNumTypes, int32_t* outTypes,
                                    float* outMaxLuminance, float* outMaxAverageLuminance,
                                    float* outMinLuminance) {
    if (outNumTypes == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    (void)outTypes;
    if (outMaxLuminance != nullptr) {
        *outMaxLuminance = 0.0f;
    }
    if (outMaxAverageLuminance != nullptr) {
        *outMaxAverageLuminance = 0.0f;
    }
    if (outMinLuminance != nullptr) {
        *outMinLuminance = 0.0f;
    }
    *outNumTypes = 0;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetReleaseFences(uint32_t* outNumElements, hwc2_layer_t* outLayers,
                                  int32_t* outFences) {
    if (outNumElements == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    (void)outLayers;
    (void)outFences;
    *outNumElements = 0;
    return HWC2_ERROR_NONE;
}

// Presentation and display controls.

int32_t Display::PresentDisplay(int32_t* outPresentFence) {
    if (outPresentFence == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    FrameSubmission submission;
    {
        std::lock_guard lock(mutex_);
        if (!validated_ || !type_changes_.empty()) {
            return HWC2_ERROR_NOT_VALIDATED;
        }

        submission = TakeFrameSubmissionLocked();
    }

    FrameSinkResult result = frame_sink_->Submit(std::move(submission));
    *outPresentFence = result.present_fence.release();
    return HWC2_ERROR_NONE;
}

int32_t Display::SetActiveConfig(hwc2_config_t config) {
    const Mode* mode = FindMode(config);
    if (mode == nullptr) {
        return HWC2_ERROR_BAD_CONFIG;
    }
    std::lock_guard lock(mutex_);
    ApplyActiveConfigLocked(config, *mode);
    return HWC2_ERROR_NONE;
}

int32_t Display::SetClientTarget(buffer_handle_t target, int32_t acquireFence, int32_t dataspace,
                                 hwc_region_t damage) {
    android::base::unique_fd ownedFence(acquireFence);
    if (damage.numRects > 0 && damage.rects == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    std::vector<DamageRect> copiedDamage;
    copiedDamage.reserve(damage.numRects);
    for (size_t index = 0; index < damage.numRects; ++index) {
        const hwc_rect_t& rect = damage.rects[index];
        copiedDamage.push_back({rect.left, rect.top, rect.right, rect.bottom});
    }

    std::lock_guard lock(mutex_);
    client_target_ = target;
    client_target_acquire_fence_ = std::move(ownedFence);
    client_target_dataspace_ = dataspace;
    client_target_damage_ = std::move(copiedDamage);
    return HWC2_ERROR_NONE;
}

int32_t Display::SetColorMode(int32_t mode) {
    return mode == HAL_COLOR_MODE_NATIVE ? HWC2_ERROR_NONE : HWC2_ERROR_UNSUPPORTED;
}

int32_t Display::SetColorTransform(const float* matrix, int32_t hint) {
    (void)hint;
    return matrix != nullptr ? HWC2_ERROR_NONE : HWC2_ERROR_BAD_PARAMETER;
}

int32_t Display::SetOutputBuffer(buffer_handle_t buffer, int32_t releaseFence) {
    (void)buffer;
    android::base::unique_fd ownedFence(releaseFence);
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t Display::SetPowerMode(int32_t mode) {
    if (mode != HWC2_POWER_MODE_OFF && mode != HWC2_POWER_MODE_ON) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    {
        std::lock_guard lock(mutex_);
        power_mode_ = mode;
    }
    if (mode == HWC2_POWER_MODE_OFF) {
        vsync_thread_.SetEnabled(false);
    }
    return HWC2_ERROR_NONE;
}

int32_t Display::SetVsyncEnabled(int32_t enabled) {
    if (enabled != HWC2_VSYNC_ENABLE && enabled != HWC2_VSYNC_DISABLE) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    vsync_thread_.SetEnabled(enabled == HWC2_VSYNC_ENABLE);
    return HWC2_ERROR_NONE;
}

int32_t Display::ValidateDisplay(uint32_t* outNumTypes, uint32_t* outNumRequests) {
    if (outNumTypes == nullptr || outNumRequests == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    type_changes_.clear();
    for (const auto& [id, layer] : layers_) {
        // Device composition is never selected in the first vertical slice,
        // so HWC must not retain per-layer buffers or acquire fences.
        layer->ReleaseBuffer();
        if (layer->composition_type() != HWC2_COMPOSITION_CLIENT) {
            type_changes_.emplace_back(id, HWC2_COMPOSITION_CLIENT);
        }
    }
    validated_ = true;
    ++validate_count_;
    *outNumTypes = static_cast<uint32_t>(type_changes_.size());
    *outNumRequests = 0;
    return type_changes_.empty() ? HWC2_ERROR_NONE : HWC2_ERROR_HAS_CHANGES;
}

// Composer 2.4 capabilities and constrained mode switching.

int32_t Display::GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                        int32_t dataspace) {
    (void)dataspace;
    if (width != config_.width || height != config_.height) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    if (format != HAL_PIXEL_FORMAT_RGBA_8888 && format != HAL_PIXEL_FORMAT_RGBX_8888) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayIdentificationData(uint8_t* outPort, uint32_t* outDataSize,
                                              uint8_t* outData) {
    if (outPort == nullptr || outDataSize == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    *outPort = config_.port;
    if (outData == nullptr) {
        *outDataSize = static_cast<uint32_t>(edid_.size());
        return HWC2_ERROR_NONE;
    }
    const uint32_t count = std::min<uint32_t>(*outDataSize, edid_.size());
    std::copy_n(edid_.begin(), count, outData);
    *outDataSize = count;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayCapabilities(uint32_t* outNumCapabilities, uint32_t* outCapabilities) {
    if (outNumCapabilities == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    (void)outCapabilities;
    // In particular, do not advertise protected-content or readback support.
    *outNumCapabilities = 0;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayBrightnessSupport(bool* outSupport) {
    if (outSupport == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    *outSupport = false;
    return HWC2_ERROR_NONE;
}

int32_t Display::SetDisplayBrightness(float brightness) {
    (void)brightness;
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t Display::GetDisplayConnectionType(uint32_t* outType) {
    if (outType == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    *outType = config_.connection_type == ConnectionType::kInternal
                       ? HWC2_DISPLAY_CONNECTION_TYPE_INTERNAL
                       : HWC2_DISPLAY_CONNECTION_TYPE_EXTERNAL;
    return HWC2_ERROR_NONE;
}

int32_t Display::GetDisplayVsyncPeriod(hwc2_vsync_period_t* outVsyncPeriod) {
    if (outVsyncPeriod == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    *outVsyncPeriod = current_vsync_period_nanos_;
    return HWC2_ERROR_NONE;
}

int32_t Display::SetActiveConfigWithConstraints(hwc2_config_t config,
                                                hwc_vsync_period_change_constraints_t* constraints,
                                                hwc_vsync_period_change_timeline_t* outTimeline) {
    if (constraints == nullptr || outTimeline == nullptr) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    const Mode* mode = FindMode(config);
    if (mode == nullptr) {
        return HWC2_ERROR_BAD_CONFIG;
    }

    const int64_t nowNanos = MonotonicNanos();
    const int64_t applyTimeNanos = std::max<int64_t>(nowNanos, constraints->desiredTimeNanos);
    {
        std::lock_guard lock(mutex_);
        ApplyActiveConfigLocked(config, *mode, applyTimeNanos);
    }

    outTimeline->newVsyncAppliedTimeNanos = applyTimeNanos;
    outTimeline->refreshRequired = false;
    outTimeline->refreshTimeNanos = 0;
    return HWC2_ERROR_NONE;
}

// Layer state updates.

int32_t Display::SetCursorPosition(hwc2_layer_t layer, int32_t x, int32_t y) {
    (void)x;
    (void)y;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerBuffer(hwc2_layer_t layer, buffer_handle_t buffer, int32_t acquireFence) {
    std::lock_guard lock(mutex_);
    Layer* target = FindLayerLocked(layer);
    if (target == nullptr) {
        android::base::unique_fd ownedFence(acquireFence);
        return HWC2_ERROR_BAD_LAYER;
    }
    target->SetBuffer(buffer, acquireFence);
    return HWC2_ERROR_NONE;
}

int32_t Display::SetLayerSurfaceDamage(hwc2_layer_t layer, hwc_region_t damage) {
    (void)damage;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerBlendMode(hwc2_layer_t layer, int32_t mode) {
    (void)mode;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerColor(hwc2_layer_t layer, hwc_color_t color) {
    (void)color;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerCompositionType(hwc2_layer_t layer, int32_t type) {
    if (!IsKnownCompositionType(type)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    Layer* target = FindLayerLocked(layer);
    if (target == nullptr) {
        return HWC2_ERROR_BAD_LAYER;
    }
    target->SetCompositionType(type);
    validated_ = false;
    return HWC2_ERROR_NONE;
}

int32_t Display::SetLayerDataspace(hwc2_layer_t layer, int32_t dataspace) {
    (void)dataspace;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerDisplayFrame(hwc2_layer_t layer, hwc_rect_t frame) {
    (void)frame;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerPlaneAlpha(hwc2_layer_t layer, float alpha) {
    if (alpha < 0.0f || alpha > 1.0f) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerSidebandStream(hwc2_layer_t layer, const native_handle_t* stream) {
    (void)stream;
    std::lock_guard lock(mutex_);
    return FindLayerLocked(layer) == nullptr ? HWC2_ERROR_BAD_LAYER : HWC2_ERROR_UNSUPPORTED;
}

int32_t Display::SetLayerSourceCrop(hwc2_layer_t layer, hwc_frect_t crop) {
    (void)crop;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerTransform(hwc2_layer_t layer, int32_t transform) {
    (void)transform;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerVisibleRegion(hwc2_layer_t layer, hwc_region_t visible) {
    (void)visible;
    std::lock_guard lock(mutex_);
    return RequireLayerLocked(layer);
}

int32_t Display::SetLayerZOrder(hwc2_layer_t layer, uint32_t z) {
    std::lock_guard lock(mutex_);
    Layer* target = FindLayerLocked(layer);
    if (target == nullptr) {
        return HWC2_ERROR_BAD_LAYER;
    }
    target->SetZOrder(z);
    return HWC2_ERROR_NONE;
}

// Diagnostics and private helpers.

std::string Display::Dump() const {
    std::lock_guard lock(mutex_);
    const FrameSinkStats sinkStats = frame_sink_->GetStats();
    std::ostringstream output;
    const char* connection =
            config_.connection_type == ConnectionType::kInternal ? "INTERNAL" : "EXTERNAL";
    output << "Display " << config_.id << " (port " << static_cast<uint32_t>(config_.port) << ")\n"
           << "  name: " << config_.name << "\n"
           << "  connection: " << connection << "\n"
           << "  active_config: " << active_config_ << "\n"
           << "  mode: " << config_.width << 'x' << config_.height << " @ "
           << (1'000'000'000.0 / current_vsync_period_nanos_) << " Hz\n"
           << "  supported_refresh_rates:";
    for (const Mode& mode : modes_) {
        output << ' ' << (1'000'000'000.0 / mode.vsync_period_nanos);
    }
    output << " Hz\n"
           << "  layers: " << layers_.size() << "\n"
           << "  validate_count: " << validate_count_ << "\n"
           << "  present_count: " << present_count_ << "\n"
           << "  frame_sink: " << frame_sink_->Name() << "\n"
           << "  submitted_frames: " << sinkStats.submitted_frames << "\n"
           << "  frames_with_buffer: " << sinkStats.frames_with_buffer << "\n"
           << "  frames_with_acquire_fence: " << sinkStats.frames_with_acquire_fence << "\n"
           << "  returned_present_fences: " << sinkStats.returned_present_fences << "\n"
           << "  resolver_failures: " << sinkStats.resolver_failures << "\n"
           << "  register_failures: " << sinkStats.register_failures << "\n"
           << "  submit_failures: " << sinkStats.submit_failures << "\n"
           << "  last_frame_sequence: " << sinkStats.last_sequence << "\n"
           << "  last_submission_time_nanos: " << sinkStats.last_submission_time_nanos << "\n"
           << "  last_dataspace: " << sinkStats.last_dataspace << "\n"
           << "  last_damage_rect_count: " << sinkStats.last_damage_rect_count << "\n"
           << "  raw_frame_export: disabled\n"
           << "  protected_content: unsupported\n";
    return output.str();
}

Layer* Display::FindLayerLocked(hwc2_layer_t layer) {
    const auto found = layers_.find(layer);
    return found == layers_.end() ? nullptr : found->second.get();
}

int32_t Display::RequireLayerLocked(hwc2_layer_t layer) {
    return FindLayerLocked(layer) == nullptr ? HWC2_ERROR_BAD_LAYER : HWC2_ERROR_NONE;
}

}  // namespace floral::display
