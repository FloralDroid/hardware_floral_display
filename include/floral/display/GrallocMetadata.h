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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLORAL_GRALLOC_MODULE_PERFORM_GET_BUFFER_METADATA = 0x464c0001,
    FLORAL_GRALLOC_BUFFER_METADATA_VERSION_1 = 1,
    FLORAL_GRALLOC_BUFFER_METADATA_V1_SIZE = 256,
    FLORAL_GRALLOC_DRM_OBJECT_V1_SIZE = 24,
    FLORAL_GRALLOC_DRM_PLANE_V1_SIZE = 24,
    FLORAL_GRALLOC_BUFFER_MAX_DRM_OBJECTS = 4,
    FLORAL_GRALLOC_BUFFER_MAX_DRM_PLANES = 4,
};

enum {
    FLORAL_GRALLOC_BUFFER_METADATA_FLAG_DRM_PRIME = 1U << 0,
    FLORAL_GRALLOC_BUFFER_METADATA_FLAG_PROTECTED = 1U << 1,
    FLORAL_GRALLOC_BUFFER_METADATA_FLAG_SHARED_MEMORY = 1U << 2,
};

enum {
    FLORAL_GRALLOC_DRM_OBJECT_FLAG_DEDICATED = 1U << 0,
};

typedef struct floral_gralloc_drm_object_v1 {
    // Index into the file descriptor prefix of native_handle_t::data.
    uint32_t fd_index;
    uint32_t flags;
    uint64_t size;
    uint64_t modifier;
} floral_gralloc_drm_object_v1_t;

typedef struct floral_gralloc_drm_plane_v1 {
    uint32_t object_index;
    uint32_t reserved_0;
    uint64_t offset;
    uint64_t pitch;
} floral_gralloc_drm_plane_v1_t;

// Versioned gralloc0 extension describing an Android graphics buffer without
// exposing an allocator's private native_handle layout. File descriptors are
// borrowed through fd_index and remain owned by the native handle.
typedef struct floral_gralloc_buffer_metadata_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    int32_t format;
    uint32_t stride;
    uint64_t usage;
    uint64_t buffer_id;
    uint32_t drm_format;
    uint32_t drm_object_count;
    uint32_t drm_plane_count;
    uint32_t reserved_0;
    floral_gralloc_drm_object_v1_t drm_objects[FLORAL_GRALLOC_BUFFER_MAX_DRM_OBJECTS];
    floral_gralloc_drm_plane_v1_t drm_planes[FLORAL_GRALLOC_BUFFER_MAX_DRM_PLANES];
} floral_gralloc_buffer_metadata_v1_t;

#ifdef __cplusplus
}
#endif
