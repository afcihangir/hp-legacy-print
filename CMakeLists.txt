cmake_minimum_required(VERSION 3.16)
project(hp_legacy_print VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_library(hplp_core
    src/models.c
    src/usb_discovery.c
)

target_include_directories(hplp_core PUBLIC include)
target_link_libraries(hplp_core PUBLIC PkgConfig::LIBUSB)

target_compile_options(hplp_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hp-legacy-print src/main.c)
target_link_libraries(hp-legacy-print PRIVATE hplp_core)
target_compile_options(hp-legacy-print PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(test-models tests/test_models.c)
target_link_libraries(test-models PRIVATE hplp_core)
add_test(NAME model_table COMMAND test-models)
