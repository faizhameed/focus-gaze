cmake_minimum_required(VERSION 3.20)
project(focusGaze VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FOCUSGAZE_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(SQLite3 REQUIRED)

set(FOCUSGAZE_CORE_SOURCES
  src/core/Settings.cpp
  src/core/Storage.cpp
  src/core/FocusSession.cpp
  src/core/FileSystem.cpp
)

if(APPLE)
  list(APPEND FOCUSGAZE_CORE_SOURCES src/adapters/macos/PlatformPaths.cpp)
elseif(WIN32)
  list(APPEND FOCUSGAZE_CORE_SOURCES src/adapters/windows/PlatformPaths.cpp)
else()
  # Generic Unix fallback (Linux CI / future)
  list(APPEND FOCUSGAZE_CORE_SOURCES src/adapters/macos/PlatformPaths.cpp)
endif()

add_library(focusgaze_core STATIC ${FOCUSGAZE_CORE_SOURCES})
target_include_directories(focusgaze_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(focusgaze_core
  PUBLIC
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)
target_compile_definitions(focusgaze_core
  PUBLIC
    FOCUSGAZE_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
  target_compile_options(focusgaze_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(focusgaze_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(focusgaze src/app/main.cpp)
target_link_libraries(focusgaze PRIVATE focusgaze_core)

if(FOCUSGAZE_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

  add_executable(focusgaze_tests
    tests/test_platform_paths.cpp
    tests/test_settings.cpp
    tests/test_storage.cpp
    tests/test_focus_session.cpp
  )
  target_link_libraries(focusgaze_tests PRIVATE focusgaze_core Catch2::Catch2WithMain)
  include(CTest)
  include(Catch)
  catch_discover_tests(focusgaze_tests)
endif()
