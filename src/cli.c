/*
  Simple DirectMedia Layer Shader Cross Compiler
  Copyright (C) 2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "cli.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#ifdef LEAKCHECK
#include <SDL3/SDL_test_memory.h>
#endif

#include <stdio.h>

typedef struct ShaderCross_TintExecutableInfo {
    const char *path;
    const char *source;
    const char *manifest_path;
    char *owned_path;
    char *owned_manifest_path;
} ShaderCross_TintExecutableInfo;

extern bool ShaderCross_CLI_WriteSPIRVReflectionJSON(
    SDL_IOStream *outputIO,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    bool allow_observed_storage_access);
extern bool ShaderCross_CLI_WriteSPIRVResourceLayoutC(
    SDL_IOStream *outputIO,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *symbol_prefix,
    const char *input_filename,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies,
    Uint32 num_sampled_slot_policies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    bool allow_observed_storage_access);
extern bool ShaderCross_CLI_WriteSPIRVResourceLayoutPolicySuggestions(
    SDL_IOStream *outputIO,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies,
    Uint32 num_sampled_slot_policies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    bool allow_observed_storage_access);
extern bool ShaderCross_CLI_UpdateStorageTextureDeclarationsForWGSL(
    char **wgsl_text,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies);
extern bool ShaderCross_CLI_UpdateStorageTextureFormatsForSPIRV(
    Uint8 **bytecode,
    size_t *bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    char **provenance_note);

void ShaderCross_CLIOptions_Init(ShaderCross_CLIOptions *options)
{
    SDL_zero(*options);
    options->destination_format = SHADERFORMAT_INVALID;
    options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    options->entrypoint_name = SDL_strdup("main");
}

void ShaderCross_CLIOptions_Free(ShaderCross_CLIOptions *options)
{
    if (options == NULL) {
        return;
    }

    SDL_free(options->filename);
    SDL_free(options->output_filename);
    SDL_free(options->resource_layout_filename);
    SDL_free(options->resource_layout_symbol_prefix);
    SDL_free(options->tint_executable);
    SDL_free(options->entrypoint_name);
    SDL_free(options->include_dir);
    SDL_free(options->msl_version);
    for (size_t i = 0; i < options->num_defines; i += 1) {
        SDL_free(options->defines[i].name);
        SDL_free((char *)options->defines[i].value);
    }
    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        SDL_free(options->shader_targets[i].filename);
    }
    SDL_free(options->defines);
    SDL_free(options->sampled_slot_policies);
    SDL_free(options->storage_texture_slot_policies);
    SDL_free(options->shader_targets);
    SDL_zero(*options);
}

bool ShaderCross_CLIOptions_SetString(char **field, const char *value)
{
    char *copy = NULL;

    if (value != NULL) {
        copy = SDL_strdup(value);
        if (copy == NULL) {
            return SDL_OutOfMemory();
        }
    }
    SDL_free(*field);
    *field = copy;
    return true;
}

bool ShaderCross_CLIOptions_SetSourceFormat(ShaderCross_CLIOptions *options, const char *value)
{
    if (SDL_strcasecmp(value, "spirv") == 0) {
        options->spirv_source = true;
        options->source_valid = true;
        return true;
    }
    if (SDL_strcasecmp(value, "hlsl") == 0) {
        options->spirv_source = false;
        options->source_valid = true;
        return true;
    }
    return SDL_SetError("unrecognized source input %s, source must be SPIRV or HLSL", value);
}

bool ShaderCross_CLIOptions_ParseDestinationFormat(const char *value, ShaderCross_ShaderFormat *format)
{
    if (SDL_strcasecmp(value, "DXBC") == 0) {
        *format = SHADERFORMAT_DXBC;
    } else if (SDL_strcasecmp(value, "DXIL") == 0) {
        *format = SHADERFORMAT_DXIL;
    } else if (SDL_strcasecmp(value, "MSL") == 0) {
        *format = SHADERFORMAT_MSL;
    } else if (SDL_strcasecmp(value, "SPIRV") == 0) {
        *format = SHADERFORMAT_SPIRV;
    } else if (SDL_strcasecmp(value, "HLSL") == 0) {
        *format = SHADERFORMAT_HLSL;
    } else if (SDL_strcasecmp(value, "JSON") == 0) {
        *format = SHADERFORMAT_JSON;
    } else if (SDL_strcasecmp(value, "WGSL") == 0) {
        *format = SHADERFORMAT_WGSL;
    } else {
        return SDL_SetError("unrecognized destination input %s, destination must be DXBC, DXIL, MSL, SPIRV, HLSL, JSON or WGSL", value);
    }

    return true;
}

bool ShaderCross_CLIOptions_SetDestinationFormat(ShaderCross_CLIOptions *options, const char *value)
{
    if (!ShaderCross_CLIOptions_ParseDestinationFormat(value, &options->destination_format)) {
        return false;
    }
    options->destination_valid = true;
    return true;
}

static const char *ShaderCross_CLIOutputFileMode(ShaderCross_ShaderFormat format)
{
    switch (format) {
    case SHADERFORMAT_DXBC:
    case SHADERFORMAT_DXIL:
    case SHADERFORMAT_SPIRV:
        return "wb";
    case SHADERFORMAT_MSL:
    case SHADERFORMAT_HLSL:
    case SHADERFORMAT_JSON:
    case SHADERFORMAT_WGSL:
    default:
        return "w";
    }
}

bool ShaderCross_CLIOptions_AddShaderTarget(ShaderCross_CLIOptions *options, ShaderCross_ShaderFormat format, const char *filename)
{
    ShaderCross_CLIShaderTarget *targets = NULL;
    char *filename_copy = NULL;

    if (format == SHADERFORMAT_INVALID) {
        return SDL_SetError("%s", "shader target format is invalid");
    }
    if (filename == NULL || filename[0] == '\0') {
        return SDL_SetError("%s", "shader target output path must not be empty");
    }

    filename_copy = SDL_strdup(filename);
    if (filename_copy == NULL) {
        return SDL_OutOfMemory();
    }

    targets = (ShaderCross_CLIShaderTarget *)SDL_realloc(
        options->shader_targets,
        sizeof(*options->shader_targets) * (options->num_shader_targets + 1));
    if (targets == NULL) {
        SDL_free(filename_copy);
        return SDL_OutOfMemory();
    }

    options->shader_targets = targets;
    options->shader_targets[options->num_shader_targets].format = format;
    options->shader_targets[options->num_shader_targets].filename = filename_copy;
    options->num_shader_targets += 1;
    return true;
}

bool ShaderCross_CLIOptions_SetStage(ShaderCross_CLIOptions *options, const char *value)
{
    if (SDL_strcasecmp(value, "vertex") == 0) {
        options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    } else if (SDL_strcasecmp(value, "fragment") == 0) {
        options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    } else if (SDL_strcasecmp(value, "compute") == 0) {
        options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
    } else {
        return SDL_SetError("unrecognized shader stage input %s, must be vertex, fragment, or compute", value);
    }

    options->stage_valid = true;
    return true;
}

bool ShaderCross_CLIOptions_AddDefine(ShaderCross_CLIOptions *options, const char *name, const char *value)
{
    SDL_ShaderCross_HLSL_Define *defines = NULL;
    char *name_copy = NULL;
    char *value_copy = NULL;

    if (name == NULL || name[0] == '\0') {
        return SDL_SetError("define name must not be empty");
    }
    name_copy = SDL_strdup(name);
    if (name_copy == NULL) {
        return SDL_OutOfMemory();
    }
    if (value != NULL) {
        value_copy = SDL_strdup(value);
        if (value_copy == NULL) {
            SDL_free(name_copy);
            return SDL_OutOfMemory();
        }
    }

    defines = (SDL_ShaderCross_HLSL_Define *)SDL_realloc(
        options->defines,
        sizeof(*options->defines) * (options->num_defines + 2));
    if (defines == NULL) {
        SDL_free(value_copy);
        SDL_free(name_copy);
        return SDL_OutOfMemory();
    }
    options->defines = defines;
    options->defines[options->num_defines].name = name_copy;
    options->defines[options->num_defines].value = value_copy;
    options->num_defines += 1;
    options->defines[options->num_defines].name = NULL;
    options->defines[options->num_defines].value = NULL;
    return true;
}

bool ShaderCross_CLIOptions_AddDefineArgument(ShaderCross_CLIOptions *options, const char *argument)
{
    const char *name = argument + 2;
    const char *equal_sign = SDL_strchr(argument, '=');
    char *owned_name = NULL;
    bool result = false;

    if (name[0] == '\0' || name[0] == '=') {
        return SDL_SetError("invalid define argument '%s'", argument);
    }

    if (equal_sign != NULL) {
        const size_t name_len = (size_t)(equal_sign - name);
        owned_name = (char *)SDL_malloc(name_len + 1);
        if (owned_name == NULL) {
            return SDL_OutOfMemory();
        }
        SDL_memcpy(owned_name, name, name_len);
        owned_name[name_len] = '\0';
        result = ShaderCross_CLIOptions_AddDefine(options, owned_name, equal_sign + 1);
        SDL_free(owned_name);
        return result;
    }

    return ShaderCross_CLIOptions_AddDefine(options, name, NULL);
}

static bool write_resource_layout_c_file(
    const char *resourceLayoutFilename,
    const Uint8 *bytecode,
    size_t bytecodeSize,
    const char *resourceLayoutSymbolPrefix,
    const char *filename,
    const char *entrypointName,
    SDL_ShaderCross_ShaderStage shaderStage,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampledSlotPolicies,
    Uint32 numSampledSlotPolicies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storageTextureSlotPolicies,
    Uint32 numStorageTextureSlotPolicies,
    bool allowObservedStorageAccess,
    const char *provenanceNote)
{
    bool result = true;
    bool closeResult = true;
    SDL_IOStream *layoutIO = SDL_IOFromFile(resourceLayoutFilename, "w");

    if (layoutIO == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        return false;
    }

    if (provenanceNote && provenanceNote[0] != '\0') {
        if (SDL_IOprintf(layoutIO, "/* %s */\n", provenanceNote) == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write SDL_GPU resource layout C provenance: %s", SDL_GetError());
            result = false;
        }
    }

    if (result && !ShaderCross_CLI_WriteSPIRVResourceLayoutC(
            layoutIO,
            bytecode,
            bytecodeSize,
            resourceLayoutSymbolPrefix,
            filename,
            entrypointName,
            shaderStage,
            sampledSlotPolicies,
            numSampledSlotPolicies,
            storageTextureSlotPolicies,
            numStorageTextureSlotPolicies,
            allowObservedStorageAccess)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write SDL_GPU resource layout C: %s", SDL_GetError());
        result = false;
    }
    if (result && SDL_GetIOStatus(layoutIO) != SDL_IO_STATUS_READY) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write SDL_GPU resource layout C '%s': %s", resourceLayoutFilename, SDL_GetError());
        result = false;
    }

    closeResult = SDL_CloseIO(layoutIO);
    if (!closeResult) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to close SDL_GPU resource layout C '%s': %s", resourceLayoutFilename, SDL_GetError());
        result = false;
    }
    if (!result) {
        SDL_RemovePath(resourceLayoutFilename);
    }
    return result;
}

static bool write_resource_layout_policy_suggestions_to_stdout(
    const Uint8 *bytecode,
    size_t bytecodeSize,
    const char *entrypointName,
    SDL_ShaderCross_ShaderStage shaderStage,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampledSlotPolicies,
    Uint32 numSampledSlotPolicies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storageTextureSlotPolicies,
    Uint32 numStorageTextureSlotPolicies,
    bool allowObservedStorageAccess)
{
    SDL_IOStream *suggestionsIO = SDL_IOFromDynamicMem();
    const char *suggestions;
    Sint64 size;
    bool result = true;

    if (suggestionsIO == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize resource layout policy suggestion output: %s", SDL_GetError());
        return false;
    }

    if (!ShaderCross_CLI_WriteSPIRVResourceLayoutPolicySuggestions(
            suggestionsIO,
            bytecode,
            bytecodeSize,
            entrypointName,
            shaderStage,
            sampledSlotPolicies,
            numSampledSlotPolicies,
            storageTextureSlotPolicies,
            numStorageTextureSlotPolicies,
            allowObservedStorageAccess)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to suggest SDL_GPU resource layout policies: %s", SDL_GetError());
        result = false;
        goto done;
    }

    size = SDL_TellIO(suggestionsIO);
    if (size < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read generated resource layout policy suggestions: %s", SDL_GetError());
        result = false;
        goto done;
    }
    suggestions = (const char *)SDL_GetPointerProperty(
        SDL_GetIOProperties(suggestionsIO),
        SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER,
        NULL);
    if (size > 0 &&
        (suggestions == NULL || fwrite(suggestions, 1, (size_t)size, stdout) != (size_t)size || fflush(stdout) != 0)) {
        SDL_SetError("failed to write resource layout policy suggestions to stdout");
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        result = false;
    }

done:
    SDL_CloseIO(suggestionsIO);
    return result;
}

/* Generated from the test/cli_wgsl_codegen.py matrix-order HLSL fixture:
 * column_major float4x4 Matrix in cbuffer b0/space1, mul(Matrix, position).
 * DXC emits a RowMajor SPIR-V mat4x4 uniform member and OpVectorTimesMatrix.
 * Regenerate with shadercross -s HLSL -t vertex -d SPIRV, then validate with
 * spirv-val --target-env vulkan1.0 and inspect with spirv-dis.
 */
static const Uint8 tint_matrix_order_canary_spv[] = {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00,
    0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00, 0x05, 0x00, 0x07, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x74, 0x79, 0x70, 0x65, 0x2e, 0x56, 0x65, 0x72,
    0x74, 0x65, 0x78, 0x55, 0x6e, 0x69, 0x66, 0x6f, 0x72, 0x6d, 0x73, 0x00,
    0x06, 0x00, 0x05, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4d, 0x61, 0x74, 0x72, 0x69, 0x78, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x56, 0x65, 0x72, 0x74, 0x65, 0x78, 0x55, 0x6e,
    0x69, 0x66, 0x6f, 0x72, 0x6d, 0x73, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x2e, 0x76, 0x61, 0x72, 0x2e, 0x50,
    0x4f, 0x53, 0x49, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x05, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x48, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
    0x17, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x18, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x36, 0x00, 0x05, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x02, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x16, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x51, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x50, 0x00, 0x07, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x90, 0x00, 0x05, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x1a, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00,
    0x38, 0x00, 0x01, 0x00
};

static bool sanitize_log_value(const char *value, char **sanitized)
{
    size_t len;
    char *copy;
    size_t i;

    if (value == NULL) {
        value = "";
    }
    len = SDL_strlen(value);
    copy = (char *)SDL_malloc(len + 1);
    if (copy == NULL) {
        return SDL_OutOfMemory();
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        copy[i] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
    }
    copy[len] = '\0';
    *sanitized = copy;
    return true;
}

#if defined(SDL_SHADERCROSS_BUNDLED_TINT_SIBLING_NAME)
static bool shadercross_path_is_file(const char *path)
{
    SDL_PathInfo pathInfo;

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (!SDL_GetPathInfo(path, &pathInfo)) {
        SDL_ClearError();
        return false;
    }
    return pathInfo.type == SDL_PATHTYPE_FILE;
}

static bool make_base_relative_path(const char *relativePath, char **path)
{
    const char *basePath = SDL_GetBasePath();

    *path = NULL;
    if (basePath == NULL || basePath[0] == '\0') {
        SDL_ClearError();
        return true;
    }
    if (SDL_asprintf(path, "%s%s", basePath, relativePath) < 0) {
        return SDL_OutOfMemory();
    }
    return true;
}

static bool make_bundled_manifest_relative_path(const char *relativeExecutablePath, char **relativeManifestPath)
{
    const char *slash = SDL_strrchr(relativeExecutablePath, '/');
    const char *backslash = SDL_strrchr(relativeExecutablePath, '\\');
    const char *separator = slash;
    size_t prefixLen = 0;

    if (backslash != NULL && (separator == NULL || backslash > separator)) {
        separator = backslash;
    }
    if (separator != NULL) {
        prefixLen = (size_t)(separator - relativeExecutablePath) + 1;
    }
    if (SDL_asprintf(relativeManifestPath, "%.*s%s", (int)prefixLen, relativeExecutablePath, SDL_SHADERCROSS_BUNDLED_TINT_MANIFEST_NAME) < 0) {
        return SDL_OutOfMemory();
    }
    return true;
}

static bool try_resolve_bundled_tint(
    const char *relativeExecutablePath,
    ShaderCross_TintExecutableInfo *info,
    bool *found)
{
    char *candidatePath = NULL;
    char *relativeManifestPath = NULL;
    char *candidateManifestPath = NULL;

    *found = false;

    if (!make_base_relative_path(relativeExecutablePath, &candidatePath)) {
        return false;
    }
    if (candidatePath == NULL) {
        return true;
    }

    if (!shadercross_path_is_file(candidatePath)) {
        SDL_free(candidatePath);
        return true;
    }

    info->path = candidatePath;
    info->owned_path = candidatePath;
    info->source = "bundled";
    *found = true;

    if (!make_bundled_manifest_relative_path(relativeExecutablePath, &relativeManifestPath)) {
        return false;
    }
    if (!make_base_relative_path(relativeManifestPath, &candidateManifestPath)) {
        SDL_free(relativeManifestPath);
        return false;
    }
    if (candidateManifestPath != NULL && shadercross_path_is_file(candidateManifestPath)) {
        info->manifest_path = candidateManifestPath;
        info->owned_manifest_path = candidateManifestPath;
        candidateManifestPath = NULL;
    }
    SDL_free(candidateManifestPath);
    SDL_free(relativeManifestPath);
    return true;
}
#endif

static void cleanup_tint_executable(ShaderCross_TintExecutableInfo *info)
{
    SDL_free(info->owned_path);
    SDL_free(info->owned_manifest_path);
    SDL_zero(*info);
}

static bool resolve_tint_executable(const char *tintExecutable, ShaderCross_TintExecutableInfo *info)
{
    const char *envTint = NULL;
#if defined(SDL_SHADERCROSS_BUNDLED_TINT_SIBLING_NAME)
    bool found = false;
#endif

    SDL_zero(*info);

    if (tintExecutable != NULL && tintExecutable[0] != '\0') {
        info->path = tintExecutable;
        info->source = "explicit";
        return true;
    }

#if defined(SDL_SHADERCROSS_BUNDLED_TINT_SIBLING_NAME) && defined(SDL_SHADERCROSS_BUNDLED_TINT_INSTALL_RELATIVE_PATH)
    if (!try_resolve_bundled_tint(SDL_SHADERCROSS_BUNDLED_TINT_INSTALL_RELATIVE_PATH, info, &found)) {
        return false;
    }
    if (found) {
        return true;
    }
#endif
#if defined(SDL_SHADERCROSS_BUNDLED_TINT_SIBLING_NAME)
    if (!try_resolve_bundled_tint(SDL_SHADERCROSS_BUNDLED_TINT_SIBLING_NAME, info, &found)) {
        return false;
    }
    if (found) {
        return true;
    }
#endif

    envTint = SDL_getenv("SDL_SHADERCROSS_TINT");
    if (envTint != NULL && envTint[0] != '\0') {
        info->path = envTint;
        info->source = "environment";
        return true;
    }

    info->path = "tint";
    info->source = "PATH";
    return true;
}

static bool init_spirv_info_for_cli(
    SDL_ShaderCross_SPIRV_Info *spirvInfo,
    const Uint8 *bytecode,
    size_t bytecodeSize,
    const char *entrypointName,
    SDL_ShaderCross_ShaderStage shaderStage,
    const char *debugName,
    bool enableDebug,
    bool cullUnusedBindings,
    const char *mslVersion)
{
    SDL_PropertiesID props = SDL_CreateProperties();

    if (props == 0) {
        return false;
    }

    spirvInfo->bytecode = bytecode;
    spirvInfo->bytecode_size = bytecodeSize;
    spirvInfo->entrypoint = entrypointName;
    spirvInfo->shader_stage = shaderStage;
    spirvInfo->props = props;

    if (enableDebug) {
        SDL_SetBooleanProperty(props, SDL_SHADERCROSS_PROP_SHADER_DEBUG_ENABLE_BOOLEAN, true);
        SDL_SetStringProperty(props, SDL_SHADERCROSS_PROP_SHADER_DEBUG_NAME_STRING, debugName);
    }
    if (cullUnusedBindings) {
        SDL_SetBooleanProperty(props, SDL_SHADERCROSS_PROP_SHADER_CULL_UNUSED_BINDINGS_BOOLEAN, true);
    }
    if (mslVersion) {
        SDL_SetStringProperty(props, SDL_SHADERCROSS_PROP_SPIRV_MSL_VERSION_STRING, mslVersion);
    }

    return true;
}

static bool run_tint_spirv_to_wgsl(
    const char *tintExecutable,
    const char *entrypointName,
    const char *spirvFilename,
    const char *wgslFilename)
{
    const char *explicitInputArgs[] = {
        tintExecutable,
        "--input-format=spirv",
        "--format=wgsl",
        "--entry-point",
        entrypointName,
        "--output-name",
        wgslFilename,
        spirvFilename,
        NULL
    };
    const char *extensionInputArgs[] = {
        tintExecutable,
        "--format=wgsl",
        "--entry-point",
        entrypointName,
        "--output-name",
        wgslFilename,
        spirvFilename,
        NULL
    };
    SDL_PropertiesID props = 0;
    SDL_Process *process = NULL;
    char *processOutput = NULL;
    size_t processOutputSize = 0;
    int exitCode = -1;
    bool retryWithoutExplicitInputFormat = false;

    props = SDL_CreateProperties();
    if (props == 0) {
        return false;
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)explicitInputArgs);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);

    process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == NULL) {
        return SDL_SetError("Failed to run Tint executable '%s': %s", tintExecutable, SDL_GetError());
    }

    processOutput = (char *)SDL_ReadProcess(process, &processOutputSize, &exitCode);
    SDL_DestroyProcess(process);
    if (processOutput == NULL) {
        return SDL_SetError("Failed to read Tint process output: %s", SDL_GetError());
    }

    if (exitCode == 0) {
        SDL_free(processOutput);
        return true;
    }

    retryWithoutExplicitInputFormat =
        processOutput != NULL &&
        SDL_strcasestr(processOutput, "input-format") != NULL &&
        (SDL_strcasestr(processOutput, "unknown") != NULL ||
         SDL_strcasestr(processOutput, "unrecognized") != NULL ||
         SDL_strcasestr(processOutput, "unsupported") != NULL ||
         SDL_strcasestr(processOutput, "invalid option") != NULL);

    if (retryWithoutExplicitInputFormat) {
        SDL_free(processOutput);
        processOutput = NULL;
        processOutputSize = 0;
        exitCode = -1;
        SDL_RemovePath(wgslFilename);

        props = SDL_CreateProperties();
        if (props == 0) {
            return false;
        }
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)extensionInputArgs);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);

        process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (process == NULL) {
            return SDL_SetError("Failed to run Tint executable '%s': %s", tintExecutable, SDL_GetError());
        }

        processOutput = (char *)SDL_ReadProcess(process, &processOutputSize, &exitCode);
        SDL_DestroyProcess(process);
        if (processOutput == NULL) {
            return SDL_SetError("Failed to read Tint process output after retrying without --input-format=spirv: %s", SDL_GetError());
        }

        if (exitCode == 0) {
            SDL_free(processOutput);
            return true;
        }

        if (processOutputSize > 0) {
            SDL_SetError("Tint failed with exit code %d after retrying without --input-format=spirv: %s", exitCode, processOutput);
        } else {
            SDL_SetError("Tint failed with exit code %d after retrying without --input-format=spirv", exitCode);
        }
        SDL_free(processOutput);
        return false;
    }

    if (processOutputSize > 0) {
        SDL_SetError("Tint failed with exit code %d: %s", exitCode, processOutput);
    } else {
        SDL_SetError("Tint failed with exit code %d", exitCode);
    }

    SDL_free(processOutput);
    return false;
}

static bool run_tint_wgsl_validate(
    const char *tintExecutable,
    const char *wgslFilename,
    const char *validatedWgslFilename)
{
    const char *args[] = {
        tintExecutable,
        "--format=wgsl",
        "--validate=true",
        "--output-name",
        validatedWgslFilename,
        wgslFilename,
        NULL
    };
    SDL_PropertiesID props = 0;
    SDL_Process *process = NULL;
    char *processOutput = NULL;
    size_t processOutputSize = 0;
    int exitCode = -1;

    props = SDL_CreateProperties();
    if (props == 0) {
        return false;
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);

    process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == NULL) {
        return SDL_SetError("Failed to run Tint executable '%s' for WGSL validation: %s", tintExecutable, SDL_GetError());
    }

    processOutput = (char *)SDL_ReadProcess(process, &processOutputSize, &exitCode);
    SDL_DestroyProcess(process);
    if (processOutput == NULL) {
        return SDL_SetError("Failed to read Tint WGSL validation output: %s", SDL_GetError());
    }

    if (exitCode == 0) {
        SDL_free(processOutput);
        return true;
    }

    if (processOutputSize > 0) {
        SDL_SetError("Tint WGSL validation failed with exit code %d: %s", exitCode, processOutput);
    } else {
        SDL_SetError("Tint WGSL validation failed with exit code %d", exitCode);
    }

    SDL_free(processOutput);
    return false;
}

static bool build_tint_provenance_note(
    const ShaderCross_TintExecutableInfo *tintInfo,
    const char *conformance,
    char **provenanceNote)
{
    char *safeTintSource = NULL;
    char *safeTintManifestPath = NULL;
    char *safeConformance = NULL;
    const char *tintManifestState = (tintInfo->manifest_path != NULL && tintInfo->manifest_path[0] != '\0') ? "present" : "none";
    bool result = false;

    *provenanceNote = NULL;

    if (!sanitize_log_value(tintInfo->source, &safeTintSource) ||
        !sanitize_log_value(tintManifestState, &safeTintManifestPath) ||
        !sanitize_log_value(conformance, &safeConformance)) {
        goto done;
    }

    if (SDL_asprintf(
            provenanceNote,
            "shadercross: WGSL produced via Tint and SDL_shadercross; tint_source=%s; tint_manifest=%s; tint_conformance=%s.",
            safeTintSource,
            safeTintManifestPath,
            safeConformance) < 0) {
        SDL_OutOfMemory();
        goto done;
    }

    result = true;

done:
    SDL_free(safeConformance);
    SDL_free(safeTintManifestPath);
    SDL_free(safeTintSource);
    return result;
}

static bool tint_canary_wgsl_has_matrix_order_transpose(const char *wgslText)
{
    const char *cursor = wgslText;

    while ((cursor = SDL_strstr(cursor, "transpose(")) != NULL) {
        const char *close = SDL_strchr(cursor, ')');
        const char *matrix = SDL_strstr(cursor, ".Matrix");
        if (close != NULL && matrix != NULL && matrix < close) {
            return true;
        }
        cursor += SDL_strlen("transpose(");
    }
    return false;
}

static bool check_tint_matrix_order_conformance(
    const ShaderCross_TintExecutableInfo *tintInfo,
    const char *outputFilename,
    char **provenanceNote)
{
    char *tempBase = NULL;
    char *tempSpirvFilename = NULL;
    char *tempWgslFilename = NULL;
    char *wgslText = NULL;
    size_t wgslSize = 0;
    void *wgslData = NULL;
    bool result = false;

    *provenanceNote = NULL;

    if (SDL_asprintf(&tempBase, "%s.tint-canary-%" SDL_PRIu64, outputFilename, SDL_GetTicksNS()) < 0 ||
        SDL_asprintf(&tempSpirvFilename, "%s.spv", tempBase) < 0 ||
        SDL_asprintf(&tempWgslFilename, "%s.wgsl", tempBase) < 0) {
        SDL_OutOfMemory();
        goto done;
    }

    if (!SDL_SaveFile(tempSpirvFilename, tint_matrix_order_canary_spv, sizeof(tint_matrix_order_canary_spv))) {
        SDL_SetError("Failed to write Tint conformance canary SPIR-V: %s", SDL_GetError());
        goto done;
    }

    if (!run_tint_spirv_to_wgsl(tintInfo->path, "main", tempSpirvFilename, tempWgslFilename)) {
        char error[512];
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
        if (SDL_strncmp(error, "Failed to run Tint executable", SDL_strlen("Failed to run Tint executable")) == 0 ||
            SDL_strncmp(error, "Failed to read Tint process output", SDL_strlen("Failed to read Tint process output")) == 0) {
            SDL_SetError("Failed to run SDL_shadercross WGSL conformance canary with Tint executable '%s': %s", tintInfo->path, error);
        } else {
            SDL_SetError(
                "Tint executable '%s' failed SDL_shadercross WGSL conformance canary 'matrix-order-row-major-square': %s",
                tintInfo->path,
                error);
        }
        goto done;
    }

    wgslData = SDL_LoadFile(tempWgslFilename, &wgslSize);
    if (wgslData == NULL) {
        SDL_SetError("Failed to read Tint conformance canary WGSL output: %s", SDL_GetError());
        goto done;
    }
    wgslText = (char *)SDL_malloc(wgslSize + 1);
    if (wgslText == NULL) {
        SDL_OutOfMemory();
        goto done;
    }
    SDL_memcpy(wgslText, wgslData, wgslSize);
    wgslText[wgslSize] = '\0';

    if (!tint_canary_wgsl_has_matrix_order_transpose(wgslText)) {
        SDL_SetError(
            "Tint executable '%s' failed SDL_shadercross WGSL conformance canary 'matrix-order-row-major-square': expected square row-major uniform matrix loads to use transpose(... .Matrix ...); use a newer Tint build or pass --allow-unvalidated-tint for development-only output",
            tintInfo->path);
        goto done;
    }

    if (!build_tint_provenance_note(tintInfo, "matrix-order-row-major-square:passed", provenanceNote)) {
        goto done;
    }

    result = true;

done:
    SDL_free(wgslText);
    SDL_free(wgslData);
    if (tempSpirvFilename != NULL) {
        SDL_RemovePath(tempSpirvFilename);
    }
    if (tempWgslFilename != NULL) {
        SDL_RemovePath(tempWgslFilename);
    }
    SDL_free(tempWgslFilename);
    SDL_free(tempSpirvFilename);
    SDL_free(tempBase);
    if (!result) {
        SDL_free(*provenanceNote);
        *provenanceNote = NULL;
    }
    return result;
}

static bool write_wgsl_file_from_spirv(
    const char *outputFilename,
    const Uint8 *bytecode,
    size_t bytecodeSize,
    const char *entrypointName,
    SDL_ShaderCross_ShaderStage shaderStage,
    const char *tintExecutable,
    bool allowUnvalidatedTint,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storageTextureSlotPolicies,
    Uint32 numStorageTextureSlotPolicies)
{
    ShaderCross_TintExecutableInfo tintInfo;
    char *tempBase = NULL;
    char *tempSpirvFilename = NULL;
    char *tempWgslFilename = NULL;
    char *tempValidatedWgslFilename = NULL;
    char *tintProvenanceNote = NULL;
    bool result = false;

    if (!resolve_tint_executable(tintExecutable, &tintInfo)) {
        goto done;
    }

    if (SDL_asprintf(&tempBase, "%s.shadercross-%" SDL_PRIu64, outputFilename, SDL_GetTicksNS()) < 0 ||
        SDL_asprintf(&tempSpirvFilename, "%s.spv", tempBase) < 0 ||
        SDL_asprintf(&tempWgslFilename, "%s.wgsl", tempBase) < 0 ||
        SDL_asprintf(&tempValidatedWgslFilename, "%s.validated.wgsl", tempBase) < 0) {
        SDL_OutOfMemory();
        goto done;
    }

    if (!SDL_SaveFile(tempSpirvFilename, bytecode, bytecodeSize)) {
        char error[256];
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
        SDL_SetError("Failed to write temporary SPIR-V for Tint: %s", error);
        goto done;
    }

    if (allowUnvalidatedTint) {
        if (!build_tint_provenance_note(&tintInfo, "not_checked:allow-unvalidated-tint", &tintProvenanceNote)) {
            goto done;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WGSL output is using unvalidated Tint: --allow-unvalidated-tint skipped the matrix-order conformance gate.");
    } else if (!check_tint_matrix_order_conformance(&tintInfo, outputFilename, &tintProvenanceNote)) {
        goto done;
    }

    if (!run_tint_spirv_to_wgsl(tintInfo.path, entrypointName, tempSpirvFilename, tempWgslFilename)) {
        goto done;
    }

    {
        size_t wgslSize = 0;
        void *wgslData = SDL_LoadFile(tempWgslFilename, &wgslSize);
        char *wgslText = NULL;

        if (wgslData == NULL) {
            SDL_SetError("Failed to read Tint WGSL output for storage texture declaration updates: %s", SDL_GetError());
            goto done;
        }
        wgslText = (char *)SDL_malloc(wgslSize + 1);
        if (wgslText == NULL) {
            SDL_free(wgslData);
            SDL_OutOfMemory();
            goto done;
        }
        SDL_memcpy(wgslText, wgslData, wgslSize);
        wgslText[wgslSize] = '\0';
        SDL_free(wgslData);

        if (!ShaderCross_CLI_UpdateStorageTextureDeclarationsForWGSL(
                &wgslText,
                bytecode,
                bytecodeSize,
                entrypointName,
                shaderStage,
                storageTextureSlotPolicies,
                numStorageTextureSlotPolicies)) {
            SDL_free(wgslText);
            goto done;
        }
        if (!SDL_SaveFile(tempWgslFilename, wgslText, SDL_strlen(wgslText))) {
            SDL_free(wgslText);
            SDL_SetError("Failed to write WGSL output after storage texture declaration updates: %s", SDL_GetError());
            goto done;
        }
        SDL_free(wgslText);
    }

    if (!run_tint_wgsl_validate(tintInfo.path, tempWgslFilename, tempValidatedWgslFilename)) {
        goto done;
    }

    if (!SDL_RenamePath(tempWgslFilename, outputFilename)) {
        char error[256];
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
        SDL_SetError("Failed to move Tint WGSL output into place: %s", error);
        goto done;
    }

    result = true;
    if (tintProvenanceNote != NULL) {
        SDL_Log("Tint note: %s", tintProvenanceNote);
    }

done:
    if (!result && tempWgslFilename != NULL) {
        SDL_RemovePath(tempWgslFilename);
    }
    if (tempSpirvFilename != NULL) {
        SDL_RemovePath(tempSpirvFilename);
    }
    if (tempValidatedWgslFilename != NULL) {
        SDL_RemovePath(tempValidatedWgslFilename);
    }
    SDL_free(tempValidatedWgslFilename);
    SDL_free(tempWgslFilename);
    SDL_free(tempSpirvFilename);
    SDL_free(tempBase);
    SDL_free(tintProvenanceNote);
    cleanup_tint_executable(&tintInfo);
    return result;
}

static bool parse_sampled_slot_policy_texture_type(const char *value, SDL_GPUTextureType *texture_type)
{
    if (SDL_strcmp(value, "2d") == 0) {
        *texture_type = SDL_GPU_TEXTURETYPE_2D;
        return true;
    }
    if (SDL_strcmp(value, "2d_array") == 0) {
        *texture_type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        return true;
    }
    if (SDL_strcmp(value, "3d") == 0) {
        *texture_type = SDL_GPU_TEXTURETYPE_3D;
        return true;
    }
    if (SDL_strcmp(value, "cube") == 0) {
        *texture_type = SDL_GPU_TEXTURETYPE_CUBE;
        return true;
    }
    if (SDL_strcmp(value, "cube_array") == 0) {
        *texture_type = SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
        return true;
    }
    return SDL_SetError("unknown texture token '%s'", value);
}

static bool parse_sampled_slot_policy_sample_type(const char *value, SDL_GPUShaderTextureSampleType *sample_type)
{
    if (SDL_strcmp(value, "filterable_float") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT;
        return true;
    }
    if (SDL_strcmp(value, "unfilterable_float") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT;
        return true;
    }
    if (SDL_strcmp(value, "depth") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH;
        return true;
    }
    if (SDL_strcmp(value, "sint") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT;
        return true;
    }
    if (SDL_strcmp(value, "uint") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT;
        return true;
    }
    if (SDL_strcmp(value, "multisampled_unfilterable_float") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT;
        return true;
    }
    if (SDL_strcmp(value, "multisampled_depth") == 0) {
        *sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH;
        return true;
    }
    return SDL_SetError("unknown sample token '%s'", value);
}

static bool parse_sampled_slot_policy_sampler_type(const char *value, SDL_GPUShaderSamplerType *sampler_type)
{
    if (SDL_strcmp(value, "filtering") == 0) {
        *sampler_type = SDL_GPU_SHADERSAMPLERTYPE_FILTERING;
        return true;
    }
    if (SDL_strcmp(value, "non_filtering") == 0) {
        *sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING;
        return true;
    }
    if (SDL_strcmp(value, "comparison") == 0) {
        *sampler_type = SDL_GPU_SHADERSAMPLERTYPE_COMPARISON;
        return true;
    }
    if (SDL_strcmp(value, "none") == 0) {
        *sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE;
        return true;
    }
    return SDL_SetError("unknown sampler token '%s'", value);
}

static bool parse_sampled_slot_policy(
    const char *argument,
    SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *policy)
{
    char *copy = SDL_strdup(argument);
    char *cursor = copy;
    bool have_slot = false;
    bool have_texture = false;
    bool have_sample = false;
    bool have_sampler = false;

    if (copy == NULL) {
        return SDL_OutOfMemory();
    }

    SDL_zero(*policy);
    while (cursor != NULL && *cursor != '\0') {
        char *item = cursor;
        char *comma = SDL_strchr(item, ',');
        char *equal = NULL;
        const char *key = NULL;
        const char *value = NULL;

        if (item[0] == '\0') {
            SDL_SetError("empty token");
            goto invalid;
        }
        if (comma != NULL) {
            if (comma[1] == '\0') {
                SDL_SetError("empty token");
                goto invalid;
            }
            *comma = '\0';
            cursor = comma + 1;
        } else {
            cursor = NULL;
        }

        equal = SDL_strchr(item, '=');
        if (equal == NULL || equal == item || equal[1] == '\0') {
            SDL_SetError("invalid token '%s'", item);
            goto invalid;
        }
        *equal = '\0';
        key = item;
        value = equal + 1;

        if (SDL_strcmp(key, "slot") == 0) {
            char *end = NULL;
            unsigned long long parsed;
            if (have_slot) {
                SDL_SetError("duplicate slot key");
                goto invalid;
            }
            parsed = SDL_strtoull(value, &end, 10);
            if (value[0] == '\0' || *end != '\0' || parsed > 0xffffffffull) {
                SDL_SetError("invalid slot token '%s'", value);
                goto invalid;
            }
            policy->slot = (Uint32)parsed;
            have_slot = true;
        } else if (SDL_strcmp(key, "texture") == 0) {
            if (have_texture) {
                SDL_SetError("duplicate texture key");
                goto invalid;
            }
            if (!parse_sampled_slot_policy_texture_type(value, &policy->description.texture_type)) {
                goto invalid;
            }
            have_texture = true;
        } else if (SDL_strcmp(key, "sample") == 0) {
            if (have_sample) {
                SDL_SetError("duplicate sample key");
                goto invalid;
            }
            if (!parse_sampled_slot_policy_sample_type(value, &policy->description.sample_type)) {
                goto invalid;
            }
            have_sample = true;
        } else if (SDL_strcmp(key, "sampler") == 0) {
            if (have_sampler) {
                SDL_SetError("duplicate sampler key");
                goto invalid;
            }
            if (!parse_sampled_slot_policy_sampler_type(value, &policy->description.sampler_type)) {
                goto invalid;
            }
            have_sampler = true;
        } else {
            SDL_SetError("unknown key '%s'", key);
            goto invalid;
        }
    }

    if (!have_slot || !have_texture || !have_sample || !have_sampler) {
        SDL_SetError("expected slot, texture, sample, and sampler keys");
        goto invalid;
    }

    SDL_free(copy);
    return true;

invalid:
    SDL_free(copy);
    return false;
}

static bool append_sampled_slot_policy(
    SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy **sampledSlotPolicies,
    Uint32 *numSampledSlotPolicies,
    const char *argument,
    const char *optionName)
{
    SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy policy;
    SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *policies = NULL;

    if (!parse_sampled_slot_policy(argument, &policy)) {
        char parse_error[256];
        SDL_strlcpy(parse_error, SDL_GetError(), sizeof(parse_error));
        SDL_SetError("invalid %s '%s': %s", optionName, argument, parse_error);
        return false;
    }
    for (Uint32 i = 0; i < *numSampledSlotPolicies; i += 1) {
        if ((*sampledSlotPolicies)[i].slot == policy.slot) {
            return SDL_SetError("invalid %s '%s': duplicate slot %u", optionName, argument, policy.slot);
        }
    }

    policies = (SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *)SDL_realloc(
        *sampledSlotPolicies,
        sizeof(**sampledSlotPolicies) * ((size_t)*numSampledSlotPolicies + 1));
    if (policies == NULL) {
        return SDL_OutOfMemory();
    }
    policies[*numSampledSlotPolicies] = policy;
    *sampledSlotPolicies = policies;
    *numSampledSlotPolicies += 1;
    return true;
}

static bool parse_storage_texture_slot_policy_class(
    const char *value,
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass *resource_class)
{
    if (SDL_strcmp(value, "storage") == 0 || SDL_strcmp(value, "graphics") == 0 || SDL_strcmp(value, "graphics_storage") == 0) {
        *resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE;
        return true;
    }
    if (SDL_strcmp(value, "readonly") == 0 || SDL_strcmp(value, "compute_readonly") == 0) {
        *resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY;
        return true;
    }
    if (SDL_strcmp(value, "readwrite") == 0 || SDL_strcmp(value, "compute_readwrite") == 0) {
        *resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE;
        return true;
    }
    if (SDL_strcmp(value, "fragment_writable") == 0) {
        return SDL_SetError("fragment-writable storage texture policies are not supported by SDL_GPU resource layout facts");
    }
    return SDL_SetError("unknown class token '%s'", value);
}

static bool parse_storage_texture_slot_policy_format(const char *value, SDL_GPUTextureFormat *format)
{
    if (SDL_strcmp(value, "rgba8unorm") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        return true;
    }
    if (SDL_strcmp(value, "rgba8snorm") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
        return true;
    }
    if (SDL_strcmp(value, "rgba16float") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        return true;
    }
    if (SDL_strcmp(value, "rg32float") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        return true;
    }
    if (SDL_strcmp(value, "rgba32float") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        return true;
    }
    if (SDL_strcmp(value, "rgba8uint") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT;
        return true;
    }
    if (SDL_strcmp(value, "rgba16uint") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT;
        return true;
    }
    if (SDL_strcmp(value, "r32uint") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R32_UINT;
        return true;
    }
    if (SDL_strcmp(value, "rgba8sint") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT;
        return true;
    }
    if (SDL_strcmp(value, "rgba16sint") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT;
        return true;
    }
    if (SDL_strcmp(value, "r32sint") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R32_INT;
        return true;
    }
    if (SDL_strcmp(value, "r32float") == 0) {
        *format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        return true;
    }
    return SDL_SetError("unknown format token '%s'", value);
}

static bool parse_storage_texture_slot_policy_access(const char *value, SDL_GPUStorageTextureAccess *access)
{
    if (SDL_strcmp(value, "read") == 0 || SDL_strcmp(value, "read_only") == 0) {
        *access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY;
        return true;
    }
    if (SDL_strcmp(value, "write") == 0 || SDL_strcmp(value, "write_only") == 0) {
        *access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY;
        return true;
    }
    if (SDL_strcmp(value, "read_write") == 0) {
        *access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE;
        return true;
    }
    return SDL_SetError("unknown access token '%s'", value);
}

static bool parse_storage_texture_slot_policy_format_authority(const char *value, bool *format_authority)
{
    if (SDL_strcmp(value, "match") == 0) {
        *format_authority = false;
        return true;
    }
    if (SDL_strcmp(value, "explicit") == 0) {
        *format_authority = true;
        return true;
    }
    return SDL_SetError("unknown format_authority token '%s'", value);
}

static bool parse_storage_texture_slot_policy(
    const char *argument,
    SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *policy)
{
    char *copy = SDL_strdup(argument);
    char *cursor = copy;
    bool have_class = false;
    bool have_slot = false;
    bool have_texture = false;
    bool have_format = false;
    bool have_access = false;
    bool have_format_authority = false;

    if (copy == NULL) {
        return SDL_OutOfMemory();
    }

    SDL_zero(*policy);
    while (cursor != NULL && *cursor != '\0') {
        char *item = cursor;
        char *comma = SDL_strchr(item, ',');
        char *equal = NULL;
        const char *key = NULL;
        const char *value = NULL;

        if (item[0] == '\0') {
            SDL_SetError("empty token");
            goto invalid;
        }
        if (comma != NULL) {
            if (comma[1] == '\0') {
                SDL_SetError("empty token");
                goto invalid;
            }
            *comma = '\0';
            cursor = comma + 1;
        } else {
            cursor = NULL;
        }

        equal = SDL_strchr(item, '=');
        if (equal == NULL || equal == item || equal[1] == '\0') {
            SDL_SetError("invalid token '%s'", item);
            goto invalid;
        }
        *equal = '\0';
        key = item;
        value = equal + 1;

        if (SDL_strcmp(key, "class") == 0) {
            if (have_class) {
                SDL_SetError("duplicate class key");
                goto invalid;
            }
            if (!parse_storage_texture_slot_policy_class(value, &policy->resource_class)) {
                goto invalid;
            }
            have_class = true;
        } else if (SDL_strcmp(key, "slot") == 0) {
            char *end = NULL;
            unsigned long long parsed;
            if (have_slot) {
                SDL_SetError("duplicate slot key");
                goto invalid;
            }
            parsed = SDL_strtoull(value, &end, 10);
            if (value[0] == '\0' || *end != '\0' || parsed > 0xffffffffull) {
                SDL_SetError("invalid slot token '%s'", value);
                goto invalid;
            }
            policy->slot = (Uint32)parsed;
            have_slot = true;
        } else if (SDL_strcmp(key, "texture") == 0) {
            if (have_texture) {
                SDL_SetError("duplicate texture key");
                goto invalid;
            }
            if (!parse_sampled_slot_policy_texture_type(value, &policy->description.texture_type)) {
                goto invalid;
            }
            have_texture = true;
        } else if (SDL_strcmp(key, "format") == 0) {
            if (have_format) {
                SDL_SetError("duplicate format key");
                goto invalid;
            }
            if (!parse_storage_texture_slot_policy_format(value, &policy->description.format)) {
                goto invalid;
            }
            have_format = true;
        } else if (SDL_strcmp(key, "access") == 0) {
            if (have_access) {
                SDL_SetError("duplicate access key");
                goto invalid;
            }
            if (!parse_storage_texture_slot_policy_access(value, &policy->description.access)) {
                goto invalid;
            }
            have_access = true;
        } else if (SDL_strcmp(key, "format_authority") == 0) {
            if (have_format_authority) {
                SDL_SetError("duplicate format_authority key");
                goto invalid;
            }
            if (!parse_storage_texture_slot_policy_format_authority(value, &policy->format_authority)) {
                goto invalid;
            }
            have_format_authority = true;
        } else {
            SDL_SetError("unknown key '%s'", key);
            goto invalid;
        }
    }

    if (!have_class || !have_slot || !have_texture || !have_format || !have_access) {
        SDL_SetError("expected class, slot, texture, format, and access keys");
        goto invalid;
    }

    SDL_free(copy);
    return true;

invalid:
    SDL_free(copy);
    return false;
}

static bool storage_texture_slot_policies_have_format_authority(
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storageTextureSlotPolicies,
    Uint32 numStorageTextureSlotPolicies)
{
    for (Uint32 i = 0; i < numStorageTextureSlotPolicies; i += 1) {
        if (storageTextureSlotPolicies[i].format_authority) {
            return true;
        }
    }
    return false;
}

static bool cli_has_shader_target_format(const ShaderCross_CLIOptions *options, ShaderCross_ShaderFormat format)
{
    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        if (options->shader_targets[i].format == format) {
            return true;
        }
    }
    return false;
}

#if SDL_PLATFORM_GDK
static bool cli_shader_target_uses_direct_hlsl_dxil(
    const ShaderCross_CLIOptions *options,
    const ShaderCross_CLIShaderTarget *target)
{
    return !options->spirv_source && target->format == SHADERFORMAT_DXIL;
}
#else
static bool cli_shader_target_uses_direct_hlsl_dxil(
    const ShaderCross_CLIOptions *options,
    const ShaderCross_CLIShaderTarget *target)
{
    (void)options;
    (void)target;
    return false;
}
#endif

static bool cli_hlsl_spirv_used_for_shader_output(const ShaderCross_CLIOptions *options)
{
    if (options->spirv_source) {
        return false;
    }
    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        if (!cli_shader_target_uses_direct_hlsl_dxil(options, &options->shader_targets[i])) {
            return true;
        }
    }
    return false;
}

static bool cli_shader_target_path_conflicts(
    const ShaderCross_CLIOptions *options,
    const char *filename,
    size_t skipIndex,
    const char **conflictingFilename)
{
    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        if (i != skipIndex && SDL_strcmp(options->shader_targets[i].filename, filename) == 0) {
            if (conflictingFilename != NULL) {
                *conflictingFilename = options->shader_targets[i].filename;
            }
            return true;
        }
    }
    if (options->resource_layout_filename != NULL &&
        SDL_strcmp(options->resource_layout_filename, filename) == 0) {
        if (conflictingFilename != NULL) {
            *conflictingFilename = options->resource_layout_filename;
        }
        return true;
    }
    return false;
}

static bool validate_cli_output_paths_are_unique(const ShaderCross_CLIOptions *options)
{
    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        const char *conflictingFilename = NULL;
        if (cli_shader_target_path_conflicts(options, options->shader_targets[i].filename, i, &conflictingFilename)) {
            return SDL_SetError("output path '%s' is used by more than one shadercross target", conflictingFilename);
        }
    }
    return true;
}

static bool append_storage_texture_slot_policy(
    SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy **storageTextureSlotPolicies,
    Uint32 *numStorageTextureSlotPolicies,
    const char *argument)
{
    SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy policy;
    SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *policies = NULL;

    if (!parse_storage_texture_slot_policy(argument, &policy)) {
        char parse_error[256];
        SDL_strlcpy(parse_error, SDL_GetError(), sizeof(parse_error));
        SDL_SetError("invalid --storage-texture-slot '%s': %s", argument, parse_error);
        return false;
    }
    for (Uint32 i = 0; i < *numStorageTextureSlotPolicies; i += 1) {
        if ((*storageTextureSlotPolicies)[i].resource_class == policy.resource_class &&
            (*storageTextureSlotPolicies)[i].slot == policy.slot) {
            return SDL_SetError("invalid --storage-texture-slot '%s': duplicate class/slot pair", argument);
        }
    }

    policies = (SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *)SDL_realloc(
        *storageTextureSlotPolicies,
        sizeof(**storageTextureSlotPolicies) * ((size_t)*numStorageTextureSlotPolicies + 1));
    if (policies == NULL) {
        return SDL_OutOfMemory();
    }
    policies[*numStorageTextureSlotPolicies] = policy;
    *storageTextureSlotPolicies = policies;
    *numStorageTextureSlotPolicies += 1;
    return true;
}

bool ShaderCross_CLIOptions_AppendSampledSlotPolicy(ShaderCross_CLIOptions *options, const char *argument, const char *option_name)
{
    return append_sampled_slot_policy(
        &options->sampled_slot_policies,
        &options->num_sampled_slot_policies,
        argument,
        option_name);
}

bool ShaderCross_CLIOptions_AppendStorageTextureSlotPolicy(ShaderCross_CLIOptions *options, const char *argument)
{
    return append_storage_texture_slot_policy(
        &options->storage_texture_slot_policies,
        &options->num_storage_texture_slot_policies,
        argument);
}

void print_help(void)
{
    int column_width = 32;
    SDL_Log("Usage: shadercross <input> [options]");
    SDL_Log("       shadercross --manifest <file> [--check]");
    SDL_Log("Input and shader-output options:\n");
    SDL_Log("  %-*s %s", column_width, "-s | --source <value>", "Source language format. May be inferred from the filename. Values: [SPIRV, HLSL]");
    SDL_Log("  %-*s %s", column_width, "-d | --dest <value>", "Destination format for shader output. May be inferred from the filename. Values: [DXBC, DXIL, MSL, SPIRV, HLSL, JSON, WGSL]");
    SDL_Log("  %-*s %s", column_width, "-t | --stage <value>", "Shader stage. May be inferred from the filename. Values: [vertex, fragment, compute]");
    SDL_Log("  %-*s %s", column_width, "-e | --entrypoint <value>", "Entrypoint function name. Default: \"main\".");
    SDL_Log("  %-*s %s", column_width, "-o | --output <value>", "Shader output file. Required when generating shader output.");
    SDL_Log("\n");
    SDL_Log("Optional options:\n");
    SDL_Log("  %-*s %s", column_width, "-I | --include <value>", "HLSL include directory. Only used with HLSL source.");
    SDL_Log("  %-*s %s", column_width, "-D<name>[=<value>]", "HLSL define. Only used with HLSL source. Can be repeated.");
    SDL_Log("  %-*s %s", column_width, "", "If =<value> is omitted the define will be treated as equal to 1.");
    SDL_Log("  %-*s %s", column_width, "--msl-version <value>", "Target MSL version. Only used when transpiling to MSL. The default is 1.2.0.");
    SDL_Log("  %-*s %s", column_width, "-c | --cull", "Allow the compiler to cull unused resource bindings. This may lead to surprising binding behavior so be careful when enabling this!");
    SDL_Log("  %-*s %s", column_width, "-g | --debug", "Generate debug information when possible. Shaders are valid only when graphics debuggers are attached.");
    SDL_Log("  %-*s %s", column_width, "-p | --pssl", "Generate PSSL-compatible shader. Destination format should be HLSL.");
    SDL_Log("  %-*s %s", column_width, "--resource-layout-c <value>", "Emit SDL_GPU resource layout C source for SPIR-V or HLSL input.");
    SDL_Log("  %-*s %s", column_width, "--resource-layout-symbol-prefix <value>", "C identifier prefix for --resource-layout-c output. Required with --resource-layout-c.");
    SDL_Log("  %-*s %s", column_width, "--resource-layout-sampled-slot <value>", "Explicit sampled slot policy for resource layout C: slot=N,texture=2d,sample=unfilterable_float,sampler=non_filtering. Can be repeated.");
    SDL_Log("  %-*s %s", column_width, "--storage-texture-slot <value>", "Explicit storage texture slot policy: class=readwrite,slot=N,texture=2d,format=rgba8unorm,access=write[,format_authority=explicit]. Can be repeated.");
    SDL_Log("  %-*s %s", column_width, "", "format_authority=explicit requires --resource-layout-c with WGSL output or HLSL-to-SPIRV output.");
    SDL_Log("  %-*s %s", column_width, "--suggest-resource-layout-policy", "Print copy-pasteable sampled/storage resource layout policy suggestions to stdout.");
    SDL_Log("  %-*s %s", column_width, "--tint <value>", "Tint executable path for CLI-only WGSL output. Defaults to bundled shadercross-tint when available, SDL_SHADERCROSS_TINT, or tint on PATH.");
    SDL_Log("  %-*s %s", column_width, "--allow-unvalidated-tint", "Development-only WGSL output: skip the Tint matrix-order conformance gate.");
    SDL_Log("  %-*s %s", column_width, "--manifest <value>", "Versioned CLI shader job manifest. Cannot be combined with other job options except --check.");
    SDL_Log("  %-*s %s", column_width, "--check", "With --manifest, regenerate manifest outputs into temporary files and report drift without overwriting existing outputs.");
}

static bool parse_cli_arguments(const char *programName, int argc, char *argv[], ShaderCross_CLIOptions *options, bool *handled)
{
    char *manifestFilename = NULL;
    bool manifestMode = false;
    bool checkManifest = false;
    bool accept_optionals = true;

    *handled = false;

    for (int i = 1; i < argc; i += 1) {
        char *arg = argv[i];

        if (accept_optionals && arg[0] == '-') {
            if (SDL_strcmp(arg, "-h") == 0 || SDL_strcmp(arg, "--help") == 0) {
                print_help();
                *handled = true;
                return true;
            } else if (SDL_strcmp(arg, "-s") == 0 || SDL_strcmp(arg, "--source") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetSourceFormat(options, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "-d") == 0 || SDL_strcmp(arg, "--dest") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetDestinationFormat(options, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "-t") == 0 || SDL_strcmp(arg, "--stage") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetStage(options, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "-e") == 0 || SDL_strcmp(arg, "--entrypoint") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->entrypoint_name, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "-I") == 0 || SDL_strcmp(arg, "--include") == 0) {
                if (options->include_dir) {
                    return SDL_SetError("'%s' can only be used once", arg);
                }
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->include_dir, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "-o") == 0 || SDL_strcmp(arg, "--output") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->output_filename, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "--resource-layout-c") == 0) {
                if (options->resource_layout_filename != NULL) {
                    return SDL_SetError("%s cannot be specified more than once", arg);
                }
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->resource_layout_filename, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "--resource-layout-symbol-prefix") == 0) {
                if (options->resource_layout_symbol_prefix != NULL) {
                    return SDL_SetError("%s cannot be specified more than once", arg);
                }
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->resource_layout_symbol_prefix, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "--resource-layout-sampled-slot") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!append_sampled_slot_policy(&options->sampled_slot_policies, &options->num_sampled_slot_policies, argv[i], "--resource-layout-sampled-slot")) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "--storage-texture-slot") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!append_storage_texture_slot_policy(&options->storage_texture_slot_policies, &options->num_storage_texture_slot_policies, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "--suggest-resource-layout-policy") == 0) {
                options->suggest_resource_layout_policy = true;
            } else if (SDL_strcmp(arg, "--tint") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->tint_executable, argv[i])) {
                    return false;
                }
            } else if (SDL_strncmp(argv[i], "-D", SDL_strlen("-D")) == 0) {
                if (!ShaderCross_CLIOptions_AddDefineArgument(options, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "--msl-version") == 0) {
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                if (!ShaderCross_CLIOptions_SetString(&options->msl_version, argv[i])) {
                    return false;
                }
            } else if (SDL_strcmp(arg, "-c") == 0 || SDL_strcmp(arg, "--cull") == 0) {
                options->cull_unused_bindings = true;
            } else if (SDL_strcmp(arg, "--allow-unvalidated-tint") == 0) {
                options->allow_unvalidated_tint = true;
            } else if (SDL_strcmp(arg, "-g") == 0 || SDL_strcmp(arg, "--debug") == 0) {
                options->enable_debug = true;
            } else if (SDL_strcmp(arg, "-p") == 0 || SDL_strcmp(arg, "--pssl") == 0) {
                options->pssl_compat = true;
            } else if (SDL_strcmp(arg, "--manifest") == 0) {
                if (manifestFilename != NULL) {
                    return SDL_SetError("%s cannot be specified more than once", arg);
                }
                if (i + 1 >= argc) {
                    return SDL_SetError("%s requires an argument", arg);
                }
                i += 1;
                manifestFilename = argv[i];
                manifestMode = true;
            } else if (SDL_strcmp(arg, "--check") == 0) {
                if (checkManifest) {
                    return SDL_SetError("%s cannot be specified more than once", arg);
                }
                checkManifest = true;
            } else if (SDL_strcmp(arg, "--") == 0) {
                accept_optionals = false;
            } else {
                return SDL_SetError("%s: Unknown argument: %s", programName, arg);
            }
        } else if (!options->filename) {
            if (!ShaderCross_CLIOptions_SetString(&options->filename, arg)) {
                return false;
            }
        } else {
            return SDL_SetError("%s: Unknown argument: %s", programName, arg);
        }
    }

    if (manifestMode) {
        if (!((argc == 3 && !checkManifest) || (argc == 4 && checkManifest))) {
            return SDL_SetError("--manifest cannot be combined with input paths or other job options except --check");
        }
        ShaderCross_CLIOptions_Free(options);
        ShaderCross_CLIOptions_Init(options);
        if (options->entrypoint_name == NULL) {
            return SDL_OutOfMemory();
        }
        if (!ShaderCross_CLI_LoadManifest(manifestFilename, options)) {
            return false;
        }
        options->check_manifest = checkManifest;
        return true;
    }

    if (checkManifest) {
        return SDL_SetError("--check requires --manifest");
    }

    return true;
}

static bool validate_cli_options(const char *programName, ShaderCross_CLIOptions *options)
{
    if (!options->filename) {
        return SDL_SetError("%s: missing input path", programName);
    }
    if (options->suggest_resource_layout_policy && (options->output_filename || options->num_shader_targets > 0 || options->destination_valid || options->resource_layout_filename || options->resource_layout_symbol_prefix)) {
        return SDL_SetError("%s", "--suggest-resource-layout-policy cannot be combined with -d, -o, --resource-layout-c, or --resource-layout-symbol-prefix");
    }
    if (!options->output_filename && options->num_shader_targets == 0 && !options->resource_layout_filename && !options->suggest_resource_layout_policy) {
        return SDL_SetError("%s: missing output path", programName);
    }
    if (options->resource_layout_filename && !options->resource_layout_symbol_prefix) {
        return SDL_SetError("%s is required with %s",
            "--resource-layout-symbol-prefix",
            "--resource-layout-c");
    }
    if (!options->resource_layout_filename && options->resource_layout_symbol_prefix) {
        return SDL_SetError("%s requires %s",
            "--resource-layout-symbol-prefix",
            "--resource-layout-c");
    }
    if (!options->resource_layout_filename && !options->suggest_resource_layout_policy && options->num_sampled_slot_policies > 0) {
        return SDL_SetError("%s requires %s",
            "--resource-layout-sampled-slot",
            "--resource-layout-c");
    }

    if (!options->source_valid) {
        if (SDL_strstr(options->filename, ".spv")) {
            options->spirv_source = true;
        } else if (SDL_strstr(options->filename, ".hlsl")) {
            options->spirv_source = false;
        } else {
            return SDL_SetError("%s", "Could not infer source format");
        }
    }

    if (options->output_filename && !options->destination_valid) {
        if (SDL_strstr(options->output_filename, ".dxbc")) {
            options->destination_format = SHADERFORMAT_DXBC;
        } else if (SDL_strstr(options->output_filename, ".dxil")) {
            options->destination_format = SHADERFORMAT_DXIL;
        } else if (SDL_strstr(options->output_filename, ".msl")) {
            options->destination_format = SHADERFORMAT_MSL;
        } else if (SDL_strstr(options->output_filename, ".spv")) {
            options->destination_format = SHADERFORMAT_SPIRV;
        } else if (SDL_strstr(options->output_filename, ".hlsl")) {
            options->destination_format = SHADERFORMAT_HLSL;
        } else if (SDL_strstr(options->output_filename, ".json")) {
            options->destination_format = SHADERFORMAT_JSON;
        } else if (SDL_strstr(options->output_filename, ".wgsl")) {
            options->destination_format = SHADERFORMAT_WGSL;
        } else {
            return SDL_SetError("%s", "Could not infer destination format");
        }
    } else if (!options->output_filename && options->destination_valid) {
        return SDL_SetError("%s", "Destination format requires an output path");
    }
    if (options->output_filename && options->num_shader_targets == 0 &&
        !ShaderCross_CLIOptions_AddShaderTarget(options, options->destination_format, options->output_filename)) {
        return false;
    }
    if (!validate_cli_output_paths_are_unique(options)) {
        return false;
    }
    const bool hasStorageTextureFormatAuthority = storage_texture_slot_policies_have_format_authority(options->storage_texture_slot_policies, options->num_storage_texture_slot_policies);
    /* Let the authority-specific check below report the stricter layout-output requirement. */
    if (!options->resource_layout_filename &&
        !options->suggest_resource_layout_policy &&
        !cli_has_shader_target_format(options, SHADERFORMAT_WGSL) &&
        !(!options->spirv_source && cli_has_shader_target_format(options, SHADERFORMAT_SPIRV) && hasStorageTextureFormatAuthority) &&
        options->num_storage_texture_slot_policies > 0) {
        return SDL_SetError("%s", "--storage-texture-slot requires --resource-layout-c or WGSL output");
    }
    if (hasStorageTextureFormatAuthority && !options->suggest_resource_layout_policy) {
        const bool supportsAuthorityOutput =
            cli_has_shader_target_format(options, SHADERFORMAT_WGSL) ||
            (!options->spirv_source && cli_has_shader_target_format(options, SHADERFORMAT_SPIRV));
        if (!supportsAuthorityOutput || !options->resource_layout_filename) {
            return SDL_SetError("%s", "--storage-texture-slot format_authority=explicit requires WGSL output or HLSL-to-SPIRV output with resource layout C output");
        }
    }

    if (!options->stage_valid) {
        if (SDL_strcasestr(options->filename, ".vert")) {
            options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
        } else if (SDL_strcasestr(options->filename, ".frag")) {
            options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
        } else if (SDL_strcasestr(options->filename, ".comp")) {
            options->shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
        } else {
            return SDL_SetError("%s", "Could not infer shader stage from filename");
        }
    }

    if (options->resource_layout_filename && !SDL_ShaderCross_INTERNAL_ValidateResourceLayoutCSymbolPrefix(options->resource_layout_symbol_prefix)) {
        char error[256];
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
        return SDL_SetError("Invalid %s: %s", "--resource-layout-symbol-prefix", error);
    }

    return true;
}

static char *transpile_hlsl_from_spirv_for_cli_target(
    const ShaderCross_CLIOptions *options,
    SDL_ShaderCross_SPIRV_Info *spirvInfo);

static bool write_cli_shader_output_data(SDL_IOStream *outputIO, const char *filename, const void *data, size_t size)
{
    if (size > 0 && SDL_WriteIO(outputIO, data, size) != size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write shader output '%s': %s", filename, SDL_GetError());
        return false;
    }
    return true;
}

static bool write_cli_shader_output_text(SDL_IOStream *outputIO, const char *filename, const char *text)
{
    return write_cli_shader_output_data(outputIO, filename, text, SDL_strlen(text));
}

static bool emit_cli_shader_output_from_spirv(
    const ShaderCross_CLIOptions *options,
    const ShaderCross_CLIShaderTarget *target,
    SDL_ShaderCross_SPIRV_Info *spirvInfo,
    const Uint8 *spirvBytecode,
    size_t spirvBytecodeSize,
    bool sourceIsSPIRV,
    bool directHlslDXILOutput,
    SDL_ShaderCross_HLSL_Info *hlslInfo)
{
    SDL_IOStream *outputIO = NULL;
    size_t bytecodeSize = 0;
    bool result = false;

    if (target->format != SHADERFORMAT_WGSL) {
        outputIO = SDL_IOFromFile(target->filename, ShaderCross_CLIOutputFileMode(target->format));
        if (outputIO == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
            return false;
        }
    }

    switch (target->format) {
    case SHADERFORMAT_DXBC: {
        Uint8 *buffer = SDL_ShaderCross_CompileDXBCFromSPIRV(
            spirvInfo,
            &bytecodeSize);
        if (buffer == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                sourceIsSPIRV ? "Failed to compile DXBC from SPIR-V: %s" : "Failed to compile DXBC from HLSL-derived SPIR-V: %s",
                SDL_GetError());
            goto done;
        }
        if (!write_cli_shader_output_data(outputIO, target->filename, buffer, bytecodeSize)) {
            SDL_free(buffer);
            goto done;
        }
        SDL_free(buffer);
        result = true;
        break;
    }

    case SHADERFORMAT_DXIL: {
#if SDL_PLATFORM_GDK
        Uint8 *buffer = directHlslDXILOutput ?
            SDL_ShaderCross_CompileDXILFromHLSL(hlslInfo, &bytecodeSize) :
            SDL_ShaderCross_CompileDXILFromSPIRV(spirvInfo, &bytecodeSize);
#else
        Uint8 *buffer = SDL_ShaderCross_CompileDXILFromSPIRV(
            spirvInfo,
            &bytecodeSize);
#endif
        if (buffer == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                sourceIsSPIRV ? "Failed to compile DXIL from SPIR-V: %s" :
#if SDL_PLATFORM_GDK
                    (directHlslDXILOutput ? "Failed to compile DXIL from HLSL: %s" : "Failed to compile DXIL from HLSL-derived SPIR-V: %s"),
#else
                    "Failed to compile DXIL from HLSL-derived SPIR-V: %s",
#endif
                SDL_GetError());
            goto done;
        }
        if (!write_cli_shader_output_data(outputIO, target->filename, buffer, bytecodeSize)) {
            SDL_free(buffer);
            goto done;
        }
        SDL_free(buffer);
        result = true;
        break;
    }

    case SHADERFORMAT_MSL: {
        char *buffer = SDL_ShaderCross_TranspileMSLFromSPIRV(spirvInfo);
        if (buffer == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                sourceIsSPIRV ? "Failed to transpile MSL from SPIR-V: %s" : "Failed to transpile MSL from HLSL-derived SPIR-V: %s",
                SDL_GetError());
            goto done;
        }
        if (!write_cli_shader_output_text(outputIO, target->filename, buffer)) {
            SDL_free(buffer);
            goto done;
        }
        SDL_free(buffer);
        result = true;
        break;
    }

    case SHADERFORMAT_HLSL: {
        char *buffer = transpile_hlsl_from_spirv_for_cli_target(options, spirvInfo);
        if (buffer == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                sourceIsSPIRV ? "Failed to transpile HLSL from SPIRV: %s" : "Failed to transpile HLSL from HLSL-derived SPIR-V: %s",
                SDL_GetError());
            goto done;
        }
        if (!write_cli_shader_output_text(outputIO, target->filename, buffer)) {
            SDL_free(buffer);
            goto done;
        }
        SDL_free(buffer);
        result = true;
        break;
    }

    case SHADERFORMAT_SPIRV:
        if (sourceIsSPIRV) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Input and output are both SPIRV. Did you mean to do that?");
            goto done;
        }
        if (!write_cli_shader_output_data(outputIO, target->filename, spirvBytecode, spirvBytecodeSize)) {
            goto done;
        }
        result = true;
        break;

    case SHADERFORMAT_JSON:
        if (!ShaderCross_CLI_WriteSPIRVReflectionJSON(outputIO, spirvBytecode, spirvBytecodeSize, options->entrypoint_name, options->shader_stage, sourceIsSPIRV)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                sourceIsSPIRV ? "Failed to write SPIRV reflection JSON: %s" : "Failed to write HLSL-derived SPIR-V reflection JSON: %s",
                SDL_GetError());
            goto done;
        }
        if (SDL_GetIOStatus(outputIO) != SDL_IO_STATUS_READY) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to write SPIRV reflection JSON '%s': %s", target->filename, SDL_GetError());
            goto done;
        }
        result = true;
        break;

    case SHADERFORMAT_WGSL:
        if (!write_wgsl_file_from_spirv(
                target->filename,
                spirvBytecode,
                spirvBytecodeSize,
                options->entrypoint_name,
                options->shader_stage,
                options->tint_executable,
                options->allow_unvalidated_tint,
                options->storage_texture_slot_policies,
                options->num_storage_texture_slot_policies)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                sourceIsSPIRV ? "Failed to transpile WGSL from SPIR-V: %s" : "Failed to transpile WGSL from HLSL: %s",
                SDL_GetError());
            goto done;
        }
        result = true;
        break;

    case SHADERFORMAT_INVALID:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Destination format not provided!");
        goto done;
    }

done:
    if (outputIO) {
        if (!SDL_CloseIO(outputIO)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to close shader output '%s': %s", target->filename, SDL_GetError());
            result = false;
        }
    }
    return result;
}

static char *transpile_hlsl_from_spirv_for_cli_target(
    const ShaderCross_CLIOptions *options,
    SDL_ShaderCross_SPIRV_Info *spirvInfo)
{
    const bool hadPSSLProperty = SDL_HasProperty(spirvInfo->props, SDL_SHADERCROSS_PROP_SPIRV_PSSL_COMPATIBILITY_BOOLEAN);
    const bool previousPSSLValue = SDL_GetBooleanProperty(spirvInfo->props, SDL_SHADERCROSS_PROP_SPIRV_PSSL_COMPATIBILITY_BOOLEAN, false);
    char *buffer = NULL;

    if (!SDL_SetBooleanProperty(spirvInfo->props, SDL_SHADERCROSS_PROP_SPIRV_PSSL_COMPATIBILITY_BOOLEAN, options->pssl_compat)) {
        return NULL;
    }

    buffer = SDL_ShaderCross_TranspileHLSLFromSPIRV(spirvInfo);

    if (hadPSSLProperty) {
        if (!SDL_SetBooleanProperty(spirvInfo->props, SDL_SHADERCROSS_PROP_SPIRV_PSSL_COMPATIBILITY_BOOLEAN, previousPSSLValue)) {
            SDL_free(buffer);
            return NULL;
        }
    } else if (!SDL_ClearProperty(spirvInfo->props, SDL_SHADERCROSS_PROP_SPIRV_PSSL_COMPATIBILITY_BOOLEAN)) {
        SDL_free(buffer);
        return NULL;
    }

    return buffer;
}

static bool build_cli_layout_provenance_note(
    const ShaderCross_CLIOptions *options,
    bool sourceIsSPIRV,
    bool hlslSpirvUsedForShaderOutput,
    const char *storageFormatAuthorityProvenance,
    char **ownedNote,
    const char **note)
{
    *ownedNote = NULL;
    *note = NULL;

    if (sourceIsSPIRV) {
        *note = options->num_shader_targets > 0 ?
            (options->cull_unused_bindings ?
                "shadercross: resource layout derived from the SPIR-V input bytecode used for CLI output; cull_unused_bindings=true." :
                "shadercross: resource layout derived from the SPIR-V input bytecode used for CLI output.") :
            (options->cull_unused_bindings ?
                "shadercross: resource layout derived from the SPIR-V input bytecode; cull_unused_bindings=true." :
                "shadercross: resource layout derived from the SPIR-V input bytecode.");
        return true;
    }

    if (storageFormatAuthorityProvenance) {
        if (SDL_asprintf(
                ownedNote,
                "shadercross: resource layout derived from the same final HLSL-to-SPIR-V bytecode%s; cull_unused_bindings=%s; %s.",
                hlslSpirvUsedForShaderOutput ? " used for CLI output" : "",
                options->cull_unused_bindings ? "true" : "false",
                storageFormatAuthorityProvenance) < 0) {
            return SDL_OutOfMemory();
        }
    } else {
        if (SDL_asprintf(
                ownedNote,
                "shadercross: resource layout derived from the same HLSL-to-SPIR-V bytecode%s; cull_unused_bindings=%s.",
                hlslSpirvUsedForShaderOutput ? " used for CLI output" : "",
                options->cull_unused_bindings ? "true" : "false") < 0) {
            return SDL_OutOfMemory();
        }
    }
    *note = *ownedNote;
    return true;
}

static bool emit_cli_resource_layout_from_spirv(
    const ShaderCross_CLIOptions *options,
    const Uint8 *spirvBytecode,
    size_t spirvBytecodeSize,
    bool sourceIsSPIRV,
    bool hlslSpirvUsedForShaderOutput,
    const char *storageFormatAuthorityProvenance)
{
    char *ownedProvenanceNote = NULL;
    const char *provenanceNote = NULL;
    bool result = false;

    if (!build_cli_layout_provenance_note(
            options,
            sourceIsSPIRV,
            hlslSpirvUsedForShaderOutput,
            storageFormatAuthorityProvenance,
            &ownedProvenanceNote,
            &provenanceNote)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to build resource layout provenance note: %s", SDL_GetError());
        return false;
    }

    result = write_resource_layout_c_file(
        options->resource_layout_filename,
        spirvBytecode,
        spirvBytecodeSize,
        options->resource_layout_symbol_prefix,
        options->filename,
        options->entrypoint_name,
        options->shader_stage,
        options->sampled_slot_policies,
        options->num_sampled_slot_policies,
        options->storage_texture_slot_policies,
        options->num_storage_texture_slot_policies,
        sourceIsSPIRV,
        provenanceNote);

    SDL_free(ownedProvenanceNote);
    return result;
}

static bool run_cli_outputs_from_spirv(
    const ShaderCross_CLIOptions *options,
    SDL_ShaderCross_SPIRV_Info *spirvInfo,
    const Uint8 *spirvBytecode,
    size_t spirvBytecodeSize,
    bool sourceIsSPIRV,
    SDL_ShaderCross_HLSL_Info *hlslInfo,
    bool hlslSpirvUsedForShaderOutput,
    const char *storageFormatAuthorityProvenance)
{
    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        const ShaderCross_CLIShaderTarget *target = &options->shader_targets[i];
        const bool directHlslDXILOutput = cli_shader_target_uses_direct_hlsl_dxil(options, target);

        if (!emit_cli_shader_output_from_spirv(
                options,
                target,
                spirvInfo,
                spirvBytecode,
                spirvBytecodeSize,
                sourceIsSPIRV,
                directHlslDXILOutput,
                hlslInfo)) {
            return false;
        }
    }

    if (options->suggest_resource_layout_policy &&
        !write_resource_layout_policy_suggestions_to_stdout(
            spirvBytecode,
            spirvBytecodeSize,
            options->entrypoint_name,
            options->shader_stage,
            options->sampled_slot_policies,
            options->num_sampled_slot_policies,
            options->storage_texture_slot_policies,
            options->num_storage_texture_slot_policies,
            sourceIsSPIRV)) {
        return false;
    }

    if (options->resource_layout_filename &&
        !emit_cli_resource_layout_from_spirv(
            options,
            spirvBytecode,
            spirvBytecodeSize,
            sourceIsSPIRV,
            hlslSpirvUsedForShaderOutput,
            storageFormatAuthorityProvenance)) {
        return false;
    }

    return true;
}

static int run_cli_job(const ShaderCross_CLIOptions *options)
{
    size_t fileSize = 0;
    void *fileData = NULL;
    int result = 0;

    fileData = SDL_LoadFile(options->filename, &fileSize);
    if (fileData == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid file (%s)", SDL_GetError());
        return 1;
    }

    const bool hasStorageTextureFormatAuthority = storage_texture_slot_policies_have_format_authority(
        options->storage_texture_slot_policies,
        options->num_storage_texture_slot_policies);

    if (options->spirv_source) {
        SDL_ShaderCross_SPIRV_Info spirvInfo;
        bool spirvInfoInitialized = false;
        SDL_zero(spirvInfo);
        if (!init_spirv_info_for_cli(
                &spirvInfo,
                (const Uint8 *)fileData,
                fileSize,
                options->entrypoint_name,
                options->shader_stage,
                options->filename,
                options->enable_debug,
                options->cull_unused_bindings,
                options->msl_version)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SPIR-V CLI properties: %s", SDL_GetError());
            result = 1;
        } else {
            spirvInfoInitialized = true;
        }

        if (result == 0 &&
            !run_cli_outputs_from_spirv(
                options,
                &spirvInfo,
                (const Uint8 *)fileData,
                fileSize,
                true,
                NULL,
                false,
                NULL)) {
            result = 1;
        }

        if (spirvInfoInitialized) {
            SDL_DestroyProperties(spirvInfo.props);
        }
    } else {
        SDL_ShaderCross_HLSL_Info hlslInfo;
        SDL_ShaderCross_SPIRV_Info spirvInfo;
        Uint8 *hlslSpirv = NULL;
        size_t hlslSpirvSize = 0;
        bool spirvInfoInitialized = false;
        char *storageFormatAuthorityProvenance = NULL;
        /* GDK DXIL intentionally preserves SDL_shadercross's direct HLSL-to-DXIL path.
           If resource-layout C output is also requested, it is produced from the
           canonical HLSL-to-SPIR-V pass but is not claimed as the bytes shipped for DXIL. */
        const bool hlslSpirvUsedForShaderOutput = cli_hlsl_spirv_used_for_shader_output(options);
        const bool needsHlslSpirv = options->suggest_resource_layout_policy || options->resource_layout_filename || hlslSpirvUsedForShaderOutput;
        const bool patchFinalSpirvForOutputAndLayout =
            cli_has_shader_target_format(options, SHADERFORMAT_SPIRV) &&
            hasStorageTextureFormatAuthority;

        SDL_zero(spirvInfo);
        hlslInfo.source = fileData;
        hlslInfo.entrypoint = options->entrypoint_name;
        hlslInfo.include_dir = options->include_dir;
        hlslInfo.defines = options->defines;
        hlslInfo.shader_stage = options->shader_stage;
        hlslInfo.props = SDL_CreateProperties();
        if (hlslInfo.props == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize HLSL CLI properties: %s", SDL_GetError());
            result = 1;
        }

        if (result == 0 && options->enable_debug) {
            SDL_SetBooleanProperty(hlslInfo.props, SDL_SHADERCROSS_PROP_SHADER_DEBUG_ENABLE_BOOLEAN, true);
            SDL_SetStringProperty(hlslInfo.props, SDL_SHADERCROSS_PROP_SHADER_DEBUG_NAME_STRING, options->filename);
        }

        if (result == 0 && options->cull_unused_bindings) {
            SDL_SetBooleanProperty(hlslInfo.props, SDL_SHADERCROSS_PROP_SHADER_CULL_UNUSED_BINDINGS_BOOLEAN, true);
        }

        if (result == 0 && needsHlslSpirv) {
            hlslSpirv = (Uint8 *)SDL_ShaderCross_CompileSPIRVFromHLSL(
                &hlslInfo,
                &hlslSpirvSize);

            if (hlslSpirv == NULL) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to compile HLSL to SPIR-V for CLI output/resource layout: %s", SDL_GetError());
                result = 1;
            }
        }

        if (result == 0 && patchFinalSpirvForOutputAndLayout) {
            if (!ShaderCross_CLI_UpdateStorageTextureFormatsForSPIRV(
                    &hlslSpirv,
                    &hlslSpirvSize,
                    options->entrypoint_name,
                    options->shader_stage,
                    options->storage_texture_slot_policies,
                    options->num_storage_texture_slot_policies,
                    &storageFormatAuthorityProvenance)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to apply storage texture format authority to SPIR-V output/resource layout: %s", SDL_GetError());
                result = 1;
            }
        }

        if (result == 0 && needsHlslSpirv) {
            if (!init_spirv_info_for_cli(
                    &spirvInfo,
                    hlslSpirv,
                    hlslSpirvSize,
                    options->entrypoint_name,
                    options->shader_stage,
                    options->filename,
                    options->enable_debug,
                    options->cull_unused_bindings,
                    options->msl_version)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SPIR-V CLI properties for HLSL output/resource layout: %s", SDL_GetError());
                result = 1;
            } else {
                spirvInfoInitialized = true;
            }
        }

        if (result == 0 &&
            !run_cli_outputs_from_spirv(
                options,
                &spirvInfo,
                hlslSpirv,
                hlslSpirvSize,
                false,
                &hlslInfo,
                hlslSpirvUsedForShaderOutput,
                storageFormatAuthorityProvenance)) {
            result = 1;
        }

        if (spirvInfoInitialized) {
            SDL_DestroyProperties(spirvInfo.props);
        }
        if (hlslInfo.props) {
            SDL_DestroyProperties(hlslInfo.props);
        }
        SDL_free(storageFormatAuthorityProvenance);
        SDL_free(hlslSpirv);
    }

    SDL_free(fileData);
    return result;
}

static bool shader_format_uses_manifest_check_exact_compare(ShaderCross_ShaderFormat format)
{
    return format != SHADERFORMAT_DXBC && format != SHADERFORMAT_DXIL;
}

static bool manifest_check_existing_output_is_file(const char *filename)
{
    SDL_PathInfo pathInfo;

    if (!SDL_GetPathInfo(filename, &pathInfo) || pathInfo.type != SDL_PATHTYPE_FILE) {
        SDL_SetError("manifest check missing-output: '%s'", filename);
        return false;
    }
    return true;
}

static bool manifest_check_generated_output_is_file(const char *manifestFilename, const char *tempFilename)
{
    SDL_PathInfo pathInfo;

    if (!SDL_GetPathInfo(tempFilename, &pathInfo) || pathInfo.type != SDL_PATHTYPE_FILE) {
        SDL_SetError("manifest check option-drift: regenerated output for '%s' was not produced", manifestFilename);
        return false;
    }
    return true;
}

static bool make_manifest_check_temp_filename(const char *filename, size_t index, char **tempFilename)
{
    const Uint64 stamp = SDL_GetTicksNS();

    *tempFilename = NULL;
    for (Uint32 attempt = 0; attempt < 128; attempt += 1) {
        SDL_PathInfo pathInfo;
        char *candidate = NULL;

        if (SDL_asprintf(&candidate, "%s.shadercross-check-%" SDL_PRIu64 "-%zu-%u.tmp", filename, stamp, index, attempt) < 0) {
            return SDL_OutOfMemory();
        }
        if (!SDL_GetPathInfo(candidate, &pathInfo) || pathInfo.type == SDL_PATHTYPE_NONE) {
            SDL_ClearError();
            *tempFilename = candidate;
            return true;
        }
        SDL_free(candidate);
    }
    return SDL_SetError("manifest check option-drift: could not reserve a temporary output path for '%s'", filename);
}

static bool manifest_check_compare_files_exact(const char *manifestFilename, const char *tempFilename, const char *driftKind)
{
    size_t manifestSize = 0;
    size_t tempSize = 0;
    void *manifestData = SDL_LoadFile(manifestFilename, &manifestSize);
    void *tempData = NULL;
    bool result = false;

    if (manifestData == NULL) {
        SDL_SetError("manifest check missing-output: '%s'", manifestFilename);
        return false;
    }

    tempData = SDL_LoadFile(tempFilename, &tempSize);
    if (tempData == NULL) {
        SDL_SetError("manifest check option-drift: regenerated output for '%s' could not be read", manifestFilename);
        goto done;
    }

    if (manifestSize != tempSize ||
        (manifestSize > 0 && SDL_memcmp(manifestData, tempData, manifestSize) != 0)) {
        SDL_SetError("manifest check %s: '%s' differs from regenerated output", driftKind, manifestFilename);
        goto done;
    }

    result = true;

done:
    SDL_free(tempData);
    SDL_free(manifestData);
    return result;
}

static bool remove_manifest_check_temp_file(const char *tempFilename, char *error, size_t errorSize)
{
    SDL_PathInfo pathInfo;

    if (tempFilename == NULL) {
        return true;
    }
    if (!SDL_GetPathInfo(tempFilename, &pathInfo) || pathInfo.type == SDL_PATHTYPE_NONE) {
        SDL_ClearError();
        return true;
    }
    if (!SDL_RemovePath(tempFilename)) {
        if (error[0] == '\0') {
            SDL_strlcpy(error, SDL_GetError(), errorSize);
        }
        return false;
    }
    return true;
}

static bool cleanup_manifest_check_temp_files(char **tempShaderFilenames, size_t numTempShaderFilenames, const char *tempLayoutFilename, char *error, size_t errorSize)
{
    bool result = true;

    if (errorSize > 0) {
        error[0] = '\0';
    }

    if (tempShaderFilenames != NULL) {
        for (size_t i = 0; i < numTempShaderFilenames; i += 1) {
            if (!remove_manifest_check_temp_file(tempShaderFilenames[i], error, errorSize)) {
                result = false;
            }
        }
    }
    if (!remove_manifest_check_temp_file(tempLayoutFilename, error, errorSize)) {
        result = false;
    }
    return result;
}

static bool run_cli_manifest_check(ShaderCross_CLIOptions *options)
{
    char **originalShaderFilenames = NULL;
    char **tempShaderFilenames = NULL;
    char *originalLayoutFilename = options->resource_layout_filename;
    char *tempLayoutFilename = NULL;
    size_t numSwappedShaderTargets = 0;
    bool result = false;

    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        if (!manifest_check_existing_output_is_file(options->shader_targets[i].filename)) {
            goto done;
        }
    }
    if (options->resource_layout_filename != NULL &&
        !manifest_check_existing_output_is_file(options->resource_layout_filename)) {
        goto done;
    }

    if (options->num_shader_targets > 0) {
        originalShaderFilenames = (char **)SDL_calloc(options->num_shader_targets, sizeof(*originalShaderFilenames));
        tempShaderFilenames = (char **)SDL_calloc(options->num_shader_targets, sizeof(*tempShaderFilenames));
        if (originalShaderFilenames == NULL || tempShaderFilenames == NULL) {
            SDL_OutOfMemory();
            goto done;
        }
    }

    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        originalShaderFilenames[i] = options->shader_targets[i].filename;
        if (!make_manifest_check_temp_filename(originalShaderFilenames[i], i, &tempShaderFilenames[i])) {
            goto done;
        }
        options->shader_targets[i].filename = tempShaderFilenames[i];
        numSwappedShaderTargets += 1;
    }

    if (options->resource_layout_filename != NULL) {
        if (!make_manifest_check_temp_filename(options->resource_layout_filename, options->num_shader_targets, &tempLayoutFilename)) {
            goto done;
        }
        options->resource_layout_filename = tempLayoutFilename;
    }

    if (run_cli_job(options) != 0) {
        char error[512];
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
        if (error[0] != '\0') {
            SDL_SetError("manifest check option-drift: failed to regenerate manifest outputs: %s", error);
        } else {
            SDL_SetError("%s", "manifest check option-drift: failed to regenerate manifest outputs");
        }
        goto done;
    }

    for (size_t i = 0; i < options->num_shader_targets; i += 1) {
        const ShaderCross_CLIShaderTarget *target = &options->shader_targets[i];
        const char *originalFilename = originalShaderFilenames[i];
        const char *tempFilename = tempShaderFilenames[i];

        if (!manifest_check_generated_output_is_file(originalFilename, tempFilename)) {
            goto done;
        }
        if (!shader_format_uses_manifest_check_exact_compare(target->format)) {
            SDL_Log("manifest check: '%s' regenerated; byte comparison skipped for DXBC/DXIL provenance-only output.", originalFilename);
            continue;
        }
        if (!manifest_check_compare_files_exact(originalFilename, tempFilename, "content-drift")) {
            goto done;
        }
    }

    if (originalLayoutFilename != NULL) {
        if (!manifest_check_generated_output_is_file(originalLayoutFilename, tempLayoutFilename)) {
            goto done;
        }
        if (!manifest_check_compare_files_exact(originalLayoutFilename, tempLayoutFilename, "policy-drift")) {
            goto done;
        }
    }

    result = true;

done:
    if (originalShaderFilenames != NULL) {
        for (size_t i = 0; i < numSwappedShaderTargets; i += 1) {
            options->shader_targets[i].filename = originalShaderFilenames[i];
        }
    }
    options->resource_layout_filename = originalLayoutFilename;
    if (tempShaderFilenames != NULL || tempLayoutFilename != NULL) {
        char primaryError[512];
        char cleanupError[512];
        SDL_strlcpy(primaryError, SDL_GetError(), sizeof(primaryError));
        if (!cleanup_manifest_check_temp_files(tempShaderFilenames, options->num_shader_targets, tempLayoutFilename, cleanupError, sizeof(cleanupError))) {
            if (result) {
                SDL_SetError("manifest check cleanup-error: failed to remove temporary outputs: %s", cleanupError);
                result = false;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "manifest check cleanup-error: failed to remove temporary outputs: %s", cleanupError);
                if (primaryError[0] != '\0') {
                    SDL_SetError("%s", primaryError);
                }
            }
        } else if (!result && primaryError[0] != '\0') {
            SDL_SetError("%s", primaryError);
        }
    }
    if (tempShaderFilenames != NULL) {
        for (size_t i = 0; i < options->num_shader_targets; i += 1) {
            SDL_free(tempShaderFilenames[i]);
        }
    }
    SDL_free(tempLayoutFilename);
    SDL_free(tempShaderFilenames);
    SDL_free(originalShaderFilenames);
    return result;
}

int main(int argc, char *argv[])
{
    ShaderCross_CLIOptions options;
    bool handled = false;
    int result = 0;

#ifdef LEAKCHECK
    SDLTest_TrackAllocations();
#endif

    ShaderCross_CLIOptions_Init(&options);
    if (options.entrypoint_name == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        return 1;
    }
    if (!parse_cli_arguments(argv[0], argc, argv, &options, &handled)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        print_help();
        ShaderCross_CLIOptions_Free(&options);
        return 1;
    }
    if (handled) {
        ShaderCross_CLIOptions_Free(&options);
        return 0;
    }
    if (!validate_cli_options(argv[0], &options)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        print_help();
        ShaderCross_CLIOptions_Free(&options);
        return 1;
    }

    if (!SDL_ShaderCross_Init())
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s", "Failed to initialize shadercross!");
        ShaderCross_CLIOptions_Free(&options);
        return 1;
    }

    if (options.check_manifest) {
        result = run_cli_manifest_check(&options) ? 0 : 1;
        if (result != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        }
    } else {
        result = run_cli_job(&options);
    }

    ShaderCross_CLIOptions_Free(&options);
    SDL_ShaderCross_Quit();
    SDL_Quit();

#ifdef LEAKCHECK
    SDLTest_LogAllocations();
#endif

    return result;
}
