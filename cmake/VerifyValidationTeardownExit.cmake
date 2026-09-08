if(NOT DEFINED SOKOBAN_EXECUTABLE OR SOKOBAN_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "SOKOBAN_EXECUTABLE must name the application")
endif()
if(NOT DEFINED SOKOBAN_SAVE_DIRECTORY OR SOKOBAN_SAVE_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "SOKOBAN_SAVE_DIRECTORY must name an isolated test directory")
endif()

get_filename_component(SOKOBAN_EXECUTABLE_DIRECTORY
    "${SOKOBAN_EXECUTABLE}" DIRECTORY)
file(REMOVE_RECURSE "${SOKOBAN_SAVE_DIRECTORY}")
file(MAKE_DIRECTORY "${SOKOBAN_SAVE_DIRECTORY}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "SOKOBAN_TEST_VALIDATION_ERROR_ON_TEARDOWN=1"
        "${SOKOBAN_EXECUTABLE}"
        --smoke-frames 3
        --save-directory "${SOKOBAN_SAVE_DIRECTORY}"
    WORKING_DIRECTORY "${SOKOBAN_EXECUTABLE_DIRECTORY}"
    RESULT_VARIABLE SOKOBAN_RESULT
    OUTPUT_VARIABLE SOKOBAN_STDOUT
    ERROR_VARIABLE SOKOBAN_STDERR
    TIMEOUT 30
)

if(NOT SOKOBAN_RESULT MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR
        "Application did not exit normally: ${SOKOBAN_RESULT}\n"
        "stdout:\n${SOKOBAN_STDOUT}\n"
        "stderr:\n${SOKOBAN_STDERR}")
endif()
if(NOT SOKOBAN_RESULT EQUAL 3)
    message(FATAL_ERROR
        "Expected teardown validation exit code 3, got ${SOKOBAN_RESULT}\n"
        "stdout:\n${SOKOBAN_STDOUT}\n"
        "stderr:\n${SOKOBAN_STDERR}")
endif()
