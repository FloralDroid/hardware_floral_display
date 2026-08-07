# Floral Display HAL

[简体中文](README.zh-CN.md)

`hardware_floral_display` provides the Floral HWC2 physical-display backend for
headless Android devices. The display is an Android physical output, not a
capture or recording path. Encoding and host transport remain separate from the
Composer call path.

## Current scope

- Composer/HWC2 2.4 dispatch surface.
- One permanent `INTERNAL` physical display on port 0.
- Refresh configurations up to the boot-time hard limit in one config group.
- CLIENT composition only.
- Monotonic VSync generation and stable Floral EDID identity.
- FrameSink submission with sequence, dataspace, damage, and fence metadata.
- Multiple physical external displays controlled by FloralStream topology.
- No HWC virtual display, readback, or protected-content claim.

The production HWC uses `StreamFrameSink` to connect to FloralStream through a
versioned AIDL endpoint. Until the service advertises an active generation, the
sink passes through the client-target acquire fence. Protected client targets
remain local to the display path and are never registered with the stream
service.

`libfloral_display_stream_bridge` keeps this boundary behind injectable
consumer and client-target resolver interfaces. Its tests cover
generation-scoped buffer registration, drop fallback, protected-buffer
rejection, and release-fence ownership. Encoding and socket transport remain
outside the Composer module.

## Display topology

The version-independent topology core can connect and disconnect physical
external displays while preserving the permanent primary identity. Registry
lookups retain shared display ownership so an in-flight HWC call can finish
safely after hot-unplug.

The Android 12 frontend subscribes to the container-internal
`floral.device.display.topology` VINTF AIDL service. Each increasing generation
replaces the complete external-display snapshot. Removal or a configuration
change emits disconnect events before replacement displays become visible. The
primary display is implicit in HWC and cannot be removed or renumbered by the
service.

Hotplug callback installation shares the same ordering boundary as topology
mutations, so the initial connected snapshot cannot race later events. If the
topology service remains unavailable, HWC retains the last external snapshot
for `ro.boot.floral_control_disconnect_lease_ms`, then removes it.

## Boot properties

The Floral display path reads these read-only boot properties:

| Property | Default | Valid range | Purpose |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | Primary logical width in pixels. |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | Primary logical height in pixels. |
| `ro.boot.floral_fps` | `60` | `1`-`60` | Hard upper bound for primary refresh rate. |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | Primary density and `ro.sf.lcd_density`. |

Invalid values fall back to deterministic defaults. HWC never advertises a
refresh mode above `ro.boot.floral_fps` and always includes the exact limit as a
mode. For example, 30 publishes 15 and 30 Hz, while 24 publishes 15 and 24 Hz.
All refresh configurations remain in one config group, so a switch does not
change resolution, display identity, or Android logical display.

## Build and Android versions

Map this repository to `hardware/floral/display`, inherit `display.mk` from the
product, and use the platform Composer 2.4 service with its matching VINTF
manifest.

`libfloral_display_core` owns version-independent identity, topology, EDID, and
VSync policy. Android 12 uses an HWC2.4 frontend; Android 13 and later can add a
Composer 3 AIDL frontend without duplicating the core. Long-lived Android
baselines should use matching branches such as `android-12.0`, `android-13.0`,
and `android-14.0`.

## Security boundary

This HAL does not call capture APIs, expose readback, or provide raw-frame debug
interfaces. DRM protected buffers are outside the current contract and fail
closed.
