cmake_minimum_required(VERSION 3.18)
project(pad_engine CXX)

# Biblioteca do motor: sem dependência de Android, usada pelo app, pelos testes e pelo gerador de WAV.
set(PAD_ENGINE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/PadEngine.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/MusicTheory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/SampleBank.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/Voice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/VoiceManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/Presets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/WavIO.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/Wavetable.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/Reverb.cpp
)

if(NOT TARGET pad_engine)
    add_library(pad_engine STATIC ${PAD_ENGINE_SOURCES})
    target_include_directories(pad_engine PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
    target_compile_features(pad_engine PUBLIC cxx_std_17)
    find_package(Threads REQUIRED)
    target_link_libraries(pad_engine PUBLIC Threads::Threads)
    if(MSVC)
        target_compile_options(pad_engine PRIVATE /W4)
    else()
        target_compile_options(pad_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endif()

# Testes e ferramentas apenas fora do Android.
if(NOT ANDROID AND PAD_ENGINE_BUILD_TESTS)
    enable_testing()
    add_executable(pad_tests
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestMain.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/AudioAnalysis.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestsDsp.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestsEngine.cpp
    )
    target_link_libraries(pad_tests PRIVATE pad_engine)
    add_test(NAME pad_tests COMMAND pad_tests)

    add_executable(pad_wavgen ${CMAKE_CURRENT_LIST_DIR}/../tools/wavgen/WavGen.cpp)
    target_link_libraries(pad_wavgen PRIVATE pad_engine)
endif()
