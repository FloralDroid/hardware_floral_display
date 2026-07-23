# Floral Display HAL

`hardware_floral_display` provides the Floral HWC2 physical display backend for
headless Android devices. The first implementation deliberately contains one
permanent internal display and a sink presentation backend. Encoding and host
transport are separate components and are not performed on the Composer call
path.

## First vertical slice

- Composer/HWC2 2.4 dispatch surface.
- One permanent `INTERNAL` physical display on port 0.
- One 1920x1080@60 configuration by default.
- CLIENT composition only.
- Monotonic VSync generation.
- Stable Floral EDID identity.
- No virtual displays, readback, protected-content claim, encoder, or socket.

The module reads the existing redroid boot properties when present:

```text
ro.boot.redroid_width
ro.boot.redroid_height
ro.boot.redroid_fps
ro.boot.redroid_dpi
```

Values are validated before use. Phase one caps the advertised refresh rate at
60 Hz.

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
