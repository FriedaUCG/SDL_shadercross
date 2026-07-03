enable_testing()

set(shader_sources
    "shaders/simple.vert.hlsl"
)

find_package(Python3 COMPONENTS Interpreter)

set(includeable_shader_sources)
foreach(src IN LISTS shader_sources)
    get_filename_component(src_name "${src}" NAME)
    set(src_path "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    set(hdr_path "${CMAKE_CURRENT_SOURCE_DIR}/shaders/hinc/${src_name}.h")
    list(APPEND includeable_shader_sources "${hdr_path}")
    if(Python3_VERSION VERSION_GREATER_EQUAL "3.11")
        add_custom_command(OUTPUT "${hdr_path}"
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/embed.py"
                -i "${src_path}" -o "${hdr_path}" -z
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/embed.py" "${src_path}"
        )
    endif()
endforeach()

find_package(SDL3 REQUIRED COMPONENTS SDL3_test)

add_executable(testshadercross
    main.c
    ${includeable_shader_sources}
)
sdl_add_warning_options(testshadercross)
target_link_libraries(testshadercross PRIVATE SDL3_shadercross::SDL3_shadercross)
target_link_libraries(testshadercross PRIVATE SDL3::SDL3_test)
target_link_libraries(testshadercross PRIVATE SDL3::SDL3)

set(test_args)
if(SDLSHADERCROSS_TESTS_TRACKMEM)
    list(APPEND test_args "--trackmem")
endif()
add_test(NAME testshadercross COMMAND testshadercross ${test_args})
set_property(TEST testshadercross PROPERTY TIMEOUT 10)

add_executable(testlayout_facts_mapper
    layout_facts_mapper.c
    "${PROJECT_SOURCE_DIR}/src/SDL_shadercross_layout_facts.c"
)
sdl_add_warning_options(testlayout_facts_mapper)
target_compile_features(testlayout_facts_mapper PRIVATE c_std_99)
target_include_directories(testlayout_facts_mapper PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(testlayout_facts_mapper PRIVATE SDL3::SDL3)
add_test(NAME testlayout_facts_mapper COMMAND testlayout_facts_mapper)
set_property(TEST testlayout_facts_mapper PROPERTY TIMEOUT 10)

set(shadercross_spirv_test_tools)
foreach(spirv_tool spirv-as spirv-val)
    if(TARGET ${spirv_tool})
        list(APPEND shadercross_spirv_test_tools ${spirv_tool})
    endif()
endforeach()
if(shadercross_spirv_test_tools)
    add_custom_target(SDL_shadercross_test_spirv_tools ALL)
    add_dependencies(SDL_shadercross_test_spirv_tools ${shadercross_spirv_test_tools})
endif()

set(have_layout_facts_cli_codegen OFF)
if(TARGET shadercross AND TARGET spirv-as AND TARGET spirv-val AND Python3_Interpreter_FOUND)
    set(have_layout_facts_cli_codegen ON)
    set(layout_facts_cli_codegen_args
        Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_layout_facts_codegen.py"
        "$<TARGET_FILE:shadercross>"
        "$<TARGET_FILE:spirv-as>"
        "$<TARGET_FILE:spirv-val>"
        "${CMAKE_CURRENT_BINARY_DIR}/layout-facts-cli"
        "${CMAKE_COMMAND}")
    if(SDL3_DIR)
        list(APPEND layout_facts_cli_codegen_args "--sdl3-dir" "${SDL3_DIR}")
    endif()
    if(SDLSHADERCROSS_DXC)
        list(APPEND layout_facts_cli_codegen_args "--hlsl-required")
    endif()
    if(CMAKE_GENERATOR)
        list(APPEND layout_facts_cli_codegen_args "--cmake-generator" "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND layout_facts_cli_codegen_args "--cmake-generator-platform" "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND layout_facts_cli_codegen_args "--cmake-generator-toolset" "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND layout_facts_cli_codegen_args "--cmake-make-program" "${CMAKE_MAKE_PROGRAM}")
    endif()
    foreach(cmake_define
        CMAKE_TOOLCHAIN_FILE
        CMAKE_SYSROOT
        CMAKE_OSX_ARCHITECTURES
        CMAKE_OSX_DEPLOYMENT_TARGET
        CMAKE_C_COMPILER
        CMAKE_C_COMPILER_TARGET
        CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN
    )
        if(DEFINED ${cmake_define} AND NOT "${${cmake_define}}" STREQUAL "")
            string(REPLACE ";" "$<SEMICOLON>" cmake_define_value "${${cmake_define}}")
            list(APPEND layout_facts_cli_codegen_args "--cmake-define" "${cmake_define}=${cmake_define_value}")
        endif()
    endforeach()
    add_test(NAME testlayout_facts_cli_codegen
        COMMAND ${layout_facts_cli_codegen_args})
    set_property(TEST testlayout_facts_cli_codegen PROPERTY TIMEOUT 180)
endif()

set(have_wgsl_cli_codegen OFF)
set(wgsl_cli_codegen_skip_reasons)
if(NOT TARGET shadercross)
    list(APPEND wgsl_cli_codegen_skip_reasons "shadercross CLI target unavailable")
endif()
if(NOT TARGET spirv-as)
    list(APPEND wgsl_cli_codegen_skip_reasons "spirv-as target unavailable")
endif()
if(NOT TARGET spirv-val)
    list(APPEND wgsl_cli_codegen_skip_reasons "spirv-val target unavailable")
endif()
if(NOT Python3_Interpreter_FOUND)
    list(APPEND wgsl_cli_codegen_skip_reasons "Python3 interpreter unavailable")
endif()
if(NOT SDLSHADERCROSS_TINT_EXECUTABLE)
    list(APPEND wgsl_cli_codegen_skip_reasons "Tint executable not configured or found")
endif()
if(NOT wgsl_cli_codegen_skip_reasons)
    set(have_wgsl_cli_codegen ON)
    set(wgsl_cli_codegen_args
        Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_wgsl_codegen.py"
        "$<TARGET_FILE:shadercross>"
        "$<TARGET_FILE:spirv-as>"
        "$<TARGET_FILE:spirv-val>"
        "${SDLSHADERCROSS_TINT_EXECUTABLE}"
        "${CMAKE_CURRENT_BINARY_DIR}/wgsl-cli")
    if(SDLSHADERCROSS_DXC)
        list(APPEND wgsl_cli_codegen_args "--hlsl-required")
    endif()
    add_test(NAME testwgsl_cli_codegen
        COMMAND ${wgsl_cli_codegen_args})
    set_property(TEST testwgsl_cli_codegen PROPERTY TIMEOUT 20)
    message(STATUS "SDL_shadercross: enabling WGSL CLI codegen tests with Tint: ${SDLSHADERCROSS_TINT_EXECUTABLE}")
else()
    string(JOIN ", " wgsl_cli_codegen_skip_reason ${wgsl_cli_codegen_skip_reasons})
    message(STATUS "SDL_shadercross: skipping WGSL CLI codegen tests (${wgsl_cli_codegen_skip_reason})")
endif()

set(have_spirv_format_authority_cli_codegen OFF)
if(TARGET shadercross AND TARGET spirv-val AND Python3_Interpreter_FOUND)
    set(have_spirv_format_authority_cli_codegen ON)
    set(spirv_format_authority_cli_codegen_args
        Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_spirv_format_authority.py"
        "$<TARGET_FILE:shadercross>"
        "$<TARGET_FILE:spirv-val>"
        "${CMAKE_CURRENT_BINARY_DIR}/spirv-format-authority-cli")
    if(SDLSHADERCROSS_DXC)
        list(APPEND spirv_format_authority_cli_codegen_args "--hlsl-required")
    endif()
    add_test(NAME testspirv_format_authority_cli_codegen
        COMMAND ${spirv_format_authority_cli_codegen_args})
    set_property(TEST testspirv_format_authority_cli_codegen PROPERTY TIMEOUT 20)
endif()

set(have_policy_suggestions_cli OFF)
if(TARGET shadercross AND Python3_Interpreter_FOUND)
    set(have_policy_suggestions_cli ON)
    set(policy_suggestions_cli_args
        Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_policy_suggestions.py"
        "$<TARGET_FILE:shadercross>"
        "${CMAKE_CURRENT_BINARY_DIR}/policy-suggestions-cli")
    if(SDLSHADERCROSS_DXC)
        list(APPEND policy_suggestions_cli_args "--hlsl-required")
    endif()
    if(TARGET spirv-as AND TARGET spirv-val)
        list(APPEND policy_suggestions_cli_args
            "--spirv-as" "$<TARGET_FILE:spirv-as>"
            "--spirv-val" "$<TARGET_FILE:spirv-val>")
    endif()
    add_test(NAME testpolicy_suggestions_cli
        COMMAND ${policy_suggestions_cli_args})
    set_property(TEST testpolicy_suggestions_cli PROPERTY TIMEOUT 20)
endif()

set(have_manifest_cli OFF)
if(TARGET shadercross AND TARGET spirv-as AND TARGET spirv-val AND Python3_Interpreter_FOUND)
    set(have_manifest_cli ON)
    set(manifest_cli_args
        Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_manifest.py"
        "$<TARGET_FILE:shadercross>"
        "$<TARGET_FILE:spirv-as>"
        "$<TARGET_FILE:spirv-val>"
        "${CMAKE_CURRENT_BINARY_DIR}/manifest-cli")
    if(SDLSHADERCROSS_DXC)
        list(APPEND manifest_cli_args "--hlsl-required")
    endif()
    if(SDLSHADERCROSS_TINT_EXECUTABLE)
        list(APPEND manifest_cli_args "--tint" "${SDLSHADERCROSS_TINT_EXECUTABLE}")
    endif()
    add_test(NAME testmanifest_cli
        COMMAND ${manifest_cli_args})
    set_property(TEST testmanifest_cli PROPERTY TIMEOUT 30)
endif()

if(UNIX AND NOT APPLE)
    set_property(TEST testshadercross APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${SDL3_shadercross_BINARY_DIR}/vkd3d-prefix/lib")
    if(have_layout_facts_cli_codegen)
        set_property(TEST testlayout_facts_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${SDL3_shadercross_BINARY_DIR}/vkd3d-prefix/lib")
    endif()
    if(have_wgsl_cli_codegen)
        set_property(TEST testwgsl_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${SDL3_shadercross_BINARY_DIR}/vkd3d-prefix/lib")
    endif()
    if(have_spirv_format_authority_cli_codegen)
        set_property(TEST testspirv_format_authority_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${SDL3_shadercross_BINARY_DIR}/vkd3d-prefix/lib")
    endif()
    if(have_policy_suggestions_cli)
        set_property(TEST testpolicy_suggestions_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${SDL3_shadercross_BINARY_DIR}/vkd3d-prefix/lib")
    endif()
    if(have_manifest_cli)
        set_property(TEST testmanifest_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${SDL3_shadercross_BINARY_DIR}/vkd3d-prefix/lib")
    endif()
    if(SDLSHADERCROSS_INSTALL_RUNTIME AND TARGET DirectXShaderCompiler::dxcompiler)
        set_property(TEST testshadercross APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        if(have_layout_facts_cli_codegen)
            set_property(TEST testlayout_facts_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_wgsl_cli_codegen)
            set_property(TEST testwgsl_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_spirv_format_authority_cli_codegen)
            set_property(TEST testspirv_format_authority_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_policy_suggestions_cli)
            set_property(TEST testpolicy_suggestions_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_manifest_cli)
            set_property(TEST testmanifest_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
    endif()
elseif(WIN32)
    if(TARGET DirectXShaderCompiler::dxcompiler)
        set_property(TEST testshadercross APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        if(have_layout_facts_cli_codegen)
            set_property(TEST testlayout_facts_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_wgsl_cli_codegen)
            set_property(TEST testwgsl_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_spirv_format_authority_cli_codegen)
            set_property(TEST testspirv_format_authority_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_policy_suggestions_cli)
            set_property(TEST testpolicy_suggestions_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
        if(have_manifest_cli)
            set_property(TEST testmanifest_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:DirectXShaderCompiler::dxcompiler>")
        endif()
    endif()
    set_property(TEST testshadercross APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
    set_property(TEST testshadercross APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3_shadercross::SDL3_shadercross>")
    set_property(TEST testlayout_facts_mapper APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
    if(have_layout_facts_cli_codegen)
        set_property(TEST testlayout_facts_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
        set_property(TEST testlayout_facts_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3_shadercross::SDL3_shadercross>")
    endif()
    if(have_wgsl_cli_codegen)
        set_property(TEST testwgsl_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
        set_property(TEST testwgsl_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3_shadercross::SDL3_shadercross>")
    endif()
    if(have_spirv_format_authority_cli_codegen)
        set_property(TEST testspirv_format_authority_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
        set_property(TEST testspirv_format_authority_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3_shadercross::SDL3_shadercross>")
    endif()
    if(have_policy_suggestions_cli)
        set_property(TEST testpolicy_suggestions_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
        set_property(TEST testpolicy_suggestions_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3_shadercross::SDL3_shadercross>")
    endif()
    if(have_manifest_cli)
        set_property(TEST testmanifest_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3::SDL3>")
        set_property(TEST testmanifest_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:SDL3_shadercross::SDL3_shadercross>")
    endif()
    if(TARGET spirv-cross-c-shared)
        set_property(TEST testshadercross APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spirv-cross-c-shared>")
        if(have_layout_facts_cli_codegen)
            set_property(TEST testlayout_facts_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spirv-cross-c-shared>")
        endif()
        if(have_wgsl_cli_codegen)
            set_property(TEST testwgsl_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spirv-cross-c-shared>")
        endif()
        if(have_spirv_format_authority_cli_codegen)
            set_property(TEST testspirv_format_authority_cli_codegen APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spirv-cross-c-shared>")
        endif()
        if(have_policy_suggestions_cli)
            set_property(TEST testpolicy_suggestions_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spirv-cross-c-shared>")
        endif()
        if(have_manifest_cli)
            set_property(TEST testmanifest_cli APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spirv-cross-c-shared>")
        endif()
    endif()
endif()
if(SDLSHADERCROSS_TESTS_TRACKMEM)
    set_property(TEST testshadercross PROPERTY FAIL_REGULAR_EXPRESSION "Total: [0-9]+\\.[0-9]+ Kb in [1-9][0-9]* allocations")
endif()
