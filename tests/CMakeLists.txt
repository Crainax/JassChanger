add_executable(golden_runner golden_runner.cpp)

add_test(
    NAME golden_fixtures
    COMMAND golden_runner $<TARGET_FILE:vjassc> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures ${CMAKE_BINARY_DIR}/test-output
)
