cmake_minimum_required(VERSION 3.18)
project(pad_workspace CXX)

# Raiz para compilar no computador/servidor: motor + testes + gerador de WAV.
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   ./build/engine/pad_tests      ./build/engine/pad_wavgen saida_wav
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(PAD_ENGINE_BUILD_TESTS "Compilar testes e ferramentas" ON)
add_subdirectory(engine)
