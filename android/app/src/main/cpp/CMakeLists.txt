cmake_minimum_required(VERSION 3.22.1)
project(padjni CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Motor de áudio (mesmo código testado no computador). Caminho: android/app/src/main/cpp -> raiz do repositório.
set(PAD_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
add_subdirectory(${PAD_REPO_ROOT}/engine ${CMAKE_BINARY_DIR}/pad_engine)

find_package(oboe REQUIRED CONFIG)

add_library(padjni SHARED
    JniBridge.cpp
    OboePlayer.cpp
)
target_link_libraries(padjni PRIVATE pad_engine oboe::oboe log android)

# O processamento de áudio é sempre compilado otimizado, inclusive no APK de debug
# (com -O0 o motor ficaria várias vezes mais lento e poderia falhar em tempo real).
target_compile_options(pad_engine PRIVATE -O3)
target_compile_options(padjni PRIVATE -O3 -Wall)
