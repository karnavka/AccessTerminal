cmake_minimum_required(VERSION 3.16)
project(AccessTerminal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)


add_executable(AccessTerminal
        main.cpp
        Services/DataBaseInitializer.cpp
        Repositories/UserRepository.cpp
        Repositories/CardRepository.cpp
        Repositories/LogRepository.cpp
        Services/UserService.h
        Services/UserService.cpp
        Services/SecurityService.cpp
        Services/SecurityService.h
        Services/CardService.cpp
        Services/CardService.h
        Entity/SecurityConfig.h
        Services/LogService.h
        Services/LogService.cpp
        "  Controllers/AdminController.cpp"
)


target_include_directories(AccessTerminal PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AccessTerminal PRIVATE sqlite3)
target_compile_definitions(AccessTerminal PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}")

if (MSVC)
    target_compile_options(AccessTerminal PRIVATE /utf-8 /W4)
else()
    target_compile_options(AccessTerminal PRIVATE -Wall -Wextra)
endif()