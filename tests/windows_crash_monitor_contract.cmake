if(NOT DEFINED MONITOR OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "Windows crash-monitor contract arguments are incomplete")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
set(pid_file "${OUTPUT_DIRECTORY}/pid.txt")
set(result_file "${OUTPUT_DIRECTORY}/result.txt")
set(dump_file "${OUTPUT_DIRECTORY}/crash.dmp")
set(argument_file "${OUTPUT_DIRECTORY}/arguments.txt")
file(WRITE "${argument_file}" "")

get_filename_component(fixture_directory "${FIXTURE}" DIRECTORY)
execute_process(
    COMMAND
        "${MONITOR}"
        "${pid_file}"
        "${result_file}"
        "${dump_file}"
        "${fixture_directory}"
        "${FIXTURE}"
        "${argument_file}"
    RESULT_VARIABLE monitor_result
    TIMEOUT 60
)
if(NOT monitor_result EQUAL 0)
    message(FATAL_ERROR "Windows crash monitor returned ${monitor_result}")
endif()
if(NOT EXISTS "${pid_file}" OR NOT EXISTS "${result_file}" OR NOT EXISTS "${dump_file}")
    message(FATAL_ERROR "Windows crash monitor did not produce every contract artifact")
endif()
file(SIZE "${dump_file}" dump_size)
if(dump_size LESS 1024)
    message(FATAL_ERROR "Windows crash monitor produced an invalid ${dump_size}-byte dump")
endif()
file(READ "${result_file}" result_text)
if(NOT result_text MATCHES "UNHANDLED_EXCEPTION_CODE: 0xe0424b53")
    message(FATAL_ERROR "Windows crash monitor did not record the contract exception:\n${result_text}")
endif()
if(NOT result_text MATCHES "DUMP: written")
    message(FATAL_ERROR "Windows crash monitor did not report a written dump:\n${result_text}")
endif()
if(NOT result_text MATCHES "MONITOR: complete")
    message(FATAL_ERROR "Windows crash monitor did not complete:\n${result_text}")
endif()
