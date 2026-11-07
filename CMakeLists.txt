cmake_minimum_required(VERSION 3.16)
project(GunGamePortable C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# No SDK/RTOS/platform dependency: usable by any target toolchain.
file(GLOB core_sources CONFIGURE_DEPENDS "firmware/components/gg_core/src/*.c")
add_library(gg_core_portable STATIC ${core_sources})
target_include_directories(gg_core_portable PUBLIC firmware/components/gg_core/include)
add_library(gg_app_portable STATIC
    firmware/components/gg_app/src/gg_app.c firmware/components/gg_app/src/pedal_keys.c)
target_include_directories(gg_app_portable PUBLIC firmware/components/gg_app/include)
target_link_libraries(gg_app_portable PUBLIC gg_core_portable)
if(NOT MSVC)
    target_link_libraries(gg_core_portable PUBLIC m)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_app_hal tests/test_app_hal.c)
    target_link_libraries(test_app_hal PRIVATE gg_app_portable)
    add_test(NAME application_hal COMMAND test_app_hal)

    add_executable(test_gg_core tests/test_gg_core.c)
    target_include_directories(test_gg_core PRIVATE firmware/platform/esp32/gun)
    target_link_libraries(test_gg_core PRIVATE gg_app_portable)
    add_test(NAME core COMMAND test_gg_core)

    add_executable(test_usb_transport tests/test_usb_transport.c)
    target_include_directories(test_usb_transport PRIVATE tests
        firmware/platform/esp32/components/gg_usb/include)
    target_link_libraries(test_usb_transport PRIVATE gg_core_portable)
    add_test(NAME esp_usb_transport COMMAND test_usb_transport)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        foreach(target gg_core_portable gg_app_portable test_app_hal test_gg_core test_usb_transport)
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        endforeach()
    endif()
endif()
