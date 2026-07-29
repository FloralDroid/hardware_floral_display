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

#include <cutils/native_handle.h>
#include <gtest/gtest.h>

#include <cstdarg>
#include <memory>

#include "floral/display/GrallocMetadata.h"

namespace floral::display {
namespace {

struct NativeHandleReleaser {
    void operator()(native_handle_t* handle) const {
        if (handle != nullptr) {
            native_handle_close(handle);
            native_handle_delete(handle);
        }
    }
};

using NativeHandle = std::unique_ptr<native_handle_t, NativeHandleReleaser>;

int GetBufferMetadata(const gralloc_module_t* module, int operation, ...) {
    (void)module;
    if (operation != FLORAL_GRALLOC_MODULE_PERFORM_GET_BUFFER_METADATA) {
        return -1;
    }

    va_list arguments;
    va_start(arguments, operation);
    const buffer_handle_t buffer = va_arg(arguments, buffer_handle_t);
    floral_gralloc_buffer_metadata_v1_t* metadata =
            va_arg(arguments, floral_gralloc_buffer_metadata_v1_t*);
    va_end(arguments);
    if (buffer == nullptr || metadata == nullptr || metadata->struct_size != sizeof(*metadata) ||
        metadata->version != FLORAL_GRALLOC_BUFFER_METADATA_VERSION_1) {
        return -1;
    }

    metadata->width = 64;
    metadata->height = 32;
    metadata->layers = 1;
    metadata->format = 1;
    metadata->stride = 64;
    metadata->usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
    metadata->buffer_id = 37;
    return 0;
}

gralloc_module_t CreateGrallocModule() {
    gralloc_module_t module{};
    module.perform = GetBufferMetadata;
    return module;
}

TEST(GraphicBufferClientTargetResolverTest, ReadsFloralAllocatorMetadata) {
    NativeHandle buffer(native_handle_create(0, 0));
    ASSERT_NE(buffer, nullptr);
    gralloc_module_t module = CreateGrallocModule();

    std::shared_ptr<ClientTargetResolver> resolver =
            CreateGraphicBufferClientTargetResolver(&module);
    ASSERT_NE(resolver, nullptr);
    ResolvedClientTarget target;
    ASSERT_TRUE(resolver->Resolve(buffer.get(), 64, 32, &target));

    EXPECT_EQ(target.identity, 37u);
    EXPECT_EQ(target.descriptor.width, 64u);
    EXPECT_EQ(target.descriptor.height, 32u);
    EXPECT_EQ(target.descriptor.layers, 1u);
    EXPECT_EQ(target.descriptor.format, 1);
    EXPECT_EQ(target.descriptor.usage,
              static_cast<uint64_t>(GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE));
    EXPECT_EQ(target.descriptor.stride, 64u);
    EXPECT_FALSE(target.descriptor.protected_content);
}

TEST(GraphicBufferClientTargetResolverTest, RejectsMismatchedDisplayDimensions) {
    NativeHandle buffer(native_handle_create(0, 0));
    ASSERT_NE(buffer, nullptr);
    gralloc_module_t module = CreateGrallocModule();

    std::shared_ptr<ClientTargetResolver> resolver =
            CreateGraphicBufferClientTargetResolver(&module);
    ASSERT_NE(resolver, nullptr);
    ResolvedClientTarget target;
    EXPECT_FALSE(resolver->Resolve(buffer.get(), 128, 32, &target));
}

}  // namespace
}  // namespace floral::display
