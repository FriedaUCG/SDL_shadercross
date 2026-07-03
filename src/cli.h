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

#ifndef SDL_SHADERCROSS_CLI_H
#define SDL_SHADERCROSS_CLI_H

#include "SDL_shadercross_layout_facts.h"

#include <SDL3_shadercross/SDL_shadercross.h>

typedef enum ShaderCross_DestinationFormat {
    SHADERFORMAT_INVALID,
    SHADERFORMAT_SPIRV,
    SHADERFORMAT_DXBC,
    SHADERFORMAT_DXIL,
    SHADERFORMAT_MSL,
    SHADERFORMAT_HLSL,
    SHADERFORMAT_JSON,
    SHADERFORMAT_WGSL
} ShaderCross_ShaderFormat;

typedef struct ShaderCross_CLIShaderTarget {
    ShaderCross_ShaderFormat format;
    char *filename;
} ShaderCross_CLIShaderTarget;

typedef struct ShaderCross_CLIOptions {
    bool source_valid;
    bool destination_valid;
    bool stage_valid;

    bool spirv_source;
    ShaderCross_ShaderFormat destination_format;
    SDL_ShaderCross_ShaderStage shader_stage;

    char *filename;
    char *output_filename;
    char *resource_layout_filename;
    char *resource_layout_symbol_prefix;
    char *tint_executable;
    char *entrypoint_name;
    char *include_dir;
    char *msl_version;

    SDL_ShaderCross_HLSL_Define *defines;
    size_t num_defines;
    SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies;
    Uint32 num_sampled_slot_policies;
    SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies;
    Uint32 num_storage_texture_slot_policies;
    ShaderCross_CLIShaderTarget *shader_targets;
    size_t num_shader_targets;

    bool cull_unused_bindings;
    bool enable_debug;
    bool allow_unvalidated_tint;
    bool suggest_resource_layout_policy;
    bool check_manifest;
    bool pssl_compat;
} ShaderCross_CLIOptions;

void ShaderCross_CLIOptions_Init(ShaderCross_CLIOptions *options);
void ShaderCross_CLIOptions_Free(ShaderCross_CLIOptions *options);

bool ShaderCross_CLIOptions_SetSourceFormat(ShaderCross_CLIOptions *options, const char *value);
bool ShaderCross_CLIOptions_SetDestinationFormat(ShaderCross_CLIOptions *options, const char *value);
bool ShaderCross_CLIOptions_SetStage(ShaderCross_CLIOptions *options, const char *value);
bool ShaderCross_CLIOptions_SetString(char **field, const char *value);
bool ShaderCross_CLIOptions_AddDefine(ShaderCross_CLIOptions *options, const char *name, const char *value);
bool ShaderCross_CLIOptions_AddDefineArgument(ShaderCross_CLIOptions *options, const char *argument);
bool ShaderCross_CLIOptions_AppendSampledSlotPolicy(ShaderCross_CLIOptions *options, const char *argument, const char *option_name);
bool ShaderCross_CLIOptions_AppendStorageTextureSlotPolicy(ShaderCross_CLIOptions *options, const char *argument);
bool ShaderCross_CLIOptions_AddShaderTarget(ShaderCross_CLIOptions *options, ShaderCross_ShaderFormat format, const char *filename);
bool ShaderCross_CLIOptions_ParseDestinationFormat(const char *value, ShaderCross_ShaderFormat *format);

bool ShaderCross_CLI_LoadManifest(const char *manifest_path, ShaderCross_CLIOptions *options);

#endif
