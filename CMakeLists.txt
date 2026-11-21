cmake_minimum_required(VERSION 3.24)
project(ses VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SES_ENABLE_UNITY_BUILD "Combine source files to reduce compile overhead" ON)
if(SES_ENABLE_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)

option(SES_BUILD_TESTS "Build ses tests" ON)

add_compile_options(-Wall -Wextra)

# ---- core: pure data + logic. Qt6::Core + the QtWidgets undo framework
# (QUndoStack/QUndoCommand live in Widgets upstream; core contains no GUI code).
add_library(ses_core STATIC
  src/core/model/Project.cpp
  src/core/model/ProjectModel.cpp
  src/core/commands/Commands.cpp
  src/core/render/FadeCurve.cpp
  src/core/persistence/Settings.cpp
  src/core/media/MediaProbe.cpp
)
target_include_directories(ses_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ses_core PUBLIC Qt6::Core Qt6::Widgets)

# ---- miniaudio: single header, single implementation TU (ROADMAP §2.2) -------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio/miniaudio.h")
  message(FATAL_ERROR
    "external/miniaudio/miniaudio.h is missing — vendor it first:\n"
    "  curl -fsSL -o external/miniaudio/miniaudio.h \"\n"
    "    https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h\"\n"
    "  then record its sha256 in external/miniaudio/CHECKSUMS (docs/ROADMAP.md §2.2)")
endif()
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio)

# ---- engine: RT audio facade (NO Qt). Real device + decode pool live here. ---
add_library(ses_engine STATIC
  src/engine/Engine.cpp
  src/engine/io/miniaudio_impl.cpp
  src/engine/io/AudioDevice.cpp
)
target_include_directories(ses_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Keep the 95k-line header out of unity batches (ROADMAP §2.2).
set_source_files_properties(src/engine/io/miniaudio_impl.cpp
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(ses_engine PUBLIC miniaudio)

# ---- inference: catalog data only for now; audio.cpp ABI lands in P5. -------
add_library(ses_inference STATIC
  src/inference/Catalog.cpp
)
target_include_directories(ses_inference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ses_inference PUBLIC Qt6::Core ses_core)

# ---- ui: Qt Widgets + Svg, styled entirely by QSS ----------------------------
add_library(ses_ui STATIC
  assets/assets.qrc
  src/ui/theme/ThemeManager.cpp
  src/ui/theme/Icons.cpp
  src/ui/shell/MainWindow.cpp
  src/ui/shell/Transport.cpp
  src/ui/shell/Smoke.cpp
  src/ui/timeline/TimelineWidget.cpp
  src/ui/timeline/TrackHeaders.cpp
  src/ui/mixer/AbFader.cpp
  src/ui/mixer/MixerPanel.cpp
  src/ui/panels/HistoryPanel.cpp
  src/ui/panels/AiPanel.cpp
  src/ui/panels/ModelManagerPanel.cpp
  src/ui/widgets/EmptyState.cpp
  src/ui/widgets/Toast.cpp
  src/ui/widgets/CoachMarks.cpp
  src/ui/widgets/CheatSheet.cpp
)
target_link_libraries(ses_ui PUBLIC Qt6::Widgets Qt6::Svg Qt6::Network ses_core ses_engine ses_inference)

# ---- app ---------------------------------------------------------------------
add_executable(ses src/app/main.cpp)
target_link_libraries(ses PRIVATE ses_ui)

# ---- tests -------------------------------------------------------------------
if(SES_BUILD_TESTS)
  enable_testing()
  add_executable(tst_core tests/tst_core.cpp)
  target_link_libraries(tst_core PRIVATE ses_core Qt6::Test)
  add_test(NAME core COMMAND tst_core)
  set_tests_properties(core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(tst_engine tests/tst_engine.cpp)
  target_link_libraries(tst_engine PRIVATE ses_engine Qt6::Test)
  add_test(NAME engine COMMAND tst_engine)

  add_executable(tst_ui tests/tst_ui.cpp)
  target_link_libraries(tst_ui PRIVATE ses_ui Qt6::Test)
  add_test(NAME ui COMMAND tst_ui)
  set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()