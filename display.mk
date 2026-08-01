# Floral physical display composer HAL.
REDROID_USE_FLORAL_HWC := true

PRODUCT_PACKAGES += \
    hwcomposer.floral \
    floral_device_service

PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.hwcomposer=floral
