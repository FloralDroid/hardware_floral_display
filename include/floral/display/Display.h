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

#include <android-base/unique_fd.h>
#include <hardware/hwcomposer2.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "floral/display/DisplayConfig.h"
#include "floral/display/Edid.h"
#include "floral/display/FrameSink.h"
#include "floral/display/Layer.h"
#include "floral/display/VsyncThread.h"

namespace floral::display {

class Display {
  public:
    using VsyncCallback = std::function<void(hwc2_display_t, int64_t, int64_t)>;

    Display(DisplayConfig config, VsyncCallback vsyncCallback,
            std::unique_ptr<FrameSink> frameSink);
    ~Display() = default;

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    hwc2_display_t id() const { return static_cast<hwc2_display_t>(config_.id); }
    uint8_t port() const { return config_.port; }

    // Core composition and display queries.
    int32_t AcceptDisplayChanges();
    int32_t CreateLayer(hwc2_layer_t* outLayer);
    int32_t DestroyLayer(hwc2_layer_t layer);
    int32_t GetActiveConfig(hwc2_config_t* outConfig);
    int32_t GetChangedCompositionTypes(uint32_t* outNumElements, hwc2_layer_t* outLayers,
                                       int32_t* outTypes);
    int32_t GetColorModes(uint32_t* outNumModes, int32_t* outModes);
    int32_t GetDisplayAttribute(hwc2_config_t config, int32_t attribute, int32_t* outValue);
    int32_t GetDisplayConfigs(uint32_t* outNumConfigs, hwc2_config_t* outConfigs);
    int32_t GetDisplayName(uint32_t* outSize, char* outName);
    int32_t GetDisplayRequests(int32_t* outDisplayRequests, uint32_t* outNumElements,
                               hwc2_layer_t* outLayers, int32_t* outLayerRequests);
    int32_t GetDisplayType(int32_t* outType);
    int32_t GetDozeSupport(int32_t* outSupport);
    int32_t GetHdrCapabilities(uint32_t* outNumTypes, int32_t* outTypes, float* outMaxLuminance,
                               float* outMaxAverageLuminance, float* outMinLuminance);
    int32_t GetReleaseFences(uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outFences);

    // Presentation and display controls.
    int32_t PresentDisplay(int32_t* outPresentFence);
    int32_t SetActiveConfig(hwc2_config_t config);
    int32_t SetClientTarget(buffer_handle_t target, int32_t acquireFence, int32_t dataspace,
                            hwc_region_t damage);
    int32_t SetColorMode(int32_t mode);
    int32_t SetColorTransform(const float* matrix, int32_t hint);
    int32_t SetOutputBuffer(buffer_handle_t buffer, int32_t releaseFence);
    int32_t SetPowerMode(int32_t mode);
    int32_t SetVsyncEnabled(int32_t enabled);
    int32_t ValidateDisplay(uint32_t* outNumTypes, uint32_t* outNumRequests);

    // Composer 2.4 capabilities and constrained mode switching.
    int32_t GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                   int32_t dataspace);
    int32_t GetDisplayIdentificationData(uint8_t* outPort, uint32_t* outDataSize, uint8_t* outData);
    int32_t GetDisplayCapabilities(uint32_t* outNumCapabilities, uint32_t* outCapabilities);
    int32_t GetDisplayBrightnessSupport(bool* outSupport);
    int32_t SetDisplayBrightness(float brightness);
    int32_t GetDisplayConnectionType(uint32_t* outType);
    int32_t GetDisplayVsyncPeriod(hwc2_vsync_period_t* outVsyncPeriod);
    int32_t SetActiveConfigWithConstraints(hwc2_config_t config,
                                           hwc_vsync_period_change_constraints_t* constraints,
                                           hwc_vsync_period_change_timeline_t* outTimeline);

    // Layer state updates.
    int32_t SetCursorPosition(hwc2_layer_t layer, int32_t x, int32_t y);
    int32_t SetLayerBuffer(hwc2_layer_t layer, buffer_handle_t buffer, int32_t acquireFence);
    int32_t SetLayerSurfaceDamage(hwc2_layer_t layer, hwc_region_t damage);
    int32_t SetLayerBlendMode(hwc2_layer_t layer, int32_t mode);
    int32_t SetLayerColor(hwc2_layer_t layer, hwc_color_t color);
    int32_t SetLayerCompositionType(hwc2_layer_t layer, int32_t type);
    int32_t SetLayerDataspace(hwc2_layer_t layer, int32_t dataspace);
    int32_t SetLayerDisplayFrame(hwc2_layer_t layer, hwc_rect_t frame);
    int32_t SetLayerPlaneAlpha(hwc2_layer_t layer, float alpha);
    int32_t SetLayerSidebandStream(hwc2_layer_t layer, const native_handle_t* stream);
    int32_t SetLayerSourceCrop(hwc2_layer_t layer, hwc_frect_t crop);
    int32_t SetLayerTransform(hwc2_layer_t layer, int32_t transform);
    int32_t SetLayerVisibleRegion(hwc2_layer_t layer, hwc_region_t visible);
    int32_t SetLayerZOrder(hwc2_layer_t layer, uint32_t z);

    // Diagnostics.
    std::string Dump() const;

  private:
    struct Mode {
        hwc2_config_t id;
        int64_t vsync_period_nanos;
    };

    static std::vector<Mode> BuildModes(const DisplayConfig& config);
    static hwc2_config_t FindInitialConfig(const std::vector<Mode>& modes,
                                           int64_t initialVsyncPeriodNanos);
    const Mode* FindMode(hwc2_config_t config) const;
    void ApplyActiveConfigLocked(hwc2_config_t config, const Mode& mode);
    void ApplyActiveConfigLocked(hwc2_config_t config, const Mode& mode, int64_t applyTimeNanos);

    FrameSubmission TakeFrameSubmissionLocked();

    Layer* FindLayerLocked(hwc2_layer_t layer);
    int32_t RequireLayerLocked(hwc2_layer_t layer);

    // Immutable display description and advertised modes.
    const DisplayConfig config_;
    const std::vector<Mode> modes_;
    const EdidBlock edid_;

    // Mutable display state synchronization.
    mutable std::mutex mutex_;

    // Display mode and power state.
    hwc2_config_t active_config_ = 0;
    int64_t current_vsync_period_nanos_ = 0;
    uint64_t config_change_generation_ = 0;
    int32_t power_mode_ = HWC2_POWER_MODE_ON;

    // Layer registry and composition state.
    std::unordered_map<hwc2_layer_t, std::unique_ptr<Layer>> layers_;
    std::vector<std::pair<hwc2_layer_t, int32_t>> type_changes_;
    hwc2_layer_t next_layer_id_ = 1;
    bool validated_ = false;

    // Client target submitted by SurfaceFlinger.
    buffer_handle_t client_target_ = nullptr;
    android::base::unique_fd client_target_acquire_fence_;
    int32_t client_target_dataspace_ = 0;
    std::vector<DamageRect> client_target_damage_;

    // Frame delivery pipeline and presentation statistics.
    uint64_t next_frame_sequence_ = 1;
    uint64_t validate_count_ = 0;
    uint64_t present_count_ = 0;
    std::unique_ptr<FrameSink> frame_sink_;

    // Vsync scheduling worker.
    VsyncThread vsync_thread_;
};

}  // namespace floral::display
