find_package(Qt6 REQUIRED COMPONENTS Test)

function(lightide_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE lightide_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endfunction()

lightide_add_test(test_diagnostics)
lightide_add_test(test_compile_command)
lightide_add_test(test_terminal_filter)
lightide_add_test(test_mi_parser)
lightide_add_test(test_files)
lightide_add_test(test_settings)
lightide_add_test(test_project)
lightide_add_test(test_build)
lightide_add_test(test_runner)
lightide_add_test(test_debugger)
lightide_add_test(test_symbols)

lightide_add_test(test_leetcode)
target_compile_definitions(test_leetcode PRIVATE
    LIGHTIDE_LEETCODE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/leetcode")

qt_add_executable(test_ui test_ui.cpp)
target_link_libraries(test_ui PRIVATE lightide_ui Qt6::Test)
target_compile_definitions(test_ui PRIVATE
    LIGHTIDE_LEETCODE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/leetcode")
add_test(NAME test_ui COMMAND test_ui)
set_tests_properties(test_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300)
