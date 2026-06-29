cmake_minimum_required(VERSION 3.20)
project(focusGaze VERSION 0.2.0 LANGUAGES CXX)

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

# cpp-httplib (header-only)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

set(FOCUSGAZE_CORE_SOURCES
  src/core/Settings.cpp
  src/core/Storage.cpp
  src/core/FocusSession.cpp
  src/core/FileSystem.cpp
  src/core/DefaultBlocklist.cpp
  src/core/UrlClassifier.cpp
  src/core/AlarmController.cpp
  src/core/BrowserMonitor.cpp
  src/bridge/HttpBrowserBridge.cpp
)

if(APPLE)
  list(APPEND FOCUSGAZE_CORE_SOURCES src/adapters/macos/PlatformPaths.cpp)
elseif(WIN32)
  list(APPEND FOCUSGAZE_CORE_SOURCES src/adapters/windows/PlatformPaths.cpp)
else()
  list(APPEND FOCUSGAZE_CORE_SOURCES src/adapters/macos/PlatformPaths.cpp)
endif()

add_library(focusgaze_core STATIC ${FOCUSGAZE_CORE_SOURCES})
target_include_directories(focusgaze_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${httplib_SOURCE_DIR}
)
target_link_libraries(focusgaze_core
  PUBLIC
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
    Threads::Threads
)
target_compile_definitions(focusgaze_core
  PUBLIC
    FOCUSGAZE_VERSION="${PROJECT_VERSION}"
)
# Do not define CPPHTTPLIB_OPENSSL_SUPPORT — we only use http://127.0.0.1 (no OpenSSL link).

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
    tests/test_blocklist_file.cpp
    tests/test_storage.cpp
    tests/test_focus_session.cpp
    tests/test_url_classifier.cpp
    tests/test_alarm_controller.cpp
    tests/test_browser_monitor.cpp
    tests/test_http_bridge.cpp
  )
  target_link_libraries(focusgaze_tests PRIVATE focusgaze_core Catch2::Catch2WithMain)
  include(CTest)
  include(Catch)
  catch_discover_tests(focusgaze_tests)
endif()
