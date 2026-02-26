cmake_minimum_required(VERSION 3.20)
project(voice_changer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- SoundTouch (build from source) ---
add_library(soundtouch STATIC
    third_party/soundtouch/source/SoundTouch/AAFilter.cpp
    third_party/soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp
    third_party/soundtouch/source/SoundTouch/FIRFilter.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateCubic.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateLinear.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateShannon.cpp
    third_party/soundtouch/source/SoundTouch/RateTransposer.cpp
    third_party/soundtouch/source/SoundTouch/SoundTouch.cpp
    third_party/soundtouch/source/SoundTouch/TDStretch.cpp
    third_party/soundtouch/source/SoundTouch/cpu_detect_x86.cpp
    third_party/soundtouch/source/SoundTouch/sse_optimized.cpp
    third_party/soundtouch/source/SoundTouch/mmx_optimized.cpp
)

target_include_directories(soundtouch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundtouch/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundtouch/source/SoundTouch
)

target_compile_definitions(soundtouch PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)


# --- Main application ---
add_executable(voice_changer
    src/app/main.cpp
    src/audio/DeviceUtils.cpp
    src/audio/WasapiCapture.cpp
    src/audio/WasapiRender.cpp
    src/core/RingBuffer.cpp
    src/dsp/GainEffect.cpp
    src/dsp/SoftClipperEffect.cpp
    src/dsp/PitchShiftEffect.cpp
    src/dsp/Biquad.cpp
    src/dsp/EQEffect.cpp
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
    soundtouch
)