# Floral Display HAL

`hardware_floral_display` provides the Floral HWC2 physical display backend for
headless Android devices. The first implementation deliberately contains one
permanent internal display and a sink presentation backend. Encoding and host
transport are separate components and are not performed on the Composer call
path.

## First vertical slice

- Composer/HWC2 2.4 dispatch surface.
- One permanent `INTERNAL` physical display on port 0.
- One 1920x1080 internal display with 15, 30, and 60 Hz configurations.
- CLIENT composition only.
- Monotonic VSync generation.
- Stable Floral EDID identity.
- FrameSink submission with sequence, dataspace, damage, and fence metadata.
- No virtual displays, readback, or protected-content claim.

The production HWC uses `StreamFrameSink` to connect to FloralStream through a
versioned AIDL endpoint. Until the service advertises an active generation, the
sink passes through the client-target acquire fence. Protected client targets
remain local to the display path and are never registered with the stream service.

`libfloral_display_stream_bridge` keeps that boundary behind injectable
consumer and client-target resolver interfaces. Its tests cover generation-scoped
buffer registration, drop fallback, protected-buffer rejection, and release-fence
ownership. Encoding and socket transport remain outside the Composer module.

The version-independent topology core can connect and disconnect additional
physical external displays while preserving the permanent primary identity.
Registry lookups retain shared display ownership so an in-flight HWC call can
finish safely after a hot-unplug. The Android 12 frontend subscribes to the
container-internal `floral.display.topology` VINTF AIDL service. Each increasing
generation replaces the complete external-display snapshot; removal or a
configuration change emits disconnect events before new display objects and
connect events become visible. The primary display is implicit in HWC and
cannot be removed or renumbered by the service. Hotplug callback installation
shares the same ordering boundary as topology mutations, so the initial
connected snapshot cannot race later events. If the topology service remains
unavailable, HWC retains the last external snapshot for the bounded
`ro.boot.floral_control_disconnect_lease_ms` interval and then removes it.

The module reads the existing redroid boot properties when present:

```text
ro.boot.redroid_width
ro.boot.redroid_height
ro.boot.redroid_fps
ro.boot.redroid_dpi
```

Values are validated before use. Phase one caps the advertised refresh rate at
60 Hz. A requested boot frame rate selects the lowest advertised refresh rate
that is not lower than the request, such as 24 FPS selecting 30 Hz and 45 FPS
selecting 60 Hz. All refresh configurations remain in one config group, so the
resolution, internal-display identity, and Android logical display remain
unchanged during a switch.

## Build integration

Map this repository to `hardware/floral/display`, inherit `display.mk` from the
product, and use the platform Composer 2.4 service and matching VINTF manifest.
Product integration intentionally remains outside this repository.

## Android versioning

`libfloral_display_core` owns version-independent display identity, topology,
EDID, and VSync policy. The Android 12 output is an HWC2.4 module; Android 13
and later can add a Composer 3 AIDL frontend without duplicating the core.

Long-lived Android baselines should use matching branches such as
`android-12.0`, `android-13.0`, and `android-14.0`. Platform-specific service,
VINTF, and build integration belongs to those branches rather than broad
preprocessor conditionals in the common code.

## Security boundary

This HAL is a physical display output and does not call capture APIs. It does
not expose readback or raw-frame debug interfaces and does not advertise
protected-content support. DRM protected buffers are outside the first-phase
contract and must fail closed.
