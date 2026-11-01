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
