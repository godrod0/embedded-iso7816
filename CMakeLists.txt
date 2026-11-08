cmake_minimum_required(VERSION 3.25)
project(iso7816 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ISO7816_BUILD_TESTS "Build host (PC) unit tests with mock HAL" ON)
option(ISO7816_BUILD_STM32U5_PORT "Build the STM32U5 HAL port (needs STM32 HAL headers)" OFF)

add_library(iso7816 STATIC
    src/iso7816_status.c
    src/iso7816_types.c
    src/iso7816_char.c
    src/iso7816_atr.c
    src/iso7816_pps.c
    src/iso7816_apdu.c
    src/iso7816_t0.c
    src/iso7816_t1.c
    src/iso7816.c
)
target_include_directories(iso7816 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(iso7816 PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(ISO7816_BUILD_STM32U5_PORT)
    add_library(iso7816_port_stm32u5 STATIC port/stm32u5/iso7816_hal_stm32u5.c)
    target_link_libraries(iso7816_port_stm32u5 PUBLIC iso7816)
endif()

if(ISO7816_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_subdirectory(tests)
endif()