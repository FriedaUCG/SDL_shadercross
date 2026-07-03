include(ExternalProject)
include(ProcessorCount)

enable_language(CXX)

find_package(Git REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(GO_EXECUTABLE NAMES go)
if(NOT GO_EXECUTABLE)
	message(FATAL_ERROR "SDL_shadercross bundled Tint requires Go at build time because the pinned Dawn/Tint generator pipeline uses it")
endif()

set(SDLSHADERCROSS_BUNDLED_TINT_PINS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tint/dawn-tint-pins.json")
if(NOT EXISTS "${SDLSHADERCROSS_BUNDLED_TINT_PINS_FILE}")
	message(FATAL_ERROR "SDL_shadercross bundled Tint requires ${SDLSHADERCROSS_BUNDLED_TINT_PINS_FILE}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SDLSHADERCROSS_BUNDLED_TINT_PINS_FILE}")
file(READ "${SDLSHADERCROSS_BUNDLED_TINT_PINS_FILE}" SDL_shadercross_bundled_tint_pins_json)
function(sdl_shadercross_read_bundled_tint_pin out_var)
	string(JSON value ERROR_VARIABLE json_error GET "${SDL_shadercross_bundled_tint_pins_json}" ${ARGN})
	if(NOT json_error STREQUAL "NOTFOUND")
		message(FATAL_ERROR "Failed to read ${SDLSHADERCROSS_BUNDLED_TINT_PINS_FILE} key '${ARGN}': ${json_error}")
	endif()
	if(value STREQUAL "")
		message(FATAL_ERROR "SDL_shadercross bundled Tint pin '${ARGN}' must not be empty")
	endif()
	set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

function(sdl_shadercross_sha256_text_file out_var path)
	file(READ "${path}" contents)
	string(REPLACE "\r\n" "\n" contents "${contents}")
	string(REPLACE "\r" "\n" contents "${contents}")
	string(SHA256 hash "${contents}")
	set(${out_var} "${hash}" PARENT_SCOPE)
endfunction()

sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_DAWN_REPOSITORY dawn repository)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_DAWN_COMMIT dawn commit)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_DAWN_DEPS_SHA256 dawn deps_sha256)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_CL upstream_fix gerrit_cl)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_CHANGE_ID upstream_fix change_id)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_BUG upstream_fix bug)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_BUILD_TARGET build target)
sdl_shadercross_read_bundled_tint_pin(SDLSHADERCROSS_BUNDLED_TINT_INTEGRATION build integration)
set(SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/dawn" CACHE PATH "Pinned Dawn checkout used to stage the bundled Tint CLI build")
set(SDLSHADERCROSS_BUNDLED_TINT_BUILD_TYPE "Release" CACHE STRING "Build type used for the isolated bundled Tint ExternalProject")
ProcessorCount(SDL_shadercross_bundled_tint_processor_count)
if(NOT SDL_shadercross_bundled_tint_processor_count OR SDL_shadercross_bundled_tint_processor_count LESS 1)
	set(SDL_shadercross_bundled_tint_processor_count 1)
endif()
set(SDLSHADERCROSS_BUNDLED_TINT_PARALLEL_LEVEL "${SDL_shadercross_bundled_tint_processor_count}" CACHE STRING "Parallel build level for the isolated bundled Tint ExternalProject")

if(NOT EXISTS "${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "SDL_shadercross bundled Tint requires the pinned top-level Dawn submodule at ${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}; run SDL_SHADERCROSS_DOWNLOAD_DAWN=1 external/download.sh, external/download.sh --with-dawn, external/Get-GitModules.ps1 -WithDawn, or git submodule update --init external/dawn")
endif()

sdl_shadercross_sha256_text_file(dawn_deps_sha256 "${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}/DEPS")
if(NOT dawn_deps_sha256 STREQUAL SDLSHADERCROSS_BUNDLED_TINT_DAWN_DEPS_SHA256)
	message(FATAL_ERROR "SDL_shadercross bundled Tint Dawn canonical DEPS hash mismatch: expected ${SDLSHADERCROSS_BUNDLED_TINT_DAWN_DEPS_SHA256}, got ${dawn_deps_sha256}")
endif()

set(SDLSHADERCROSS_BUNDLED_TINT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/bundled-tint")
set(SDLSHADERCROSS_BUNDLED_TINT_STAGE_DIR "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/src/dawn")
set(SDLSHADERCROSS_BUNDLED_TINT_DAWN_BUILD_DIR "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/build/dawn")
set(SDLSHADERCROSS_BUNDLED_TINT_EXECUTABLE "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/bin/shadercross-tint${CMAKE_EXECUTABLE_SUFFIX}" CACHE FILEPATH "Bundled Tint executable" FORCE)
set(SDLSHADERCROSS_BUNDLED_TINT_MANIFEST "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/shadercross-tint-manifest.json" CACHE FILEPATH "Bundled Tint provenance manifest" FORCE)
set(SDLSHADERCROSS_BUNDLED_TINT_LICENSE_DIR "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/licenses" CACHE PATH "Bundled Tint license notice directory" FORCE)
set(SDLSHADERCROSS_TINT_EXECUTABLE "${SDLSHADERCROSS_BUNDLED_TINT_EXECUTABLE}" CACHE FILEPATH "Tint executable used by optional WGSL CLI output tests" FORCE)
set(SDLSHADERCROSS_BUNDLED_TINT_CONFIGURE_COMMAND_FILE "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/dawn-tint-configure-command.cmake")
file(MAKE_DIRECTORY "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}" "${SDLSHADERCROSS_BUNDLED_TINT_LICENSE_DIR}")

set(SDL_shadercross_bundled_tint_cmake_args
	"-DCMAKE_BUILD_TYPE=${SDLSHADERCROSS_BUNDLED_TINT_BUILD_TYPE}"
	"-DCMAKE_CXX_STANDARD=20"
	"-DDAWN_FETCH_DEPENDENCIES=ON"
	"-DDAWN_ENABLE_INSTALL=OFF"
	"-DDAWN_BUILD_BENCHMARKS=OFF"
	"-DDAWN_BUILD_FUZZERS=OFF"
	"-DDAWN_BUILD_MONOLITHIC_LIBRARY=OFF"
	"-DDAWN_BUILD_NODE_BINDINGS=OFF"
	"-DDAWN_BUILD_PROTOBUF=OFF"
	"-DDAWN_BUILD_SAMPLES=OFF"
	"-DDAWN_BUILD_TESTS=OFF"
	"-DDAWN_ENABLE_D3D11=OFF"
	"-DDAWN_ENABLE_D3D12=OFF"
	"-DDAWN_ENABLE_DESKTOP_GL=OFF"
	"-DDAWN_ENABLE_METAL=OFF"
	"-DDAWN_ENABLE_NULL=OFF"
	"-DDAWN_ENABLE_OPENGLES=OFF"
	"-DDAWN_ENABLE_SPIRV_VALIDATION=OFF"
	"-DDAWN_ENABLE_VULKAN=OFF"
	"-DDAWN_ENABLE_WEBGPU_ON_WEBGPU=OFF"
	"-DDAWN_USE_GLFW=OFF"
	"-DDAWN_USE_WINDOWS_UI=OFF"
	"-DDAWN_USE_WAYLAND=OFF"
	"-DDAWN_USE_X11=OFF"
	"-DGO_EXECUTABLE=${GO_EXECUTABLE}"
	"-DTINT_BUILD_BENCHMARKS=OFF"
	"-DTINT_BUILD_CMD_TOOLS=ON"
	"-DTINT_BUILD_FUZZERS=OFF"
	"-DTINT_BUILD_GLSL_VALIDATOR=OFF"
	"-DTINT_BUILD_GLSL_WRITER=OFF"
	"-DTINT_BUILD_HLSL_WRITER=OFF"
	"-DTINT_BUILD_IR_BINARY=OFF"
	"-DTINT_BUILD_MSL_WRITER=OFF"
	"-DTINT_BUILD_NULL_WRITER=OFF"
	"-DTINT_BUILD_SPV_READER=ON"
	"-DTINT_BUILD_SPV_WRITER=OFF"
	"-DTINT_BUILD_TESTS=OFF"
	"-DTINT_BUILD_TINTD=OFF"
	"-DTINT_BUILD_WGSL_READER=ON"
	"-DTINT_BUILD_WGSL_WRITER=ON"
)

set(SDL_shadercross_bundled_tint_source_path "SDL_shadercross_bundled_tint/source")
set(SDL_shadercross_bundled_tint_build_path "SDL_shadercross_bundled_tint/build")
set(SDL_shadercross_bundled_tint_pathmap_flags)
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	list(APPEND SDL_shadercross_bundled_tint_pathmap_flags
		"/experimental:deterministic"
		"/pathmap:${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}=${SDL_shadercross_bundled_tint_source_path}"
		"/pathmap:${SDLSHADERCROSS_BUNDLED_TINT_ROOT}=${SDL_shadercross_bundled_tint_build_path}"
	)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
	list(APPEND SDL_shadercross_bundled_tint_pathmap_flags
		"-ffile-prefix-map=${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}=${SDL_shadercross_bundled_tint_source_path}"
		"-ffile-prefix-map=${SDLSHADERCROSS_BUNDLED_TINT_ROOT}=${SDL_shadercross_bundled_tint_build_path}"
		"-fmacro-prefix-map=${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}=${SDL_shadercross_bundled_tint_source_path}"
		"-fmacro-prefix-map=${SDLSHADERCROSS_BUNDLED_TINT_ROOT}=${SDL_shadercross_bundled_tint_build_path}"
		"-fdebug-prefix-map=${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}=${SDL_shadercross_bundled_tint_source_path}"
		"-fdebug-prefix-map=${SDLSHADERCROSS_BUNDLED_TINT_ROOT}=${SDL_shadercross_bundled_tint_build_path}"
	)
endif()
if(SDL_shadercross_bundled_tint_pathmap_flags)
	set(SDL_shadercross_bundled_tint_c_flags "${CMAKE_C_FLAGS}")
	set(SDL_shadercross_bundled_tint_cxx_flags "${CMAKE_CXX_FLAGS}")
	foreach(pathmap_flag IN LISTS SDL_shadercross_bundled_tint_pathmap_flags)
		string(APPEND SDL_shadercross_bundled_tint_c_flags " ${pathmap_flag}")
		string(APPEND SDL_shadercross_bundled_tint_cxx_flags " ${pathmap_flag}")
	endforeach()
	list(APPEND SDL_shadercross_bundled_tint_cmake_args
		"-DCMAKE_C_FLAGS=${SDL_shadercross_bundled_tint_c_flags}"
		"-DCMAKE_CXX_FLAGS=${SDL_shadercross_bundled_tint_cxx_flags}"
	)
endif()

foreach(language C CXX)
	if(CMAKE_${language}_COMPILER)
		list(APPEND SDL_shadercross_bundled_tint_cmake_args "-DCMAKE_${language}_COMPILER=${CMAKE_${language}_COMPILER}")
	endif()
endforeach()
if(CMAKE_TOOLCHAIN_FILE)
	list(APPEND SDL_shadercross_bundled_tint_cmake_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
set(SDL_shadercross_bundled_tint_osx_architectures_arg "")
if(APPLE)
	if(CMAKE_OSX_DEPLOYMENT_TARGET)
		list(APPEND SDL_shadercross_bundled_tint_cmake_args "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
	endif()
	if(CMAKE_OSX_ARCHITECTURES)
		string(REPLACE ";" "\\;" SDL_shadercross_bundled_tint_osx_architectures "${CMAKE_OSX_ARCHITECTURES}")
		set(SDL_shadercross_bundled_tint_osx_architectures_arg "-DCMAKE_OSX_ARCHITECTURES=${SDL_shadercross_bundled_tint_osx_architectures}")
	endif()
endif()

set(SDL_shadercross_bundled_tint_configure_command
	"${CMAKE_COMMAND}" -S "${SDLSHADERCROSS_BUNDLED_TINT_STAGE_DIR}" -B "${SDLSHADERCROSS_BUNDLED_TINT_DAWN_BUILD_DIR}" -G "${CMAKE_GENERATOR}"
)
if(CMAKE_GENERATOR_PLATFORM)
	list(APPEND SDL_shadercross_bundled_tint_configure_command -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
	list(APPEND SDL_shadercross_bundled_tint_configure_command -T "${CMAKE_GENERATOR_TOOLSET}")
endif()
foreach(configure_arg IN LISTS SDL_shadercross_bundled_tint_cmake_args)
	list(APPEND SDL_shadercross_bundled_tint_configure_command "${configure_arg}")
endforeach()

file(WRITE "${SDLSHADERCROSS_BUNDLED_TINT_CONFIGURE_COMMAND_FILE}" "set(TINT_CONFIGURE_COMMAND\n")
foreach(configure_arg IN LISTS SDL_shadercross_bundled_tint_configure_command)
	set(serialized_configure_arg "${configure_arg}")
	string(REPLACE ";" "\\;" serialized_configure_arg "${serialized_configure_arg}")
	file(APPEND "${SDLSHADERCROSS_BUNDLED_TINT_CONFIGURE_COMMAND_FILE}" "  [==[${serialized_configure_arg}]==]\n")
endforeach()
if(NOT SDL_shadercross_bundled_tint_osx_architectures_arg STREQUAL "")
	file(APPEND "${SDLSHADERCROSS_BUNDLED_TINT_CONFIGURE_COMMAND_FILE}" "  [==[${SDL_shadercross_bundled_tint_osx_architectures_arg}]==]\n")
endif()
file(APPEND "${SDLSHADERCROSS_BUNDLED_TINT_CONFIGURE_COMMAND_FILE}" ")\n")

ExternalProject_Add(SDL_shadercross_bundled_tint
	PREFIX "${SDLSHADERCROSS_BUNDLED_TINT_ROOT}/external-project"
	SOURCE_DIR "${SDLSHADERCROSS_BUNDLED_TINT_STAGE_DIR}"
	BINARY_DIR "${SDLSHADERCROSS_BUNDLED_TINT_DAWN_BUILD_DIR}"
	DOWNLOAD_COMMAND
		"${CMAKE_COMMAND}"
		"-DDAWN_SOURCE_DIR=${SDLSHADERCROSS_BUNDLED_TINT_SOURCE_DIR}"
		"-DDAWN_STAGE_DIR=<SOURCE_DIR>"
		"-DDAWN_COMMIT=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_COMMIT}"
		"-DDAWN_DEPS_SHA256=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_DEPS_SHA256}"
		"-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tint/StageDawnTint.cmake"
	UPDATE_COMMAND ""
	CONFIGURE_COMMAND ""
	USES_TERMINAL_BUILD TRUE
	BUILD_COMMAND
		"${CMAKE_COMMAND}"
		"-DCMAKE_COMMAND_PATH=${CMAKE_COMMAND}"
		"-DTINT_CONFIGURE_COMMAND_FILE=${SDLSHADERCROSS_BUNDLED_TINT_CONFIGURE_COMMAND_FILE}"
		"-DTINT_BUILD_DIR=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_BUILD_DIR}"
		"-DTINT_BUILD_TYPE=${SDLSHADERCROSS_BUNDLED_TINT_BUILD_TYPE}"
		"-DTINT_BUILD_TARGET=${SDLSHADERCROSS_BUNDLED_TINT_BUILD_TARGET}"
		"-DTINT_PARALLEL_LEVEL=${SDLSHADERCROSS_BUNDLED_TINT_PARALLEL_LEVEL}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tint/BuildDawnTint.cmake"
	INSTALL_COMMAND
		"${CMAKE_COMMAND}"
		"-DTINT_BUILD_DIR=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_BUILD_DIR}"
		"-DTINT_SOURCE_DIR=${SDLSHADERCROSS_BUNDLED_TINT_STAGE_DIR}"
		"-DTINT_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
		"-DBUNDLED_TINT_EXECUTABLE=${SDLSHADERCROSS_BUNDLED_TINT_EXECUTABLE}"
		"-DBUNDLED_TINT_MANIFEST=${SDLSHADERCROSS_BUNDLED_TINT_MANIFEST}"
		"-DBUNDLED_TINT_LICENSE_DIR=${SDLSHADERCROSS_BUNDLED_TINT_LICENSE_DIR}"
		"-DBUNDLED_TINT_BUILD_TARGET=${SDLSHADERCROSS_BUNDLED_TINT_BUILD_TARGET}"
		"-DBUNDLED_TINT_INTEGRATION=${SDLSHADERCROSS_BUNDLED_TINT_INTEGRATION}"
		"-DDAWN_REPOSITORY=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_REPOSITORY}"
		"-DDAWN_COMMIT=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_COMMIT}"
		"-DDAWN_DEPS_SHA256=${SDLSHADERCROSS_BUNDLED_TINT_DAWN_DEPS_SHA256}"
		"-DUPSTREAM_FIX_CL=${SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_CL}"
		"-DUPSTREAM_FIX_CHANGE_ID=${SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_CHANGE_ID}"
		"-DUPSTREAM_FIX_BUG=${SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_BUG}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tint/CopyBundledTint.cmake"
	BUILD_BYPRODUCTS
		"${SDLSHADERCROSS_BUNDLED_TINT_EXECUTABLE}"
		"${SDLSHADERCROSS_BUNDLED_TINT_MANIFEST}"
)

message(STATUS "SDL_shadercross: bundled Tint enabled; Dawn ${SDLSHADERCROSS_BUNDLED_TINT_DAWN_COMMIT} includes upstream row-major fix CL ${SDLSHADERCROSS_BUNDLED_TINT_UPSTREAM_FIX_CL}")
