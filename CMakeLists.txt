# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(SR24_OLED C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# --- U8g2 Bibliothek Einbindung ---
# Sucht Quellen in lib/u8g2 (empfohlen) oder im PlatformIO-Ordner (Fallback)
file(GLOB U8G2_SOURCES "lib/u8g2/*.c")
if(NOT U8G2_SOURCES)
    file(GLOB U8G2_SOURCES ".pio/libdeps/pico/U8g2/src/clib/*.c")
    set(U8G2_INCLUDE_PATH ".pio/libdeps/pico/U8g2/src/clib")
else()
    set(U8G2_INCLUDE_PATH "lib/u8g2")
endif()

add_library(u8g2 STATIC ${U8G2_SOURCES})
target_include_directories(u8g2 PUBLIC ${U8G2_INCLUDE_PATH})
target_compile_options(u8g2 PRIVATE -w) # Unterdrückt Warnungen aus der Lib
# WICHTIG: Verknüpfung mit pico_stdlib sorgt dafür, dass Compiler-Flags wie 
# -fdata-sections gesetzt werden. Das erlaubt dem Linker, unbenutzte Fonts zu entfernen.
target_link_libraries(u8g2 PRIVATE pico_stdlib)

add_executable(SR24_OLED
    src/main.cpp
)

# Include-Pfad für tusb_config.h
target_include_directories(SR24_OLED PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# UART aktivieren, USB-Device deaktivieren (damit wir Host sein können)
pico_enable_stdio_uart(SR24_OLED 1)
pico_enable_stdio_usb(SR24_OLED 0)

# Bibliotheken linken
target_link_libraries(SR24_OLED
    pico_stdlib
    hardware_i2c
    tinyusb_host
    tinyusb_board
    u8g2
)

pico_add_extra_outputs(SR24_OLED)