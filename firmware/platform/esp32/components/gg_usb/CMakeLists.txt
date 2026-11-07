idf_component_register(SRCS "gg_usb_transport.c" INCLUDE_DIRS "include"
    REQUIRES gg_core PRIV_REQUIRES esp_timer espressif__esp_tinyusb)
