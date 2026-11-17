cmake_minimum_required(VERSION 3.25)
project(TinyLog VERSION 1.0.0 LANGUAGES CXX RC)

# Ownership: Wave 0 (orchestrator). WP agents must not edit this file.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MSVC)
  message(FATAL_ERROR "TinyLog requires MSVC")
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/WX>
                    $<$<COMPILE_LANGUAGE:CXX>:/permissive-> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
                    $<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:CXX>:/GR->
                    $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)

# Release: size-first (Spec 2.3). External PDB, never shipped.
set(CMAKE_CXX_FLAGS_RELEASE "/O1 /Gy /Gw /GL /Zi /DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG")
set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/LTCG")

set(SRC ${CMAKE_SOURCE_DIR}/src)

# ---- core: testable, no windows of its own -------------------------------
add_library(tinylog_core STATIC
  src/util/StringHelpers.cpp
  src/util/TimeHelpers.cpp
  src/util/Crc32.cpp
  src/util/FileHelpers.cpp
  src/storage/SessionRecord.cpp
  src/storage/Json.cpp
  src/storage/StateStore.cpp
  src/storage/JsonLinesStore.cpp
  src/export/ZipWriter.cpp
  src/export/SpreadsheetWriter.cpp
  src/export/ExportController.cpp
  src/app/SessionManager.cpp
  src/win32/BubblePlacement.cpp
  src/win32/LayeredAnimation.cpp
  src/win32/StartupShortcut.cpp
  src/win32/SaveDialog.cpp
)
target_include_directories(tinylog_core PUBLIC ${SRC})
target_link_libraries(tinylog_core PUBLIC kernel32 user32 shell32 ole32 uuid)

# ---- ui: windows, tray, rendering ----------------------------------------
add_library(tinylog_ui STATIC
  src/win32/SingleInstance.cpp
  src/win32/TrayIcon.cpp
  src/win32/TrayMenu.cpp
  src/win32/BubbleWindow.cpp
  src/win32/GdiRenderer.cpp
  src/win32/NativeControls.cpp
)
target_link_libraries(tinylog_ui PUBLIC tinylog_core user32 gdi32 shell32 comctl32 uxtheme)

# ---- shipping executable --------------------------------------------------
add_executable(TinyLog WIN32
  src/main.cpp
  src/app/App.cpp
  resources/TinyLog.rc
)
target_link_libraries(TinyLog PRIVATE tinylog_ui tinylog_core)
target_link_options(TinyLog PRIVATE /MANIFEST:NO)
add_custom_command(TARGET TinyLog POST_BUILD
  COMMAND $<$<CONFIG:Release>:powershell> $<$<CONFIG:Release>:-NoProfile> $<$<CONFIG:Release>:-ExecutionPolicy> $<$<CONFIG:Release>:Bypass>
          $<$<CONFIG:Release>:-File> $<$<CONFIG:Release>:${CMAKE_SOURCE_DIR}/tools/check-size.ps1>
          $<$<CONFIG:Release>:-Path> $<$<CONFIG:Release>:$<TARGET_FILE:TinyLog>> $<$<CONFIG:Release>:-Max> $<$<CONFIG:Release>:3000000>
  COMMAND_EXPAND_LISTS
  VERBATIM)

# ---- unit tests (not shipped) ---------------------------------------------
add_executable(TinyLogTests
  tests/TestMain.cpp
  tests/Util_Tests.cpp
  tests/Storage_Tests.cpp
  tests/Session_Tests.cpp
  tests/Zip_Tests.cpp
  tests/Spreadsheet_Tests.cpp
  tests/Startup_Tests.cpp
  tests/Placement_Tests.cpp
  tests/Animation_Tests.cpp
)
target_include_directories(TinyLogTests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(TinyLogTests PRIVATE tinylog_core psapi)
target_link_options(TinyLogTests PRIVATE /SUBSYSTEM:CONSOLE)

enable_testing()
add_test(NAME TinyLogTests COMMAND TinyLogTests)

# ---- visual demo of the bubble (not shipped) ------------------------------
add_executable(BubbleDemo WIN32
  demo/BubbleDemo.cpp
  resources/TinyLog.rc
)
target_link_libraries(BubbleDemo PRIVATE tinylog_ui tinylog_core)
target_link_options(BubbleDemo PRIVATE /MANIFEST:NO)

# The resource compiler's dependency scan misses icons/manifest; rebuild the .res when they change.
set_source_files_properties(resources/TinyLog.rc PROPERTIES OBJECT_DEPENDS
  "${CMAKE_SOURCE_DIR}/resources/idle.ico;${CMAKE_SOURCE_DIR}/resources/recording.ico;${CMAKE_SOURCE_DIR}/resources/app.manifest")
