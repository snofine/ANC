cmake_minimum_required(VERSION 3.24)

project(ANCApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(anc_core
    src/DSP/ManualAncProcessor.cpp
)
target_include_directories(anc_core PUBLIC src)
target_compile_definitions(anc_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(anc_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(anc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(anc_app
        src/main.cpp
        src/Audio/DeviceManager.cpp
        src/Audio/WasapiEngine.cpp
    )
    target_link_libraries(anc_app PRIVATE anc_core ole32 uuid avrt)
    target_compile_definitions(anc_app PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
    if(MSVC)
        # C4324 is expected: the SPSC indices are deliberately isolated on cache lines.
        target_compile_options(anc_app PRIVATE /W4 /permissive- /EHsc /wd4324)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(anc_dsp_tests tests/dsp_tests.cpp)
    target_link_libraries(anc_dsp_tests PRIVATE anc_core)
    add_test(NAME anc_dsp_tests COMMAND anc_dsp_tests)
endif()
