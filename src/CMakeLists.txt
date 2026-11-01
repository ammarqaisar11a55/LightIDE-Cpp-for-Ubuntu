add_library(lightide_core STATIC
    compiler/BuildProcess.cpp
    compiler/BuildProcess.h
    compiler/CompileCommand.cpp
    compiler/CompileCommand.h
    compiler/CompilerManager.cpp
    compiler/CompilerManager.h
    compiler/Diagnostic.h
    compiler/DiagnosticsParser.cpp
    compiler/DiagnosticsParser.h
    core/Settings.cpp
    core/Settings.h
    core/SymbolScanner.cpp
    core/SymbolScanner.h
    debugger/DebuggerManager.cpp
    debugger/DebuggerManager.h
    debugger/MiParser.cpp
    debugger/MiParser.h
    execution/ProcessRunner.cpp
    execution/ProcessRunner.h
    execution/PtyChannel.cpp
    execution/PtyChannel.h
    execution/TerminalFilter.cpp
    execution/TerminalFilter.h
    filesystem/FileManager.cpp
    filesystem/FileManager.h
    filesystem/FileSearch.cpp
    filesystem/FileSearch.h
    project/Project.cpp
    project/Project.h
)

target_include_directories(lightide_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lightide_core PUBLIC Qt6::Core)
target_compile_options(lightide_core PRIVATE -Wall -Wextra)
target_compile_definitions(lightide_core PUBLIC
    QT_NO_CAST_TO_ASCII
    QT_DISABLE_DEPRECATED_BEFORE=0x060400)

add_library(lightide_ui STATIC
    editor/CodeEditor.cpp
    editor/CodeEditor.h
    editor/SyntaxHighlighter.cpp
    editor/SyntaxHighlighter.h
    ui/DebugPanel.cpp
    ui/DebugPanel.h
    ui/EditorWidget.cpp
    ui/EditorWidget.h
    ui/FindBar.cpp
    ui/FindBar.h
    ui/FindInFilesDialog.cpp
    ui/FindInFilesDialog.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/OutputPanel.cpp
    ui/OutputPanel.h
    ui/ProjectDialogs.cpp
    ui/ProjectDialogs.h
    ui/SettingsDialog.cpp
    ui/SettingsDialog.h
    ui/Sidebar.cpp
    ui/Sidebar.h
    ui/TerminalView.cpp
    ui/TerminalView.h
    ui/Theme.cpp
    ui/Theme.h
    ui/WelcomeWidget.cpp
    ui/WelcomeWidget.h
    ${PROJECT_SOURCE_DIR}/resources/lightide.qrc
)

target_link_libraries(lightide_ui PUBLIC
    lightide_core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    QScintilla::QScintilla)
target_compile_options(lightide_ui PRIVATE -Wall -Wextra)

qt_add_executable(lightide main.cpp)
target_link_libraries(lightide PRIVATE lightide_ui)
target_compile_definitions(lightide PRIVATE LIGHTIDE_VERSION="${PROJECT_VERSION}")
set_target_properties(lightide PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

include(GNUInstallDirs)
install(TARGETS lightide RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/resources/lightide.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES ${PROJECT_SOURCE_DIR}/resources/lightide.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
