cmake_minimum_required(VERSION 3.22)

foreach(required_var CMAKE_COMMAND_PATH TINT_CONFIGURE_COMMAND_FILE TINT_BUILD_DIR TINT_BUILD_TYPE TINT_BUILD_TARGET TINT_PARALLEL_LEVEL)
	if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
		message(FATAL_ERROR "BuildDawnTint.cmake requires ${required_var}")
	endif()
endforeach()
if(NOT EXISTS "${TINT_CONFIGURE_COMMAND_FILE}")
	message(FATAL_ERROR "BuildDawnTint.cmake could not find configure command file: ${TINT_CONFIGURE_COMMAND_FILE}")
endif()

include("${TINT_CONFIGURE_COMMAND_FILE}")
if(NOT DEFINED TINT_CONFIGURE_COMMAND)
	message(FATAL_ERROR "BuildDawnTint.cmake configure command file did not define TINT_CONFIGURE_COMMAND")
endif()

execute_process(
	COMMAND ${TINT_CONFIGURE_COMMAND}
	RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
	message(FATAL_ERROR "Bundled Tint configure failed")
endif()

execute_process(
	COMMAND "${CMAKE_COMMAND_PATH}" --build "${TINT_BUILD_DIR}" --config "${TINT_BUILD_TYPE}" --target "${TINT_BUILD_TARGET}" --parallel "${TINT_PARALLEL_LEVEL}"
	RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
	message(FATAL_ERROR "Bundled Tint build failed")
endif()
