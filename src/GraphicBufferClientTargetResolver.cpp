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

#include "floral/display/GraphicBufferClientTargetResolver.h"

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <ui/GraphicBufferMapper.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "floral/display/GrallocMetadata.h"

namespace floral::display {
namespace {

static_assert(sizeof(floral_gralloc_buffer_metadata_v1_t) == FLORAL_GRALLOC_BUFFER_METADATA_V1_SIZE,
              "Floral gralloc metadata ABI layout changed");

bool ToPositiveUint32(uint64_t value, uint32_t* output) {
    if (output == nullptr || value == 0 || value > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    *output = static_cast<uint32_t>(value);
    return true;
}

bool ResolveStride(android::GraphicBufferMapper& mapper, buffer_handle_t buffer,
                   uint32_t* outStride) {
    std::vector<android::ui::PlaneLayout> planeLayouts;
    if (mapper.getPlaneLayouts(buffer, &planeLayouts) != android::NO_ERROR ||
        planeLayouts.empty()) {
        return false;
    }

    const int64_t strideBytes = planeLayouts.front().strideInBytes;
    const int64_t sampleBits = planeLayouts.front().sampleIncrementInBits;
    if (strideBytes <= 0 || sampleBits <= 0 ||
        strideBytes > std::numeric_limits<int64_t>::max() / 8) {
        return false;
    }
    const int64_t strideBits = strideBytes * 8;
    if (strideBits % sampleBits != 0) {
        return false;
    }
    return ToPositiveUint32(static_cast<uint64_t>(strideBits / sampleBits), outStride);
}

bool ResolveFloralMetadata(const gralloc_module_t* grallocModule, buffer_handle_t buffer,
                           uint32_t displayWidth, uint32_t displayHeight,
                           ResolvedClientTarget* outTarget) {
    if (grallocModule == nullptr || grallocModule->perform == nullptr) {
        return false;
    }

    floral_gralloc_buffer_metadata_v1_t metadata{};
    metadata.struct_size = sizeof(metadata);
    metadata.version = FLORAL_GRALLOC_BUFFER_METADATA_VERSION_1;
    if (grallocModule->perform(grallocModule, FLORAL_GRALLOC_MODULE_PERFORM_GET_BUFFER_METADATA,
                               buffer, &metadata) != 0 ||
        metadata.struct_size != sizeof(metadata) ||
        metadata.version != FLORAL_GRALLOC_BUFFER_METADATA_VERSION_1 || metadata.width == 0 ||
        metadata.height == 0 || metadata.layers == 0 || metadata.format == 0 ||
        metadata.stride == 0 || metadata.buffer_id == 0 || metadata.width != displayWidth ||
        metadata.height != displayHeight) {
        return false;
    }

    outTarget->identity = metadata.buffer_id;
    outTarget->descriptor.width = metadata.width;
    outTarget->descriptor.height = metadata.height;
    outTarget->descriptor.layers = metadata.layers;
    outTarget->descriptor.format = metadata.format;
    outTarget->descriptor.usage = metadata.usage;
    outTarget->descriptor.stride = metadata.stride;
    outTarget->descriptor.protected_content =
            (metadata.usage & static_cast<uint64_t>(GRALLOC_USAGE_PROTECTED)) != 0;
    return true;
}

bool ResolveStandardMetadata(buffer_handle_t buffer, uint32_t displayWidth, uint32_t displayHeight,
                             ResolvedClientTarget* outTarget) {
    android::GraphicBufferMapper& mapper = android::GraphicBufferMapper::get();
    uint64_t identity = 0;
    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t layers = 0;
    uint64_t usage = 0;
    android::ui::PixelFormat format = static_cast<android::ui::PixelFormat>(0);
    if (mapper.getBufferId(buffer, &identity) != android::NO_ERROR ||
        mapper.getWidth(buffer, &width) != android::NO_ERROR ||
        mapper.getHeight(buffer, &height) != android::NO_ERROR ||
        mapper.getLayerCount(buffer, &layers) != android::NO_ERROR ||
        mapper.getPixelFormatRequested(buffer, &format) != android::NO_ERROR ||
        mapper.getUsage(buffer, &usage) != android::NO_ERROR) {
        return false;
    }

    ClientTargetDescriptor descriptor;
    if (!ToPositiveUint32(width, &descriptor.width) ||
        !ToPositiveUint32(height, &descriptor.height) ||
        !ToPositiveUint32(layers, &descriptor.layers) ||
        !ResolveStride(mapper, buffer, &descriptor.stride)) {
        return false;
    }
    if (descriptor.width != displayWidth || descriptor.height != displayHeight ||
        static_cast<int32_t>(format) == 0 || identity == 0) {
        return false;
    }

    uint64_t protectedContent = 0;
    const bool hasProtectedMetadata =
            mapper.getProtectedContent(buffer, &protectedContent) == android::NO_ERROR;
    descriptor.format = static_cast<int32_t>(format);
    descriptor.usage = usage;
    descriptor.protected_content = (hasProtectedMetadata && protectedContent != 0) ||
                                   (usage & static_cast<uint64_t>(GRALLOC_USAGE_PROTECTED)) != 0;

    outTarget->identity = identity;
    outTarget->descriptor = descriptor;
    return true;
}

class GraphicBufferClientTargetResolver final : public ClientTargetResolver {
  public:
    explicit GraphicBufferClientTargetResolver(const gralloc_module_t* grallocModule)
        : gralloc_module_(grallocModule) {}

    bool Resolve(const native_handle_t* buffer, uint32_t displayWidth, uint32_t displayHeight,
                 ResolvedClientTarget* outTarget) override {
        if (buffer == nullptr || outTarget == nullptr) {
            return false;
        }
        return ResolveFloralMetadata(gralloc_module_, buffer, displayWidth, displayHeight,
                                     outTarget) ||
               ResolveStandardMetadata(buffer, displayWidth, displayHeight, outTarget);
    }

  private:
    const gralloc_module_t* gralloc_module_ = nullptr;
};

}  // namespace

std::shared_ptr<ClientTargetResolver> CreateGraphicBufferClientTargetResolver() {
    const hw_module_t* module = nullptr;
    const gralloc_module_t* grallocModule = nullptr;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        grallocModule = reinterpret_cast<const gralloc_module_t*>(module);
    }
    return CreateGraphicBufferClientTargetResolver(grallocModule);
}

std::shared_ptr<ClientTargetResolver> CreateGraphicBufferClientTargetResolver(
        const gralloc_module_t* grallocModule) {
    return std::make_shared<GraphicBufferClientTargetResolver>(grallocModule);
}

}  // namespace floral::display
