cmake_minimum_required(VERSION 3.22)

foreach(required_var DAWN_SOURCE_DIR DAWN_STAGE_DIR DAWN_COMMIT DAWN_DEPS_SHA256 GIT_EXECUTABLE)
	if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
		message(FATAL_ERROR "StageDawnTint.cmake requires ${required_var}")
	endif()
endforeach()

execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${DAWN_SOURCE_DIR}" rev-parse HEAD
	OUTPUT_VARIABLE actual_commit
	OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE git_result
)
if(NOT git_result EQUAL 0)
	message(FATAL_ERROR "Failed to inspect Dawn checkout at ${DAWN_SOURCE_DIR}")
endif()
if(NOT actual_commit STREQUAL DAWN_COMMIT)
	message(FATAL_ERROR "Pinned Dawn checkout mismatch: expected ${DAWN_COMMIT}, got ${actual_commit}")
endif()

file(READ "${DAWN_SOURCE_DIR}/DEPS" deps_contents)
string(REPLACE "\r\n" "\n" deps_contents "${deps_contents}")
string(REPLACE "\r" "\n" deps_contents "${deps_contents}")
string(SHA256 actual_deps_sha256 "${deps_contents}")
if(NOT actual_deps_sha256 STREQUAL DAWN_DEPS_SHA256)
	message(FATAL_ERROR "Pinned Dawn canonical DEPS mismatch: expected ${DAWN_DEPS_SHA256}, got ${actual_deps_sha256}")
endif()

set(archive_path "${DAWN_STAGE_DIR}.tar")
file(REMOVE_RECURSE "${DAWN_STAGE_DIR}")
file(REMOVE "${archive_path}")
file(MAKE_DIRECTORY "${DAWN_STAGE_DIR}")

execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${DAWN_SOURCE_DIR}" archive --format=tar "--output=${archive_path}" "${DAWN_COMMIT}"
	RESULT_VARIABLE archive_result
)
if(NOT archive_result EQUAL 0)
	message(FATAL_ERROR "Failed to archive Dawn ${DAWN_COMMIT} from ${DAWN_SOURCE_DIR}")
endif()

execute_process(
	COMMAND "${CMAKE_COMMAND}" -E tar xf "${archive_path}"
	WORKING_DIRECTORY "${DAWN_STAGE_DIR}"
	RESULT_VARIABLE extract_result
)
file(REMOVE "${archive_path}")
if(NOT extract_result EQUAL 0)
	message(FATAL_ERROR "Failed to extract Dawn staging archive into ${DAWN_STAGE_DIR}")
endif()
