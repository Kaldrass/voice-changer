cmake_minimum_required(VERSION 3.20)
project(voice_changer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(voice_changer
    src/app/main.cpp
    src/audio/DeviceUtils.cpp
    src/audio/WasapiCapture.cpp
    src/audio/WasapiRender.cpp
    src/core/RingBuffer.cpp
    src/dsp/GainEffect.cpp
    src/dsp/SoftClipperEffect.cpp
)

target_include_directories(voice_changer PRIVATE src)

target_compile_definitions(voice_changer PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(voice_changer PRIVATE
    ole32
    avrt
    mmdevapi
)