cmake_minimum_required(VERSION 4.1)
project(pjurominer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FetchContent)

# 1. Biblioteka nlohmann/json (bez zmian)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. Biblioteka Asio (bez zmian)
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-24-0
)
FetchContent_MakeAvailable(asio)

# --- NOWA SEKCJA ---
# 3. Biblioteka libRandomX
FetchContent_Declare(
        RandomX
        GIT_REPOSITORY https://github.com/tevador/RandomX.git
        GIT_TAG v1.2.0 # Możesz użyć nowszej wersji
)
FetchContent_MakeAvailable(RandomX)
# --- KONIEC NOWEJ SEKCJI ---


# --- ZMIANA NAZWY PLIKÓW ---
# Zmieniamy RandomXStub na MiningCommon (dla funkcji pomocniczych)
# Dodajemy nowy RandomXHasher
add_executable(pjurominer
        main.cpp
        StratumClient.cpp
        MinerWorker.cpp
        MiningCommon.cpp
        RandomXHasher.cpp
        RandomXHasher.cpp
        RandomXHasher.h
)

# --- ZMIANY W LINKOWANIU ---
target_link_libraries(pjurominer
        PRIVATE
        nlohmann_json::nlohmann_json
        randomx # Linkujemy libRandomX
        ws2_32
)

# Musimy dodać ścieżki dołączania (include)
target_include_directories(pjurominer
        PRIVATE
        ${asio_SOURCE_DIR}/asio/include
        ${randomx_SOURCE_DIR}/src # Ścieżka do randomx.h
)
# --- KONIEC ZMIAN W LINKOWANIU ---

target_compile_definitions(pjurominer PRIVATE ASIO_STANDALONE)