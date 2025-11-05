cmake_minimum_required(VERSION 4.1)
project(pjurominer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FetchContent)

# 1. Biblioteka nlohmann/json (bez zmian)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        # GIT_TAG v3.12.0
        GIT_TAG develop
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. Biblioteka Asio (bez zmian)
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-24-0
)
FetchContent_MakeAvailable(asio)

# --- NOWA SEKCJA (libRandomX) ---
# 3. Biblioteka libRandomX
FetchContent_Declare(
        RandomX

        GIT_REPOSITORY
        https://github.com/tevador/RandomX.git
        GIT_TAG v1.2.1 # Możesz użyć nowszej wersji
)
FetchContent_MakeAvailable(RandomX)
# --- KONIEC NOWEJ SEKCJI ---

# --- NOWA SEKCJA (fmt) ---
# 4. Biblioteka fmt
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 12.1.0
)
FetchContent_MakeAvailable(fmt)
# --- KONIEC NOWEJ SEKCJI (fmt) ---


# --- ZMIANA W PLIKACH ---
add_executable(pjurominer
        main.cpp
        StratumClient.cpp
        StratumClient.h

        MinerWorker.cpp
        MinerWorker.h
        MiningCommon.cpp
        MiningCommon.h
        RandomXHasher.cpp
        RandomXHasher.h
        # DODANO NOWE PLIKI
        RandomXManager.cpp
        RandomXManager.h
)

# --- ZMIANY W LINKOWANIU ---
target_link_libraries(pjurominer

        PRIVATE
        nlohmann_json::nlohmann_json
        randomx # Linkujemy libRandomX
        fmt::fmt # Linkujemy libfmt
        ws2_32
)

# Musimy dodać ścieżki dołączania (include)
target_include_directories(pjurominer

        PRIVATE
        ${asio_SOURCE_DIR}/asio/include
        ${randomx_SOURCE_DIR}/src # Ścieżka do randomx.h
        ${fmt_SOURCE_DIR}/include # Ścieżka do nagłówków fmt
)
# --- KONIEC ZMIAN W LINKOWANIU ---

target_compile_definitions(pjurominer PRIVATE ASIO_STANDALONE)