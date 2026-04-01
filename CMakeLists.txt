cmake_minimum_required(VERSION 3.20)

# Some CMake/MSVC combinations fail to parse localized compiler diagnostics
# (notably on non-English toolchains), which can break implicit link detection.
set(ENV{VSLANG} 1033)
set(ENV{PreferredUILang} en-US)

project(voice_changer LANGUAGES CXX)

option(USE_ONNXRUNTIME "Enable ONNX Runtime integration" OFF)
set(ONNXRUNTIME_INCLUDE_DIR "" CACHE PATH "Path to ONNX Runtime include directory")
set(ONNXRUNTIME_LIB "" CACHE FILEPATH "Path to ONNX Runtime library file")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

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
    src/ai/LocalVoiceConverter.cpp
    src/ai/ONNXVoiceConverter.cpp
    src/core/RingBuffer.cpp
    src/core/PresetManager.cpp
    src/dsp/GainEffect.cpp
    src/dsp/SoftClipperEffect.cpp
    src/dsp/PitchShiftEffect.cpp
    src/dsp/VoiceConverterEffect.cpp
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

if (USE_ONNXRUNTIME)
    if (NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
        message(FATAL_ERROR "USE_ONNXRUNTIME=ON requires ONNXRUNTIME_INCLUDE_DIR and ONNXRUNTIME_LIB")
    endif()

    target_compile_definitions(voice_changer PRIVATE USE_ONNXRUNTIME=1)
    target_include_directories(voice_changer PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(voice_changer PRIVATE ${ONNXRUNTIME_LIB})
endif()

# --- Unit tests ---
enable_testing()

add_executable(voice_changer_tests
    tests/main.cpp
    tests/test_ringbuffer.cpp
    tests/test_biquad.cpp
    tests/test_eqeffect.cpp
    tests/test_pitchshift.cpp
    tests/test_voice_converter.cpp
    src/ai/LocalVoiceConverter.cpp
    src/ai/ONNXVoiceConverter.cpp
    src/core/RingBuffer.cpp
    src/core/PresetManager.cpp
    src/dsp/Biquad.cpp
    src/dsp/EQEffect.cpp
    src/dsp/PitchShiftEffect.cpp
    src/dsp/VoiceConverterEffect.cpp
)

target_include_directories(voice_changer_tests PRIVATE src)

if (MSVC)
    target_compile_definitions(voice_changer_tests PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

target_link_libraries(voice_changer_tests PRIVATE soundtouch)

add_test(NAME voice_changer_tests COMMAND voice_changer_tests)

add_executable(voice_changer_ui WIN32
    src/ui/main.cpp
)

target_compile_definitions(voice_changer_ui PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(voice_changer_ui PRIVATE
    comdlg32
    shell32
)

if (WIN32)
    add_custom_target(run_diagnostic_runtime
        COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/diagnostic-runtime.ps1 -BuildConfig $<CONFIG> -DurationSec 10
        DEPENDS voice_changer
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Run runtime diagnostic script"
        VERBATIM
    )
endif()