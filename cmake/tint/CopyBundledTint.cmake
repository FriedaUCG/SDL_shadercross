cmake_minimum_required(VERSION 3.22)

foreach(required_var
	TINT_BUILD_DIR
	TINT_SOURCE_DIR
	BUNDLED_TINT_EXECUTABLE
	BUNDLED_TINT_MANIFEST
	BUNDLED_TINT_LICENSE_DIR
	BUNDLED_TINT_BUILD_TARGET
	BUNDLED_TINT_INTEGRATION
	DAWN_REPOSITORY
	DAWN_COMMIT
	DAWN_DEPS_SHA256
	UPSTREAM_FIX_CL
	UPSTREAM_FIX_CHANGE_ID
	UPSTREAM_FIX_BUG)
	if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
		message(FATAL_ERROR "CopyBundledTint.cmake requires ${required_var}")
	endif()
endforeach()
if(NOT DEFINED TINT_EXECUTABLE_SUFFIX)
	message(FATAL_ERROR "CopyBundledTint.cmake requires TINT_EXECUTABLE_SUFFIX")
endif()

file(GLOB_RECURSE tint_candidates "${TINT_BUILD_DIR}/tint${TINT_EXECUTABLE_SUFFIX}")
set(tint_executable "")
set(tint_executable_count 0)
foreach(candidate IN LISTS tint_candidates)
	if(IS_DIRECTORY "${candidate}")
		continue()
	endif()
	if(candidate MATCHES "/CMakeFiles/" OR candidate MATCHES "/_deps/")
		continue()
	endif()
	set(tint_executable "${candidate}")
	math(EXPR tint_executable_count "${tint_executable_count} + 1")
endforeach()

if(tint_executable STREQUAL "")
	message(FATAL_ERROR "Failed to find built Tint CLI under ${TINT_BUILD_DIR}")
endif()
if(NOT tint_executable_count EQUAL 1)
	message(FATAL_ERROR "Expected exactly one built Tint CLI under ${TINT_BUILD_DIR}, found ${tint_executable_count}")
endif()

get_filename_component(output_dir "${BUNDLED_TINT_EXECUTABLE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(COPY_FILE "${tint_executable}" "${BUNDLED_TINT_EXECUTABLE}" ONLY_IF_DIFFERENT)
if(UNIX)
	file(CHMOD "${BUNDLED_TINT_EXECUTABLE}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

file(SHA256 "${BUNDLED_TINT_EXECUTABLE}" bundled_tint_sha256)

file(WRITE "${BUNDLED_TINT_MANIFEST}" [=[
{
  "tool": "shadercross-tint",
  "producer": "SDL_shadercross bundled Tint",
  "tint_cli_sha256": "]=] "${bundled_tint_sha256}" [=[",
  "dawn": {
    "repository": "]=] "${DAWN_REPOSITORY}" [=[",
    "commit": "]=] "${DAWN_COMMIT}" [=[",
    "deps_sha256": "]=] "${DAWN_DEPS_SHA256}" [=["
  },
  "upstream_fix": {
    "gerrit_cl": "]=] "${UPSTREAM_FIX_CL}" [=[",
    "change_id": "]=] "${UPSTREAM_FIX_CHANGE_ID}" [=[",
    "bug": "]=] "${UPSTREAM_FIX_BUG}" [=["
  },
  "build": {
    "target": "]=] "${BUNDLED_TINT_BUILD_TARGET}" [=[",
    "integration": "]=] "${BUNDLED_TINT_INTEGRATION}" [=[",
    "dawn_fetch_dependencies": true,
    "dawn_backends": "disabled",
    "tests_samples_fuzzers_benchmarks_tintd": "disabled",
    "spv_reader": true,
    "wgsl_reader": true,
    "wgsl_writer": true
  }
}
]=])

file(REMOVE_RECURSE "${BUNDLED_TINT_LICENSE_DIR}")
file(MAKE_DIRECTORY "${BUNDLED_TINT_LICENSE_DIR}")
file(WRITE "${BUNDLED_TINT_LICENSE_DIR}/README.txt" "License notices for the bundled shadercross-tint CLI. The tool is built from the pinned Dawn source tree and dependency pins recorded in shadercross-tint-manifest.json.\n")

file(GLOB_RECURSE license_candidates
	"${TINT_SOURCE_DIR}/LICENSE"
	"${TINT_SOURCE_DIR}/LICENSE.*"
	"${TINT_SOURCE_DIR}/LICENCE"
	"${TINT_SOURCE_DIR}/LICENCE.*"
	"${TINT_SOURCE_DIR}/COPYING"
	"${TINT_SOURCE_DIR}/COPYING.*"
	"${TINT_SOURCE_DIR}/NOTICE"
	"${TINT_SOURCE_DIR}/NOTICE.*"
	"${TINT_SOURCE_DIR}/README.chromium"
)
list(REMOVE_DUPLICATES license_candidates)
set(license_skip_regex "(^|/)(\\.git|\\.cipd|out|build|cmake-build|CMakeFiles|_deps|node_modules)(/|$)")
set(copied_license_count 0)
foreach(license_file IN LISTS license_candidates)
	if(IS_DIRECTORY "${license_file}")
		continue()
	endif()
	file(RELATIVE_PATH license_rel "${TINT_SOURCE_DIR}" "${license_file}")
	if(license_rel MATCHES "${license_skip_regex}")
		continue()
	endif()
	get_filename_component(license_rel_dir "${license_rel}" DIRECTORY)
	file(MAKE_DIRECTORY "${BUNDLED_TINT_LICENSE_DIR}/${license_rel_dir}")
	file(COPY_FILE "${license_file}" "${BUNDLED_TINT_LICENSE_DIR}/${license_rel}")
	math(EXPR copied_license_count "${copied_license_count} + 1")
endforeach()
if(copied_license_count EQUAL 0)
	message(FATAL_ERROR "Failed to collect bundled Tint license notices from ${TINT_SOURCE_DIR}")
endif()
