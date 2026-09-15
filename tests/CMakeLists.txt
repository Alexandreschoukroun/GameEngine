find_package(doctest CONFIG REQUIRED)

add_executable(engine_tests
    core/test_time.cpp
)

target_link_libraries(engine_tests PRIVATE engine::core doctest::doctest)

set_project_warnings(engine_tests)

include(CTest)
add_test(NAME engine_tests COMMAND engine_tests)
