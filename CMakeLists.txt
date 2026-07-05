cmake_minimum_required(VERSION 3.20)
project(focusGaze VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FOCUSGAZE_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.15.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(httplib)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

set(OpenCV_DIR "/opt/homebrew/opt/opencv/lib/cmake/opencv4" CACHE PATH "")
find_package(OpenCV QUIET COMPONENTS core imgproc videoio highgui)
if(OpenCV_FOUND)
  message(STATUS "OpenCV ${OpenCV_VERSION}")
  set(FOCUSGAZE_HAS_OPENCV 1)
else()
  set(FOCUSGAZE_HAS_OPENCV 0)
endif()

set(onnxruntime_DIR "/opt/homebrew/opt/onnxruntime/lib/cmake/onnxruntime" CACHE PATH "")
find_package(onnxruntime QUIET)
if(onnxruntime_FOUND AND FOCUSGAZE_HAS_OPENCV)
  message(STATUS "ONNX Runtime found — YOLO11n enabled")
  set(FOCUSGAZE_HAS_YOLO 1)
elseif(FOCUSGAZE_HAS_OPENCV)
  # Manual fallback for Homebrew layout
  if(EXISTS "/opt/homebrew/opt/onnxruntime/include/onnxruntime/onnxruntime_cxx_api.h")
    message(STATUS "ONNX Runtime (Homebrew paths) — YOLO11n enabled")
    set(FOCUSGAZE_HAS_YOLO 1)
    set(FOCUSGAZE_ORT_INCLUDE "/opt/homebrew/opt/onnxruntime/include/onnxruntime")
    set(FOCUSGAZE_ORT_LIB "/opt/homebrew/opt/onnxruntime/lib/libonnxruntime.dylib")
  else()
    set(FOCUSGAZE_HAS_YOLO 0)
    message(STATUS "ONNX Runtime not found — YOLO disabled")
  endif()
else()
  set(FOCUSGAZE_HAS_YOLO 0)
endif()

set(FOCUSGAZE_CORE_SOURCES
  src/core/Settings.cpp src/core/Storage.cpp src/core/FocusSession.cpp
  src/core/FileSystem.cpp src/core/DefaultBlocklist.cpp src/core/UrlClassifier.cpp
  src/core/AlarmController.cpp src/core/BrowserMonitor.cpp
  src/core/PhonePresenceTracker.cpp src/core/PhoneMonitor.cpp
  src/core/AlarmPresenter.cpp src/core/ProductivityStats.cpp
  src/vision/VisionLoop.cpp src/vision/CameraSource.cpp src/vision/CameraPreview.cpp
  src/vision/YoloDetector.cpp
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
target_include_directories(focusgaze_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${httplib_SOURCE_DIR})
target_link_libraries(focusgaze_core PUBLIC nlohmann_json::nlohmann_json SQLite3::SQLite3 Threads::Threads)
target_compile_definitions(focusgaze_core PUBLIC FOCUSGAZE_VERSION="${PROJECT_VERSION}")

if(FOCUSGAZE_HAS_OPENCV)
  target_include_directories(focusgaze_core PUBLIC ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(focusgaze_core PUBLIC ${OpenCV_LIBS})
  target_compile_definitions(focusgaze_core PUBLIC FOCUSGAZE_HAS_OPENCV=1)
endif()

if(FOCUSGAZE_HAS_YOLO)
  target_compile_definitions(focusgaze_core PUBLIC FOCUSGAZE_HAS_YOLO=1)
  if(onnxruntime_FOUND)
    target_link_libraries(focusgaze_core PUBLIC onnxruntime::onnxruntime)
  else()
    target_include_directories(focusgaze_core PUBLIC ${FOCUSGAZE_ORT_INCLUDE})
    target_link_libraries(focusgaze_core PUBLIC ${FOCUSGAZE_ORT_LIB})
  endif()
endif()

if(MSVC)
  target_compile_options(focusgaze_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(focusgaze_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(focusgaze src/app/main.cpp)
target_link_libraries(focusgaze PRIVATE focusgaze_core)
# rpath for ORT dylib when running from build/
if(APPLE AND FOCUSGAZE_HAS_YOLO)
  set_target_properties(focusgaze PROPERTIES BUILD_RPATH "/opt/homebrew/opt/onnxruntime/lib")
endif()

if(FOCUSGAZE_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  add_executable(focusgaze_tests
    tests/test_platform_paths.cpp tests/test_settings.cpp tests/test_blocklist_file.cpp
    tests/test_storage.cpp tests/test_focus_session.cpp tests/test_url_classifier.cpp
    tests/test_alarm_controller.cpp tests/test_browser_monitor.cpp tests/test_http_bridge.cpp
    tests/test_phone_presence.cpp tests/test_phone_monitor.cpp tests/test_productivity_stats.cpp
    tests/test_yolo_detector.cpp tests/test_camera_source.cpp
  )
  target_link_libraries(focusgaze_tests PRIVATE focusgaze_core Catch2::Catch2WithMain)
  if(APPLE AND FOCUSGAZE_HAS_YOLO)
    set_target_properties(focusgaze_tests PROPERTIES BUILD_RPATH "/opt/homebrew/opt/onnxruntime/lib")
  endif()
  # Model path for tests run from build/
  target_compile_definitions(focusgaze_tests PRIVATE
    FOCUSGAZE_TEST_MODEL_PATH="${CMAKE_SOURCE_DIR}/models/yolo11n.onnx")
  include(CTest)
  include(Catch)
  catch_discover_tests(focusgaze_tests)
endif()

# --- Phase 5: Qt tray GUI (MACOSX_BUNDLE) ---
option(FOCUSGAZE_BUILD_GUI "Build Qt system-tray GUI app" ON)
if(FOCUSGAZE_BUILD_GUI)
  set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase;${CMAKE_PREFIX_PATH}")
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    message(STATUS "Qt6 found — building focusGaze tray app")
    set(CMAKE_AUTOMOC ON)
    add_executable(focusGaze_gui
      src/ui/main_gui.cpp
      src/ui/TrayController.cpp
      src/ui/TrayController.hpp
      src/ui/CameraWindow.cpp
      src/ui/CameraWindow.hpp
      src/ui/DashboardWindow.cpp
      src/ui/DashboardWindow.hpp
      src/ui/OnboardingWizard.cpp
      src/ui/OnboardingWizard.hpp
    )
    target_link_libraries(focusGaze_gui PRIVATE focusgaze_core Qt6::Widgets)
    target_compile_definitions(focusGaze_gui PRIVATE FOCUSGAZE_GUI=1)
    if(APPLE AND FOCUSGAZE_HAS_YOLO)
      set_target_properties(focusGaze_gui PROPERTIES BUILD_RPATH "/opt/homebrew/opt/onnxruntime/lib")
    endif()
    if(APPLE)
      set_target_properties(focusGaze_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "focusGaze"
        MACOSX_BUNDLE_BUNDLE_NAME "focusGaze"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.focusgaze.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
      )
    endif()
    # Convenience: copy YOLO model next to bundle for first run (dev)
    add_custom_command(TARGET focusGaze_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:focusGaze_gui>/../Resources/models"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/models/yolo11n.onnx"
        "$<TARGET_FILE_DIR:focusGaze_gui>/../Resources/models/yolo11n.onnx"
      COMMENT "Copy yolo11n.onnx into app Resources (if present)"
      VERBATIM
    )
  else()
    message(STATUS "Qt6 Widgets not found — skipping GUI (CLI focusgaze still built)")
  endif()
endif()
