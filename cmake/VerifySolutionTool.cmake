if(NOT DEFINED SOKOBAN_SOLVE_EXECUTABLE OR SOKOBAN_SOLVE_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "SOKOBAN_SOLVE_EXECUTABLE must name the solution tool")
endif()
if(NOT DEFINED SOKOBAN_TEST_DIRECTORY OR SOKOBAN_TEST_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "SOKOBAN_TEST_DIRECTORY must name a dedicated test directory")
endif()
get_filename_component(SOKOBAN_SOLVE_EXECUTABLE
    "${SOKOBAN_SOLVE_EXECUTABLE}" ABSOLUTE)
if(NOT EXISTS "${SOKOBAN_SOLVE_EXECUTABLE}" OR IS_DIRECTORY "${SOKOBAN_SOLVE_EXECUTABLE}")
    message(FATAL_ERROR "Solution tool does not exist: ${SOKOBAN_SOLVE_EXECUTABLE}")
endif()
get_filename_component(SOKOBAN_TEST_DIRECTORY "${SOKOBAN_TEST_DIRECTORY}" ABSOLUTE)
file(MAKE_DIRECTORY "${SOKOBAN_TEST_DIRECTORY}")

# Each invocation owns a fresh child and retains its small fixtures/logs for
# diagnostics. Never delete an existing test directory or another run's files.
set(SOKOBAN_FIXTURE_DIRECTORY "")
foreach(attempt RANGE 1 20)
    string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef fixture_id)
    set(candidate "${SOKOBAN_TEST_DIRECTORY}/solution-tool-${fixture_id}")
    if(NOT EXISTS "${candidate}")
        set(SOKOBAN_FIXTURE_DIRECTORY "${candidate}")
        file(MAKE_DIRECTORY "${SOKOBAN_FIXTURE_DIRECTORY}")
        break()
    endif()
endforeach()
if(SOKOBAN_FIXTURE_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "Could not allocate a unique solution-tool fixture directory")
endif()

set(SOKOBAN_LEVELS_DIRECTORY "${SOKOBAN_FIXTURE_DIRECTORY}/levels")
set(SOKOBAN_SOLUTIONS_DIRECTORY "${SOKOBAN_FIXTURE_DIRECTORY}/solutions")
set(SOKOBAN_RECORDING "${SOKOBAN_SOLUTIONS_DIRECTORY}/level0-screen0.solution")
file(MAKE_DIRECTORY "${SOKOBAN_LEVELS_DIRECTORY}/level0")
file(WRITE "${SOKOBAN_LEVELS_DIRECTORY}/level0/screen0.scr"
    "@layer 0\n...\n\n@layer 1\nC E\n")

function(run_solution_tool step expected_result)
    execute_process(
        COMMAND "${SOKOBAN_SOLVE_EXECUTABLE}"
            "${SOKOBAN_LEVELS_DIRECTORY}" "${SOKOBAN_SOLUTIONS_DIRECTORY}"
            --level 0 --screen 0 ${ARGN}
        WORKING_DIRECTORY "${SOKOBAN_FIXTURE_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 20
    )
    file(WRITE "${SOKOBAN_FIXTURE_DIRECTORY}/${step}.stdout.txt" "${stdout}")
    file(WRITE "${SOKOBAN_FIXTURE_DIRECTORY}/${step}.stderr.txt" "${stderr}")
    set(SOKOBAN_LAST_STDOUT "${stdout}" PARENT_SCOPE)
    set(SOKOBAN_LAST_STDERR "${stderr}" PARENT_SCOPE)
    if(NOT "${result}" STREQUAL "${expected_result}")
        message(FATAL_ERROR
            "${step}: expected exit ${expected_result}, got ${result}\n"
            "Fixture: ${SOKOBAN_FIXTURE_DIRECTORY}\n"
            "stdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
endfunction()

function(solution_tool_failure detail)
    message(FATAL_ERROR
        "${detail}\nFixture: ${SOKOBAN_FIXTURE_DIRECTORY}\n"
        "stdout:\n${SOKOBAN_LAST_STDOUT}\nstderr:\n${SOKOBAN_LAST_STDERR}")
endfunction()

function(require_recording_sha step expected_sha)
    if(NOT EXISTS "${SOKOBAN_RECORDING}")
        solution_tool_failure("${step}: the recording is missing")
    endif()
    file(SHA256 "${SOKOBAN_RECORDING}" actual_sha)
    if(NOT "${actual_sha}" STREQUAL "${expected_sha}")
        solution_tool_failure(
            "${step}: recording SHA256 changed unexpectedly; expected ${expected_sha}, got ${actual_sha}")
    endif()
endfunction()

run_solution_tool(initial-search 0 --max-states 1000)
if(NOT EXISTS "${SOKOBAN_RECORDING}")
    solution_tool_failure("Initial bounded search did not publish a recording")
endif()
file(READ "${SOKOBAN_RECORDING}" SOKOBAN_BASELINE_TEXT)
file(SHA256 "${SOKOBAN_RECORDING}" SOKOBAN_BASELINE_SHA)
string(REGEX MATCH "level-digest [0-9a-fA-F]+" digest_line "${SOKOBAN_BASELINE_TEXT}")
string(LENGTH "${digest_line}" digest_line_length)
if(NOT digest_line_length EQUAL 29)
    solution_tool_failure("Initial recording does not have a 16-digit level digest")
endif()

# A one-state search cannot solve the two-input fixture. Success here must
# come from replaying the unchanged current recording before skipping search.
run_solution_tool(valid-current-skip 0 --max-states 1)
require_recording_sha(valid-current-skip "${SOKOBAN_BASELINE_SHA}")
if(NOT SOKOBAN_LAST_STDOUT MATCHES "already solved by")
    solution_tool_failure("A passing current recording was not reported as already solved")
endif()

# Alter only expected state. The digest and inputs still match the current
# level, so blindly trusting the digest would produce a false successful skip.
string(REPLACE "\"quarterTurns\":0" "\"quarterTurns\":1"
    corrupted_text "${SOKOBAN_BASELINE_TEXT}")
if(corrupted_text STREQUAL SOKOBAN_BASELINE_TEXT)
    solution_tool_failure("Fixture recording contains no zero quarterTurns to corrupt")
endif()
file(WRITE "${SOKOBAN_RECORDING}" "${corrupted_text}")
file(SHA256 "${SOKOBAN_RECORDING}" corrupted_sha)
run_solution_tool(invalid-current-preserved 1 --max-states 1)
require_recording_sha(invalid-current-preserved "${corrupted_sha}")
if(NOT SOKOBAN_LAST_STDOUT MATCHES "failed verification")
    solution_tool_failure("The default command did not report the invalid current replay")
endif()

run_solution_tool(refresh-current-state 0 --refresh-existing --max-states 1)
require_recording_sha(refresh-current-state "${SOKOBAN_BASELINE_SHA}")
if(NOT SOKOBAN_LAST_STDOUT MATCHES "refreshed 2 existing input steps")
    solution_tool_failure("Refreshing current inputs did not report a two-step repair")
endif()

string(REPLACE "${digest_line}" "level-digest 0000000000000000"
    stale_text "${SOKOBAN_BASELINE_TEXT}")
if(stale_text STREQUAL SOKOBAN_BASELINE_TEXT)
    solution_tool_failure("The stale-digest fixture did not change the recording")
endif()
file(WRITE "${SOKOBAN_RECORDING}" "${stale_text}")
run_solution_tool(refresh-stale-digest 0 --refresh-existing --max-states 1)
require_recording_sha(refresh-stale-digest "${SOKOBAN_BASELINE_SHA}")

file(WRITE "${SOKOBAN_RECORDING}" "format invalid\n")
file(SHA256 "${SOKOBAN_RECORDING}" malformed_sha)
run_solution_tool(malformed-preserved 1 --refresh-existing --max-states 1)
require_recording_sha(malformed-preserved "${malformed_sha}")
if(NOT SOKOBAN_LAST_STDOUT MATCHES "cannot refresh")
    solution_tool_failure("Malformed input was not reported before bounded search fallback")
endif()

run_solution_tool(malformed-search-repair 0 --refresh-existing --max-states 1000)
require_recording_sha(malformed-search-repair "${SOKOBAN_BASELINE_SHA}")
run_solution_tool(repaired-current-skip 0 --max-states 1)
require_recording_sha(repaired-current-skip "${SOKOBAN_BASELINE_SHA}")
if(NOT SOKOBAN_LAST_STDOUT MATCHES "already solved by")
    solution_tool_failure("The search-repaired recording did not pass default replay verification")
endif()

message(STATUS "Solution-tool search, verified skip, refresh and failure preservation passed; fixtures: ${SOKOBAN_FIXTURE_DIRECTORY}")
