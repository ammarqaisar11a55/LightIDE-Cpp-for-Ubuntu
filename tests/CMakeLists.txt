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
