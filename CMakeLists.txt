cmake_minimum_required(VERSION 3.20)
project(cccpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)

# QScintilla - try pkg-config first, then manual search
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(QSCINTILLA QUIET qscintilla2-qt6)
endif()

if(NOT QSCINTILLA_FOUND)
    find_library(QSCINTILLA_LIB NAMES qscintilla2_qt6 qscintilla2-qt6 libqscintilla2_qt6)
    find_path(QSCINTILLA_INCLUDE NAMES Qsci/qsciscintilla.h PATH_SUFFIXES qt6)
    if(QSCINTILLA_LIB AND QSCINTILLA_INCLUDE)
        set(QSCINTILLA_FOUND TRUE)
        set(QSCINTILLA_LIBRARIES ${QSCINTILLA_LIB})
        set(QSCINTILLA_INCLUDE_DIRS ${QSCINTILLA_INCLUDE})
    endif()
endif()

if(NOT QSCINTILLA_FOUND)
    message(WARNING "QScintilla not found - code viewer will use plain QPlainTextEdit fallback")
    add_compile_definitions(NO_QSCINTILLA)
endif()

# libvterm (vendored)
add_subdirectory(third_party/libvterm)

set(CORE_SOURCES
    src/core/ClaudeProcess.cpp
    src/core/StreamParser.cpp
    src/core/SessionManager.cpp
    src/core/DiffEngine.cpp
    src/core/FileSnapshot.cpp
    src/core/Database.cpp
    src/core/SnapshotManager.cpp
    src/core/GitManager.cpp
    src/core/PtyProcess.cpp
    src/core/UnixPty.cpp
    src/core/WinPty.cpp
)

set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/WorkspaceTree.cpp
    src/ui/CodeViewer.cpp
    src/ui/ChatPanel.cpp
    src/ui/ChatMessageWidget.cpp
    src/ui/ToolCallGroupWidget.cpp
    src/ui/InputBar.cpp
    src/ui/ModeSelector.cpp
    src/ui/ModelSelector.cpp
    src/ui/TerminalWidget.cpp
    src/ui/QuestionWidget.cpp
    src/ui/TerminalPanel.cpp
    src/ui/GitPanel.cpp
    src/ui/DiffSplitView.cpp
    src/ui/ThinkingIndicator.cpp
    src/ui/ToastNotification.cpp
    src/ui/ToastManager.cpp
    src/ui/SettingsDialog.cpp
    src/ui/ThemeManager.cpp
    src/ui/SearchPanel.cpp
)

set(UTIL_SOURCES
    src/util/MarkdownRenderer.cpp
    src/util/JsonUtils.cpp
    src/util/Config.cpp
)

add_executable(cccpp
    src/main.cpp
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${UTIL_SOURCES}
)

target_include_directories(cccpp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(cccpp PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    vterm
)

# Platform-specific linking for PTY support
if(UNIX AND NOT APPLE)
    target_link_libraries(cccpp PRIVATE util)
endif()

if(QSCINTILLA_FOUND)
    target_include_directories(cccpp PRIVATE ${QSCINTILLA_INCLUDE_DIRS})
    target_link_libraries(cccpp PRIVATE ${QSCINTILLA_LIBRARIES})
endif()
