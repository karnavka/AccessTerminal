cmake_minimum_required(VERSION 3.16)
project(AccessTerminal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------
# SQLite
# ---------------------------------------------------------------
add_library(sqlite3 STATIC
    third_party/sqlite/sqlite3.c
)

target_include_directories(sqlite3 PUBLIC
    third_party/sqlite
)

# ---------------------------------------------------------------
# Argon2
# ---------------------------------------------------------------
find_package(Threads REQUIRED)

add_library(argon2 STATIC
    third_party/argon2/src/argon2.c
    third_party/argon2/src/core.c
    third_party/argon2/src/encoding.c
    third_party/argon2/src/ref.c
    third_party/argon2/src/thread.c
    third_party/argon2/src/blake2/blake2b.c
)

target_include_directories(argon2
    PUBLIC  third_party/argon2/include
    PRIVATE third_party/argon2/src
)

target_link_libraries(argon2 PUBLIC Threads::Threads)

# ---------------------------------------------------------------
# Shared application logic
# ---------------------------------------------------------------
add_library(access_core STATIC
    Services/DataBaseInitializer.cpp
    Services/UserService.cpp
    Services/SecurityService.cpp
    Services/CardService.cpp
    Services/AccessPolicyService.cpp
    Services/LogService.cpp
    Services/HashService.cpp

    Repositories/UserRepository.cpp
    Repositories/CardRepository.cpp
    Repositories/LogRepository.cpp

    "  Controllers/AdminController.cpp"
    "  Controllers/TerminalController.cpp"
    "  Controllers/ConsoleInput.cpp"
)

target_include_directories(access_core PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(access_core PUBLIC
    sqlite3
    argon2
)

if (MSVC)
    target_compile_options(access_core PRIVATE /utf-8 /W4)
else()
    target_compile_options(access_core PRIVATE -Wall -Wextra)
endif()

# Platform-specific libraries
if(WIN32)
    target_link_libraries(access_core PUBLIC bcrypt)

elseif(APPLE)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    target_link_libraries(access_core PUBLIC ${SECURITY_FRAMEWORK})
endif()

# ---------------------------------------------------------------
# Main application
# ---------------------------------------------------------------
add_executable(AccessTerminal
    main.cpp
    tests_part/TestsPart.cpp
)

target_link_libraries(AccessTerminal PRIVATE access_core)

target_compile_definitions(AccessTerminal PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)

if (MSVC)
    target_compile_options(AccessTerminal PRIVATE /utf-8 /W4)
else()
    target_compile_options(AccessTerminal PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------
# Tests
# ---------------------------------------------------------------
enable_testing()

add_executable(TerminalControllerTests
    tests_part/TerminalControllerTests.cpp
)

target_include_directories(TerminalControllerTests PRIVATE
    "${CMAKE_SOURCE_DIR}/  Controllers"
)

target_link_libraries(TerminalControllerTests PRIVATE access_core)

target_compile_definitions(TerminalControllerTests PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)

if (MSVC)
    target_compile_options(TerminalControllerTests PRIVATE /utf-8 /W4)
else()
    target_compile_options(TerminalControllerTests PRIVATE -Wall -Wextra)
endif()

add_test(
    NAME TerminalControllerTests
    COMMAND TerminalControllerTests
)
