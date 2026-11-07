set(platform_root "${CMAKE_CURRENT_LIST_DIR}/../../platform/esp32")
if(CONFIG_GG_DEBUG_NO_USB_HID)
    set(usb_source "usb_hid_stub.c")
else()
    set(usb_source "usb_hid_kbd.c")
endif()
idf_component_register(
    SRCS "main.c"
         "${platform_root}/common/gg_esp_platform.c"
         "${platform_root}/pedal/board.c"
         "${platform_root}/pedal/${usb_source}"
    INCLUDE_DIRS "${platform_root}/common" "${platform_root}/pedal"
    PRIV_REQUIRES driver esp_timer gg_app gg_usb
)
