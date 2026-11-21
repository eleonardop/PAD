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
    ${CMAKE_CURRENT_LIST_DIR}/src/core/Sources.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/OneShots.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/TexturePlayer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/core/Conductor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/tools/Benchmark.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/Wavetable.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/Reverb.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/PadSynth.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/DattorroPlate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dsp/CloudReverb.cpp
    # Cloud Seed Core (MIT, Ghost Note Audio) — ver third_party/README.md
    ${CMAKE_CURRENT_LIST_DIR}/third_party/cloudseed/DSP/RandomBuffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/third_party/cloudseed/DSP/Biquad.cpp
)

if(NOT TARGET pad_engine)
    add_library(pad_engine STATIC ${PAD_ENGINE_SOURCES})
    target_include_directories(pad_engine PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
    target_compile_features(pad_engine PUBLIC cxx_std_17)
    # Bloco máximo interno do Cloud Seed (o motor chama em sub-blocos de 32 amostras).
    target_compile_definitions(pad_engine PUBLIC BUFFER_SIZE=256)
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
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestsSources.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestsEngineV2.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestsV3.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/TestsV4.cpp
    )
    target_link_libraries(pad_tests PRIVATE pad_engine)
    add_test(NAME pad_tests COMMAND pad_tests)
    # Os testes leem os samples de <repo>/assets
    target_compile_definitions(pad_tests PRIVATE PAD_ASSETS_DIR="${CMAKE_CURRENT_LIST_DIR}/../assets")

    add_executable(pad_wavgen ${CMAKE_CURRENT_LIST_DIR}/../tools/wavgen/WavGen.cpp)

    # Cenários de comparação (mesmo código compila contra a v0.1 em tools/compare/compare.sh)
    add_executable(pad_scenarios ${CMAKE_CURRENT_LIST_DIR}/../tools/compare/Scenarios.cpp)
    target_link_libraries(pad_scenarios PRIVATE pad_engine)
    target_compile_definitions(pad_scenarios PRIVATE PAD_ASSETS_DIR="${CMAKE_CURRENT_LIST_DIR}/../assets")
    target_link_libraries(pad_wavgen PRIVATE pad_engine)
    add_executable(pad_scenarios_v3 ${CMAKE_CURRENT_LIST_DIR}/../tools/compare/ScenariosV3.cpp)
    target_link_libraries(pad_scenarios_v3 PRIVATE pad_engine)
    add_executable(pad_bench ${CMAKE_CURRENT_LIST_DIR}/../tools/bench/Bench.cpp)
    target_link_libraries(pad_bench PRIVATE pad_engine)
    target_compile_definitions(pad_bench PRIVATE PAD_ASSETS_DIR="${CMAKE_CURRENT_LIST_DIR}/../assets")
    add_executable(pad_reverblab ${CMAKE_CURRENT_LIST_DIR}/../tools/reverblab/ReverbLab.cpp)
    target_link_libraries(pad_reverblab PRIVATE pad_engine)
    target_compile_definitions(pad_wavgen PRIVATE PAD_ASSETS_DIR="${CMAKE_CURRENT_LIST_DIR}/../assets")
endif()
