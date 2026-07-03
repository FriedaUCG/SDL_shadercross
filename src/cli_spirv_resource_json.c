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

#include "SDL_shadercross_layout_facts.h"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <spirv_cross_c.h>

#include <stdarg.h>

static const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

static void write_json_string(SDL_IOStream *outputIO, const char *text)
{
    SDL_IOprintf(outputIO, "\"");

    if (text) {
        for (const unsigned char *ch = (const unsigned char *)text; *ch; ch += 1) {
            switch (*ch) {
            case '\\':
                SDL_IOprintf(outputIO, "\\\\");
                break;
            case '"':
                SDL_IOprintf(outputIO, "\\\"");
                break;
            case '\b':
                SDL_IOprintf(outputIO, "\\b");
                break;
            case '\f':
                SDL_IOprintf(outputIO, "\\f");
                break;
            case '\n':
                SDL_IOprintf(outputIO, "\\n");
                break;
            case '\r':
                SDL_IOprintf(outputIO, "\\r");
                break;
            case '\t':
                SDL_IOprintf(outputIO, "\\t");
                break;
            default:
                if (*ch < 0x20) {
                    SDL_IOprintf(outputIO, "\\u%04x", (unsigned int)*ch);
                } else {
                    SDL_IOprintf(outputIO, "%c", *ch);
                }
                break;
            }
        }
    }

    SDL_IOprintf(outputIO, "\"");
}

static void write_json_null_or_string(SDL_IOStream *outputIO, const char *text)
{
    if (text) {
        write_json_string(outputIO, text);
    } else {
        SDL_IOprintf(outputIO, "null");
    }
}

static void write_json_null_or_uint(SDL_IOStream *outputIO, Uint32 value, bool valid)
{
    if (valid) {
        SDL_IOprintf(outputIO, "%u", value);
    } else {
        SDL_IOprintf(outputIO, "null");
    }
}

static void write_c_comment_text(SDL_IOStream *outputIO, const char *text)
{
    if (!text) {
        return;
    }

    for (const unsigned char *ch = (const unsigned char *)text; *ch; ch += 1) {
        if ((*ch >= 'A' && *ch <= 'Z') ||
            (*ch >= 'a' && *ch <= 'z') ||
            (*ch >= '0' && *ch <= '9') ||
            *ch == ' ' ||
            *ch == '_' ||
            *ch == '-' ||
            *ch == '.' ||
            *ch == '<' ||
            *ch == '>' ||
            *ch == ':' ||
            *ch == '[' ||
            *ch == ']' ||
            *ch == '(' ||
            *ch == ')' ||
            *ch == '/') {
            SDL_IOprintf(outputIO, "%c", *ch);
        } else {
            SDL_IOprintf(outputIO, "_");
        }
    }
}

static const char *resource_type_to_string(spvc_resource_type resource_type)
{
    switch (resource_type) {
    case SPVC_RESOURCE_TYPE_UNIFORM_BUFFER:
        return "uniform_buffer";
    case SPVC_RESOURCE_TYPE_STORAGE_BUFFER:
        return "storage_buffer";
    case SPVC_RESOURCE_TYPE_STORAGE_IMAGE:
        return "storage_image";
    case SPVC_RESOURCE_TYPE_SAMPLED_IMAGE:
        return "sampled_image";
    case SPVC_RESOURCE_TYPE_SEPARATE_IMAGE:
        return "separate_image";
    case SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS:
        return "separate_sampler";
    default:
        return "unknown";
    }
}

static const char *execution_model_to_string(SpvExecutionModel execution_model)
{
    switch (execution_model) {
    case SpvExecutionModelVertex:
        return "vertex";
    case SpvExecutionModelFragment:
        return "fragment";
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        return "compute";
    default:
        return "unknown";
    }
}

static const char *base_type_to_string(spvc_basetype base_type)
{
    switch (base_type) {
    case SPVC_BASETYPE_IMAGE:
        return "image";
    case SPVC_BASETYPE_SAMPLED_IMAGE:
        return "sampled_image";
    case SPVC_BASETYPE_SAMPLER:
        return "sampler";
    case SPVC_BASETYPE_STRUCT:
        return "struct";
    case SPVC_BASETYPE_FP16:
    case SPVC_BASETYPE_FP32:
    case SPVC_BASETYPE_FP64:
        return "float";
    case SPVC_BASETYPE_INT8:
    case SPVC_BASETYPE_INT16:
    case SPVC_BASETYPE_INT32:
    case SPVC_BASETYPE_INT64:
        return "sint";
    case SPVC_BASETYPE_UINT8:
    case SPVC_BASETYPE_UINT16:
    case SPVC_BASETYPE_UINT32:
    case SPVC_BASETYPE_UINT64:
        return "uint";
    default:
        return "unknown";
    }
}

static const char *image_dimension_to_string(SpvDim dim, bool arrayed)
{
    switch (dim) {
    case SpvDim1D:
        return arrayed ? "1d_array" : "1d";
    case SpvDim2D:
        return arrayed ? "2d_array" : "2d";
    case SpvDim3D:
        return "3d";
    case SpvDimCube:
        return arrayed ? "cube_array" : "cube";
    case SpvDimRect:
        return "rect";
    case SpvDimBuffer:
        return "buffer";
    case SpvDimSubpassData:
        return "subpass";
    default:
        return "unknown";
    }
}

static const char *image_format_to_string(SpvImageFormat format)
{
    switch (format) {
    case SpvImageFormatUnknown:
        return "unknown";
    case SpvImageFormatRgba32f:
        return "rgba32f";
    case SpvImageFormatRgba16f:
        return "rgba16f";
    case SpvImageFormatRg32f:
        return "rg32f";
    case SpvImageFormatR32f:
        return "r32f";
    case SpvImageFormatRgba8:
        return "rgba8";
    case SpvImageFormatRgba8Snorm:
        return "rgba8_snorm";
    case SpvImageFormatRgba32i:
        return "rgba32i";
    case SpvImageFormatRgba16i:
        return "rgba16i";
    case SpvImageFormatRgba8i:
        return "rgba8i";
    case SpvImageFormatR32i:
        return "r32i";
    case SpvImageFormatRgba32ui:
        return "rgba32ui";
    case SpvImageFormatRgba16ui:
        return "rgba16ui";
    case SpvImageFormatRgba8ui:
        return "rgba8ui";
    case SpvImageFormatR32ui:
        return "r32ui";
    default:
        return "other";
    }
}

static const char *access_qualifier_to_string(SpvAccessQualifier access)
{
    switch (access) {
    case SpvAccessQualifierReadOnly:
        return "read_only";
    case SpvAccessQualifierWriteOnly:
        return "write_only";
    case SpvAccessQualifierReadWrite:
        return "read_write";
    default:
        return "unknown";
    }
}

static const char *storage_access_to_string(bool non_readable, bool non_writable)
{
    if (non_readable && !non_writable) {
        return "write_only";
    }
    if (!non_readable && non_writable) {
        return "read_only";
    }
    if (!non_readable && !non_writable) {
        return "read_write_or_unspecified";
    }
    return "invalid";
}

static const char *sampler_binding_type_to_string(bool depth_sample, bool comparison_sample, spvc_basetype sampled_base_type)
{
    if (comparison_sample) {
        return "comparison";
    }
    if (depth_sample) {
        return "non_filtering";
    }

    switch (sampled_base_type) {
    case SPVC_BASETYPE_INT8:
    case SPVC_BASETYPE_INT16:
    case SPVC_BASETYPE_INT32:
    case SPVC_BASETYPE_INT64:
    case SPVC_BASETYPE_UINT8:
    case SPVC_BASETYPE_UINT16:
    case SPVC_BASETYPE_UINT32:
    case SPVC_BASETYPE_UINT64:
        return "non_filtering";
    case SPVC_BASETYPE_FP16:
    case SPVC_BASETYPE_FP32:
    case SPVC_BASETYPE_FP64:
        return "filtering_or_non_filtering_float";
    default:
        return "unknown";
    }
}

static const char *resolved_sampler_binding_type_to_string(bool depth_sample, bool comparison_sample, spvc_basetype sampled_base_type)
{
    if (comparison_sample) {
        return "comparison";
    }
    if (depth_sample) {
        return "non_filtering";
    }

    switch (sampled_base_type) {
    case SPVC_BASETYPE_INT8:
    case SPVC_BASETYPE_INT16:
    case SPVC_BASETYPE_INT32:
    case SPVC_BASETYPE_INT64:
    case SPVC_BASETYPE_UINT8:
    case SPVC_BASETYPE_UINT16:
    case SPVC_BASETYPE_UINT32:
    case SPVC_BASETYPE_UINT64:
        return "non_filtering";
    default:
        return NULL;
    }
}

static const char *webgpu_storage_texture_format_to_string(SpvImageFormat format)
{
    switch (format) {
    case SpvImageFormatRgba32f:
        return "rgba32float";
    case SpvImageFormatRgba16f:
        return "rgba16float";
    case SpvImageFormatRg32f:
        return "rg32float";
    case SpvImageFormatR32f:
        return "r32float";
    case SpvImageFormatRgba8:
        return "rgba8unorm";
    case SpvImageFormatRgba8Snorm:
        return "rgba8snorm";
    case SpvImageFormatRgba32i:
        return "rgba32sint";
    case SpvImageFormatRgba16i:
        return "rgba16sint";
    case SpvImageFormatRgba8i:
        return "rgba8sint";
    case SpvImageFormatR32i:
        return "r32sint";
    case SpvImageFormatRgba32ui:
        return "rgba32uint";
    case SpvImageFormatRgba16ui:
        return "rgba16uint";
    case SpvImageFormatRgba8ui:
        return "rgba8uint";
    case SpvImageFormatR32ui:
        return "r32uint";
    default:
        return NULL;
    }
}

static const char *sdl_storage_texture_format_to_string(SpvImageFormat format)
{
    switch (format) {
    case SpvImageFormatRgba32f:
        return "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT";
    case SpvImageFormatRgba16f:
        return "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT";
    case SpvImageFormatRg32f:
        return "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT";
    case SpvImageFormatR32f:
        return "SDL_GPU_TEXTUREFORMAT_R32_FLOAT";
    case SpvImageFormatRgba8:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM";
    case SpvImageFormatRgba8Snorm:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM";
    case SpvImageFormatRgba32i:
        return "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT";
    case SpvImageFormatRgba16i:
        return "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT";
    case SpvImageFormatRgba8i:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT";
    case SpvImageFormatR32i:
        return "SDL_GPU_TEXTUREFORMAT_R32_INT";
    case SpvImageFormatRgba32ui:
        return "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT";
    case SpvImageFormatRgba16ui:
        return "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT";
    case SpvImageFormatRgba8ui:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT";
    case SpvImageFormatR32ui:
        return "SDL_GPU_TEXTUREFORMAT_R32_UINT";
    default:
        return NULL;
    }
}

static void write_storage_texture_feature_requirements_json(SDL_IOStream *outputIO, const char *final_access, SpvImageFormat format)
{
    bool wrote = false;

    SDL_IOprintf(outputIO, "[");

    if (final_access && (SDL_strcmp(final_access, "read_only") == 0 || SDL_strcmp(final_access, "read_write") == 0)) {
        write_json_string(outputIO, "readonly_and_readwrite_storage_textures");
        wrote = true;
    }

    if (final_access && SDL_strcmp(final_access, "read_write") == 0 && format == SpvImageFormatRgba8) {
        if (wrote) {
            SDL_IOprintf(outputIO, ", ");
        }
        write_json_string(outputIO, "texture-formats-tier2");
    }

    SDL_IOprintf(outputIO, "]");
}

static const char *buffer_binding_type_to_string(spvc_resource_type resource_type, bool non_readable, bool non_writable, unsigned descriptor_set)
{
    switch (resource_type) {
    case SPVC_RESOURCE_TYPE_UNIFORM_BUFFER:
        return "uniform";
    case SPVC_RESOURCE_TYPE_STORAGE_BUFFER:
        if (!non_readable && non_writable) {
            return "read_only_storage";
        }
        if (!non_readable && !non_writable && (descriptor_set == 0 || descriptor_set == 2)) {
            return "read_only_storage";
        }
        if (!non_readable && !non_writable) {
            return "storage";
        }
        return "unknown";
    default:
        return "unknown";
    }
}

typedef struct DrefUsageAnalysis
{
    SpvId *load_resource_ids;
    SpvId *sampled_image_ids;
    SpvId *sampled_sampler_ids;
    bool *sampled_image_uses_dref;
    bool *sampled_image_fetches;
    bool *image_resource_reads;
    bool *image_resource_writes;
    bool *image_resource_texel_pointers;
    bool *buffer_resource_writes;
    bool *buffer_resource_atomics;
    SDL_ShaderCross_INTERNAL_ImageDepthOperand *image_depth_operands;
    Uint32 id_bound;
} DrefUsageAnalysis;

typedef struct SPIRVFunctionRange
{
    size_t start_offset;
    size_t end_offset;
} SPIRVFunctionRange;

#define SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT 4

typedef struct SDLResourceLayoutContext
{
    SpvExecutionModel execution_model;
    Uint32 sampled_texture_slots[SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT];
    Uint32 storage_texture_slots[SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT];
    Uint32 storage_buffer_slots[SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT];
    Uint32 uniform_buffer_slots[SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT];
} SDLResourceLayoutContext;

static const char *storage_texture_resource_class(const SDLResourceLayoutContext *slot_context, unsigned descriptor_set)
{
    switch (slot_context->execution_model) {
    case SpvExecutionModelVertex:
    case SpvExecutionModelFragment:
        return "storage_texture";
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        if (descriptor_set == 0) {
            return "readonly_storage_texture";
        }
        if (descriptor_set == 1) {
            return "readwrite_storage_texture";
        }
        break;
    default:
        break;
    }

    return "unknown";
}

static const char *storage_buffer_resource_class(const SDLResourceLayoutContext *slot_context, unsigned descriptor_set, const char *binding_type)
{
    switch (slot_context->execution_model) {
    case SpvExecutionModelVertex:
    case SpvExecutionModelFragment:
        return "storage_buffer";
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        if (descriptor_set == 0) {
            return "readonly_storage_buffer";
        }
        if (descriptor_set == 1) {
            return "readwrite_storage_buffer";
        }
        break;
    default:
        break;
    }

    return binding_type;
}

static void free_dref_usage_analysis(DrefUsageAnalysis *analysis)
{
    SDL_free(analysis->load_resource_ids);
    SDL_free(analysis->sampled_image_ids);
    SDL_free(analysis->sampled_sampler_ids);
    SDL_free(analysis->sampled_image_uses_dref);
    SDL_free(analysis->sampled_image_fetches);
    SDL_free(analysis->image_resource_reads);
    SDL_free(analysis->image_resource_writes);
    SDL_free(analysis->image_resource_texel_pointers);
    SDL_free(analysis->buffer_resource_writes);
    SDL_free(analysis->buffer_resource_atomics);
    SDL_free(analysis->image_depth_operands);
    SDL_zero(*analysis);
}

static bool is_dref_sample_opcode(SpvOp opcode)
{
    switch (opcode) {
    case SpvOpImageSampleDrefImplicitLod:
    case SpvOpImageSampleDrefExplicitLod:
    case SpvOpImageSampleProjDrefImplicitLod:
    case SpvOpImageSampleProjDrefExplicitLod:
    case SpvOpImageSparseSampleDrefImplicitLod:
    case SpvOpImageSparseSampleDrefExplicitLod:
    case SpvOpImageSparseSampleProjDrefImplicitLod:
    case SpvOpImageSparseSampleProjDrefExplicitLod:
    case SpvOpImageDrefGather:
    case SpvOpImageSparseDrefGather:
        return true;
    default:
        return false;
    }
}

static bool spirv_instruction_string_matches(
    const SpvId *words,
    size_t instruction_offset,
    Uint16 word_length,
    Uint16 string_word_index,
    const char *text)
{
    const char *instruction_string;
    size_t max_bytes;
    size_t text_length;

    if (!text || string_word_index >= word_length) {
        return false;
    }

    max_bytes = (size_t)(word_length - string_word_index) * sizeof(SpvId);
    text_length = SDL_strlen(text);
    if (text_length >= max_bytes) {
        return false;
    }

    instruction_string = (const char *)&words[instruction_offset + string_word_index];
    return SDL_memcmp(instruction_string, text, text_length) == 0 && instruction_string[text_length] == '\0';
}

static bool collect_active_spirv_functions(
    bool **active_functions_out,
    const SpvId *words,
    size_t word_count,
    Uint32 id_bound,
    const char *entrypoint,
    SpvExecutionModel execution_model)
{
    SPIRVFunctionRange *function_ranges = NULL;
    bool *active_functions = NULL;
    SpvId entry_function_id = 0;
    SpvId current_function_id = 0;
    bool changed = false;
    bool result = false;

    *active_functions_out = NULL;

    if (id_bound == 0) {
        return true;
    }

    function_ranges = (SPIRVFunctionRange *)SDL_calloc(id_bound, sizeof(*function_ranges));
    active_functions = (bool *)SDL_calloc(id_bound, sizeof(*active_functions));
    if (!function_ranges || !active_functions) {
        SDL_free(function_ranges);
        SDL_free(active_functions);
        return SDL_OutOfMemory();
    }

    for (size_t offset = 5; offset < word_count;) {
        Uint32 instruction = words[offset];
        Uint16 word_length = (Uint16)(instruction >> 16);
        SpvOp opcode = (SpvOp)(instruction & 0xffffu);

        if (word_length == 0 || offset + word_length > word_count) {
            SDL_SetError("invalid SPIR-V instruction stream");
            goto done;
        }

        switch (opcode) {
        case SpvOpEntryPoint:
            if (word_length >= 4 &&
                words[offset + 1] == (SpvId)execution_model &&
                words[offset + 2] < id_bound &&
                spirv_instruction_string_matches(words, offset, word_length, 3, entrypoint)) {
                entry_function_id = words[offset + 2];
            }
            break;
        case SpvOpFunction:
            if (word_length >= 4 && words[offset + 2] < id_bound) {
                current_function_id = words[offset + 2];
                function_ranges[current_function_id].start_offset = offset;
                function_ranges[current_function_id].end_offset = word_count;
            } else {
                current_function_id = 0;
            }
            break;
        case SpvOpFunctionEnd:
            if (current_function_id < id_bound && function_ranges[current_function_id].start_offset != 0) {
                function_ranges[current_function_id].end_offset = offset + word_length;
            }
            current_function_id = 0;
            break;
        default:
            break;
        }

        offset += word_length;
    }

    if (entry_function_id == 0 || function_ranges[entry_function_id].start_offset == 0) {
        SDL_SetError("SPIR-V entry point '%s' function body is not present", entrypoint);
        goto done;
    }

    active_functions[entry_function_id] = true;
    do {
        changed = false;
        for (SpvId function_id = 1; function_id < id_bound; function_id += 1) {
            const SPIRVFunctionRange *range;

            if (!active_functions[function_id]) {
                continue;
            }

            range = &function_ranges[function_id];
            for (size_t offset = range->start_offset; offset < range->end_offset;) {
                Uint32 instruction = words[offset];
                Uint16 word_length = (Uint16)(instruction >> 16);
                SpvOp opcode = (SpvOp)(instruction & 0xffffu);

                if (word_length == 0 || offset + word_length > word_count) {
                    SDL_SetError("invalid SPIR-V instruction stream");
                    goto done;
                }

                if (opcode == SpvOpFunctionCall && word_length >= 4) {
                    SpvId called_function_id = words[offset + 3];
                    if (called_function_id < id_bound &&
                        function_ranges[called_function_id].start_offset != 0 &&
                        !active_functions[called_function_id]) {
                        active_functions[called_function_id] = true;
                        changed = true;
                    }
                }

                offset += word_length;
            }
        }
    } while (changed);

    *active_functions_out = active_functions;
    active_functions = NULL;
    result = true;

done:
    SDL_free(function_ranges);
    SDL_free(active_functions);
    return result;
}

static SpvId resolve_loaded_resource_id(const DrefUsageAnalysis *analysis, SpvId id)
{
    if (id < analysis->id_bound && analysis->load_resource_ids[id] != 0) {
        return analysis->load_resource_ids[id];
    }
    return id;
}

static void mark_atomic_pointer_usage(DrefUsageAnalysis *analysis, SpvId pointer_id, bool reads, bool writes)
{
    SpvId resource_id = resolve_loaded_resource_id(analysis, pointer_id);

    if (resource_id >= analysis->id_bound) {
        return;
    }

    if (analysis->image_resource_texel_pointers[resource_id]) {
        analysis->image_resource_reads[resource_id] |= reads;
        analysis->image_resource_writes[resource_id] |= writes;
    } else {
        analysis->buffer_resource_atomics[resource_id] = true;
        analysis->buffer_resource_writes[resource_id] |= writes;
    }
}

static bool analyze_dref_usage(
    DrefUsageAnalysis *analysis,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SpvExecutionModel execution_model)
{
    const SpvId *words = (const SpvId *)bytecode;
    size_t word_count = bytecode_size / sizeof(SpvId);
    bool *active_functions = NULL;
    SpvId current_function_id = 0;
    bool current_function_active = false;
    size_t offset;

    SDL_zero(*analysis);

    if (bytecode_size % sizeof(SpvId) != 0 || word_count < 5) {
        SDL_SetError("invalid SPIR-V bytecode size");
        return false;
    }

    analysis->id_bound = words[3];
    if (analysis->id_bound == 0) {
        return true;
    }

    if (!collect_active_spirv_functions(&active_functions, words, word_count, analysis->id_bound, entrypoint, execution_model)) {
        return false;
    }

    analysis->load_resource_ids = (SpvId *)SDL_calloc(analysis->id_bound, sizeof(*analysis->load_resource_ids));
    analysis->sampled_image_ids = (SpvId *)SDL_calloc(analysis->id_bound, sizeof(*analysis->sampled_image_ids));
    analysis->sampled_sampler_ids = (SpvId *)SDL_calloc(analysis->id_bound, sizeof(*analysis->sampled_sampler_ids));
    analysis->sampled_image_uses_dref = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->sampled_image_uses_dref));
    analysis->sampled_image_fetches = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->sampled_image_fetches));
    analysis->image_resource_reads = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->image_resource_reads));
    analysis->image_resource_writes = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->image_resource_writes));
    analysis->image_resource_texel_pointers = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->image_resource_texel_pointers));
    analysis->buffer_resource_writes = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->buffer_resource_writes));
    analysis->buffer_resource_atomics = (bool *)SDL_calloc(analysis->id_bound, sizeof(*analysis->buffer_resource_atomics));
    analysis->image_depth_operands = (SDL_ShaderCross_INTERNAL_ImageDepthOperand *)SDL_calloc(analysis->id_bound, sizeof(*analysis->image_depth_operands));
    if (!analysis->load_resource_ids || !analysis->sampled_image_ids || !analysis->sampled_sampler_ids || !analysis->sampled_image_uses_dref ||
        !analysis->sampled_image_fetches || !analysis->image_resource_reads || !analysis->image_resource_writes ||
        !analysis->image_resource_texel_pointers || !analysis->buffer_resource_writes || !analysis->buffer_resource_atomics ||
        !analysis->image_depth_operands) {
        free_dref_usage_analysis(analysis);
        SDL_free(active_functions);
        SDL_SetError("out of memory");
        return false;
    }

    for (offset = 5; offset < word_count;) {
        Uint32 instruction = words[offset];
        Uint16 word_length = (Uint16)(instruction >> 16);
        SpvOp opcode = (SpvOp)(instruction & 0xffffu);

        if (word_length == 0 || offset + word_length > word_count) {
            free_dref_usage_analysis(analysis);
            SDL_free(active_functions);
            SDL_SetError("invalid SPIR-V instruction stream");
            return false;
        }

        switch (opcode) {
        case SpvOpFunction:
            current_function_id = (word_length >= 4 && words[offset + 2] < analysis->id_bound) ? words[offset + 2] : 0;
            current_function_active = current_function_id != 0 && active_functions && active_functions[current_function_id];
            break;
        case SpvOpFunctionEnd:
            current_function_id = 0;
            current_function_active = false;
            break;
        case SpvOpTypeImage:
            if (word_length >= 9 && words[offset + 1] < analysis->id_bound) {
                switch (words[offset + 4]) {
                case 0:
                    analysis->image_depth_operands[words[offset + 1]] = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH;
                    break;
                case 1:
                    analysis->image_depth_operands[words[offset + 1]] = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_DEPTH;
                    break;
                case 2:
                default:
                    analysis->image_depth_operands[words[offset + 1]] = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN;
                    break;
                }
            }
            break;
        case SpvOpLoad:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4 && words[offset + 2] < analysis->id_bound) {
                analysis->load_resource_ids[words[offset + 2]] = words[offset + 3];
            }
            break;
        case SpvOpCopyObject:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4 && words[offset + 2] < analysis->id_bound) {
                analysis->load_resource_ids[words[offset + 2]] = resolve_loaded_resource_id(analysis, words[offset + 3]);
            }
            break;
        case SpvOpAccessChain:
        case SpvOpInBoundsAccessChain:
        case SpvOpPtrAccessChain:
        case SpvOpInBoundsPtrAccessChain:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4 && words[offset + 2] < analysis->id_bound) {
                analysis->load_resource_ids[words[offset + 2]] = resolve_loaded_resource_id(analysis, words[offset + 3]);
            }
            break;
        case SpvOpStore:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 3) {
                SpvId resource_id = resolve_loaded_resource_id(analysis, words[offset + 1]);
                if (resource_id < analysis->id_bound) {
                    analysis->buffer_resource_writes[resource_id] = true;
                }
            }
            break;
        case SpvOpCopyMemory:
        case SpvOpCopyMemorySized:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 3) {
                SpvId resource_id = resolve_loaded_resource_id(analysis, words[offset + 1]);
                if (resource_id < analysis->id_bound) {
                    analysis->buffer_resource_writes[resource_id] = true;
                }
            }
            break;
        case SpvOpAtomicStore:
        case SpvOpAtomicFlagClear:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 2) {
                mark_atomic_pointer_usage(analysis, words[offset + 1], false, true);
            }
            break;
        case SpvOpAtomicLoad:
        case SpvOpAtomicExchange:
        case SpvOpAtomicCompareExchange:
        case SpvOpAtomicCompareExchangeWeak:
        case SpvOpAtomicIIncrement:
        case SpvOpAtomicIDecrement:
        case SpvOpAtomicIAdd:
        case SpvOpAtomicISub:
        case SpvOpAtomicSMin:
        case SpvOpAtomicUMin:
        case SpvOpAtomicSMax:
        case SpvOpAtomicUMax:
        case SpvOpAtomicAnd:
        case SpvOpAtomicOr:
        case SpvOpAtomicXor:
        case SpvOpAtomicFlagTestAndSet:
        case SpvOpAtomicFMinEXT:
        case SpvOpAtomicFMaxEXT:
        case SpvOpAtomicFAddEXT:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4) {
                mark_atomic_pointer_usage(analysis, words[offset + 3], true, opcode != SpvOpAtomicLoad);
            }
            break;
        case SpvOpSampledImage:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 5 && words[offset + 2] < analysis->id_bound) {
                SpvId result_id = words[offset + 2];
                analysis->sampled_image_ids[result_id] = resolve_loaded_resource_id(analysis, words[offset + 3]);
                analysis->sampled_sampler_ids[result_id] = resolve_loaded_resource_id(analysis, words[offset + 4]);
            }
            break;
        case SpvOpImageFetch:
        case SpvOpImageSparseFetch:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4) {
                SpvId resource_id = resolve_loaded_resource_id(analysis, words[offset + 3]);
                if (resource_id < analysis->id_bound) {
                    analysis->sampled_image_fetches[resource_id] = true;
                }
            }
            break;
        case SpvOpImageRead:
        case SpvOpImageSparseRead:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4) {
                SpvId resource_id = resolve_loaded_resource_id(analysis, words[offset + 3]);
                if (resource_id < analysis->id_bound) {
                    analysis->image_resource_reads[resource_id] = true;
                }
            }
            break;
        case SpvOpImageWrite:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 4) {
                SpvId resource_id = resolve_loaded_resource_id(analysis, words[offset + 1]);
                if (resource_id < analysis->id_bound) {
                    analysis->image_resource_writes[resource_id] = true;
                }
            }
            break;
        case SpvOpImageTexelPointer:
            if (!current_function_active) {
                break;
            }
            if (word_length >= 5 && words[offset + 2] < analysis->id_bound) {
                SpvId resource_id = resolve_loaded_resource_id(analysis, words[offset + 3]);
                analysis->load_resource_ids[words[offset + 2]] = resource_id;
                if (resource_id < analysis->id_bound) {
                    analysis->image_resource_texel_pointers[resource_id] = true;
                }
            }
            break;
        default:
            if (!current_function_active) {
                break;
            }
            if (is_dref_sample_opcode(opcode) && word_length >= 4 && words[offset + 3] < analysis->id_bound) {
                analysis->sampled_image_uses_dref[words[offset + 3]] = true;
            }
            break;
        }

        offset += word_length;
    }

    SDL_free(active_functions);
    return true;
}

static bool pair_uses_dref_sample(const DrefUsageAnalysis *analysis, SpvId image_id, SpvId sampler_id)
{
    for (Uint32 id = 1; id < analysis->id_bound; id += 1) {
        if (analysis->sampled_image_uses_dref[id] &&
            analysis->sampled_image_ids[id] == image_id &&
            analysis->sampled_sampler_ids[id] == sampler_id) {
            return true;
        }
    }
    return false;
}

static bool image_has_sampled_image_fetch(const DrefUsageAnalysis *analysis, SpvId image_id)
{
    return image_id < analysis->id_bound && analysis->sampled_image_fetches[image_id];
}

static bool image_has_sampled_image_pair(const DrefUsageAnalysis *analysis, SpvId image_id)
{
    for (Uint32 id = 1; id < analysis->id_bound; id += 1) {
        if (analysis->sampled_image_ids[id] == image_id) {
            return true;
        }
    }
    return false;
}

static const char *observed_resource_access_to_string(const DrefUsageAnalysis *analysis, SpvId resource_id)
{
    bool reads;
    bool writes;

    if (resource_id >= analysis->id_bound) {
        return "unknown";
    }

    reads = analysis->image_resource_reads[resource_id];
    writes = analysis->image_resource_writes[resource_id];

    if (reads && writes) {
        return "read_write";
    }
    if (reads) {
        return "read_only";
    }
    if (writes) {
        return "write_only";
    }
    return "not_observed";
}

static spvc_basetype image_sampled_base_type(spvc_compiler compiler, spvc_type image_type)
{
    spvc_type_id sampled_type_id = spvc_type_get_image_sampled_type(image_type);
    spvc_type sampled_type;

    if (sampled_type_id == 0) {
        return SPVC_BASETYPE_UNKNOWN;
    }

    sampled_type = spvc_compiler_get_type_handle(compiler, sampled_type_id);
    if (!sampled_type) {
        return SPVC_BASETYPE_UNKNOWN;
    }

    return spvc_type_get_basetype(sampled_type);
}

static const spvc_reflected_resource *find_resource_by_id(spvc_resources resources, spvc_resource_type resource_type, spvc_variable_id id)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;

    if (spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count) < 0) {
        return NULL;
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        if (resource_list[i].id == id) {
            return &resource_list[i];
        }
    }

    return NULL;
}

static bool sampled_texture_pair_already_seen(
    const DrefUsageAnalysis *dref_usage,
    Uint32 current_id)
{
    SpvId image_id = dref_usage->sampled_image_ids[current_id];
    SpvId sampler_id = dref_usage->sampled_sampler_ids[current_id];

    for (Uint32 id = 1; id < current_id; id += 1) {
        if (dref_usage->sampled_image_ids[id] == image_id &&
            dref_usage->sampled_sampler_ids[id] == sampler_id) {
            return true;
        }
    }
    return false;
}

static const spvc_reflected_resource *find_texture_resource_by_id(spvc_resources resources, spvc_variable_id id)
{
    const spvc_reflected_resource *resource = find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, id);

    if (resource) {
        return resource;
    }

    return find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, id);
}

static spvc_basetype sampled_base_type_for_resource(spvc_compiler compiler, const spvc_reflected_resource *resource)
{
    spvc_type base_type;

    if (!resource) {
        return SPVC_BASETYPE_UNKNOWN;
    }

    base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
    if (!base_type) {
        return SPVC_BASETYPE_UNKNOWN;
    }

    return image_sampled_base_type(compiler, base_type);
}

static bool sampled_descriptor_set_for_execution_model(SpvExecutionModel execution_model, unsigned *descriptor_set);

static bool get_resource_binding(spvc_compiler compiler, const spvc_reflected_resource *resource, unsigned *descriptor_set, unsigned *binding)
{
    if (!resource ||
        !spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationDescriptorSet) ||
        !spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationBinding)) {
        return false;
    }

    *descriptor_set = spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationDescriptorSet);
    *binding = spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationBinding);
    return true;
}

static bool get_sdl_sampled_slot_for_pair(
    const SDLResourceLayoutContext *slot_context,
    bool has_image_binding,
    unsigned image_descriptor_set,
    unsigned image_binding,
    const spvc_reflected_resource *sampler,
    bool has_sampler_binding,
    unsigned sampler_descriptor_set,
    unsigned sampler_binding,
    Uint32 *slot)
{
    unsigned sampled_descriptor_set = 0;

    if (!sampled_descriptor_set_for_execution_model(slot_context->execution_model, &sampled_descriptor_set)) {
        return false;
    }
    if (!has_image_binding || image_descriptor_set != sampled_descriptor_set) {
        return false;
    }
    if (sampler &&
        (!has_sampler_binding ||
         sampler_descriptor_set != sampled_descriptor_set ||
         image_binding != sampler_binding)) {
        return false;
    }

    *slot = image_binding;
    return true;
}

static bool descriptor_set_is_in_sdl_range(unsigned descriptor_set)
{
    return descriptor_set < SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT;
}

static void note_slot_count(Uint32 *slot_count, unsigned binding)
{
    if (binding != 0xffffffffu && *slot_count < binding + 1) {
        *slot_count = binding + 1;
    }
}

static bool image_resource_is_storage(spvc_compiler compiler, const spvc_reflected_resource *resource)
{
    spvc_type base_type;

    if (!resource) {
        return false;
    }

    base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
    if (!base_type) {
        return false;
    }

    return spvc_type_get_image_is_storage(base_type) == SPVC_TRUE;
}

static bool collect_sdl_slot_resource_slots(
    SDLResourceLayoutContext *slot_context,
    spvc_compiler compiler,
    spvc_resources resources,
    spvc_resource_type resource_type,
    bool collect_storage_images)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    spvc_result result = spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count);

    if (result < 0) {
        SDL_SetError("spvc_resources_get_resource_list_for_type failed");
        return false;
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        unsigned descriptor_set;
        unsigned binding;

        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding) ||
            !descriptor_set_is_in_sdl_range(descriptor_set)) {
            continue;
        }

        if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_IMAGE ||
            (collect_storage_images && image_resource_is_storage(compiler, resource))) {
            Uint32 sampled_texture_slots = slot_context->sampled_texture_slots[descriptor_set];

            if (binding >= sampled_texture_slots) {
                note_slot_count(&slot_context->storage_texture_slots[descriptor_set], binding - sampled_texture_slots);
            }
        } else if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER) {
            if ((slot_context->execution_model == SpvExecutionModelVertex && descriptor_set == 0) ||
                (slot_context->execution_model == SpvExecutionModelFragment && descriptor_set == 2) ||
                ((slot_context->execution_model == SpvExecutionModelGLCompute ||
                  slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 0)) {
                Uint32 offset = slot_context->sampled_texture_slots[descriptor_set] + slot_context->storage_texture_slots[descriptor_set];
                if (binding >= offset) {
                    note_slot_count(&slot_context->storage_buffer_slots[descriptor_set], binding - offset);
                }
            } else if ((slot_context->execution_model == SpvExecutionModelGLCompute ||
                        slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 1) {
                Uint32 offset = slot_context->storage_texture_slots[descriptor_set];
                if (binding >= offset) {
                    note_slot_count(&slot_context->storage_buffer_slots[descriptor_set], binding - offset);
                }
            }
        } else if (resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER) {
            if ((slot_context->execution_model == SpvExecutionModelVertex && descriptor_set == 1) ||
                (slot_context->execution_model == SpvExecutionModelFragment && descriptor_set == 3) ||
                ((slot_context->execution_model == SpvExecutionModelGLCompute ||
                  slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 2)) {
                note_slot_count(&slot_context->uniform_buffer_slots[descriptor_set], binding);
            }
        } else if (resource_type == SPVC_RESOURCE_TYPE_SAMPLED_IMAGE ||
                   resource_type == SPVC_RESOURCE_TYPE_SEPARATE_IMAGE) {
            if (!image_resource_is_storage(compiler, resource)) {
                note_slot_count(&slot_context->sampled_texture_slots[descriptor_set], binding);
            }
        }
    }

    return true;
}

static bool collect_sdl_resource_layout_context(
    SDLResourceLayoutContext *slot_context,
    spvc_compiler compiler,
    spvc_resources resources)
{
    SDL_zero(*slot_context);
    slot_context->execution_model = spvc_compiler_get_execution_model(compiler);

    if (!collect_sdl_slot_resource_slots(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, false)) {
        return false;
    }
    if (!collect_sdl_slot_resource_slots(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, false)) {
        return false;
    }
    if (!collect_sdl_slot_resource_slots(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, false)) {
        return false;
    }
    if (!collect_sdl_slot_resource_slots(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, true)) {
        return false;
    }
    if (!collect_sdl_slot_resource_slots(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, false)) {
        return false;
    }
    if (!collect_sdl_slot_resource_slots(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, false)) {
        return false;
    }

    return true;
}

static const char *sdl_stage_for_descriptor_set(const SDLResourceLayoutContext *slot_context, unsigned descriptor_set)
{
    switch (slot_context->execution_model) {
    case SpvExecutionModelVertex:
        if (descriptor_set == 0 || descriptor_set == 1) {
            return "vertex";
        }
        break;
    case SpvExecutionModelFragment:
        if (descriptor_set == 2 || descriptor_set == 3) {
            return "fragment";
        }
        break;
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        if (descriptor_set == 0 || descriptor_set == 1 || descriptor_set == 2) {
            return "compute";
        }
        break;
    default:
        break;
    }

    return NULL;
}

static bool get_sdl_slot_for_resource(
    const SDLResourceLayoutContext *slot_context,
    spvc_resource_type resource_type,
    unsigned descriptor_set,
    unsigned binding,
    Uint32 *slot)
{
    if (!sdl_stage_for_descriptor_set(slot_context, descriptor_set) ||
        !descriptor_set_is_in_sdl_range(descriptor_set)) {
        return false;
    }

    switch (resource_type) {
    case SPVC_RESOURCE_TYPE_UNIFORM_BUFFER:
        if ((slot_context->execution_model == SpvExecutionModelVertex && descriptor_set == 1) ||
            (slot_context->execution_model == SpvExecutionModelFragment && descriptor_set == 3) ||
            ((slot_context->execution_model == SpvExecutionModelGLCompute ||
              slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 2)) {
            *slot = binding;
            return true;
        }
        break;
    case SPVC_RESOURCE_TYPE_STORAGE_IMAGE:
        if ((slot_context->execution_model == SpvExecutionModelVertex && descriptor_set == 0) ||
            (slot_context->execution_model == SpvExecutionModelFragment && descriptor_set == 2) ||
            ((slot_context->execution_model == SpvExecutionModelGLCompute ||
              slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 0)) {
            Uint32 offset = slot_context->sampled_texture_slots[descriptor_set];
            if (binding >= offset) {
                *slot = binding - offset;
                return true;
            }
        } else if ((slot_context->execution_model == SpvExecutionModelGLCompute ||
                    slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 1) {
            *slot = binding;
            return true;
        }
        break;
    case SPVC_RESOURCE_TYPE_STORAGE_BUFFER:
        if ((slot_context->execution_model == SpvExecutionModelVertex && descriptor_set == 0) ||
            (slot_context->execution_model == SpvExecutionModelFragment && descriptor_set == 2) ||
            ((slot_context->execution_model == SpvExecutionModelGLCompute ||
              slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 0)) {
            Uint32 offset = slot_context->sampled_texture_slots[descriptor_set] + slot_context->storage_texture_slots[descriptor_set];
            if (binding >= offset) {
                *slot = binding - offset;
                return true;
            }
        } else if ((slot_context->execution_model == SpvExecutionModelGLCompute ||
                    slot_context->execution_model == SpvExecutionModelKernel) && descriptor_set == 1) {
            Uint32 offset = slot_context->storage_texture_slots[descriptor_set];
            if (binding >= offset) {
                *slot = binding - offset;
                return true;
            }
        }
        break;
    default:
        break;
    }

    return false;
}

static void write_recommended_sdl_slot_json(
    SDL_IOStream *outputIO,
    const SDLResourceLayoutContext *slot_context,
    spvc_resource_type resource_type,
    unsigned descriptor_set,
    unsigned binding)
{
    Uint32 slot = 0;
    bool has_slot = get_sdl_slot_for_resource(slot_context, resource_type, descriptor_set, binding, &slot);

    SDL_IOprintf(outputIO, ", \"recommended_sdl_stage\": ");
    write_json_null_or_string(outputIO, sdl_stage_for_descriptor_set(slot_context, descriptor_set));
    SDL_IOprintf(outputIO, ", \"recommended_sdl_slot\": ");
    write_json_null_or_uint(outputIO, slot, has_slot);
}

static void write_resource_binding_json(SDL_IOStream *outputIO, spvc_compiler compiler, const spvc_reflected_resource *resource, const char *prefix)
{
    SDL_IOprintf(outputIO, ", \"%s_set\": ", prefix);
    if (resource && spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationDescriptorSet)) {
        SDL_IOprintf(outputIO, "%u", spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationDescriptorSet));
    } else {
        SDL_IOprintf(outputIO, "null");
    }

    SDL_IOprintf(outputIO, ", \"%s_binding\": ", prefix);
    if (resource && spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationBinding)) {
        SDL_IOprintf(outputIO, "%u", spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationBinding));
    } else {
        SDL_IOprintf(outputIO, "null");
    }
}

static bool resource_has_buffer_writes(const DrefUsageAnalysis *dref_usage, const spvc_reflected_resource *resource);
static bool resource_has_buffer_atomics(const DrefUsageAnalysis *dref_usage, const spvc_reflected_resource *resource);
static bool fragment_storage_binding_slot(const SDLResourceLayoutContext *slot_context, unsigned descriptor_set, unsigned binding, Uint32 *slot);
static SDL_ShaderCross_INTERNAL_StorageTextureAccess resource_layout_storage_access_from_evidence(bool non_readable, bool non_writable, SpvAccessQualifier access, const DrefUsageAnalysis *dref_usage, SpvId resource_id, bool allow_observed_storage_access);
static bool storage_texture_access_is_writable(SDL_ShaderCross_INTERNAL_StorageTextureAccess access);
static bool append_storage_texture_rejection_context(char **message, const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence);

static const char *buffer_resource_constant_class(
    const SDLResourceLayoutContext *slot_context,
    spvc_resource_type resource_type,
    unsigned descriptor_set,
    const char *binding_type)
{
    if (resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER) {
        return "uniform_buffer";
    }
    return storage_buffer_resource_class(slot_context, descriptor_set, binding_type);
}

typedef struct BufferMemberLayoutFacts
{
    const char *name;
    const char *matrix_major;
    spvc_basetype scalar_type;
    size_t size;
    unsigned offset;
    unsigned array_stride;
    unsigned matrix_stride;
    unsigned next_offset;
    unsigned vector_size;
    unsigned columns;
    unsigned array_dimensions;
    Uint32 scalar_size;
    bool has_size;
    bool has_offset;
    bool has_array_stride;
    bool has_matrix_stride;
    bool has_next_offset;
    bool is_float3;
    bool has_packed_following_member;
    bool has_tail_or_following_padding;
    bool has_runtime_or_dynamic_array;
} BufferMemberLayoutFacts;

typedef struct BufferResourceLayoutFacts
{
    spvc_type base_type;
    const char *binding_type;
    const char *resource_class;
    const char *constant_class;
    size_t size;
    unsigned descriptor_set;
    unsigned binding;
    Uint32 slot;
    unsigned member_count;
    bool non_readable;
    bool non_writable;
    bool is_fragment_writable_storage_buffer;
    bool has_fragment_storage_buffer_atomics;
    bool has_binding;
    bool has_slot;
    bool has_size;
    bool has_fixed_binding_size;
    bool has_runtime_or_dynamic_array;
} BufferResourceLayoutFacts;

typedef struct FragmentStorageBufferFactSlots
{
    Uint32 num_readonly_storage_textures;
    Uint32 num_readonly_storage_buffers;
    Uint32 num_writable_storage_textures;
    Uint32 num_writable_storage_buffers;
} FragmentStorageBufferFactSlots;

static const char *buffer_member_matrix_major(spvc_compiler compiler, spvc_type_id struct_type_id, unsigned member_index)
{
    if (spvc_compiler_has_member_decoration(compiler, struct_type_id, member_index, SpvDecorationRowMajor) == SPVC_TRUE) {
        return "row_major";
    }
    if (spvc_compiler_has_member_decoration(compiler, struct_type_id, member_index, SpvDecorationColMajor) == SPVC_TRUE) {
        return "column_major";
    }
    return NULL;
}

static const char *buffer_member_scalar_type(spvc_basetype base_type)
{
    switch (base_type) {
    case SPVC_BASETYPE_FP16:
        return "float16";
    case SPVC_BASETYPE_FP32:
        return "float32";
    case SPVC_BASETYPE_FP64:
        return "float64";
    case SPVC_BASETYPE_INT8:
        return "int8";
    case SPVC_BASETYPE_INT16:
        return "int16";
    case SPVC_BASETYPE_INT32:
        return "int32";
    case SPVC_BASETYPE_INT64:
        return "int64";
    case SPVC_BASETYPE_UINT8:
        return "uint8";
    case SPVC_BASETYPE_UINT16:
        return "uint16";
    case SPVC_BASETYPE_UINT32:
        return "uint32";
    case SPVC_BASETYPE_UINT64:
        return "uint64";
    case SPVC_BASETYPE_STRUCT:
        return "struct";
    default:
        return "unknown";
    }
}

static Uint32 buffer_member_scalar_size(spvc_basetype base_type)
{
    switch (base_type) {
    case SPVC_BASETYPE_FP16:
    case SPVC_BASETYPE_INT16:
    case SPVC_BASETYPE_UINT16:
        return 2;
    case SPVC_BASETYPE_FP32:
    case SPVC_BASETYPE_INT32:
    case SPVC_BASETYPE_UINT32:
        return 4;
    case SPVC_BASETYPE_FP64:
    case SPVC_BASETYPE_INT64:
    case SPVC_BASETYPE_UINT64:
        return 8;
    case SPVC_BASETYPE_INT8:
    case SPVC_BASETYPE_UINT8:
        return 1;
    default:
        return 0;
    }
}

static bool buffer_type_has_runtime_or_dynamic_array_recursive(spvc_compiler compiler, spvc_type type, unsigned depth)
{
    unsigned array_dimensions;

    if (!type) {
        return false;
    }
    if (depth > 16) {
        return true;
    }

    array_dimensions = spvc_type_get_num_array_dimensions(type);
    for (unsigned dimension = 0; dimension < array_dimensions; dimension += 1) {
        if (spvc_type_array_dimension_is_literal(type, dimension) != SPVC_TRUE ||
            spvc_type_get_array_dimension(type, dimension) == 0) {
            return true;
        }
    }

    if (spvc_type_get_basetype(type) == SPVC_BASETYPE_STRUCT) {
        unsigned member_count = spvc_type_get_num_member_types(type);
        for (unsigned member_index = 0; member_index < member_count; member_index += 1) {
            spvc_type_id member_type_id = spvc_type_get_member_type(type, member_index);
            spvc_type member_type = spvc_compiler_get_type_handle(compiler, member_type_id);
            if (buffer_type_has_runtime_or_dynamic_array_recursive(compiler, member_type, depth + 1u)) {
                return true;
            }
        }
    }

    return false;
}

static bool buffer_member_is_float3(spvc_type member_type)
{
    return member_type &&
           spvc_type_get_basetype(member_type) == SPVC_BASETYPE_FP32 &&
           spvc_type_get_vector_size(member_type) == 3 &&
           spvc_type_get_columns(member_type) == 1 &&
           spvc_type_get_num_array_dimensions(member_type) == 0;
}

static bool get_buffer_declared_size(spvc_compiler compiler, spvc_type base_type, size_t *declared_size)
{
    return base_type &&
           spvc_compiler_get_declared_struct_size(compiler, base_type, declared_size) >= 0;
}

static bool get_buffer_member_declared_size(
    spvc_compiler compiler,
    spvc_type base_type,
    unsigned member_index,
    size_t *declared_size)
{
    return base_type &&
           spvc_compiler_get_declared_struct_member_size(compiler, base_type, member_index, declared_size) >= 0;
}

static bool get_buffer_member_offset(
    spvc_compiler compiler,
    spvc_type base_type,
    unsigned member_index,
    unsigned *offset)
{
    return base_type &&
           spvc_compiler_type_struct_member_offset(compiler, base_type, member_index, offset) >= 0;
}

static bool get_buffer_member_array_stride(
    spvc_compiler compiler,
    spvc_type base_type,
    unsigned member_index,
    unsigned *stride)
{
    return base_type &&
           spvc_compiler_type_struct_member_array_stride(compiler, base_type, member_index, stride) >= 0;
}

static bool get_buffer_member_matrix_stride(
    spvc_compiler compiler,
    spvc_type base_type,
    unsigned member_index,
    unsigned *stride)
{
    return base_type &&
           spvc_compiler_type_struct_member_matrix_stride(compiler, base_type, member_index, stride) >= 0;
}

static bool get_buffer_next_member_offset(
    spvc_compiler compiler,
    spvc_type base_type,
    unsigned member_index,
    unsigned *next_offset)
{
    unsigned member_count = base_type ? spvc_type_get_num_member_types(base_type) : 0;
    if (member_index + 1u >= member_count) {
        return false;
    }
    return get_buffer_member_offset(compiler, base_type, member_index + 1u, next_offset);
}

static bool get_fragment_storage_buffer_fact_slot(
    const SDLResourceLayoutContext *slot_context,
    const FragmentStorageBufferFactSlots *fragment_slots,
    const BufferResourceLayoutFacts *facts,
    Uint32 *slot)
{
    Uint32 storage_slot;

    if (!fragment_slots ||
        facts->descriptor_set != 2 ||
        slot_context->execution_model != SpvExecutionModelFragment ||
        facts->binding < slot_context->sampled_texture_slots[facts->descriptor_set]) {
        return false;
    }

    storage_slot = facts->binding - slot_context->sampled_texture_slots[facts->descriptor_set];
    if (facts->is_fragment_writable_storage_buffer) {
        const Uint32 writable_start =
            fragment_slots->num_readonly_storage_textures +
            fragment_slots->num_readonly_storage_buffers +
            fragment_slots->num_writable_storage_textures;
        if (storage_slot < writable_start) {
            return false;
        }
        *slot = storage_slot - writable_start;
        if (*slot >= fragment_slots->num_writable_storage_buffers) {
            return false;
        }
    } else {
        if (storage_slot < fragment_slots->num_readonly_storage_textures) {
            return false;
        }
        *slot = storage_slot - fragment_slots->num_readonly_storage_textures;
        if (*slot >= fragment_slots->num_readonly_storage_buffers) {
            return false;
        }
    }

    return true;
}

static bool get_fragment_storage_texture_fact_slot(
    const SDLResourceLayoutContext *slot_context,
    const FragmentStorageBufferFactSlots *fragment_slots,
    unsigned descriptor_set,
    unsigned binding,
    bool writable,
    Uint32 *slot)
{
    Uint32 storage_slot;

    if (!fragment_slots ||
        !fragment_storage_binding_slot(slot_context, descriptor_set, binding, &storage_slot)) {
        return false;
    }

    if (writable) {
        const Uint32 writable_start =
            fragment_slots->num_readonly_storage_textures +
            fragment_slots->num_readonly_storage_buffers;
        if (storage_slot < writable_start) {
            return false;
        }
        *slot = storage_slot - writable_start;
        if (*slot >= fragment_slots->num_writable_storage_textures) {
            return false;
        }
    } else {
        *slot = storage_slot;
        if (*slot >= fragment_slots->num_readonly_storage_textures) {
            return false;
        }
    }

    return true;
}

static const char *resource_layout_storage_texture_access_to_string(SDL_ShaderCross_INTERNAL_StorageTextureAccess access)
{
    switch (access) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY:
        return "read_only";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY:
        return "write_only";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE:
        return "read_write";
    default:
        return NULL;
    }
}

static void collect_buffer_member_layout_facts(
    spvc_compiler compiler,
    spvc_type_id struct_type_id,
    spvc_type base_type,
    unsigned member_index,
    size_t buffer_size,
    BufferMemberLayoutFacts *facts)
{
    spvc_type_id member_type_id = spvc_type_get_member_type(base_type, member_index);
    spvc_type member_type = spvc_compiler_get_type_handle(compiler, member_type_id);
    unsigned float3_payload_end;

    SDL_zero(*facts);
    facts->scalar_type = member_type ? spvc_type_get_basetype(member_type) : SPVC_BASETYPE_UNKNOWN;
    facts->name = spvc_compiler_get_member_name(compiler, struct_type_id, member_index);
    facts->matrix_major = buffer_member_matrix_major(compiler, struct_type_id, member_index);
    facts->has_offset = get_buffer_member_offset(compiler, base_type, member_index, &facts->offset);
    facts->has_size = get_buffer_member_declared_size(compiler, base_type, member_index, &facts->size) && facts->size <= 0xffffffffu;
    facts->has_array_stride = get_buffer_member_array_stride(compiler, base_type, member_index, &facts->array_stride);
    facts->has_matrix_stride = get_buffer_member_matrix_stride(compiler, base_type, member_index, &facts->matrix_stride);
    facts->has_next_offset = get_buffer_next_member_offset(compiler, base_type, member_index, &facts->next_offset);
    facts->vector_size = member_type ? spvc_type_get_vector_size(member_type) : 0;
    facts->columns = member_type ? spvc_type_get_columns(member_type) : 0;
    facts->array_dimensions = member_type ? spvc_type_get_num_array_dimensions(member_type) : 0;
    facts->scalar_size = buffer_member_scalar_size(facts->scalar_type);
    facts->is_float3 = buffer_member_is_float3(member_type);
    facts->has_runtime_or_dynamic_array = buffer_type_has_runtime_or_dynamic_array_recursive(compiler, member_type, 0);

    float3_payload_end = facts->has_offset && facts->scalar_size > 0 ? facts->offset + 3u * facts->scalar_size : 0;
    facts->has_packed_following_member = facts->is_float3 && facts->has_offset && facts->has_next_offset && facts->next_offset == float3_payload_end;
    facts->has_tail_or_following_padding = facts->is_float3 && facts->has_offset && facts->scalar_size > 0 &&
        (facts->has_next_offset ? facts->next_offset > float3_payload_end : buffer_size > (size_t)float3_payload_end);
}

static void collect_buffer_resource_layout_facts(
    spvc_compiler compiler,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const FragmentStorageBufferFactSlots *fragment_slots,
    const spvc_reflected_resource *resource,
    spvc_resource_type resource_type,
    BufferResourceLayoutFacts *facts)
{
    SDL_zero(*facts);
    facts->non_readable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonReadable) == SPVC_TRUE;
    facts->non_writable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonWritable) == SPVC_TRUE;
    facts->has_binding = get_resource_binding(compiler, resource, &facts->descriptor_set, &facts->binding);
    facts->binding_type = buffer_binding_type_to_string(resource_type, facts->non_readable, facts->non_writable, facts->descriptor_set);
    facts->base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
    facts->member_count = facts->base_type ? spvc_type_get_num_member_types(facts->base_type) : 0;
    facts->has_size = get_buffer_declared_size(compiler, facts->base_type, &facts->size) && facts->size <= 0xffffffffu;
    facts->has_runtime_or_dynamic_array = buffer_type_has_runtime_or_dynamic_array_recursive(compiler, facts->base_type, 0);
    facts->has_fragment_storage_buffer_atomics =
        resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER &&
        slot_context->execution_model == SpvExecutionModelFragment &&
        dref_usage &&
        resource_has_buffer_atomics(dref_usage, resource);
    facts->is_fragment_writable_storage_buffer =
        resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER &&
        slot_context->execution_model == SpvExecutionModelFragment &&
        dref_usage &&
        !facts->has_fragment_storage_buffer_atomics &&
        resource_has_buffer_writes(dref_usage, resource);

    if (facts->has_binding) {
        if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER &&
            slot_context->execution_model == SpvExecutionModelFragment) {
            facts->has_slot = get_fragment_storage_buffer_fact_slot(slot_context, fragment_slots, facts, &facts->slot);
        } else {
            facts->has_slot = get_sdl_slot_for_resource(slot_context, resource_type, facts->descriptor_set, facts->binding, &facts->slot);
        }
    }

    if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER) {
        facts->resource_class = facts->has_binding ? storage_buffer_resource_class(slot_context, facts->descriptor_set, facts->binding_type) : "unknown";
    } else {
        facts->resource_class = "uniform_buffer";
    }
    facts->constant_class = facts->has_binding ? buffer_resource_constant_class(slot_context, resource_type, facts->descriptor_set, facts->binding_type) : facts->resource_class;
    facts->has_fixed_binding_size = facts->has_size && facts->size > 0 && facts->size <= 0x7fffffffu && !facts->has_runtime_or_dynamic_array;
}

static bool count_fragment_storage_texture_fact_slots_for_type(
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access,
    spvc_resource_type resource_type,
    bool collect_storage_images,
    FragmentStorageBufferFactSlots *slots)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;

    if (spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count) < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        spvc_type base_type;
        unsigned descriptor_set = 0;
        unsigned binding = 0;
        bool non_readable;
        bool non_writable;
        SDL_ShaderCross_INTERNAL_StorageTextureAccess access;

        if (resource_type != SPVC_RESOURCE_TYPE_STORAGE_IMAGE) {
            if (!collect_storage_images || !image_resource_is_storage(compiler, resource)) {
                continue;
            }
        }
        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding) ||
            !fragment_storage_binding_slot(slot_context, descriptor_set, binding, NULL)) {
            continue;
        }

        base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
        non_readable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonReadable) == SPVC_TRUE;
        non_writable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonWritable) == SPVC_TRUE;
        access = base_type ?
            resource_layout_storage_access_from_evidence(
                non_readable,
                non_writable,
                spvc_type_get_image_access_qualifier(base_type),
                dref_usage,
                resource->id,
                allow_observed_storage_access) :
            SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;

        if (storage_texture_access_is_writable(access)) {
            slots->num_writable_storage_textures += 1;
        } else {
            slots->num_readonly_storage_textures += 1;
        }
    }

    return true;
}

static bool collect_fragment_storage_fact_slots_for_json(
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access,
    FragmentStorageBufferFactSlots *slots)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;

    SDL_zero(*slots);

    if (slot_context->execution_model != SpvExecutionModelFragment) {
        return true;
    }
    if (!count_fragment_storage_texture_fact_slots_for_type(
            compiler,
            resources,
            slot_context,
            dref_usage,
            allow_observed_storage_access,
            SPVC_RESOURCE_TYPE_STORAGE_IMAGE,
            false,
            slots)) {
        return false;
    }
    if (!count_fragment_storage_texture_fact_slots_for_type(
            compiler,
            resources,
            slot_context,
            dref_usage,
            allow_observed_storage_access,
            SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,
            true,
            slots)) {
        return false;
    }

    if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &resource_list, &resource_count) < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        unsigned descriptor_set = 0;
        unsigned binding = 0;

        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding) ||
            !fragment_storage_binding_slot(slot_context, descriptor_set, binding, NULL)) {
            continue;
        }
        if (!resource_has_buffer_atomics(dref_usage, resource) &&
            resource_has_buffer_writes(dref_usage, resource)) {
            slots->num_writable_storage_buffers += 1;
        } else {
            slots->num_readonly_storage_buffers += 1;
        }
    }

    return true;
}

static void collect_fragment_storage_buffer_fact_slots_from_layout(
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    FragmentStorageBufferFactSlots *slots)
{
    SDL_zero(*slots);
    if (!layout) {
        return;
    }

    slots->num_readonly_storage_textures = layout->num_storage_textures;
    slots->num_readonly_storage_buffers = layout->num_storage_buffers;
}

static void write_buffer_member_layout_json(
    SDL_IOStream *outputIO,
    const BufferMemberLayoutFacts *facts,
    spvc_resource_type resource_type)
{
    bool wrote_hazard = false;

    SDL_IOprintf(outputIO, "{ \"name\": ");
    write_json_string(outputIO, facts->name);
    SDL_IOprintf(outputIO, ", \"offset\": ");
    write_json_null_or_uint(outputIO, facts->offset, facts->has_offset);
    SDL_IOprintf(outputIO, ", \"size\": ");
    write_json_null_or_uint(outputIO, (Uint32)facts->size, facts->has_size);
    SDL_IOprintf(outputIO, ", \"scalar_type\": ");
    write_json_string(outputIO, buffer_member_scalar_type(facts->scalar_type));
    SDL_IOprintf(outputIO, ", \"vector_size\": %u", facts->vector_size);
    SDL_IOprintf(outputIO, ", \"columns\": %u", facts->columns);
    SDL_IOprintf(outputIO, ", \"array_dimensions\": %u", facts->array_dimensions);
    SDL_IOprintf(outputIO, ", \"runtime_or_dynamic_array\": %s", json_bool(facts->has_runtime_or_dynamic_array));
    SDL_IOprintf(outputIO, ", \"array_stride\": ");
    write_json_null_or_uint(outputIO, facts->array_stride, facts->has_array_stride);
    SDL_IOprintf(outputIO, ", \"matrix_stride\": ");
    write_json_null_or_uint(outputIO, facts->matrix_stride, facts->has_matrix_stride);
    SDL_IOprintf(outputIO, ", \"matrix_major\": ");
    write_json_null_or_string(outputIO, facts->matrix_major);
    SDL_IOprintf(outputIO, ", \"hazards\": [");
    if (facts->is_float3) {
        write_json_string(outputIO, resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER ? "storage_buffer_float3_align16" : "float3_requires_cpu_padding_attention");
        wrote_hazard = true;
        if (facts->has_packed_following_member) {
            SDL_IOprintf(outputIO, ", ");
            write_json_string(outputIO, "scalar_or_member_after_float3_is_packed_at_plus12");
        } else if (facts->has_tail_or_following_padding) {
            SDL_IOprintf(outputIO, ", ");
            write_json_string(outputIO, "float3_tail_or_following_padding");
        }
    }
    if (facts->array_dimensions > 0) {
        if (wrote_hazard) {
            SDL_IOprintf(outputIO, ", ");
        }
        write_json_string(outputIO, facts->has_runtime_or_dynamic_array ? "runtime_or_dynamic_array_stride_must_match_cpu_layout" : "array_stride_must_match_cpu_layout");
        wrote_hazard = true;
    }
    if (facts->columns > 1 || facts->has_matrix_stride) {
        if (wrote_hazard) {
            SDL_IOprintf(outputIO, ", ");
        }
        write_json_string(outputIO, "matrix_stride_and_major_order_must_match_cpu_layout");
    }
    SDL_IOprintf(outputIO, "] }");
}

static void write_buffer_layout_members_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    const spvc_reflected_resource *resource,
    const BufferResourceLayoutFacts *resource_facts,
    spvc_resource_type resource_type)
{
    SDL_IOprintf(outputIO, "[");
    for (unsigned member_index = 0; member_index < resource_facts->member_count; member_index += 1) {
        BufferMemberLayoutFacts member_facts;
        if (member_index > 0) {
            SDL_IOprintf(outputIO, ", ");
        }
        collect_buffer_member_layout_facts(
            compiler,
            resource->base_type_id,
            resource_facts->base_type,
            member_index,
            resource_facts->has_size ? resource_facts->size : 0,
            &member_facts);
        write_buffer_member_layout_json(outputIO, &member_facts, resource_type);
    }
    SDL_IOprintf(outputIO, "]");
}

static void write_buffer_member_layout_comments(
    SDL_IOStream *outputIO,
    const BufferMemberLayoutFacts *facts,
    unsigned member_index,
    spvc_resource_type resource_type)
{
    const char *member_name = facts->name && facts->name[0] ? facts->name : "<unnamed>";

    SDL_IOprintf(
        outputIO,
        "//   member %u ",
        member_index);
    write_c_comment_text(outputIO, member_name);
    SDL_IOprintf(
        outputIO,
        ": offset=%s%u size=%s%u type=%s vec=%u columns=%u",
        facts->has_offset ? "" : "?",
        facts->has_offset ? facts->offset : 0,
        facts->has_size ? "" : "?",
        facts->has_size ? (unsigned)facts->size : 0,
        buffer_member_scalar_type(facts->scalar_type),
        facts->vector_size,
        facts->columns);
    if (facts->array_dimensions > 0) {
        SDL_IOprintf(outputIO, " array_dimensions=%u", facts->array_dimensions);
    }
    if (facts->has_runtime_or_dynamic_array) {
        SDL_IOprintf(outputIO, " runtime_or_dynamic_array=true");
    }
    if (facts->has_array_stride) {
        SDL_IOprintf(outputIO, " array_stride=%u", facts->array_stride);
    }
    if (facts->has_matrix_stride) {
        SDL_IOprintf(outputIO, " matrix_stride=%u", facts->matrix_stride);
    }
    if (facts->matrix_major) {
        SDL_IOprintf(outputIO, " matrix_major=%s", facts->matrix_major);
    }
    SDL_IOprintf(outputIO, "\n");

    if (facts->is_float3) {
        SDL_IOprintf(outputIO, "//   layout warning: member ");
        write_c_comment_text(outputIO, member_name);
        SDL_IOprintf(outputIO, " is float3; CPU upload structs must preserve the reflected offset/size");
        if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER) {
            SDL_IOprintf(outputIO, " and storage-buffer vec3 alignment");
        }
        if (facts->has_packed_following_member) {
            SDL_IOprintf(outputIO, "; the next member is packed at byte %u", facts->next_offset);
        } else if (facts->has_tail_or_following_padding) {
            SDL_IOprintf(outputIO, "; reflected layout leaves padding after the three float components");
        }
        SDL_IOprintf(outputIO, ".\n");
    }
    if (facts->array_dimensions > 0) {
        SDL_IOprintf(outputIO, "//   layout warning: member ");
        write_c_comment_text(outputIO, member_name);
        SDL_IOprintf(outputIO, " is an array; CPU upload stride must match reflected array_stride when present");
        if (facts->has_runtime_or_dynamic_array) {
            SDL_IOprintf(outputIO, "; no fixed binding-size constant is emitted for runtime or specialization-sized arrays");
        }
        SDL_IOprintf(outputIO, ".\n");
    }
    if (facts->columns > 1 || facts->has_matrix_stride) {
        SDL_IOprintf(outputIO, "//   layout warning: member ");
        write_c_comment_text(outputIO, member_name);
        SDL_IOprintf(outputIO, " is a matrix; CPU upload layout must match reflected matrix_stride and matrix_major.\n");
    }
}

static bool write_buffer_layout_c_for_type(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const char *symbol_prefix,
    spvc_resource_type resource_type)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    FragmentStorageBufferFactSlots fragment_slots;
    const FragmentStorageBufferFactSlots *fragment_slots_ptr = NULL;

    if (spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count) < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER &&
        slot_context->execution_model == SpvExecutionModelFragment) {
        collect_fragment_storage_buffer_fact_slots_from_layout(layout, &fragment_slots);
        fragment_slots_ptr = &fragment_slots;
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        BufferResourceLayoutFacts facts;

        collect_buffer_resource_layout_facts(compiler, slot_context, dref_usage, fragment_slots_ptr, resource, resource_type, &facts);
        if (!facts.has_binding || !facts.has_slot) {
            continue;
        }

        SDL_IOprintf(
            outputIO,
            "// buffer layout: %s slot %u source_set=%u source_binding=%u name=",
            facts.constant_class,
            (unsigned)facts.slot,
            facts.descriptor_set,
            facts.binding);
        write_c_comment_text(outputIO, resource->name && resource->name[0] ? resource->name : "<unnamed>");
        SDL_IOprintf(outputIO, " size=");
        if (facts.has_size) {
            SDL_IOprintf(outputIO, "%u", (unsigned)facts.size);
        } else {
            SDL_IOprintf(outputIO, "unknown");
        }
        SDL_IOprintf(outputIO, " fixed_size=%s\n", json_bool(facts.has_fixed_binding_size));
        if (facts.has_fixed_binding_size) {
            SDL_IOprintf(outputIO, "enum { %s_%s_%u_size = %u };\n", symbol_prefix, facts.constant_class, (unsigned)facts.slot, (unsigned)facts.size);
        } else if (!facts.has_size) {
            SDL_IOprintf(outputIO, "//   no fixed binding-size constant emitted; reflected block size is unknown or too large.\n");
        } else if (facts.has_runtime_or_dynamic_array) {
            SDL_IOprintf(outputIO, "//   no fixed binding-size constant emitted; block includes a runtime or specialization-sized array.\n");
        } else if (facts.size > 0x7fffffffu) {
            SDL_IOprintf(outputIO, "//   no fixed binding-size constant emitted; reflected block size is too large for a portable C enum constant.\n");
        } else {
            SDL_IOprintf(outputIO, "//   no fixed binding-size constant emitted; reflected block size is zero.\n");
        }

        for (unsigned member_index = 0; member_index < facts.member_count; member_index += 1) {
            BufferMemberLayoutFacts member_facts;
            collect_buffer_member_layout_facts(
                compiler,
                resource->base_type_id,
                facts.base_type,
                member_index,
                facts.has_size ? facts.size : 0,
                &member_facts);
            write_buffer_member_layout_comments(outputIO, &member_facts, member_index, resource_type);
        }
        SDL_IOprintf(outputIO, "\n");
    }

    return true;
}

static bool write_resource_layout_buffer_facts_c(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const char *symbol_prefix)
{
    return write_buffer_layout_c_for_type(outputIO, compiler, resources, slot_context, dref_usage, layout, symbol_prefix, SPVC_RESOURCE_TYPE_STORAGE_BUFFER) &&
           write_buffer_layout_c_for_type(outputIO, compiler, resources, slot_context, dref_usage, layout, symbol_prefix, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER);
}

static void write_image_details_json(SDL_IOStream *outputIO, spvc_compiler compiler, spvc_type image_type)
{
    bool arrayed = spvc_type_get_image_arrayed(image_type) == SPVC_TRUE;
    bool depth = spvc_type_get_image_is_depth(image_type) == SPVC_TRUE;
    spvc_basetype sampled_base_type = image_sampled_base_type(compiler, image_type);

    SDL_IOprintf(outputIO, ", \"dimension\": ");
    write_json_string(outputIO, image_dimension_to_string(spvc_type_get_image_dimension(image_type), arrayed));
    SDL_IOprintf(outputIO, ", \"arrayed\": %s", json_bool(arrayed));
    SDL_IOprintf(outputIO, ", \"multisampled\": %s", json_bool(spvc_type_get_image_multisampled(image_type) == SPVC_TRUE));
    SDL_IOprintf(outputIO, ", \"depth\": %s", json_bool(depth));
    SDL_IOprintf(outputIO, ", \"storage\": %s", json_bool(spvc_type_get_image_is_storage(image_type) == SPVC_TRUE));
    SDL_IOprintf(outputIO, ", \"sampled_type\": ");
    write_json_string(outputIO, depth ? "depth" : base_type_to_string(sampled_base_type));
    SDL_IOprintf(outputIO, ", \"storage_format\": ");
    write_json_string(outputIO, image_format_to_string(spvc_type_get_image_storage_format(image_type)));
    SDL_IOprintf(outputIO, ", \"spv_access\": ");
    write_json_string(outputIO, access_qualifier_to_string(spvc_type_get_image_access_qualifier(image_type)));
}

static void write_sampled_pair_image_shape_json(SDL_IOStream *outputIO, spvc_compiler compiler, const spvc_reflected_resource *image)
{
    spvc_type image_type = image ? spvc_compiler_get_type_handle(compiler, image->base_type_id) : NULL;

    SDL_IOprintf(outputIO, ", \"image_dimension\": ");
    if (image_type) {
        bool arrayed = spvc_type_get_image_arrayed(image_type) == SPVC_TRUE;
        write_json_string(outputIO, image_dimension_to_string(spvc_type_get_image_dimension(image_type), arrayed));
        SDL_IOprintf(outputIO, ", \"image_arrayed\": %s", json_bool(arrayed));
        SDL_IOprintf(outputIO, ", \"image_multisampled\": %s", json_bool(spvc_type_get_image_multisampled(image_type) == SPVC_TRUE));
        SDL_IOprintf(outputIO, ", \"image_depth\": %s", json_bool(spvc_type_get_image_is_depth(image_type) == SPVC_TRUE));
    } else {
        SDL_IOprintf(outputIO, "null, \"image_arrayed\": null, \"image_multisampled\": null, \"image_depth\": null");
    }
}

static void format_combined_sampler_resource_reference(
    char *buffer,
    size_t buffer_size,
    spvc_compiler compiler,
    const SDLResourceLayoutContext *slot_context,
    const spvc_reflected_resource *resource,
    SpvId id,
    const char *kind)
{
    unsigned descriptor_set = 0;
    unsigned binding = 0;
    unsigned sampled_descriptor_set = 0;
    const char *name;
    const char *stage;

    if (!resource) {
        SDL_snprintf(buffer, buffer_size, "%s id %u is not reflected as a selected-entrypoint resource", kind, (unsigned)id);
        return;
    }

    name = (resource->name && resource->name[0]) ? resource->name : "<unnamed>";
    if (!get_resource_binding(compiler, resource, &descriptor_set, &binding)) {
        SDL_snprintf(buffer, buffer_size, "%s '%s' id %u has no descriptor set/binding", kind, name, (unsigned)id);
        return;
    }

    if (sampled_descriptor_set_for_execution_model(slot_context->execution_model, &sampled_descriptor_set) &&
        descriptor_set == sampled_descriptor_set) {
        stage = sdl_stage_for_descriptor_set(slot_context, descriptor_set);
        SDL_snprintf(
            buffer,
            buffer_size,
            "%s '%s' id %u set %u binding %u (%s sampled slot %u)",
            kind,
            name,
            (unsigned)id,
            descriptor_set,
            binding,
            stage ? stage : "unknown",
            binding);
    } else {
        SDL_snprintf(
            buffer,
            buffer_size,
            "%s '%s' id %u set %u binding %u (outside SDL sampled-texture sets for this stage)",
            kind,
            name,
            (unsigned)id,
            descriptor_set,
            binding);
    }
}

static void format_combined_sampler_failure_message(
    char *buffer,
    size_t buffer_size,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage)
{
    char image_reference[256];
    char sampler_reference[256];

    for (Uint32 id = 1; id < dref_usage->id_bound; id += 1) {
        SpvId image_id = dref_usage->sampled_image_ids[id];
        SpvId sampler_id = dref_usage->sampled_sampler_ids[id];
        const spvc_reflected_resource *image;
        const spvc_reflected_resource *sampler;

        if (!image_id || !sampler_id || sampled_texture_pair_already_seen(dref_usage, id)) {
            continue;
        }

        image = find_texture_resource_by_id(resources, image_id);
        sampler = find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, sampler_id);
        format_combined_sampler_resource_reference(image_reference, sizeof(image_reference), compiler, slot_context, image, image_id, "image");
        format_combined_sampler_resource_reference(sampler_reference, sizeof(sampler_reference), compiler, slot_context, sampler, sampler_id, "sampler");
        SDL_snprintf(
            buffer,
            buffer_size,
            "sampled texture/sampler pair analysis failed: SPIRV-Cross could not build combined image samplers and SDL_shadercross could not recover this OpSampledImage pair from selected-entrypoint resources; %s; %s. Unrecoverable shapes usually involve image/sampler IDs that are not selected-entrypoint resources, such as inactive-entrypoint uses or non-resource helper/array-carried values.",
            image_reference,
            sampler_reference);
        return;
    }

    SDL_snprintf(
        buffer,
        buffer_size,
        "sampled texture/sampler pair analysis failed: SPIRV-Cross could not build combined image samplers and SDL_shadercross found no OpSampledImage image/sampler pairs in the selected SPIR-V scan");
}

static bool write_sampled_texture_pairs_from_spirv_scan_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool write_sdl_layout_entries,
    bool *wrote_pair,
    bool *skipped_pair,
    char *skipped_pair_message,
    size_t skipped_pair_message_size);

static void write_resource_binding_json(SDL_IOStream *outputIO, spvc_compiler compiler, const spvc_reflected_resource *resource, const char *prefix);
static void write_source_binding_json(SDL_IOStream *outputIO, const char *prefix, unsigned descriptor_set, unsigned binding, bool has_binding);
static void write_sampled_pair_image_shape_json(SDL_IOStream *outputIO, spvc_compiler compiler, const spvc_reflected_resource *image);

static bool write_resource_array_json(SDL_IOStream *outputIO, spvc_compiler compiler, spvc_resources resources, const SDLResourceLayoutContext *slot_context, const DrefUsageAnalysis *dref_usage, spvc_resource_type resource_type, const char *name)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    spvc_result result = spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count);

    if (result < 0) {
        SDL_SetError("spvc_resources_get_resource_list_for_type failed");
        return false;
    }

    SDL_IOprintf(outputIO, "\"%s\": [", name);

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        spvc_type base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
        spvc_basetype base = base_type ? spvc_type_get_basetype(base_type) : SPVC_BASETYPE_UNKNOWN;
        bool non_readable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonReadable) == SPVC_TRUE;
        bool non_writable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonWritable) == SPVC_TRUE;
        bool depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, resource->id) == SPVC_TRUE;
        unsigned descriptor_set = spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationDescriptorSet);
        unsigned binding = spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationBinding);

        if (i > 0) {
            SDL_IOprintf(outputIO, ", ");
        }

        SDL_IOprintf(outputIO, "{ \"name\": ");
        write_json_string(outputIO, resource->name);
        SDL_IOprintf(outputIO, ", \"resource_type\": ");
        write_json_string(outputIO, resource_type_to_string(resource_type));
        SDL_IOprintf(outputIO, ", \"id\": %u, \"base_type_id\": %u, \"type_id\": %u", resource->id, resource->base_type_id, resource->type_id);
        SDL_IOprintf(outputIO, ", \"set\": %u, \"binding\": %u", descriptor_set, binding);
        SDL_IOprintf(outputIO, ", \"base_type\": ");
        write_json_string(outputIO, base_type_to_string(base));
        SDL_IOprintf(outputIO, ", \"non_readable\": %s, \"non_writable\": %s", json_bool(non_readable), json_bool(non_writable));
        SDL_IOprintf(outputIO, ", \"depth_or_compare\": %s", json_bool(depth_or_compare));

        if (resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER || resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER) {
            SDL_IOprintf(outputIO, ", \"recommended_buffer_binding_type\": ");
            write_json_string(outputIO, buffer_binding_type_to_string(resource_type, non_readable, non_writable, descriptor_set));
        }

        if (resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER ||
            resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER ||
            resource_type == SPVC_RESOURCE_TYPE_STORAGE_IMAGE) {
            write_recommended_sdl_slot_json(outputIO, slot_context, resource_type, descriptor_set, binding);
        }

        if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_IMAGE || resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER) {
            SDL_IOprintf(outputIO, ", \"storage_access\": ");
            write_json_string(outputIO, storage_access_to_string(non_readable, non_writable));
        }

        if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_IMAGE) {
            SDL_IOprintf(outputIO, ", \"observed_access\": ");
            write_json_string(outputIO, observed_resource_access_to_string(dref_usage, resource->id));
        }

        if (base_type && (base == SPVC_BASETYPE_IMAGE || base == SPVC_BASETYPE_SAMPLED_IMAGE)) {
            write_image_details_json(outputIO, compiler, base_type);
        }

        SDL_IOprintf(outputIO, " }");
    }

    SDL_IOprintf(outputIO, "]");
    return true;
}

static void write_sampled_texture_sampler_pair_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    SpvId combined_id,
    bool has_combined_id,
    SpvId image_id,
    SpvId sampler_id,
    const char *derivation)
{
    const spvc_reflected_resource *image = find_texture_resource_by_id(resources, image_id);
    const spvc_reflected_resource *sampler = find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, sampler_id);
    bool image_depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, image_id) == SPVC_TRUE;
    bool sampler_depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, sampler_id) == SPVC_TRUE;
    bool uses_dref_sample_opcode = pair_uses_dref_sample(dref_usage, image_id, sampler_id);
    bool depth_sample = image_depth_or_compare || sampler_depth_or_compare || uses_dref_sample_opcode;
    bool requires_comparison_sampler = uses_dref_sample_opcode;
    spvc_basetype sampled_base_type = sampled_base_type_for_resource(compiler, image);
    unsigned image_descriptor_set = 0;
    unsigned image_binding = 0;
    unsigned sampler_descriptor_set = 0;
    unsigned sampler_binding = 0;
    bool has_image_binding = get_resource_binding(compiler, image, &image_descriptor_set, &image_binding);
    bool has_sampler_binding = get_resource_binding(compiler, sampler, &sampler_descriptor_set, &sampler_binding);
    bool has_sdl_slot = false;
    Uint32 sdl_slot = 0;
    const char *sdl_stage = NULL;

    has_sdl_slot = get_sdl_sampled_slot_for_pair(
        slot_context,
        has_image_binding,
        image_descriptor_set,
        image_binding,
        sampler,
        has_sampler_binding,
        sampler_descriptor_set,
        sampler_binding,
        &sdl_slot);
    if (has_sdl_slot) {
        sdl_stage = sdl_stage_for_descriptor_set(slot_context, image_descriptor_set);
    }

    SDL_IOprintf(outputIO, "{ \"combined_id\": ");
    write_json_null_or_uint(outputIO, combined_id, has_combined_id);
    SDL_IOprintf(outputIO, ", \"image_id\": %u, \"sampler_id\": %u", image_id, sampler_id);
    SDL_IOprintf(outputIO, ", \"image_name\": ");
    write_json_string(outputIO, image ? image->name : NULL);
    SDL_IOprintf(outputIO, ", \"sampler_name\": ");
    write_json_string(outputIO, sampler ? sampler->name : NULL);
    write_resource_binding_json(outputIO, compiler, image, "image");
    write_resource_binding_json(outputIO, compiler, sampler, "sampler");
    write_sampled_pair_image_shape_json(outputIO, compiler, image);
    SDL_IOprintf(outputIO, ", \"sampled_type\": ");
    write_json_string(outputIO, depth_sample ? "depth" : base_type_to_string(sampled_base_type));
    SDL_IOprintf(outputIO, ", \"image_depth_or_compare\": %s", json_bool(image_depth_or_compare));
    SDL_IOprintf(outputIO, ", \"sampler_depth_or_compare\": %s", json_bool(sampler_depth_or_compare));
    SDL_IOprintf(outputIO, ", \"uses_dref_sample_opcode\": %s", json_bool(uses_dref_sample_opcode));
    SDL_IOprintf(outputIO, ", \"recommended_sampler_binding_type\": ");
    write_json_string(outputIO, sampler_binding_type_to_string(depth_sample, requires_comparison_sampler, sampled_base_type));
    SDL_IOprintf(outputIO, ", \"recommended_sdl_stage\": ");
    write_json_null_or_string(outputIO, sdl_stage);
    SDL_IOprintf(outputIO, ", \"recommended_sdl_slot\": ");
    write_json_null_or_uint(outputIO, sdl_slot, has_sdl_slot);
    SDL_IOprintf(outputIO, ", \"pair_derivation\": ");
    write_json_string(outputIO, derivation);
    SDL_IOprintf(outputIO, " }");
}

static bool write_sampled_texture_sampler_pairs_json(SDL_IOStream *outputIO, spvc_compiler compiler, spvc_resources resources, const SDLResourceLayoutContext *slot_context, const DrefUsageAnalysis *dref_usage)
{
    const spvc_combined_image_sampler *samplers = NULL;
    size_t sampler_count = 0;
    spvc_result result;

    SDL_IOprintf(outputIO, "\"sampled_texture_sampler_pairs\": [");

    result = spvc_compiler_build_combined_image_samplers(compiler);
    if (result < 0) {
        bool wrote_pair = false;
        char message[768];

        bool skipped_pair = false;
        char skipped_pair_message[768];

        if (!write_sampled_texture_pairs_from_spirv_scan_json(
                outputIO,
                compiler,
                resources,
                slot_context,
                dref_usage,
                false,
                &wrote_pair,
                &skipped_pair,
                skipped_pair_message,
                sizeof(skipped_pair_message))) {
            return false;
        }
        if (!wrote_pair) {
            format_combined_sampler_failure_message(message, sizeof(message), compiler, resources, slot_context, dref_usage);
            SDL_IOprintf(outputIO, "], \"sampled_texture_sampler_pair_analysis_error\": ");
            write_json_string(outputIO, message);
        } else {
            SDL_IOprintf(outputIO, "]");
            if (skipped_pair) {
                SDL_IOprintf(outputIO, ", \"sampled_texture_sampler_pair_analysis_note\": ");
                write_json_string(outputIO, skipped_pair_message);
            }
        }
        return true;
    }

    result = spvc_compiler_get_combined_image_samplers(compiler, &samplers, &sampler_count);
    if (result < 0) {
        SDL_IOprintf(outputIO, "], \"sampled_texture_sampler_pair_analysis_error\": ");
        write_json_string(outputIO, "spvc_compiler_get_combined_image_samplers failed");
        return true;
    }

    for (size_t i = 0; i < sampler_count; i += 1) {
        const spvc_combined_image_sampler *sampler_pair = &samplers[i];
        if (i > 0) {
            SDL_IOprintf(outputIO, ", ");
        }
        write_sampled_texture_sampler_pair_json(
            outputIO,
            compiler,
            resources,
            slot_context,
            dref_usage,
            sampler_pair->combined_id,
            true,
            sampler_pair->image_id,
            sampler_pair->sampler_id,
            "spirv_cross_combined_image_sampler");
    }

    SDL_IOprintf(outputIO, "]");
    return true;
}

static void write_source_binding_json(SDL_IOStream *outputIO, const char *prefix, unsigned descriptor_set, unsigned binding, bool has_binding)
{
    SDL_IOprintf(outputIO, ", \"%s_set\": ", prefix);
    if (has_binding) {
        SDL_IOprintf(outputIO, "%u", descriptor_set);
    } else {
        SDL_IOprintf(outputIO, "null");
    }

    SDL_IOprintf(outputIO, ", \"%s_binding\": ", prefix);
    if (has_binding) {
        SDL_IOprintf(outputIO, "%u", binding);
    } else {
        SDL_IOprintf(outputIO, "null");
    }
}

static void write_sdl_sampled_texture_layout_entry_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    SpvId image_id,
    SpvId sampler_id,
    const char *pair_derivation)
{
    const spvc_reflected_resource *image = find_texture_resource_by_id(resources, image_id);
    const spvc_reflected_resource *sampler = find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, sampler_id);
    spvc_type image_type = image ? spvc_compiler_get_type_handle(compiler, image->base_type_id) : NULL;
    bool image_depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, image_id) == SPVC_TRUE;
    bool sampler_depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, sampler_id) == SPVC_TRUE;
    bool uses_dref_sample_opcode = pair_uses_dref_sample(dref_usage, image_id, sampler_id);
    bool depth_sample = image_depth_or_compare || sampler_depth_or_compare || uses_dref_sample_opcode;
    bool requires_comparison_sampler = uses_dref_sample_opcode;
    spvc_basetype sampled_base_type = sampled_base_type_for_resource(compiler, image);
    unsigned image_descriptor_set = 0;
    unsigned image_binding = 0;
    unsigned sampler_descriptor_set = 0;
    unsigned sampler_binding = 0;
    bool has_image_binding = get_resource_binding(compiler, image, &image_descriptor_set, &image_binding);
    bool has_sampler_binding = get_resource_binding(compiler, sampler, &sampler_descriptor_set, &sampler_binding);
    bool has_sdl_slot = false;
    Uint32 sdl_slot = 0;
    const char *sdl_stage = NULL;

    has_sdl_slot = get_sdl_sampled_slot_for_pair(
        slot_context,
        has_image_binding,
        image_descriptor_set,
        image_binding,
        sampler,
        has_sampler_binding,
        sampler_descriptor_set,
        sampler_binding,
        &sdl_slot);
    if (has_sdl_slot) {
        sdl_stage = sdl_stage_for_descriptor_set(slot_context, image_descriptor_set);
    }

    SDL_IOprintf(outputIO, "{ \"stage\": ");
    write_json_null_or_string(outputIO, sdl_stage);
    SDL_IOprintf(outputIO, ", \"slot\": ");
    write_json_null_or_uint(outputIO, sdl_slot, has_sdl_slot);
    SDL_IOprintf(outputIO, ", \"image_name\": ");
    write_json_string(outputIO, image ? image->name : NULL);
    SDL_IOprintf(outputIO, ", \"sampler_name\": ");
    write_json_string(outputIO, sampler ? sampler->name : NULL);
    write_source_binding_json(outputIO, "image", image_descriptor_set, image_binding, has_image_binding);
    write_source_binding_json(outputIO, "sampler", sampler_descriptor_set, sampler_binding, has_sampler_binding);
    SDL_IOprintf(outputIO, ", \"texture_dimension\": ");
    if (image_type) {
        bool arrayed = spvc_type_get_image_arrayed(image_type) == SPVC_TRUE;
        write_json_string(outputIO, image_dimension_to_string(spvc_type_get_image_dimension(image_type), arrayed));
        SDL_IOprintf(outputIO, ", \"multisampled\": %s", json_bool(spvc_type_get_image_multisampled(image_type) == SPVC_TRUE));
        SDL_IOprintf(outputIO, ", \"depth\": %s", json_bool(spvc_type_get_image_is_depth(image_type) == SPVC_TRUE));
    } else {
        SDL_IOprintf(outputIO, "null, \"multisampled\": null, \"depth\": null");
    }
    SDL_IOprintf(outputIO, ", \"sample_type\": ");
    write_json_string(outputIO, depth_sample ? "depth" : base_type_to_string(sampled_base_type));
    SDL_IOprintf(outputIO, ", \"sampler_binding_type\": ");
    write_json_null_or_string(outputIO, resolved_sampler_binding_type_to_string(depth_sample, requires_comparison_sampler, sampled_base_type));
    SDL_IOprintf(outputIO, ", \"sampler_policy\": ");
    write_json_string(outputIO, sampler_binding_type_to_string(depth_sample, requires_comparison_sampler, sampled_base_type));
    SDL_IOprintf(outputIO, ", \"uses_dref_sample_opcode\": %s", json_bool(uses_dref_sample_opcode));
    SDL_IOprintf(outputIO, ", \"derivation\": ");
    write_json_string(outputIO, has_sdl_slot ? "sdl_convention" : "unmapped");
    SDL_IOprintf(outputIO, ", \"pair_derivation\": ");
    write_json_string(outputIO, pair_derivation);
    SDL_IOprintf(outputIO, " }");
}

static bool write_sampled_texture_pairs_from_spirv_scan_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool write_sdl_layout_entries,
    bool *wrote_pair,
    bool *skipped_pair,
    char *skipped_pair_message,
    size_t skipped_pair_message_size)
{
    *wrote_pair = false;
    *skipped_pair = false;

    for (Uint32 id = 1; id < dref_usage->id_bound; id += 1) {
        SpvId image_id = dref_usage->sampled_image_ids[id];
        SpvId sampler_id = dref_usage->sampled_sampler_ids[id];
        const spvc_reflected_resource *image;
        const spvc_reflected_resource *sampler;

        if (!image_id || !sampler_id || sampled_texture_pair_already_seen(dref_usage, id)) {
            continue;
        }
        /*
         * This recovery is only for selected-entrypoint pairs that SPIRV-Cross
         * cannot remap because the same image also has samplerless query/fetch
         * use. Do not turn unresolved helper-parameter, sampler-array, or
         * inactive-entrypoint IDs into guessed SDL layout facts.
         */
        image = find_texture_resource_by_id(resources, image_id);
        sampler = find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, sampler_id);
        if (!image || !sampler) {
            if (!*skipped_pair && skipped_pair_message && skipped_pair_message_size > 0) {
                char image_reference[256];
                char sampler_reference[256];
                format_combined_sampler_resource_reference(image_reference, sizeof(image_reference), compiler, slot_context, image, image_id, "image");
                format_combined_sampler_resource_reference(sampler_reference, sizeof(sampler_reference), compiler, slot_context, sampler, sampler_id, "sampler");
                SDL_snprintf(
                    skipped_pair_message,
                    skipped_pair_message_size,
                    "sampled texture/sampler pair analysis recovered selected-entrypoint reflected pairs but skipped an unrecoverable OpSampledImage pair; %s; %s",
                    image_reference,
                    sampler_reference);
            }
            *skipped_pair = true;
            continue;
        }

        if (*wrote_pair) {
            SDL_IOprintf(outputIO, ", ");
        }
        if (write_sdl_layout_entries) {
            write_sdl_sampled_texture_layout_entry_json(
                outputIO,
                compiler,
                resources,
                slot_context,
                dref_usage,
                image_id,
                sampler_id,
                "spirv_scan");
        } else {
            write_sampled_texture_sampler_pair_json(
                outputIO,
                compiler,
                resources,
                slot_context,
                dref_usage,
                0,
                false,
                image_id,
                sampler_id,
                "spirv_scan");
        }
        *wrote_pair = true;
    }

    return true;
}

static bool write_sdl_sampled_texture_layout_json(SDL_IOStream *outputIO, spvc_compiler compiler, spvc_resources resources, const SDLResourceLayoutContext *slot_context, const DrefUsageAnalysis *dref_usage)
{
    const spvc_combined_image_sampler *samplers = NULL;
    size_t sampler_count = 0;
    spvc_result result;

    SDL_IOprintf(outputIO, "\"sampled_textures\": [");

    result = spvc_compiler_build_combined_image_samplers(compiler);
    if (result < 0) {
        bool wrote_pair = false;
        char message[768];

        bool skipped_pair = false;
        char skipped_pair_message[768];

        if (!write_sampled_texture_pairs_from_spirv_scan_json(
                outputIO,
                compiler,
                resources,
                slot_context,
                dref_usage,
                true,
                &wrote_pair,
                &skipped_pair,
                skipped_pair_message,
                sizeof(skipped_pair_message))) {
            return false;
        }
        if (!wrote_pair) {
            format_combined_sampler_failure_message(message, sizeof(message), compiler, resources, slot_context, dref_usage);
            SDL_IOprintf(outputIO, "], \"sampled_texture_layout_error\": ");
            write_json_string(outputIO, message);
        } else {
            SDL_IOprintf(outputIO, "]");
            if (skipped_pair) {
                SDL_IOprintf(outputIO, ", \"sampled_texture_layout_note\": ");
                write_json_string(outputIO, skipped_pair_message);
            }
        }
        return true;
    }

    result = spvc_compiler_get_combined_image_samplers(compiler, &samplers, &sampler_count);
    if (result < 0) {
        SDL_IOprintf(outputIO, "], \"sampled_texture_layout_error\": ");
        write_json_string(outputIO, "spvc_compiler_get_combined_image_samplers failed");
        return true;
    }

    for (size_t i = 0; i < sampler_count; i += 1) {
        const spvc_combined_image_sampler *sampler_pair = &samplers[i];
        if (i > 0) {
            SDL_IOprintf(outputIO, ", ");
        }
        write_sdl_sampled_texture_layout_entry_json(
            outputIO,
            compiler,
            resources,
            slot_context,
            dref_usage,
            sampler_pair->image_id,
            sampler_pair->sampler_id,
            "spirv_cross_combined_image_sampler");
    }

    SDL_IOprintf(outputIO, "]");
    return true;
}

static bool write_sdl_storage_texture_layout_entries(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const FragmentStorageBufferFactSlots *fragment_slots,
    bool allow_observed_storage_access,
    spvc_resource_type resource_type,
    bool collect_storage_images,
    bool *wrote_entry)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    spvc_result result = spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count);

    if (result < 0) {
        SDL_SetError("spvc_resources_get_resource_list_for_type failed");
        return false;
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        spvc_type base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
        bool non_readable;
        bool non_writable;
        unsigned descriptor_set = 0;
        unsigned binding = 0;
        bool has_binding;
        bool has_slot = false;
        Uint32 slot = 0;
        SDL_ShaderCross_INTERNAL_StorageTextureAccess final_access = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;
        SDL_ShaderCross_INTERNAL_StorageTextureAccess classification_access = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;
        bool is_fragment_writable_storage_texture = false;
        const char *resource_class = "unknown";

        if (resource_type != SPVC_RESOURCE_TYPE_STORAGE_IMAGE) {
            if (!collect_storage_images || !image_resource_is_storage(compiler, resource)) {
                continue;
            }
        }

        non_readable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonReadable) == SPVC_TRUE;
        non_writable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonWritable) == SPVC_TRUE;
        if (base_type) {
            final_access = resource_layout_storage_access_from_evidence(
                non_readable,
                non_writable,
                spvc_type_get_image_access_qualifier(base_type),
                dref_usage,
                resource->id,
                false);
            classification_access = resource_layout_storage_access_from_evidence(
                non_readable,
                non_writable,
                spvc_type_get_image_access_qualifier(base_type),
                dref_usage,
                resource->id,
                allow_observed_storage_access);
        }
        is_fragment_writable_storage_texture =
            slot_context->execution_model == SpvExecutionModelFragment &&
            storage_texture_access_is_writable(classification_access);
        has_binding = get_resource_binding(compiler, resource, &descriptor_set, &binding);
        if (has_binding) {
            if (slot_context->execution_model == SpvExecutionModelFragment) {
                has_slot = get_fragment_storage_texture_fact_slot(slot_context, fragment_slots, descriptor_set, binding, is_fragment_writable_storage_texture, &slot);
            } else {
                has_slot = get_sdl_slot_for_resource(slot_context, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, descriptor_set, binding, &slot);
            }
            if (is_fragment_writable_storage_texture) {
                has_slot = false;
            }
            resource_class = storage_texture_resource_class(slot_context, descriptor_set);
        }

        if (*wrote_entry) {
            SDL_IOprintf(outputIO, ", ");
        }
        *wrote_entry = true;

        SDL_IOprintf(outputIO, "{ \"stage\": ");
        write_json_null_or_string(outputIO, has_binding ? sdl_stage_for_descriptor_set(slot_context, descriptor_set) : NULL);
        SDL_IOprintf(outputIO, ", \"resource_class\": ");
        write_json_string(outputIO, resource_class);
        SDL_IOprintf(outputIO, ", \"slot\": ");
        write_json_null_or_uint(outputIO, slot, has_slot);
        SDL_IOprintf(outputIO, ", \"name\": ");
        write_json_string(outputIO, resource->name);
        write_source_binding_json(outputIO, "source", descriptor_set, binding, has_binding);
        SDL_IOprintf(outputIO, ", \"access\": ");
        write_json_string(outputIO, storage_access_to_string(non_readable, non_writable));
        SDL_IOprintf(outputIO, ", \"observed_access\": ");
        write_json_string(outputIO, observed_resource_access_to_string(dref_usage, resource->id));

        if (base_type) {
            bool arrayed = spvc_type_get_image_arrayed(base_type) == SPVC_TRUE;
            SpvImageFormat storage_format = spvc_type_get_image_storage_format(base_type);
            const char *final_access_string = resource_layout_storage_texture_access_to_string(final_access);
            SDL_IOprintf(outputIO, ", \"dimension\": ");
            write_json_string(outputIO, image_dimension_to_string(spvc_type_get_image_dimension(base_type), arrayed));
            SDL_IOprintf(outputIO, ", \"multisampled\": %s", json_bool(spvc_type_get_image_multisampled(base_type) == SPVC_TRUE));
            SDL_IOprintf(outputIO, ", \"depth\": %s", json_bool(spvc_type_get_image_is_depth(base_type) == SPVC_TRUE));
            SDL_IOprintf(outputIO, ", \"storage_format\": ");
            write_json_string(outputIO, image_format_to_string(storage_format));
            SDL_IOprintf(outputIO, ", \"mapped_webgpu_format\": ");
            write_json_null_or_string(outputIO, webgpu_storage_texture_format_to_string(storage_format));
            SDL_IOprintf(outputIO, ", \"mapped_sdl_format\": ");
            write_json_null_or_string(outputIO, sdl_storage_texture_format_to_string(storage_format));
            SDL_IOprintf(outputIO, ", \"final_access\": ");
            write_json_null_or_string(outputIO, final_access_string);
            SDL_IOprintf(outputIO, ", \"feature_requirements\": ");
            write_storage_texture_feature_requirements_json(outputIO, final_access_string, storage_format);
        } else {
            SDL_IOprintf(outputIO, ", \"dimension\": null, \"multisampled\": null, \"depth\": null, \"storage_format\": null");
            SDL_IOprintf(outputIO, ", \"mapped_webgpu_format\": null, \"mapped_sdl_format\": null");
            SDL_IOprintf(outputIO, ", \"final_access\": null, \"feature_requirements\": []");
        }
        SDL_IOprintf(outputIO, ", \"unsupported_features\": ");
        if (is_fragment_writable_storage_texture) {
            SDL_IOprintf(outputIO, "[");
            write_json_string(outputIO, "fragment_storage_texture_writes");
            SDL_IOprintf(outputIO, "]");
        } else {
            SDL_IOprintf(outputIO, "[]");
        }

        SDL_IOprintf(outputIO, ", \"derivation\": ");
        write_json_string(outputIO, has_slot ? "sdl_convention" : "unmapped");
        SDL_IOprintf(outputIO, " }");
    }

    return true;
}

static bool write_sdl_storage_textures_layout_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access)
{
    bool wrote_entry = false;
    FragmentStorageBufferFactSlots fragment_slots;
    const FragmentStorageBufferFactSlots *fragment_slots_ptr = NULL;

    if (slot_context->execution_model == SpvExecutionModelFragment) {
        if (!collect_fragment_storage_fact_slots_for_json(compiler, resources, slot_context, dref_usage, allow_observed_storage_access, &fragment_slots)) {
            return false;
        }
        fragment_slots_ptr = &fragment_slots;
    }

    SDL_IOprintf(outputIO, "\"storage_textures\": [");
    if (!write_sdl_storage_texture_layout_entries(outputIO, compiler, resources, slot_context, dref_usage, fragment_slots_ptr, allow_observed_storage_access, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, false, &wrote_entry)) {
        return false;
    }
    if (!write_sdl_storage_texture_layout_entries(outputIO, compiler, resources, slot_context, dref_usage, fragment_slots_ptr, allow_observed_storage_access, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, true, &wrote_entry)) {
        return false;
    }
    SDL_IOprintf(outputIO, "]");
    return true;
}

static bool write_sdl_buffer_layout_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access,
    spvc_resource_type resource_type,
    const char *name)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    FragmentStorageBufferFactSlots fragment_slots;
    const FragmentStorageBufferFactSlots *fragment_slots_ptr = NULL;
    spvc_result result = spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count);

    if (result < 0) {
        SDL_SetError("spvc_resources_get_resource_list_for_type failed");
        return false;
    }

    if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER &&
        slot_context->execution_model == SpvExecutionModelFragment) {
        if (!collect_fragment_storage_fact_slots_for_json(
                compiler,
                resources,
                slot_context,
                dref_usage,
                allow_observed_storage_access,
                &fragment_slots)) {
            return false;
        }
        fragment_slots_ptr = &fragment_slots;
    }

    SDL_IOprintf(outputIO, "\"%s\": [", name);

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        BufferResourceLayoutFacts facts;

        collect_buffer_resource_layout_facts(compiler, slot_context, dref_usage, fragment_slots_ptr, resource, resource_type, &facts);
        if (facts.is_fragment_writable_storage_buffer) {
            facts.has_slot = false;
        }

        if (i > 0) {
            SDL_IOprintf(outputIO, ", ");
        }

        SDL_IOprintf(outputIO, "{ \"stage\": ");
        write_json_null_or_string(outputIO, facts.has_binding ? sdl_stage_for_descriptor_set(slot_context, facts.descriptor_set) : NULL);
        SDL_IOprintf(outputIO, ", \"resource_class\": ");
        write_json_string(outputIO, facts.resource_class);
        SDL_IOprintf(outputIO, ", \"slot\": ");
        write_json_null_or_uint(outputIO, facts.slot, facts.has_slot);
        SDL_IOprintf(outputIO, ", \"name\": ");
        write_json_string(outputIO, resource->name);
        write_source_binding_json(outputIO, "source", facts.descriptor_set, facts.binding, facts.has_binding);
        SDL_IOprintf(outputIO, ", \"binding_type\": ");
        write_json_string(outputIO, facts.binding_type);
        if (resource_type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER) {
            bool wrote_unsupported_feature = false;

            SDL_IOprintf(outputIO, ", \"access\": ");
            write_json_string(outputIO, storage_access_to_string(facts.non_readable, facts.non_writable));
            SDL_IOprintf(outputIO, ", \"unsupported_features\": ");
            SDL_IOprintf(outputIO, "[");
            if (facts.has_fragment_storage_buffer_atomics) {
                write_json_string(outputIO, "fragment_storage_buffer_atomics");
                wrote_unsupported_feature = true;
            }
            if (facts.is_fragment_writable_storage_buffer) {
                if (wrote_unsupported_feature) {
                    SDL_IOprintf(outputIO, ", ");
                }
                write_json_string(outputIO, "fragment_storage_buffer_writes");
            }
            SDL_IOprintf(outputIO, "]");
        }
        SDL_IOprintf(outputIO, ", \"size\": ");
        write_json_null_or_uint(outputIO, (Uint32)facts.size, facts.has_size);
        SDL_IOprintf(outputIO, ", \"fixed_size\": %s", json_bool(facts.has_fixed_binding_size));
        SDL_IOprintf(outputIO, ", \"runtime_or_dynamic_array\": %s", json_bool(facts.has_runtime_or_dynamic_array));
        SDL_IOprintf(outputIO, ", \"members\": ");
        write_buffer_layout_members_json(outputIO, compiler, resource, &facts, resource_type);
        SDL_IOprintf(outputIO, ", \"derivation\": ");
        write_json_string(outputIO, facts.has_slot ? "sdl_convention" : "unmapped");
        SDL_IOprintf(outputIO, " }");
    }

    SDL_IOprintf(outputIO, "]");
    return true;
}

static bool write_sdl_resource_layout_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access)
{
    SDL_IOprintf(outputIO, "\"sdl_resource_layout\": { \"schema\": ");
    write_json_string(outputIO, "sdl-shadercross-webgpu-resource-layout-v0");
    SDL_IOprintf(outputIO, ", \"execution_model\": ");
    write_json_string(outputIO, execution_model_to_string(slot_context->execution_model));

    if (slot_context->execution_model == SpvExecutionModelGLCompute ||
        slot_context->execution_model == SpvExecutionModelKernel) {
        SDL_IOprintf(
            outputIO,
            ", \"threadcount_x\": %u, \"threadcount_y\": %u, \"threadcount_z\": %u",
            (unsigned)spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 0),
            (unsigned)spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 1),
            (unsigned)spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 2));
    }

    SDL_IOprintf(outputIO, ", ");
    if (!write_sdl_sampled_texture_layout_json(outputIO, compiler, resources, slot_context, dref_usage)) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_sdl_storage_textures_layout_json(outputIO, compiler, resources, slot_context, dref_usage, allow_observed_storage_access)) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_sdl_buffer_layout_json(outputIO, compiler, resources, slot_context, dref_usage, allow_observed_storage_access, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, "storage_buffers")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_sdl_buffer_layout_json(outputIO, compiler, resources, slot_context, dref_usage, allow_observed_storage_access, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, "uniform_buffers")) {
        return false;
    }

    SDL_IOprintf(outputIO, " }");
    return true;
}

typedef struct ShaderCross_CLIResourceLayoutEvidence
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence *sampled_textures;
    Uint32 num_sampled_textures;
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence *storage_textures;
    Uint32 num_storage_textures;
    bool has_samplerless_sampled_image;
} ShaderCross_CLIResourceLayoutEvidence;

typedef struct ShaderCross_CLIResourceLayoutEvidenceOptions
{
    bool allow_observed_storage_access;
    bool classify_fragment_writable_storage;
    bool collect_sampled_textures;
} ShaderCross_CLIResourceLayoutEvidenceOptions;

static void free_resource_layout_evidence(ShaderCross_CLIResourceLayoutEvidence *evidence)
{
    SDL_free(evidence->sampled_textures);
    SDL_free(evidence->storage_textures);
    SDL_zero(*evidence);
}

static bool append_sampled_texture_evidence(
    ShaderCross_CLIResourceLayoutEvidence *evidence,
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *entry)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence *entries = (SDL_ShaderCross_INTERNAL_SampledTextureEvidence *)SDL_realloc(
        evidence->sampled_textures,
        sizeof(*evidence->sampled_textures) * (evidence->num_sampled_textures + 1));
    if (!entries) {
        return SDL_OutOfMemory();
    }

    evidence->sampled_textures = entries;
    evidence->sampled_textures[evidence->num_sampled_textures] = *entry;
    evidence->num_sampled_textures += 1;
    return true;
}

static bool append_storage_texture_evidence(
    ShaderCross_CLIResourceLayoutEvidence *evidence,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entry)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entries = (SDL_ShaderCross_INTERNAL_StorageTextureEvidence *)SDL_realloc(
        evidence->storage_textures,
        sizeof(*evidence->storage_textures) * (evidence->num_storage_textures + 1));
    if (!entries) {
        return SDL_OutOfMemory();
    }

    evidence->storage_textures = entries;
    evidence->storage_textures[evidence->num_storage_textures] = *entry;
    evidence->num_storage_textures += 1;
    return true;
}

static SDL_ShaderCross_INTERNAL_TextureDimension resource_layout_texture_dimension_from_type(spvc_type image_type)
{
    bool arrayed;

    if (!image_type) {
        return SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_UNKNOWN;
    }

    arrayed = spvc_type_get_image_arrayed(image_type) == SPVC_TRUE;
    switch (spvc_type_get_image_dimension(image_type)) {
    case SpvDim2D:
        return arrayed ? SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY : SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D;
    case SpvDim3D:
        return SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D;
    case SpvDimCube:
        return arrayed ? SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY : SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE;
    default:
        return SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_UNKNOWN;
    }
}

static SDL_ShaderCross_INTERNAL_ImageDepthOperand resource_layout_image_depth_from_analysis(
    const DrefUsageAnalysis *analysis,
    spvc_type_id image_type_id)
{
    if (!analysis || image_type_id >= analysis->id_bound || !analysis->image_depth_operands) {
        return SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN;
    }
    return analysis->image_depth_operands[image_type_id];
}

static SDL_ShaderCross_INTERNAL_SampledTextureSampleKind resource_layout_sample_kind_from_base_type(spvc_basetype base_type)
{
    switch (base_type) {
    case SPVC_BASETYPE_FP16:
    case SPVC_BASETYPE_FP32:
    case SPVC_BASETYPE_FP64:
        return SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT;
    case SPVC_BASETYPE_INT8:
    case SPVC_BASETYPE_INT16:
    case SPVC_BASETYPE_INT32:
    case SPVC_BASETYPE_INT64:
        return SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT;
    case SPVC_BASETYPE_UINT8:
    case SPVC_BASETYPE_UINT16:
    case SPVC_BASETYPE_UINT32:
    case SPVC_BASETYPE_UINT64:
        return SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT;
    default:
        return SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UNKNOWN;
    }
}

static SDL_ShaderCross_INTERNAL_StorageTextureFormat resource_layout_storage_format_from_spv(SpvImageFormat format)
{
    switch (format) {
    case SpvImageFormatRgba8Snorm:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM;
    case SpvImageFormatRgba16f:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT;
    case SpvImageFormatRg32f:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT;
    case SpvImageFormatRgba32f:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT;
    case SpvImageFormatRgba32ui:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_UINT;
    case SpvImageFormatRgba32i:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_INT;
    case SpvImageFormatR32f:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT;
    case SpvImageFormatRgba8:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    case SpvImageFormatRgba8ui:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT;
    case SpvImageFormatRgba16ui:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT;
    case SpvImageFormatR32i:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT;
    case SpvImageFormatRgba8i:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT;
    case SpvImageFormatRgba16i:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT;
    case SpvImageFormatR32ui:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT;
    default:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_UNKNOWN;
    }
}

static SDL_ShaderCross_INTERNAL_StorageTextureAccess resource_layout_storage_access_from_spv(bool non_readable, bool non_writable, SpvAccessQualifier access)
{
    if (non_readable && !non_writable) {
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY;
    }
    if (!non_readable && non_writable) {
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY;
    }

    switch (access) {
    case SpvAccessQualifierReadOnly:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY;
    case SpvAccessQualifierWriteOnly:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY;
    case SpvAccessQualifierReadWrite:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE;
    default:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;
    }
}

static SDL_ShaderCross_INTERNAL_StorageTextureAccess resource_layout_storage_access_from_observed(
    const DrefUsageAnalysis *analysis,
    SpvId resource_id)
{
    bool reads;
    bool writes;

    if (!analysis || resource_id >= analysis->id_bound) {
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;
    }

    reads = analysis->image_resource_reads[resource_id];
    writes = analysis->image_resource_writes[resource_id];

    if (reads && writes) {
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE;
    }
    if (reads) {
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY;
    }
    if (writes) {
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY;
    }

    return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;
}

static SDL_ShaderCross_INTERNAL_StorageTextureAccess resource_layout_storage_access_from_evidence(
    bool non_readable,
    bool non_writable,
    SpvAccessQualifier access,
    const DrefUsageAnalysis *dref_usage,
    SpvId resource_id,
    bool allow_observed_storage_access)
{
    SDL_ShaderCross_INTERNAL_StorageTextureAccess resolved_access = resource_layout_storage_access_from_spv(non_readable, non_writable, access);

    if (resolved_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN && allow_observed_storage_access) {
        resolved_access = resource_layout_storage_access_from_observed(dref_usage, resource_id);
    }

    return resolved_access;
}

static bool resource_layout_kind_from_execution_model(SpvExecutionModel execution_model, SDL_ShaderCross_INTERNAL_ResourceLayoutKind *kind)
{
    switch (execution_model) {
    case SpvExecutionModelVertex:
    case SpvExecutionModelFragment:
        *kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
        return true;
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        *kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
        return true;
    default:
        return false;
    }
}

static bool storage_texture_access_is_writable(SDL_ShaderCross_INTERNAL_StorageTextureAccess access)
{
    return access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY ||
           access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE;
}

static bool reflected_resource_is_array(spvc_compiler compiler, const spvc_reflected_resource *resource)
{
    spvc_type type;

    if (resource == NULL) {
        return false;
    }

    type = spvc_compiler_get_type_handle(compiler, resource->type_id);
    return type && spvc_type_get_num_array_dimensions(type) > 0;
}

static bool execution_model_matches_shader_stage(SpvExecutionModel execution_model, SDL_ShaderCross_ShaderStage shader_stage)
{
    switch (shader_stage) {
    case SDL_SHADERCROSS_SHADERSTAGE_VERTEX:
        return execution_model == SpvExecutionModelVertex;
    case SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT:
        return execution_model == SpvExecutionModelFragment;
    case SDL_SHADERCROSS_SHADERSTAGE_COMPUTE:
        return execution_model == SpvExecutionModelGLCompute ||
               execution_model == SpvExecutionModelKernel;
    default:
        return false;
    }
}

static bool resource_layout_execution_model_for_entrypoint(
    spvc_compiler compiler,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    SpvExecutionModel *execution_model)
{
    const spvc_entry_point *entry_points = NULL;
    size_t entry_point_count = 0;
    spvc_result result;

    if (!entrypoint) {
        return SDL_InvalidParamError("entrypoint");
    }
    result = spvc_compiler_get_entry_points(compiler, &entry_points, &entry_point_count);
    if (result < 0) {
        return SDL_SetError("spvc_compiler_get_entry_points failed");
    }

    for (size_t i = 0; i < entry_point_count; i += 1) {
        const spvc_entry_point *entry = &entry_points[i];
        if (SDL_strcmp(entry->name, entrypoint) == 0 &&
            execution_model_matches_shader_stage(entry->execution_model, shader_stage)) {
            *execution_model = entry->execution_model;
            return true;
        }
    }

    return SDL_SetError("SPIR-V entry point '%s' with requested shader stage is not present", entrypoint);
}

static bool sampled_descriptor_set_for_execution_model(SpvExecutionModel execution_model, unsigned *descriptor_set)
{
    switch (execution_model) {
    case SpvExecutionModelVertex:
        *descriptor_set = 0;
        return true;
    case SpvExecutionModelFragment:
        *descriptor_set = 2;
        return true;
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        *descriptor_set = 0;
        return true;
    default:
        return false;
    }
}

static bool storage_texture_class_from_descriptor_set(
    const SDLResourceLayoutContext *slot_context,
    unsigned descriptor_set,
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass *resource_class)
{
    switch (slot_context->execution_model) {
    case SpvExecutionModelVertex:
    case SpvExecutionModelFragment:
        *resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE;
        return true;
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        if (descriptor_set == 0) {
            *resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY;
            return true;
        }
        if (descriptor_set == 1) {
            *resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE;
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

static void fill_layout_facts_input_counts(
    const SDLResourceLayoutContext *slot_context,
    SDL_ShaderCross_INTERNAL_LayoutFactsInput *input)
{
    unsigned descriptor_set = 0;

    if (sampled_descriptor_set_for_execution_model(slot_context->execution_model, &descriptor_set)) {
        input->num_sampled_texture_slots = slot_context->sampled_texture_slots[descriptor_set];
    }

    switch (slot_context->execution_model) {
    case SpvExecutionModelVertex:
        input->num_storage_textures = slot_context->storage_texture_slots[0];
        break;
    case SpvExecutionModelFragment:
        input->num_storage_textures = slot_context->storage_texture_slots[2];
        break;
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        input->num_readonly_storage_textures = slot_context->storage_texture_slots[0];
        input->num_readwrite_storage_textures = slot_context->storage_texture_slots[1];
        break;
    default:
        break;
    }
}

static bool shader_stage_from_execution_model(SpvExecutionModel execution_model, SDL_GPUShaderStage *stage)
{
    switch (execution_model) {
    case SpvExecutionModelVertex:
        *stage = SDL_GPU_SHADERSTAGE_VERTEX;
        return true;
    case SpvExecutionModelFragment:
        *stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        return true;
    default:
        return false;
    }
}

static bool fill_resource_layout_counts(
    const SDLResourceLayoutContext *slot_context,
    SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind,
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout)
{
    unsigned descriptor_set = 0;

    SDL_zero(*layout);
    layout->kind = kind;

    if (sampled_descriptor_set_for_execution_model(slot_context->execution_model, &descriptor_set)) {
        layout->num_samplers = slot_context->sampled_texture_slots[descriptor_set];
    }

    switch (slot_context->execution_model) {
    case SpvExecutionModelVertex:
        if (!shader_stage_from_execution_model(slot_context->execution_model, &layout->shader_stage)) {
            return false;
        }
        layout->num_storage_textures = slot_context->storage_texture_slots[0];
        layout->num_storage_buffers = slot_context->storage_buffer_slots[0];
        layout->num_uniform_buffers = slot_context->uniform_buffer_slots[1];
        return true;
    case SpvExecutionModelFragment:
        if (!shader_stage_from_execution_model(slot_context->execution_model, &layout->shader_stage)) {
            return false;
        }
        layout->num_storage_textures = slot_context->storage_texture_slots[2];
        layout->num_storage_buffers = slot_context->storage_buffer_slots[2];
        layout->num_uniform_buffers = slot_context->uniform_buffer_slots[3];
        return true;
    case SpvExecutionModelGLCompute:
    case SpvExecutionModelKernel:
        layout->num_readonly_storage_textures = slot_context->storage_texture_slots[0];
        layout->num_readonly_storage_buffers = slot_context->storage_buffer_slots[0];
        layout->num_readwrite_storage_textures = slot_context->storage_texture_slots[1];
        layout->num_readwrite_storage_buffers = slot_context->storage_buffer_slots[1];
        layout->num_uniform_buffers = slot_context->uniform_buffer_slots[2];
        return true;
    default:
        break;
    }

    return false;
}

static Uint32 resource_layout_buffer_slot_count(
    const SDLResourceLayoutContext *slot_context,
    spvc_resource_type resource_type,
    unsigned descriptor_set)
{
    if (descriptor_set >= SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT) {
        return 0;
    }
    if (resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER) {
        return slot_context->uniform_buffer_slots[descriptor_set];
    }
    return slot_context->storage_buffer_slots[descriptor_set];
}

static const char *resource_layout_buffer_label(spvc_resource_type resource_type)
{
    return resource_type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER ? "uniform buffer" : "storage buffer";
}

static bool validate_resource_layout_buffer_slots_for_type(
    const SDLResourceLayoutContext *slot_context,
    spvc_compiler compiler,
    spvc_resources resources,
    spvc_resource_type resource_type)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    bool *seen_slots[SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT];
    const char *label = resource_layout_buffer_label(resource_type);
    bool result = false;

    SDL_zeroa(seen_slots);

    if (spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count) < 0) {
        SDL_SetError("spvc_resources_get_resource_list_for_type failed");
        goto done;
    }

    for (unsigned descriptor_set = 0; descriptor_set < SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT; descriptor_set += 1) {
        Uint32 slot_count = resource_layout_buffer_slot_count(slot_context, resource_type, descriptor_set);
        if (slot_count > 0) {
            seen_slots[descriptor_set] = (bool *)SDL_calloc(slot_count, sizeof(*seen_slots[descriptor_set]));
            if (!seen_slots[descriptor_set]) {
                SDL_OutOfMemory();
                goto done;
            }
        }
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        unsigned descriptor_set = 0;
        unsigned binding = 0;
        Uint32 slot = 0;
        Uint32 slot_count;

        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding) ||
            !get_sdl_slot_for_resource(slot_context, resource_type, descriptor_set, binding, &slot)) {
            SDL_SetError("%s binding does not match SDL's resource layout slot convention for SDL_GPU resource layout C output", label);
            goto done;
        }

        slot_count = resource_layout_buffer_slot_count(slot_context, resource_type, descriptor_set);
        if (slot >= slot_count || !seen_slots[descriptor_set]) {
            SDL_SetError("%s binding does not match SDL's resource layout slot convention for SDL_GPU resource layout C output", label);
            goto done;
        }
        if (seen_slots[descriptor_set][slot]) {
            SDL_SetError("duplicate %s SDL slot %u for SDL_GPU resource layout C output", label, (unsigned)slot);
            goto done;
        }
        seen_slots[descriptor_set][slot] = true;
    }

    result = true;

done:
    for (unsigned descriptor_set = 0; descriptor_set < SDL_SHADERCROSS_SDL_DESCRIPTOR_SET_COUNT; descriptor_set += 1) {
        SDL_free(seen_slots[descriptor_set]);
    }
    return result;
}

static bool validate_resource_layout_buffer_slots(
    const SDLResourceLayoutContext *slot_context,
    spvc_compiler compiler,
    spvc_resources resources)
{
    return validate_resource_layout_buffer_slots_for_type(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER) &&
           validate_resource_layout_buffer_slots_for_type(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER);
}

static bool append_sampled_texture_pair_layout_evidence(
    ShaderCross_CLIResourceLayoutEvidence *layout_evidence,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    SpvId image_id,
    SpvId sampler_id,
    bool *paired_images)
{
    const spvc_reflected_resource *image = find_texture_resource_by_id(resources, image_id);
    const spvc_reflected_resource *sampler = find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, sampler_id);
    spvc_type image_type = image ? spvc_compiler_get_type_handle(compiler, image->base_type_id) : NULL;
    bool image_depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, image_id) == SPVC_TRUE;
    bool sampler_depth_or_compare = spvc_compiler_variable_is_depth_or_compare(compiler, sampler_id) == SPVC_TRUE;
    bool uses_dref_sample_opcode = pair_uses_dref_sample(dref_usage, image_id, sampler_id);
    bool depth_without_comparison = (image_depth_or_compare || sampler_depth_or_compare) && !uses_dref_sample_opcode;
    spvc_basetype sampled_base_type = sampled_base_type_for_resource(compiler, image);
    unsigned image_descriptor_set = 0;
    unsigned image_binding = 0;
    unsigned sampler_descriptor_set = 0;
    unsigned sampler_binding = 0;
    bool has_image_binding = get_resource_binding(compiler, image, &image_descriptor_set, &image_binding);
    bool has_sampler_binding = get_resource_binding(compiler, sampler, &sampler_descriptor_set, &sampler_binding);
    unsigned sampled_descriptor_set = 0;
    bool has_sampled_descriptor_set = sampled_descriptor_set_for_execution_model(slot_context->execution_model, &sampled_descriptor_set);
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence evidence;

    SDL_zero(evidence);
    /* Borrowed from SPIRV-Cross and valid until the compiler context is destroyed. */
    evidence.name = image ? image->name : NULL;
    evidence.texture_dimension = resource_layout_texture_dimension_from_type(image_type);
    evidence.multisampled = image_type && spvc_type_get_image_multisampled(image_type) == SPVC_TRUE;
    evidence.image_depth = resource_layout_image_depth_from_analysis(dref_usage, image ? image->base_type_id : 0);
    evidence.sample_kind = (uses_dref_sample_opcode || depth_without_comparison) ?
        SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH :
        resource_layout_sample_kind_from_base_type(sampled_base_type);

    if (uses_dref_sample_opcode) {
        evidence.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON;
        evidence.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON;
    } else if (depth_without_comparison) {
        evidence.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING;
        evidence.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN;
    } else if (evidence.sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT) {
        evidence.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN;
        evidence.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT;
    } else if (evidence.sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT ||
               evidence.sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT) {
        evidence.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING;
        evidence.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN;
    } else {
        evidence.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN;
        evidence.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN;
    }

    if (has_sampled_descriptor_set &&
        has_image_binding &&
        image_descriptor_set == sampled_descriptor_set &&
        (!sampler || (has_sampler_binding &&
                      sampler_descriptor_set == sampled_descriptor_set &&
                      image_binding == sampler_binding))) {
        evidence.has_sdl_slot = true;
        evidence.slot = image_binding;
    }
    if (has_image_binding) {
        evidence.has_source_binding = true;
        evidence.source_set = image_descriptor_set;
        evidence.source_binding = image_binding;
    }

    if (!append_sampled_texture_evidence(layout_evidence, &evidence)) {
        return false;
    }
    if (paired_images && image_id < dref_usage->id_bound) {
        paired_images[image_id] = true;
    }
    return true;
}

static bool collect_sampled_texture_layout_evidence_from_spirv_pairs(
    ShaderCross_CLIResourceLayoutEvidence *layout_evidence,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool *paired_images,
    bool *collected_pair)
{
    *collected_pair = false;

    for (Uint32 id = 1; id < dref_usage->id_bound; id += 1) {
        SpvId image_id = dref_usage->sampled_image_ids[id];
        SpvId sampler_id = dref_usage->sampled_sampler_ids[id];

        if (!image_id || !sampler_id || sampled_texture_pair_already_seen(dref_usage, id)) {
            continue;
        }
        /*
         * This fallback is only for selected-entrypoint pairs that SPIRV-Cross
         * cannot remap because the same image also has samplerless query/fetch
         * use. Do not turn unresolved helper-parameter, sampler-array, or
         * inactive-entrypoint IDs into guessed SDL layout facts.
         */
        if (!find_texture_resource_by_id(resources, image_id) ||
            !find_resource_by_id(resources, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, sampler_id)) {
            continue;
        }
        if (!append_sampled_texture_pair_layout_evidence(
                layout_evidence,
                compiler,
                resources,
                slot_context,
                dref_usage,
                image_id,
                sampler_id,
                paired_images)) {
            return false;
        }
        *collected_pair = true;
    }
    return true;
}

static bool collect_sampled_texture_layout_evidence(
    ShaderCross_CLIResourceLayoutEvidence *layout_evidence,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage)
{
    const spvc_combined_image_sampler *samplers = NULL;
    const spvc_reflected_resource *resource_list = NULL;
    size_t sampler_count = 0;
    size_t resource_count = 0;
    bool *paired_images = NULL;
    spvc_result result;
    bool combined_sampler_build_failed = false;
    bool collected_samplerless_sampled_image = false;
    bool collected_sampled_image_pair_from_spirv_scan = false;
    bool skipped_sampled_image_pair_due_combined_failure = false;
    unsigned sampled_descriptor_set = 0;
    bool has_sampled_descriptor_set = sampled_descriptor_set_for_execution_model(slot_context->execution_model, &sampled_descriptor_set);
    char combined_sampler_failure_message[768];

    if (dref_usage->id_bound > 0) {
        paired_images = (bool *)SDL_calloc(dref_usage->id_bound, sizeof(*paired_images));
        if (!paired_images) {
            return SDL_OutOfMemory();
        }
    }

    result = spvc_compiler_build_combined_image_samplers(compiler);
    if (result < 0) {
        combined_sampler_build_failed = true;
        if (!collect_sampled_texture_layout_evidence_from_spirv_pairs(
                layout_evidence,
                compiler,
                resources,
                slot_context,
                dref_usage,
                paired_images,
                &collected_sampled_image_pair_from_spirv_scan)) {
            SDL_free(paired_images);
            return false;
        }
    } else {
        result = spvc_compiler_get_combined_image_samplers(compiler, &samplers, &sampler_count);
        if (result < 0) {
            SDL_free(paired_images);
            return SDL_SetError("spvc_compiler_get_combined_image_samplers failed");
        }
    }

    for (size_t i = 0; i < sampler_count; i += 1) {
        const spvc_combined_image_sampler *sampler_pair = &samplers[i];
        if (!append_sampled_texture_pair_layout_evidence(
                layout_evidence,
                compiler,
                resources,
                slot_context,
                dref_usage,
                sampler_pair->image_id,
                sampler_pair->sampler_id,
                paired_images)) {
            SDL_free(paired_images);
            return false;
        }
    }

    result = spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, &resource_list, &resource_count);
    if (result < 0) {
        SDL_free(paired_images);
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *image = &resource_list[i];
        spvc_type image_type;
        spvc_basetype sampled_base_type;
        unsigned image_descriptor_set = 0;
        unsigned image_binding = 0;
        bool has_image_binding;
        SDL_ShaderCross_INTERNAL_SampledTextureEvidence evidence;

        if (image_resource_is_storage(compiler, image)) {
            continue;
        }
        if (paired_images && image->id < dref_usage->id_bound && paired_images[image->id]) {
            continue;
        }
        if (image_has_sampled_image_pair(dref_usage, image->id)) {
            if (combined_sampler_build_failed) {
                skipped_sampled_image_pair_due_combined_failure = true;
            }
            continue;
        }
        if (!image_has_sampled_image_fetch(dref_usage, image->id)) {
            layout_evidence->has_samplerless_sampled_image = true;
            continue;
        }

        image_type = spvc_compiler_get_type_handle(compiler, image->base_type_id);
        sampled_base_type = sampled_base_type_for_resource(compiler, image);
        has_image_binding = get_resource_binding(compiler, image, &image_descriptor_set, &image_binding);

        SDL_zero(evidence);
        /* Borrowed from SPIRV-Cross and valid until the compiler context is destroyed. */
        evidence.name = image->name;
        evidence.texture_dimension = resource_layout_texture_dimension_from_type(image_type);
        evidence.multisampled = image_type && spvc_type_get_image_multisampled(image_type) == SPVC_TRUE;
        evidence.image_depth = resource_layout_image_depth_from_analysis(dref_usage, image->base_type_id);
        evidence.sample_kind = spvc_compiler_variable_is_depth_or_compare(compiler, image->id) == SPVC_TRUE ?
            SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH :
            resource_layout_sample_kind_from_base_type(sampled_base_type);
        evidence.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE;
        evidence.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN;
        if (has_sampled_descriptor_set &&
            has_image_binding &&
            image_descriptor_set == sampled_descriptor_set) {
            evidence.has_sdl_slot = true;
            evidence.slot = image_binding;
        }
        if (has_image_binding) {
            evidence.has_source_binding = true;
            evidence.source_set = image_descriptor_set;
            evidence.source_binding = image_binding;
        }

        if (!append_sampled_texture_evidence(layout_evidence, &evidence)) {
            SDL_free(paired_images);
            return false;
        }
        collected_samplerless_sampled_image = true;
    }

    if (combined_sampler_build_failed && skipped_sampled_image_pair_due_combined_failure) {
        if (collected_sampled_image_pair_from_spirv_scan) {
            SDL_free(paired_images);
            return true;
        }
        if (collected_samplerless_sampled_image || layout_evidence->has_samplerless_sampled_image) {
            layout_evidence->has_samplerless_sampled_image = true;
            SDL_free(paired_images);
            return true;
        }
        SDL_free(paired_images);
        format_combined_sampler_failure_message(
            combined_sampler_failure_message,
            sizeof(combined_sampler_failure_message),
            compiler,
            resources,
            slot_context,
            dref_usage);
        return SDL_SetError("%s", combined_sampler_failure_message);
    }

    if (combined_sampler_build_failed &&
        !collected_sampled_image_pair_from_spirv_scan &&
        !collected_samplerless_sampled_image &&
        !layout_evidence->has_samplerless_sampled_image) {
        SDL_free(paired_images);
        format_combined_sampler_failure_message(
            combined_sampler_failure_message,
            sizeof(combined_sampler_failure_message),
            compiler,
            resources,
            slot_context,
            dref_usage);
        return SDL_SetError("%s", combined_sampler_failure_message);
    }

    SDL_free(paired_images);
    return true;
}

static bool collect_storage_texture_layout_entries(
    ShaderCross_CLIResourceLayoutEvidence *layout_evidence,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    spvc_resource_type resource_type,
    bool allow_observed_storage_access,
    bool classify_fragment_writable_storage,
    bool collect_storage_images)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    spvc_result result = spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count);

    if (result < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        spvc_type base_type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
        bool non_readable;
        bool non_writable;
        unsigned descriptor_set = 0;
        unsigned binding = 0;
        bool has_binding;
        SDL_ShaderCross_INTERNAL_StorageTextureEvidence evidence;

        if (resource_type != SPVC_RESOURCE_TYPE_STORAGE_IMAGE) {
            if (!collect_storage_images || !image_resource_is_storage(compiler, resource)) {
                continue;
            }
        }

        non_readable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonReadable) == SPVC_TRUE;
        non_writable = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationNonWritable) == SPVC_TRUE;
        has_binding = get_resource_binding(compiler, resource, &descriptor_set, &binding);

        SDL_zero(evidence);
        /* Borrowed from SPIRV-Cross and valid until the compiler context is destroyed. */
        evidence.name = resource->name;
        evidence.image_type_id = resource->base_type_id;
        evidence.image_format = base_type ? (Uint32)spvc_type_get_image_storage_format(base_type) : (Uint32)SpvImageFormatUnknown;
        if (has_binding) {
            evidence.has_sdl_slot = get_sdl_slot_for_resource(slot_context, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, descriptor_set, binding, &evidence.slot);
            evidence.has_source_binding = true;
            evidence.source_set = descriptor_set;
            evidence.source_binding = binding;
            if (!storage_texture_class_from_descriptor_set(slot_context, descriptor_set, &evidence.resource_class)) {
                evidence.resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE;
            }
        }

        evidence.texture_dimension = (base_type && spvc_type_get_image_multisampled(base_type) == SPVC_TRUE) ?
            SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_UNKNOWN :
            resource_layout_texture_dimension_from_type(base_type);
        evidence.format = base_type ?
            resource_layout_storage_format_from_spv(spvc_type_get_image_storage_format(base_type)) :
            SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_UNKNOWN;
        evidence.observed_access = resource_layout_storage_access_from_observed(dref_usage, resource->id);
        evidence.uses_image_texel_pointer = dref_usage &&
            resource->id < dref_usage->id_bound &&
            dref_usage->image_resource_texel_pointers[resource->id];
        evidence.final_access = base_type ?
            resource_layout_storage_access_from_evidence(
                non_readable,
                non_writable,
                spvc_type_get_image_access_qualifier(base_type),
                dref_usage,
                resource->id,
                allow_observed_storage_access) :
            SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;

        if (storage_texture_access_is_writable(evidence.final_access) &&
            reflected_resource_is_array(compiler, resource)) {
            return SDL_SetError("shader-visible writable storage texture arrays are not supported by SDL_GPU resource layout C output");
        }

        if (classify_fragment_writable_storage &&
            slot_context->execution_model == SpvExecutionModelFragment &&
            storage_texture_access_is_writable(evidence.final_access)) {
            evidence.resource_class = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE;
        } else if (classify_fragment_writable_storage &&
                   slot_context->execution_model == SpvExecutionModelVertex &&
                   storage_texture_access_is_writable(evidence.final_access)) {
            return SDL_SetError("vertex-stage writable storage textures are not supported by SDL_GPU resource layout C output");
        }

        if (!append_storage_texture_evidence(layout_evidence, &evidence)) {
            return false;
        }
    }

    return true;
}

static bool collect_resource_layout_evidence(
    ShaderCross_CLIResourceLayoutEvidence *layout_evidence,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const ShaderCross_CLIResourceLayoutEvidenceOptions *options)
{
    if (options->collect_sampled_textures) {
        if (!collect_sampled_texture_layout_evidence(layout_evidence, compiler, resources, slot_context, dref_usage)) {
            return false;
        }
    }
    if (!collect_storage_texture_layout_entries(
            layout_evidence,
            compiler,
            resources,
            slot_context,
            dref_usage,
            SPVC_RESOURCE_TYPE_STORAGE_IMAGE,
            options->allow_observed_storage_access,
            options->classify_fragment_writable_storage,
            false)) {
        return false;
    }
    if (!collect_storage_texture_layout_entries(
            layout_evidence,
            compiler,
            resources,
            slot_context,
            dref_usage,
            SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,
            options->allow_observed_storage_access,
            options->classify_fragment_writable_storage,
            true)) {
        return false;
    }
    return true;
}

static bool apply_sampled_slot_policies(
    ShaderCross_CLIResourceLayoutEvidence *evidence,
    Uint32 num_sampled_texture_slots,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies,
    Uint32 num_sampled_slot_policies)
{
    bool *seen = NULL;

    if (num_sampled_slot_policies == 0) {
        return true;
    }
    if (sampled_slot_policies == NULL) {
        return SDL_InvalidParamError("sampled_slot_policies");
    }
    if (num_sampled_texture_slots > 0) {
        seen = (bool *)SDL_calloc(num_sampled_texture_slots, sizeof(*seen));
        if (seen == NULL) {
            return SDL_OutOfMemory();
        }
    }

    for (Uint32 i = 0; i < num_sampled_slot_policies; i += 1) {
        const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *policy = &sampled_slot_policies[i];
        SDL_ShaderCross_INTERNAL_SampledTextureEvidence *match = NULL;
        Uint32 matches = 0;

        if (policy->slot >= num_sampled_texture_slots) {
            SDL_SetError(
                "sampled slot policy slot %u exceeds the reflected SDL sampled texture count %u",
                policy->slot,
                num_sampled_texture_slots);
            SDL_free(seen);
            return false;
        }
        if (seen[policy->slot]) {
            SDL_SetError("sampled slot policy slot %u was provided more than once", policy->slot);
            SDL_free(seen);
            return false;
        }
        seen[policy->slot] = true;

        for (Uint32 j = 0; j < evidence->num_sampled_textures; j += 1) {
            if (evidence->sampled_textures[j].has_sdl_slot &&
                evidence->sampled_textures[j].slot == policy->slot) {
                match = &evidence->sampled_textures[j];
                matches += 1;
            }
        }

        if (matches == 0) {
            SDL_SetError(
                "sampled slot policy slot %u does not match a reflected sampled texture",
                policy->slot);
            SDL_free(seen);
            return false;
        }
        if (matches > 1) {
            SDL_SetError(
                "sampled slot policy slot %u matches multiple reflected sampled textures",
                policy->slot);
            SDL_free(seen);
            return false;
        }

        match->has_explicit_slot_policy = true;
        match->explicit_slot_policy = policy->description;
    }

    SDL_free(seen);
    return true;
}

static bool storage_policy_slot_count(
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class,
    Uint32 *count)
{
    switch (resource_class) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE:
        if (input->kind != SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER) {
            return false;
        }
        *count = input->num_storage_textures;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
        if (input->kind != SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE) {
            return false;
        }
        *count = input->num_readonly_storage_textures;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
        if (input->kind != SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE) {
            return false;
        }
        *count = input->num_readwrite_storage_textures;
        return true;
    default:
        return false;
    }
}

static const char *storage_policy_class_name(SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class)
{
    switch (resource_class) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
        return "storage";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
        return "readonly";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
        return "readwrite";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE:
        return "fragment_writable";
    default:
        return "unknown";
    }
}

static bool apply_storage_texture_slot_policies(
    ShaderCross_CLIResourceLayoutEvidence *evidence,
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies)
{
    bool *storage_seen = NULL;
    bool *readonly_seen = NULL;
    bool *readwrite_seen = NULL;
    bool result = false;

    if (num_storage_texture_slot_policies == 0) {
        return true;
    }
    if (storage_texture_slot_policies == NULL) {
        return SDL_InvalidParamError("storage_texture_slot_policies");
    }

    if (input->num_storage_textures > 0) {
        storage_seen = (bool *)SDL_calloc(input->num_storage_textures, sizeof(*storage_seen));
        if (!storage_seen) {
            return SDL_OutOfMemory();
        }
    }
    if (input->num_readonly_storage_textures > 0) {
        readonly_seen = (bool *)SDL_calloc(input->num_readonly_storage_textures, sizeof(*readonly_seen));
        if (!readonly_seen) {
            SDL_OutOfMemory();
            goto done;
        }
    }
    if (input->num_readwrite_storage_textures > 0) {
        readwrite_seen = (bool *)SDL_calloc(input->num_readwrite_storage_textures, sizeof(*readwrite_seen));
        if (!readwrite_seen) {
            SDL_OutOfMemory();
            goto done;
        }
    }

    for (Uint32 i = 0; i < num_storage_texture_slot_policies; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *policy = &storage_texture_slot_policies[i];
        SDL_ShaderCross_INTERNAL_StorageTextureEvidence *match = NULL;
        bool *seen = NULL;
        Uint32 count = 0;
        Uint32 matches = 0;

        if (!storage_policy_slot_count(input, policy->resource_class, &count)) {
            SDL_SetError("storage texture slot policy class '%s' does not match the reflected shader stage", storage_policy_class_name(policy->resource_class));
            goto done;
        }
        if (policy->slot >= count) {
            SDL_SetError(
                "storage texture slot policy class '%s' slot %u exceeds the reflected SDL storage texture count %u",
                storage_policy_class_name(policy->resource_class),
                policy->slot,
                count);
            goto done;
        }

        switch (policy->resource_class) {
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE:
            seen = storage_seen;
            break;
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
            seen = readonly_seen;
            break;
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
            seen = readwrite_seen;
            break;
        default:
            SDL_SetError("unknown storage texture slot policy class");
            goto done;
        }
        if (seen && seen[policy->slot]) {
            SDL_SetError(
                "storage texture slot policy class '%s' slot %u was provided more than once",
                storage_policy_class_name(policy->resource_class),
                policy->slot);
            goto done;
        }
        if (seen) {
            seen[policy->slot] = true;
        }

        for (Uint32 j = 0; j < evidence->num_storage_textures; j += 1) {
            if (evidence->storage_textures[j].has_sdl_slot &&
                evidence->storage_textures[j].resource_class == policy->resource_class &&
                evidence->storage_textures[j].slot == policy->slot) {
                match = &evidence->storage_textures[j];
                matches += 1;
            }
        }

        if (matches == 0) {
            SDL_SetError(
                "storage texture slot policy class '%s' slot %u does not match a reflected storage texture",
                storage_policy_class_name(policy->resource_class),
                policy->slot);
            goto done;
        }
        if (matches > 1) {
            SDL_SetError(
                "storage texture slot policy class '%s' slot %u matches multiple reflected storage textures",
                storage_policy_class_name(policy->resource_class),
                policy->slot);
            goto done;
        }

        match->has_explicit_slot_policy = true;
        match->explicit_slot_policy_format_authority = policy->format_authority;
        match->explicit_slot_policy = policy->description;
    }

    result = true;

done:
    SDL_free(storage_seen);
    SDL_free(readonly_seen);
    SDL_free(readwrite_seen);
    return result;
}

static const char *wgsl_storage_texture_type(SDL_GPUTextureType texture_type)
{
    switch (texture_type) {
    case SDL_GPU_TEXTURETYPE_2D:
        return "2d";
    case SDL_GPU_TEXTURETYPE_2D_ARRAY:
        return "2d_array";
    case SDL_GPU_TEXTURETYPE_3D:
        return "3d";
    default:
        return NULL;
    }
}

static const char *wgsl_storage_texture_format(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return "rgba8unorm";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
        return "rgba8snorm";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        return "rgba16float";
    case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
        return "rg32float";
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
        return "rgba32float";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
        return "rgba8uint";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
        return "rgba16uint";
    case SDL_GPU_TEXTUREFORMAT_R32_UINT:
        return "r32uint";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
        return "rgba8sint";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
        return "rgba16sint";
    case SDL_GPU_TEXTUREFORMAT_R32_INT:
        return "r32sint";
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
        return "r32float";
    default:
        return NULL;
    }
}

static const char *wgsl_storage_texture_internal_format(SDL_ShaderCross_INTERNAL_StorageTextureFormat format)
{
    switch (format) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM:
        return "rgba8unorm";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM:
        return "rgba8snorm";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT:
        return "rgba16float";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT:
        return "rg32float";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT:
        return "rgba32float";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_UINT:
        return "rgba32uint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT:
        return "rgba8uint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT:
        return "rgba16uint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT:
        return "r32uint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_INT:
        return "rgba32sint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT:
        return "rgba8sint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT:
        return "rgba16sint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT:
        return "r32sint";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT:
        return "r32float";
    default:
        return NULL;
    }
}

static const char *wgsl_storage_texture_access(SDL_GPUStorageTextureAccess access)
{
    switch (access) {
    case SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY:
        return "read";
    case SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY:
        return "write";
    case SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE:
        return "read_write";
    default:
        return NULL;
    }
}

static const char *wgsl_storage_texture_internal_access(SDL_ShaderCross_INTERNAL_StorageTextureAccess access)
{
    switch (access) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY:
        return "read";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY:
        return "write";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE:
        return "read_write";
    default:
        return NULL;
    }
}

static bool replace_wgsl_range(char **text, size_t start, size_t end, const char *replacement)
{
    const size_t old_len = SDL_strlen(*text);
    const size_t replacement_len = SDL_strlen(replacement);
    const size_t new_len = old_len - (end - start) + replacement_len;
    char *updated = NULL;

    if (start > end || end > old_len) {
        return SDL_SetError("invalid WGSL replacement range");
    }

    updated = (char *)SDL_malloc(new_len + 1);
    if (updated == NULL) {
        return SDL_OutOfMemory();
    }

    SDL_memcpy(updated, *text, start);
    SDL_memcpy(updated + start, replacement, replacement_len);
    SDL_memcpy(updated + start + replacement_len, *text + end, old_len - end);
    updated[new_len] = '\0';
    SDL_free(*text);
    *text = updated;
    return true;
}

static bool wgsl_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool wgsl_range_has_attribute_u32(const char *start, const char *end, const char *attribute, Uint32 expected)
{
    const size_t attribute_len = SDL_strlen(attribute);
    const char *cursor = start;

    while (cursor < end && (cursor = SDL_strstr(cursor, attribute)) != NULL && cursor < end) {
        const char *p = cursor + attribute_len;
        Uint32 value = 0;
        bool saw_digit = false;

        while (p < end && wgsl_is_space(*p)) {
            p += 1;
        }
        if (p >= end || *p != '(') {
            cursor += attribute_len;
            continue;
        }
        p += 1;
        while (p < end && wgsl_is_space(*p)) {
            p += 1;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            saw_digit = true;
            value = value * 10u + (Uint32)(*p - '0');
            p += 1;
        }
        if (p < end && *p == 'u') {
            p += 1;
        }
        while (p < end && wgsl_is_space(*p)) {
            p += 1;
        }
        if (saw_digit && p < end && *p == ')' && value == expected) {
            return true;
        }
        cursor += attribute_len;
    }

    return false;
}

static bool wgsl_range_has_group_binding(const char *start, const char *end, Uint32 group, Uint32 binding)
{
    return wgsl_range_has_attribute_u32(start, end, "@group", group) &&
           wgsl_range_has_attribute_u32(start, end, "@binding", binding);
}

static bool copy_wgsl_type_token(char *dst, size_t dst_size, const char *start, const char *end)
{
    size_t len;

    while (start < end && wgsl_is_space(*start)) {
        start += 1;
    }
    while (end > start && wgsl_is_space(end[-1])) {
        end -= 1;
    }

    len = (size_t)(end - start);
    if (len == 0 || len >= dst_size) {
        return false;
    }

    SDL_memcpy(dst, start, len);
    dst[len] = '\0';
    return true;
}

static bool parse_wgsl_storage_texture_format_access(
    const char *type_start,
    const char *type_end,
    char *format,
    size_t format_size,
    char *access,
    size_t access_size)
{
    const char *params_start = SDL_strchr(type_start, '<');
    const char *comma;
    const char *extra_comma;

    if (params_start == NULL || params_start >= type_end) {
        return false;
    }

    params_start += 1;
    comma = SDL_strchr(params_start, ',');
    if (comma == NULL || comma >= type_end) {
        return false;
    }

    extra_comma = SDL_strchr(comma + 1, ',');
    if (extra_comma != NULL && extra_comma < type_end) {
        return false;
    }

    return copy_wgsl_type_token(format, format_size, params_start, comma) &&
           copy_wgsl_type_token(access, access_size, comma + 1, type_end);
}

static bool wgsl_storage_texture_existing_access_matches_policy(
    const char *existing_access,
    const char *policy_access,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence)
{
    const char *final_access = wgsl_storage_texture_internal_access(evidence->final_access);

    if (SDL_strcmp(existing_access, policy_access) == 0) {
        return true;
    }
    if (final_access != NULL && SDL_strcmp(existing_access, final_access) == 0) {
        return true;
    }

    return evidence->observed_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY &&
           SDL_strcmp(policy_access, "write") == 0 &&
           SDL_strcmp(existing_access, "read_write") == 0;
}

static bool patch_wgsl_storage_texture_declaration(
    char **wgsl_text,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    const SDL_GPUStorageTextureSlotDescription *slot)
{
    const char *type_name = wgsl_storage_texture_type(slot->texture_type);
    const char *format_name = wgsl_storage_texture_format(slot->format);
    const char *reflected_format_name = wgsl_storage_texture_internal_format(evidence->format);
    const char *access_name = wgsl_storage_texture_access(slot->access);
    char expected_prefix[64];
    char *replacement = NULL;
    size_t match_start = 0;
    size_t match_end = 0;
    Uint32 matches = 0;
    const char *text = *wgsl_text;
    const char *cursor = text;
    bool result = false;

    if (!type_name || !format_name || !access_name) {
        return SDL_SetError("storage texture evidence cannot be represented as WGSL");
    }
    if (!evidence->has_source_binding) {
        return SDL_SetError("storage texture class '%s' slot %u has no reflected source group/binding for WGSL output",
            storage_policy_class_name(evidence->resource_class),
            evidence->slot);
    }

    SDL_snprintf(expected_prefix, sizeof(expected_prefix), "texture_storage_%s<", type_name);
    if (SDL_asprintf(&replacement, "texture_storage_%s<%s, %s>", type_name, format_name, access_name) < 0) {
        return SDL_OutOfMemory();
    }

    while ((cursor = SDL_strstr(cursor, "texture_storage_")) != NULL) {
        const char *declaration_start = cursor;
        const char *declaration_end = SDL_strchr(cursor, ';');
        const char *type_start = cursor;
        const char *type_end = SDL_strchr(type_start, '>');

        while (declaration_start > text && declaration_start[-1] != ';') {
            declaration_start -= 1;
        }
        if (declaration_end == NULL) {
            declaration_end = type_start + SDL_strlen(type_start);
        } else {
            declaration_end += 1;
        }

        if (wgsl_range_has_group_binding(declaration_start, declaration_end, evidence->source_set, evidence->source_binding)) {
            const size_t prefix_len = SDL_strlen(expected_prefix);
            char existing_format[64];
            char existing_access[64];

            if (type_end == NULL || type_end >= declaration_end) {
                SDL_SetError(
                    "WGSL storage texture declaration for group %u binding %u could not be parsed for storage texture evidence",
                    evidence->source_set,
                    evidence->source_binding);
                goto done;
            }
            if (SDL_strncmp(type_start, expected_prefix, prefix_len) != 0) {
                SDL_SetError(
                    "WGSL storage texture declaration for group %u binding %u uses a texture type that does not match storage texture evidence",
                    evidence->source_set,
                    evidence->source_binding);
                goto done;
            }
            if (!parse_wgsl_storage_texture_format_access(
                    type_start,
                    type_end,
                    existing_format,
                    sizeof(existing_format),
                    existing_access,
                    sizeof(existing_access))) {
                SDL_SetError(
                    "WGSL storage texture declaration for group %u binding %u could not be parsed for storage texture evidence",
                    evidence->source_set,
                    evidence->source_binding);
                goto done;
            }
            if (SDL_strcmp(existing_format, format_name) != 0 &&
                (!evidence->explicit_slot_policy_format_authority ||
                 reflected_format_name == NULL ||
                 SDL_strcmp(existing_format, reflected_format_name) != 0)) {
                SDL_SetError(
                    "WGSL storage texture declaration for group %u binding %u uses a format that does not match storage texture evidence",
                    evidence->source_set,
                    evidence->source_binding);
                goto done;
            }
            if (!wgsl_storage_texture_existing_access_matches_policy(existing_access, access_name, evidence)) {
                SDL_SetError(
                    "WGSL storage texture declaration for group %u binding %u uses an access that does not match storage texture evidence",
                    evidence->source_set,
                    evidence->source_binding);
                goto done;
            }

            matches += 1;
            match_start = (size_t)(type_start - text);
            match_end = (size_t)(type_end - text) + 1;
        }

        cursor = type_start + 1;
    }

    if (matches == 0) {
        SDL_SetError(
            "WGSL storage texture declaration for group %u binding %u was not found",
            evidence->source_set,
            evidence->source_binding);
        goto done;
    }
    if (matches > 1) {
        SDL_SetError(
            "WGSL storage texture declaration for group %u binding %u matched multiple declarations",
            evidence->source_set,
            evidence->source_binding);
        goto done;
    }

    result = replace_wgsl_range(wgsl_text, match_start, match_end, replacement);

done:
    SDL_free(replacement);
    return result;
}

static bool all_layout_slots_seen(const bool *seen, Uint32 count)
{
    for (Uint32 i = 0; i < count; i += 1) {
        if (!seen[i]) {
            return false;
        }
    }
    return true;
}

static bool fragment_storage_binding_slot(
    const SDLResourceLayoutContext *slot_context,
    unsigned descriptor_set,
    unsigned binding,
    Uint32 *slot)
{
    if (slot_context->execution_model != SpvExecutionModelFragment ||
        descriptor_set != 2 ||
        binding < slot_context->sampled_texture_slots[descriptor_set]) {
        return false;
    }

    if (slot) {
        *slot = binding - slot_context->sampled_texture_slots[descriptor_set];
    }
    return true;
}

static bool validate_fragment_storage_texture_slots(
    const ShaderCross_CLIResourceLayoutEvidence *evidence,
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    Uint32 num_readonly_storage_buffers,
    Uint32 *num_writable_storage_textures)
{
    bool *readonly_seen = NULL;
    bool *writable_seen = NULL;
    Uint32 num_readonly_storage_textures = 0;
    bool result = false;

    *num_writable_storage_textures = 0;

    for (Uint32 i = 0; i < evidence->num_storage_textures; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entry = &evidence->storage_textures[i];
        if (entry->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE) {
            *num_writable_storage_textures += 1;
            if (!entry->has_sdl_slot) {
                return SDL_SetError("fragment writable storage texture binding does not match SDL's resource layout slot convention");
            }
        } else if (entry->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE) {
            if (entry->has_sdl_slot) {
                num_readonly_storage_textures += 1;
            }
        }
    }

    if (num_readonly_storage_textures > 0) {
        readonly_seen = (bool *)SDL_calloc(num_readonly_storage_textures, sizeof(*readonly_seen));
        if (!readonly_seen) {
            return SDL_OutOfMemory();
        }
    }
    if (*num_writable_storage_textures > 0) {
        writable_seen = (bool *)SDL_calloc(*num_writable_storage_textures, sizeof(*writable_seen));
        if (!writable_seen) {
            SDL_free(readonly_seen);
            return SDL_OutOfMemory();
        }
    }

    for (Uint32 i = 0; i < evidence->num_storage_textures; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entry = &evidence->storage_textures[i];

        if (!entry->has_sdl_slot) {
            continue;
        }
        if (entry->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE) {
            if (entry->slot >= num_readonly_storage_textures || readonly_seen[entry->slot]) {
                SDL_SetError("fragment storage texture binding does not match SDL's resource layout slot convention");
                goto done;
            }
            readonly_seen[entry->slot] = true;
        } else if (entry->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE) {
            const Uint32 writable_start = num_readonly_storage_textures + num_readonly_storage_buffers;
            Uint32 writable_slot;
            if (entry->slot < writable_start) {
                SDL_SetError("fragment writable storage texture binding does not follow read-only storage resources");
                goto done;
            }
            writable_slot = entry->slot - writable_start;
            if (writable_slot >= *num_writable_storage_textures || writable_seen[writable_slot]) {
                SDL_SetError("fragment writable storage texture binding does not match SDL's resource layout slot convention");
                goto done;
            }
            writable_seen[writable_slot] = true;
        }
    }

    if (!all_layout_slots_seen(readonly_seen, num_readonly_storage_textures) ||
        !all_layout_slots_seen(writable_seen, *num_writable_storage_textures)) {
        SDL_SetError("fragment storage texture binding does not match SDL's resource layout slot convention");
        goto done;
    }

    layout->num_storage_textures = num_readonly_storage_textures;
    result = true;

done:
    SDL_free(readonly_seen);
    SDL_free(writable_seen);
    return result;
}

static void remove_fragment_writable_storage_texture_evidence(ShaderCross_CLIResourceLayoutEvidence *evidence)
{
    Uint32 dst = 0;

    for (Uint32 src = 0; src < evidence->num_storage_textures; src += 1) {
        if (evidence->storage_textures[src].resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE) {
            continue;
        }
        if (dst != src) {
            evidence->storage_textures[dst] = evidence->storage_textures[src];
        }
        dst += 1;
    }

    evidence->num_storage_textures = dst;
}

static bool wgsl_storage_texture_declaration_needs_update(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence)
{
    return evidence->has_explicit_slot_policy ||
           evidence->observed_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY;
}

static bool resource_has_buffer_writes(const DrefUsageAnalysis *dref_usage, const spvc_reflected_resource *resource)
{
    return resource != NULL &&
           resource->id < dref_usage->id_bound &&
           dref_usage->buffer_resource_writes[resource->id];
}

static bool resource_has_buffer_atomics(const DrefUsageAnalysis *dref_usage, const spvc_reflected_resource *resource)
{
    return resource != NULL &&
           resource->id < dref_usage->id_bound &&
           dref_usage->buffer_resource_atomics[resource->id];
}

static bool count_fragment_storage_buffers(
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    Uint32 *num_readonly_storage_buffers,
    Uint32 *num_writable_storage_buffers)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;

    *num_readonly_storage_buffers = 0;
    *num_writable_storage_buffers = 0;

    if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &resource_list, &resource_count) < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        unsigned descriptor_set = 0;
        unsigned binding = 0;
        bool writable = resource_has_buffer_writes(dref_usage, resource);

        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding) ||
            !fragment_storage_binding_slot(slot_context, descriptor_set, binding, NULL)) {
            SDL_SetError("fragment storage buffer binding does not match SDL's resource layout slot convention");
            return false;
        }
        if (resource_has_buffer_atomics(dref_usage, resource)) {
            SDL_SetError("fragment storage buffer atomics are not supported by SDL_GPU resource layout C output");
            return false;
        }
        if (writable) {
            if (reflected_resource_is_array(compiler, resource)) {
                SDL_SetError("shader-visible writable storage buffer arrays are not supported by SDL_GPU resource layout C output");
                return false;
            }
            *num_writable_storage_buffers += 1;
        } else {
            *num_readonly_storage_buffers += 1;
        }
    }

    return true;
}

static bool validate_fragment_storage_buffer_slots(
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    Uint32 num_writable_storage_textures,
    Uint32 num_readonly_storage_buffers,
    Uint32 num_writable_storage_buffers,
    bool *has_writable_storage)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    bool *readonly_seen = NULL;
    bool *writable_seen = NULL;
    spvc_result spvc_result_value;
    bool result = false;

    spvc_result_value = spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &resource_list, &resource_count);
    if (spvc_result_value < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    *has_writable_storage = num_writable_storage_buffers > 0;

    if (num_readonly_storage_buffers > 0) {
        readonly_seen = (bool *)SDL_calloc(num_readonly_storage_buffers, sizeof(*readonly_seen));
        if (!readonly_seen) {
            return SDL_OutOfMemory();
        }
    }
    if (num_writable_storage_buffers > 0) {
        writable_seen = (bool *)SDL_calloc(num_writable_storage_buffers, sizeof(*writable_seen));
        if (!writable_seen) {
            SDL_free(readonly_seen);
            return SDL_OutOfMemory();
        }
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        unsigned descriptor_set = 0;
        unsigned binding = 0;
        bool writable = resource_has_buffer_writes(dref_usage, resource);
        Uint32 storage_slot = 0;

        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding) ||
            !fragment_storage_binding_slot(slot_context, descriptor_set, binding, &storage_slot)) {
            SDL_SetError("fragment storage buffer binding does not match SDL's resource layout slot convention");
            goto done;
        }
        if (writable) {
            const Uint32 writable_start = layout->num_storage_textures + num_readonly_storage_buffers + num_writable_storage_textures;
            Uint32 writable_slot;
            if (storage_slot < writable_start) {
                SDL_SetError("fragment writable storage buffer binding does not follow read-only storage buffers");
                goto done;
            }
            writable_slot = storage_slot - writable_start;
            if (writable_slot >= num_writable_storage_buffers || writable_seen[writable_slot]) {
                SDL_SetError("fragment writable storage buffer binding does not match SDL's resource layout slot convention");
                goto done;
            }
            writable_seen[writable_slot] = true;
        } else {
            Uint32 readonly_slot;
            if (storage_slot < layout->num_storage_textures) {
                SDL_SetError("fragment storage buffer binding does not follow read-only storage textures");
                goto done;
            }
            readonly_slot = storage_slot - layout->num_storage_textures;
            if (readonly_slot >= num_readonly_storage_buffers || readonly_seen[readonly_slot]) {
                SDL_SetError("fragment storage buffer binding does not match SDL's resource layout slot convention");
                goto done;
            }
            readonly_seen[readonly_slot] = true;
        }
    }

    if (!all_layout_slots_seen(readonly_seen, num_readonly_storage_buffers) ||
        !all_layout_slots_seen(writable_seen, num_writable_storage_buffers)) {
        SDL_SetError("fragment storage buffer binding does not match SDL's resource layout slot convention");
        goto done;
    }

    layout->num_storage_buffers = num_readonly_storage_buffers;
    result = true;

done:
    SDL_free(readonly_seen);
    SDL_free(writable_seen);
    return result;
}

static bool reject_vertex_writable_storage_buffers(
    spvc_compiler compiler,
    spvc_resources resources,
    const DrefUsageAnalysis *dref_usage)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;

    if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &resource_list, &resource_count) < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        unsigned descriptor_set = 0;
        unsigned binding = 0;

        if (!get_resource_binding(compiler, resource, &descriptor_set, &binding)) {
            continue;
        }
        if (descriptor_set == 0 &&
            (resource_has_buffer_writes(dref_usage, resource) ||
             resource_has_buffer_atomics(dref_usage, resource))) {
            SDL_SetError("vertex-stage writable storage buffers are not supported by SDL_GPU resource layout C output");
            return false;
        }
    }

    return true;
}

static bool validate_fragment_storage_resource_layout(
    const ShaderCross_CLIResourceLayoutEvidence *evidence,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    bool reject_writable_storage)
{
    Uint32 num_readonly_storage_buffers = 0;
    Uint32 num_writable_storage_buffers = 0;
    Uint32 num_writable_storage_textures = 0;
    bool has_writable_storage_buffers = false;

    if (slot_context->execution_model == SpvExecutionModelVertex) {
        return reject_vertex_writable_storage_buffers(compiler, resources, dref_usage);
    }
    if (slot_context->execution_model != SpvExecutionModelFragment) {
        return true;
    }
    if (!count_fragment_storage_buffers(
            compiler,
            resources,
            slot_context,
            dref_usage,
            &num_readonly_storage_buffers,
            &num_writable_storage_buffers)) {
        return false;
    }
    if (!validate_fragment_storage_texture_slots(
            evidence,
            layout,
            num_readonly_storage_buffers,
            &num_writable_storage_textures)) {
        return false;
    }
    if (!validate_fragment_storage_buffer_slots(
            compiler,
            resources,
            slot_context,
            dref_usage,
            layout,
            num_writable_storage_textures,
            num_readonly_storage_buffers,
            num_writable_storage_buffers,
            &has_writable_storage_buffers)) {
        return false;
    }
    if (reject_writable_storage &&
        (num_writable_storage_textures > 0 || has_writable_storage_buffers)) {
        return SDL_SetError("fragment writable storage layout C output is not supported by SDL_gpu.h resource layout facts");
    }
    return true;
}

static const char *layout_facts_reason_to_string(Uint64 reason)
{
    switch (reason) {
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLERLESS_SAMPLED_IMAGE:
        return "samplerless sampled texture evidence is missing concrete per-slot exact-load layout facts";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_UNMAPPED:
        return "sampled texture binding does not match SDL's slot convention";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_OUT_OF_RANGE:
        return "sampled texture slot exceeds the reflected SDL sampler count";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_DUPLICATE:
        return "multiple sampled texture entries map to the same SDL slot";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED:
        return "sampled texture dimension is outside the current SDL_GPU resource layout fact matrix";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED:
        return "sampled texture sample kind and sampler binding kind are outside the current SDL_GPU resource layout fact matrix";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_UNMAPPED:
        return "storage texture binding does not match SDL's slot convention";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_OUT_OF_RANGE:
        return "storage texture slot exceeds the reflected SDL storage texture count";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_DUPLICATE:
        return "multiple storage texture entries map to the same SDL slot";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED:
        return "storage texture dimension is outside the current SDL_GPU resource layout fact matrix";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED:
        return "storage texture format is outside the current SDL_GPU resource layout fact matrix";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_ACCESS_AMBIGUOUS:
        return "storage texture access could not be resolved to concrete layout facts";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED:
        return "storage texture access is not supported for the SDL storage texture group";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED:
        return "resource layout fact output capacity was too small";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DEPTH_AMBIGUOUS:
        return "automatic MSAA sampled texture layout facts require concrete final SPIR-V OpTypeImage Depth=0 or Depth=1 evidence; use an explicit sampled-slot policy when the intended SDL_GPU sampled slot shape is known";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH:
        return "explicit sampled-slot policy does not match the reflected sampled texture evidence";
    case SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH:
        return "--storage-texture-slot does not match the reflected storage texture evidence";
    default:
        return NULL;
    }
}

static bool append_diagnostic_text(char **message, const char *fmt, ...)
{
    char *text = NULL;
    char *combined = NULL;
    va_list ap;
    int size;

    va_start(ap, fmt);
    size = SDL_vasprintf(&text, fmt, ap);
    va_end(ap);
    if (size < 0) {
        return SDL_OutOfMemory();
    }

    if (*message == NULL) {
        *message = text;
        return true;
    }

    if (SDL_asprintf(&combined, "%s%s", *message, text) < 0) {
        SDL_free(text);
        return SDL_OutOfMemory();
    }

    SDL_free(*message);
    SDL_free(text);
    *message = combined;
    return true;
}

static const char *storage_texture_internal_dimension_to_string(SDL_ShaderCross_INTERNAL_TextureDimension dimension)
{
    switch (dimension) {
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D:
        return "2d";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY:
        return "2d_array";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D:
        return "3d";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE:
        return "cube";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY:
        return "cube_array";
    default:
        return "unknown";
    }
}

static const char *storage_texture_internal_access_policy_token(SDL_ShaderCross_INTERNAL_StorageTextureAccess access)
{
    switch (access) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY:
        return "read";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY:
        return "write";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE:
        return "read_write";
    default:
        return NULL;
    }
}

static bool storage_texture_internal_access_to_slot_access(
    SDL_ShaderCross_INTERNAL_StorageTextureAccess access,
    SDL_GPUStorageTextureAccess *slot_access)
{
    switch (access) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY:
        *slot_access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY:
        *slot_access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE:
        *slot_access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE;
        return true;
    default:
        return false;
    }
}

static const char *storage_texture_slot_access_policy_token(SDL_GPUStorageTextureAccess access)
{
    switch (access) {
    case SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY:
        return "read";
    case SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY:
        return "write";
    case SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE:
        return "read_write";
    default:
        return NULL;
    }
}

static const char *storage_texture_decision_authority_to_string(
    SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority authority)
{
    switch (authority) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED:
        return "reflected";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_OBSERVED_WRITE:
        return "observed_write";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_POLICY:
        return "explicit_policy";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_FORMAT_POLICY:
        return "explicit_format_policy";
    default:
        return "none";
    }
}

static const char *storage_texture_unsupported_boundary(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    const SDL_ShaderCross_INTERNAL_StorageTextureDecision *decision)
{
    if (evidence->has_explicit_slot_policy && evidence->explicit_slot_policy_format_authority) {
        if (evidence->resource_class != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE) {
            return "format_authority_explicit_requires_compute_readwrite_class";
        }
        if (decision->slot.access != SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY) {
            return "format_authority_explicit_requires_write_only_access";
        }
        if (decision->slot.texture_type != SDL_GPU_TEXTURETYPE_2D) {
            return "format_authority_explicit_requires_2d_texture";
        }
    }
    if (evidence->texture_dimension == SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_UNKNOWN) {
        return "storage_texture_dimension_unsupported_or_ambiguous";
    }
    if (evidence->format == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_UNKNOWN) {
        return "storage_texture_format_unsupported_or_ambiguous";
    }
    if (decision->effective_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN) {
        return "storage_texture_access_unsupported_or_ambiguous";
    }
    if (evidence->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE) {
        return "fragment_writable_storage_unsupported";
    }
    return "none";
}

static const char *storage_texture_evidence_stage_name(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence)
{
    switch (evidence->resource_class) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
        return "compute";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE:
        return "fragment";
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
        if (evidence->has_source_binding) {
            if (evidence->source_set == 0) {
                return "vertex";
            }
            if (evidence->source_set == 2) {
                return "fragment";
            }
        }
        return "graphics";
    default:
        return "unknown";
    }
}

static bool append_storage_texture_policy_text(
    char **message,
    const char *prefix,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_GPUTextureType texture_type,
    SDL_GPUTextureFormat format,
    const char *access,
    bool format_authority)
{
    const char *texture = wgsl_storage_texture_type(texture_type);
    const char *format_name = wgsl_storage_texture_format(format);

    if (!texture || !format_name || !access) {
        return true;
    }

    return append_diagnostic_text(
        message,
        "%s--storage-texture-slot class=%s,slot=%u,texture=%s,format=%s,access=%s%s",
        prefix,
        storage_policy_class_name(evidence->resource_class),
        evidence->slot,
        texture,
        format_name,
        access,
        format_authority ? ",format_authority=explicit" : "");
}

static bool append_storage_texture_rejection_context(
    char **message,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence)
{
    SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;
    const char *name;
    const char *reflected_format;
    const char *reflected_access;
    const char *suggested_access;
    bool has_actionable_policy;

    if (!SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(evidence, &decision)) {
        return false;
    }

    if (decision.accepted &&
        evidence->has_sdl_slot &&
        !evidence->has_explicit_slot_policy) {
        return true;
    }

    name = (evidence->name && evidence->name[0]) ? evidence->name : "<unnamed>";
    reflected_format = wgsl_storage_texture_internal_format(evidence->format);
    reflected_access = resource_layout_storage_texture_access_to_string(evidence->final_access);
    suggested_access = storage_texture_internal_access_policy_token(decision.effective_access);
    has_actionable_policy =
        evidence->has_sdl_slot &&
        evidence->resource_class != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE &&
        decision.has_reflected_texture_type &&
        decision.has_reflected_format &&
        suggested_access != NULL;

    if (!append_diagnostic_text(
            message,
            "; storage texture '%s' stage=%s source_set=%s%u source_binding=%s%u class=%s slot=%s%u reflected_dimension=%s reflected_format=%s reflected_access=%s observed_access=%s final_access=%s format_authority=%s access_authority=%s unsupported_boundary=%s",
            name,
            storage_texture_evidence_stage_name(evidence),
            evidence->has_source_binding ? "" : "?",
            evidence->has_source_binding ? evidence->source_set : 0,
            evidence->has_source_binding ? "" : "?",
            evidence->has_source_binding ? evidence->source_binding : 0,
            storage_policy_class_name(evidence->resource_class),
            evidence->has_sdl_slot ? "" : "?",
            evidence->has_sdl_slot ? evidence->slot : 0,
            storage_texture_internal_dimension_to_string(evidence->texture_dimension),
            reflected_format ? reflected_format : "unknown",
            reflected_access ? reflected_access : "unknown",
            resource_layout_storage_texture_access_to_string(evidence->observed_access) ? resource_layout_storage_texture_access_to_string(evidence->observed_access) : "unknown",
            resource_layout_storage_texture_access_to_string(decision.effective_access) ? resource_layout_storage_texture_access_to_string(decision.effective_access) : "unknown",
            storage_texture_decision_authority_to_string(decision.format_authority),
            storage_texture_decision_authority_to_string(decision.access_authority),
            storage_texture_unsupported_boundary(evidence, &decision))) {
        return false;
    }

    if (evidence->has_explicit_slot_policy) {
        const char *policy_access = storage_texture_slot_access_policy_token(evidence->explicit_slot_policy.access);
        if (!append_storage_texture_policy_text(message, " requested_policy=", evidence, evidence->explicit_slot_policy.texture_type, evidence->explicit_slot_policy.format, policy_access, evidence->explicit_slot_policy_format_authority)) {
            return false;
        }
    } else {
        if (!append_diagnostic_text(message, " requested_policy=none")) {
            return false;
        }
    }

    if (has_actionable_policy) {
        if (!append_storage_texture_policy_text(
                message,
                " suggested_policy=",
                evidence,
                decision.reflected_texture_type,
                decision.reflected_format,
                suggested_access,
                false)) {
            return false;
        }
    } else if (!append_diagnostic_text(message, " suggested_policy=unavailable")) {
        return false;
    }

    return true;
}

static bool append_storage_texture_rejection_contexts(
    char **message,
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input)
{
    for (Uint32 i = 0; i < input->num_storage_texture_entries; i += 1) {
        if (!append_storage_texture_rejection_context(message, &input->storage_textures[i])) {
            return false;
        }
    }
    return true;
}

static bool set_layout_facts_rejection_error(
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    Uint64 reasons)
{
    char *message = NULL;
    bool wrote_reason = false;
    bool result;

    if (!append_diagnostic_text(&message, "%s", "resource layout facts are outside the SDL_GPU support matrix")) {
        return false;
    }
    for (Uint32 bit = 0; bit < 64; bit += 1) {
        Uint64 reason = 1ull << bit;
        const char *reason_text;

        if ((reasons & reason) == 0) {
            continue;
        }

        reason_text = layout_facts_reason_to_string(reason);
        if (!reason_text) {
            continue;
        }

        if (!append_diagnostic_text(&message, "%s%s", wrote_reason ? "; " : ": ", reason_text)) {
            SDL_free(message);
            return false;
        }
        wrote_reason = true;
    }

    if (input != NULL &&
        (reasons & (SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_UNMAPPED |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_OUT_OF_RANGE |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_DUPLICATE |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_ACCESS_AMBIGUOUS |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED |
                    SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH)) != 0) {
        if (!append_storage_texture_rejection_contexts(&message, input)) {
            SDL_free(message);
            return false;
        }
    }

    result = SDL_SetError("%s", message);
    SDL_free(message);
    return result;
}

static bool layout_facts_reasons_include_policy_mismatch(Uint64 reasons)
{
    return (reasons & (SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH |
                       SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH)) != 0;
}

static const char *sampled_texture_dimension_policy_token(SDL_ShaderCross_INTERNAL_TextureDimension dimension)
{
    switch (dimension) {
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D:
        return "2d";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY:
        return "2d_array";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D:
        return "3d";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE:
        return "cube";
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY:
        return "cube_array";
    default:
        return NULL;
    }
}

static const char *sampled_texture_type_policy_token(SDL_GPUTextureType texture_type)
{
    switch (texture_type) {
    case SDL_GPU_TEXTURETYPE_2D:
        return "2d";
    case SDL_GPU_TEXTURETYPE_2D_ARRAY:
        return "2d_array";
    case SDL_GPU_TEXTURETYPE_3D:
        return "3d";
    case SDL_GPU_TEXTURETYPE_CUBE:
        return "cube";
    case SDL_GPU_TEXTURETYPE_CUBE_ARRAY:
        return "cube_array";
    default:
        return NULL;
    }
}

static bool sampled_texture_dimension_to_texture_type(
    SDL_ShaderCross_INTERNAL_TextureDimension dimension,
    SDL_GPUTextureType *texture_type)
{
    switch (dimension) {
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D:
        *texture_type = SDL_GPU_TEXTURETYPE_2D;
        return true;
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY:
        *texture_type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        return true;
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D:
        *texture_type = SDL_GPU_TEXTURETYPE_3D;
        return true;
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE:
        *texture_type = SDL_GPU_TEXTURETYPE_CUBE;
        return true;
    case SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY:
        *texture_type = SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
        return true;
    default:
        return false;
    }
}

static const char *sampled_texture_sample_type_policy_token(SDL_GPUShaderTextureSampleType sample_type)
{
    switch (sample_type) {
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT:
        return "filterable_float";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT:
        return "unfilterable_float";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH:
        return "depth";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT:
        return "sint";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT:
        return "uint";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT:
        return "multisampled_unfilterable_float";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH:
        return "multisampled_depth";
    default:
        return NULL;
    }
}

static const char *sampled_texture_sampler_type_policy_token(SDL_GPUShaderSamplerType sampler_type)
{
    switch (sampler_type) {
    case SDL_GPU_SHADERSAMPLERTYPE_FILTERING:
        return "filtering";
    case SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING:
        return "non_filtering";
    case SDL_GPU_SHADERSAMPLERTYPE_COMPARISON:
        return "comparison";
    case SDL_GPU_SHADERSAMPLERTYPE_NONE:
        return "none";
    default:
        return NULL;
    }
}

static bool sampled_texture_policy_candidate_is_accepted(
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence,
    SDL_GPUTextureType texture_type,
    SDL_GPUShaderTextureSampleType sample_type,
    SDL_GPUShaderSamplerType sampler_type)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence candidate = *evidence;
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots;
    Uint32 num_sampled_texture_slots;
    bool accepted = false;

    if (!evidence->has_sdl_slot || evidence->slot == SDL_MAX_UINT32) {
        return false;
    }

    num_sampled_texture_slots = evidence->slot + 1;
    sampled_texture_slots = (SDL_GPUSampledTextureSlotDescription *)SDL_calloc(num_sampled_texture_slots, sizeof(*sampled_texture_slots));
    if (sampled_texture_slots == NULL) {
        SDL_OutOfMemory();
        return false;
    }

    candidate.has_explicit_slot_policy = true;
    candidate.explicit_slot_policy.texture_type = texture_type;
    candidate.explicit_slot_policy.sample_type = sample_type;
    candidate.explicit_slot_policy.sampler_type = sampler_type;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = num_sampled_texture_slots;
    input.sampled_textures = &candidate;
    input.num_sampled_textures = 1;

    SDL_zero(output);
    output.sampled_texture_slots = sampled_texture_slots;
    output.max_sampled_texture_slots = num_sampled_texture_slots;

    if (SDL_ShaderCross_INTERNAL_MapResourceLayoutToLayoutFacts(&input, &output)) {
        accepted = output.accepted;
    }

    SDL_free(sampled_texture_slots);
    return accepted;
}

static const char *sampled_texture_sample_kind_to_string(SDL_ShaderCross_INTERNAL_SampledTextureSampleKind sample_kind)
{
    switch (sample_kind) {
    case SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT:
        return "float";
    case SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT:
        return "sint";
    case SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT:
        return "uint";
    case SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH:
        return "depth";
    default:
        return "unknown";
    }
}

static const char *sampled_texture_sampler_binding_to_string(SDL_ShaderCross_INTERNAL_SamplerBindingKind sampler_binding)
{
    switch (sampler_binding) {
    case SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING:
        return "filtering";
    case SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING:
        return "non_filtering";
    case SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON:
        return "comparison";
    case SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE:
        return "none";
    default:
        return "unknown";
    }
}

static bool write_sampled_texture_policy_line(
    SDL_IOStream *outputIO,
    Uint32 slot,
    const char *texture,
    const char *sample,
    const char *sampler)
{
    if (!texture || !sample || !sampler) {
        return false;
    }
    SDL_IOprintf(
        outputIO,
        "--resource-layout-sampled-slot slot=%u,texture=%s,sample=%s,sampler=%s\n",
        slot,
        texture,
        sample,
        sampler);
    return true;
}

static bool write_sampled_texture_policy_line_from_slot(
    SDL_IOStream *outputIO,
    Uint32 slot,
    const SDL_GPUSampledTextureSlotDescription *description)
{
    return write_sampled_texture_policy_line(
        outputIO,
        slot,
        sampled_texture_type_policy_token(description->texture_type),
        sampled_texture_sample_type_policy_token(description->sample_type),
        sampled_texture_sampler_type_policy_token(description->sampler_type));
}

static bool write_sampled_texture_policy_candidate(
    SDL_IOStream *outputIO,
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence,
    SDL_GPUTextureType texture_type,
    SDL_GPUShaderTextureSampleType sample_type,
    SDL_GPUShaderSamplerType sampler_type)
{
    if (!sampled_texture_policy_candidate_is_accepted(evidence, texture_type, sample_type, sampler_type)) {
        return false;
    }

    return write_sampled_texture_policy_line(
        outputIO,
        evidence->slot,
        sampled_texture_type_policy_token(texture_type),
        sampled_texture_sample_type_policy_token(sample_type),
        sampled_texture_sampler_type_policy_token(sampler_type));
}

static void write_sampled_texture_suggestion_context(
    SDL_IOStream *outputIO,
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence,
    SpvExecutionModel execution_model,
    const char *ambiguity)
{
    const char *name = (evidence->name && evidence->name[0]) ? evidence->name : "<unnamed>";

    SDL_IOprintf(
        outputIO,
        "# sampled texture '%s' stage=%s source_set=%s%u source_binding=%s%u slot=%s%u inferred_texture=%s inferred_sample=%s inferred_sampler=%s ambiguity=%s\n",
        name,
        execution_model_to_string(execution_model),
        evidence->has_source_binding ? "" : "?",
        evidence->has_source_binding ? evidence->source_set : 0,
        evidence->has_source_binding ? "" : "?",
        evidence->has_source_binding ? evidence->source_binding : 0,
        evidence->has_sdl_slot ? "" : "?",
        evidence->has_sdl_slot ? evidence->slot : 0,
        sampled_texture_dimension_policy_token(evidence->texture_dimension) ? sampled_texture_dimension_policy_token(evidence->texture_dimension) : "unknown",
        sampled_texture_sample_kind_to_string(evidence->sample_kind),
        sampled_texture_sampler_binding_to_string(evidence->sampler_binding),
        ambiguity);
}

static bool write_storage_texture_policy_line(
    SDL_IOStream *outputIO,
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class,
    Uint32 slot,
    SDL_GPUTextureType texture_type,
    SDL_GPUTextureFormat format,
    const char *access)
{
    const char *texture = wgsl_storage_texture_type(texture_type);
    const char *format_name = wgsl_storage_texture_format(format);

    if (!texture || !format_name || !access) {
        return false;
    }
    SDL_IOprintf(
        outputIO,
        "--storage-texture-slot class=%s,slot=%u,texture=%s,format=%s,access=%s\n",
        storage_policy_class_name(resource_class),
        slot,
        texture,
        format_name,
        access);
    return true;
}

static bool storage_texture_policy_line_is_printable(
    SDL_GPUTextureType texture_type,
    SDL_GPUTextureFormat format,
    const char *access)
{
    return wgsl_storage_texture_type(texture_type) != NULL &&
           wgsl_storage_texture_format(format) != NULL &&
           access != NULL;
}

static void write_storage_texture_suggestion_context(
    SDL_IOStream *outputIO,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    const char *ambiguity)
{
    const char *name = (evidence->name && evidence->name[0]) ? evidence->name : "<unnamed>";
    const char *reflected_format = wgsl_storage_texture_internal_format(evidence->format);
    const char *reflected_access = resource_layout_storage_texture_access_to_string(evidence->final_access);
    const char *observed_access = resource_layout_storage_texture_access_to_string(evidence->observed_access);

    SDL_IOprintf(
        outputIO,
        "# storage texture '%s' stage=%s source_set=%s%u source_binding=%s%u class=%s slot=%s%u reflected_dimension=%s reflected_format=%s reflected_access=%s observed_access=%s ambiguity=%s\n",
        name,
        storage_texture_evidence_stage_name(evidence),
        evidence->has_source_binding ? "" : "?",
        evidence->has_source_binding ? evidence->source_set : 0,
        evidence->has_source_binding ? "" : "?",
        evidence->has_source_binding ? evidence->source_binding : 0,
        storage_policy_class_name(evidence->resource_class),
        evidence->has_sdl_slot ? "" : "?",
        evidence->has_sdl_slot ? evidence->slot : 0,
        storage_texture_internal_dimension_to_string(evidence->texture_dimension),
        reflected_format ? reflected_format : "unknown",
        reflected_access ? reflected_access : "unknown",
        observed_access ? observed_access : "unknown",
        ambiguity);
}

static bool storage_texture_policy_candidate_is_accepted(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_GPUTextureType texture_type,
    SDL_GPUTextureFormat format,
    SDL_GPUStorageTextureAccess access)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence candidate = *evidence;
    SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;

    candidate.has_explicit_slot_policy = true;
    candidate.explicit_slot_policy_format_authority = false;
    candidate.explicit_slot_policy.texture_type = texture_type;
    candidate.explicit_slot_policy.format = format;
    candidate.explicit_slot_policy.access = access;

    return SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&candidate, &decision) &&
           decision.accepted;
}

static Uint32 write_sampled_texture_policy_suggestions(
    SDL_IOStream *outputIO,
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output,
    SpvExecutionModel execution_model)
{
    Uint32 num_suggestions = 0;

    for (Uint32 i = 0; i < input->num_sampled_textures; i += 1) {
        const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence = &input->sampled_textures[i];
        SDL_GPUTextureType texture_type;

        if (!evidence->has_sdl_slot ||
            evidence->has_explicit_slot_policy ||
            !sampled_texture_dimension_to_texture_type(evidence->texture_dimension, &texture_type)) {
            continue;
        }

        if (evidence->multisampled &&
            evidence->texture_dimension == SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D &&
            evidence->image_depth == SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN &&
            evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE) {
            const bool can_print_color =
                sampled_texture_policy_candidate_is_accepted(
                    evidence,
                    texture_type,
                    SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT,
                    SDL_GPU_SHADERSAMPLERTYPE_NONE);
            const bool can_print_depth =
                sampled_texture_policy_candidate_is_accepted(
                    evidence,
                    texture_type,
                    SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH,
                    SDL_GPU_SHADERSAMPLERTYPE_NONE);

            if (can_print_color || can_print_depth) {
                write_sampled_texture_suggestion_context(outputIO, evidence, execution_model, "msaa_depth_or_color");
            }
            if (can_print_color &&
                write_sampled_texture_policy_candidate(
                    outputIO,
                    evidence,
                    texture_type,
                    SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT,
                    SDL_GPU_SHADERSAMPLERTYPE_NONE)) {
                num_suggestions += 1;
            }
            if (can_print_depth &&
                write_sampled_texture_policy_candidate(
                    outputIO,
                    evidence,
                    texture_type,
                    SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH,
                    SDL_GPU_SHADERSAMPLERTYPE_NONE)) {
                num_suggestions += 1;
            }
            continue;
        }

        if (!evidence->multisampled &&
            evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT &&
            evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT &&
            evidence->sampler_binding != SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE) {
            SDL_GPUSampledTextureSlotDescription preferred;
            bool can_print_preferred = false;
            bool can_print_unfilterable = false;

            if (output->sampled_texture_slots != NULL &&
                evidence->slot < output->num_sampled_texture_slots &&
                sampled_texture_policy_candidate_is_accepted(
                    evidence,
                    output->sampled_texture_slots[evidence->slot].texture_type,
                    output->sampled_texture_slots[evidence->slot].sample_type,
                    output->sampled_texture_slots[evidence->slot].sampler_type)) {
                preferred = output->sampled_texture_slots[evidence->slot];
                can_print_preferred = true;
            } else {
                preferred.texture_type = texture_type;
                preferred.sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT;
                preferred.sampler_type = SDL_GPU_SHADERSAMPLERTYPE_FILTERING;
                can_print_preferred = sampled_texture_policy_candidate_is_accepted(
                    evidence,
                    preferred.texture_type,
                    preferred.sample_type,
                    preferred.sampler_type);
            }
            can_print_unfilterable =
                evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN &&
                sampled_texture_policy_candidate_is_accepted(
                    evidence,
                    texture_type,
                    SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT,
                    SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING);

            if (can_print_preferred || can_print_unfilterable) {
                write_sampled_texture_suggestion_context(outputIO, evidence, execution_model, "float_filtering_or_non_filtering");
            }
            if (can_print_preferred &&
                write_sampled_texture_policy_line_from_slot(outputIO, evidence->slot, &preferred)) {
                num_suggestions += 1;
            }
            if (can_print_unfilterable &&
                write_sampled_texture_policy_candidate(
                    outputIO,
                    evidence,
                    texture_type,
                    SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT,
                    SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING)) {
                num_suggestions += 1;
            }
        }
    }

    return num_suggestions;
}

static Uint32 write_storage_texture_policy_suggestions(
    SDL_IOStream *outputIO,
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input)
{
    Uint32 num_suggestions = 0;

    for (Uint32 i = 0; i < input->num_storage_texture_entries; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence = &input->storage_textures[i];
        SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;
        SDL_ShaderCross_INTERNAL_StorageTextureAccess suggested_access;
        SDL_GPUStorageTextureAccess slot_access;
        const char *access;

        if (evidence->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE ||
            !evidence->has_sdl_slot ||
            evidence->has_explicit_slot_policy) {
            continue;
        }
        if (!SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(evidence, &decision)) {
            continue;
        }
        if (decision.accepted) {
            if (decision.access_authority != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_OBSERVED_WRITE) {
                continue;
            }
            if (!storage_texture_policy_candidate_is_accepted(
                    evidence,
                    decision.slot.texture_type,
                    decision.slot.format,
                    decision.slot.access)) {
                continue;
            }
            access = storage_texture_slot_access_policy_token(decision.slot.access);
            if (!storage_texture_policy_line_is_printable(decision.slot.texture_type, decision.slot.format, access)) {
                continue;
            }
            write_storage_texture_suggestion_context(outputIO, evidence, "observed_write_access");
            if (write_storage_texture_policy_line(
                    outputIO,
                    evidence->resource_class,
                    evidence->slot,
                    decision.slot.texture_type,
                    decision.slot.format,
                    access)) {
                num_suggestions += 1;
            }
            continue;
        }

        suggested_access = decision.effective_access;
        if (suggested_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN) {
            suggested_access = evidence->observed_access;
        }
        access = storage_texture_internal_access_policy_token(suggested_access);
        if (!decision.has_reflected_texture_type ||
            !decision.has_reflected_format ||
            !storage_texture_internal_access_to_slot_access(suggested_access, &slot_access) ||
            !storage_texture_policy_candidate_is_accepted(evidence, decision.reflected_texture_type, decision.reflected_format, slot_access) ||
            !storage_texture_policy_line_is_printable(decision.reflected_texture_type, decision.reflected_format, access)) {
            continue;
        }

        write_storage_texture_suggestion_context(outputIO, evidence, "explicit_policy_required");
        if (write_storage_texture_policy_line(
                outputIO,
                evidence->resource_class,
                evidence->slot,
                decision.reflected_texture_type,
                decision.reflected_format,
                access)) {
            num_suggestions += 1;
        }
    }

    return num_suggestions;
}

static bool write_resource_layout_policy_suggestions_from_resources(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies,
    Uint32 num_sampled_slot_policies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    bool allow_observed_storage_access)
{
    ShaderCross_CLIResourceLayoutEvidence evidence;
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots = NULL;
    SDL_GPUStorageTextureSlotDescription *storage_textures = NULL;
    SDL_GPUStorageTextureSlotDescription *readonly_storage_textures = NULL;
    SDL_GPUStorageTextureSlotDescription *readwrite_storage_textures = NULL;
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence *storage_suggestion_textures = NULL;
    Uint32 num_storage_suggestion_textures = 0;
    ShaderCross_CLIResourceLayoutEvidenceOptions evidence_options;
    Uint32 num_suggestions = 0;
    bool succeeded = false;

    SDL_zero(evidence);
    SDL_zero(input);
    SDL_zero(output);
    SDL_zero(evidence_options);

    if (!resource_layout_kind_from_execution_model(slot_context->execution_model, &input.kind)) {
        return SDL_SetError("SPIR-V execution model is not supported for SDL_GPU resource layout policy suggestions");
    }

    fill_layout_facts_input_counts(slot_context, &input);
    if (slot_context->execution_model == SpvExecutionModelFragment) {
        if (!validate_resource_layout_buffer_slots_for_type(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER)) {
            goto done;
        }
    } else if (!validate_resource_layout_buffer_slots(slot_context, compiler, resources)) {
        goto done;
    }
    if (!fill_resource_layout_counts(slot_context, input.kind, &layout)) {
        SDL_SetError("SPIR-V execution model is not supported for SDL_GPU resource layout policy suggestions");
        goto done;
    }
    evidence_options.allow_observed_storage_access = allow_observed_storage_access;
    evidence_options.classify_fragment_writable_storage = true;
    evidence_options.collect_sampled_textures = true;
    if (!collect_resource_layout_evidence(
            &evidence,
            compiler,
            resources,
            slot_context,
            dref_usage,
            &evidence_options)) {
        goto done;
    }
    if (!apply_sampled_slot_policies(
            &evidence,
            input.num_sampled_texture_slots,
            sampled_slot_policies,
            num_sampled_slot_policies)) {
        goto done;
    }
    if (!apply_storage_texture_slot_policies(
            &evidence,
            &input,
            storage_texture_slot_policies,
            num_storage_texture_slot_policies)) {
        goto done;
    }
    if (evidence.num_storage_textures > 0) {
        storage_suggestion_textures = (SDL_ShaderCross_INTERNAL_StorageTextureEvidence *)SDL_calloc(evidence.num_storage_textures, sizeof(*storage_suggestion_textures));
        if (!storage_suggestion_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        SDL_memcpy(storage_suggestion_textures, evidence.storage_textures, sizeof(*storage_suggestion_textures) * evidence.num_storage_textures);
        num_storage_suggestion_textures = evidence.num_storage_textures;
    }
    if (!validate_fragment_storage_resource_layout(
            &evidence,
            compiler,
            resources,
            slot_context,
            dref_usage,
            &layout,
            false)) {
        goto done;
    }
    remove_fragment_writable_storage_texture_evidence(&evidence);

    input.sampled_textures = evidence.sampled_textures;
    input.num_sampled_textures = evidence.num_sampled_textures;
    input.storage_textures = evidence.storage_textures;
    input.num_storage_texture_entries = evidence.num_storage_textures;
    input.has_samplerless_sampled_image = evidence.has_samplerless_sampled_image;
    if (slot_context->execution_model == SpvExecutionModelFragment) {
        input.num_storage_textures = layout.num_storage_textures;
    }

    if (input.num_sampled_texture_slots > 0) {
        sampled_texture_slots = (SDL_GPUSampledTextureSlotDescription *)SDL_calloc(input.num_sampled_texture_slots, sizeof(*sampled_texture_slots));
        if (!sampled_texture_slots) {
            SDL_OutOfMemory();
            goto done;
        }
        output.sampled_texture_slots = sampled_texture_slots;
        output.max_sampled_texture_slots = input.num_sampled_texture_slots;
    }
    if (input.num_storage_textures > 0) {
        storage_textures = (SDL_GPUStorageTextureSlotDescription *)SDL_calloc(input.num_storage_textures, sizeof(*storage_textures));
        if (!storage_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        output.storage_textures = storage_textures;
        output.max_storage_textures = input.num_storage_textures;
    }
    if (input.num_readonly_storage_textures > 0) {
        readonly_storage_textures = (SDL_GPUStorageTextureSlotDescription *)SDL_calloc(input.num_readonly_storage_textures, sizeof(*readonly_storage_textures));
        if (!readonly_storage_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        output.readonly_storage_textures = readonly_storage_textures;
        output.max_readonly_storage_textures = input.num_readonly_storage_textures;
    }
    if (input.num_readwrite_storage_textures > 0) {
        readwrite_storage_textures = (SDL_GPUStorageTextureSlotDescription *)SDL_calloc(input.num_readwrite_storage_textures, sizeof(*readwrite_storage_textures));
        if (!readwrite_storage_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        output.readwrite_storage_textures = readwrite_storage_textures;
        output.max_readwrite_storage_textures = input.num_readwrite_storage_textures;
    }

    if (!SDL_ShaderCross_INTERNAL_MapResourceLayoutToLayoutFacts(&input, &output)) {
        goto done;
    }
    if (!output.accepted && layout_facts_reasons_include_policy_mismatch(output.reasons)) {
        set_layout_facts_rejection_error(&input, output.reasons);
        goto done;
    }

    SDL_IOprintf(outputIO, "# SDL_shadercross resource layout policy suggestions\n");
    SDL_IOprintf(outputIO, "# Copy a policy line into the shadercross command when it matches the intended resource shape.\n");
    num_suggestions += write_sampled_texture_policy_suggestions(outputIO, &input, &output, slot_context->execution_model);
    if (storage_suggestion_textures != NULL) {
        SDL_ShaderCross_INTERNAL_LayoutFactsInput storage_suggestion_input = input;
        storage_suggestion_input.storage_textures = storage_suggestion_textures;
        storage_suggestion_input.num_storage_texture_entries = num_storage_suggestion_textures;
        num_suggestions += write_storage_texture_policy_suggestions(outputIO, &storage_suggestion_input);
    } else {
        num_suggestions += write_storage_texture_policy_suggestions(outputIO, &input);
    }
    if (num_suggestions == 0) {
        SDL_IOprintf(outputIO, "# No resource layout policy suggestions.\n");
    }

    succeeded = true;

done:
    free_resource_layout_evidence(&evidence);
    SDL_free(sampled_texture_slots);
    SDL_free(storage_textures);
    SDL_free(readonly_storage_textures);
    SDL_free(readwrite_storage_textures);
    SDL_free(storage_suggestion_textures);
    return succeeded;
}

static const char *layout_input_basename(const char *path)
{
    const char *base = path;

    if (!path) {
        return "";
    }
    for (const char *ch = path; *ch != '\0'; ch += 1) {
        if (*ch == '/' || *ch == '\\') {
            base = ch + 1;
        }
    }
    return base;
}

static void write_resource_layout_policy_notes(
    SDL_IOStream *outputIO,
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output)
{
    if ((output->notes & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_FLOAT_SAMPLER_POLICY) != 0) {
        for (Uint32 i = 0; i < input->num_sampled_textures; i += 1) {
            const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence = &input->sampled_textures[i];
            bool resolved_to_filtering = true;
            if (evidence->has_sdl_slot &&
                evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT &&
                evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT) {
                if (output->sampled_texture_slots != NULL && evidence->slot < output->num_sampled_texture_slots) {
                    const SDL_GPUSampledTextureSlotDescription *slot = &output->sampled_texture_slots[evidence->slot];
                    resolved_to_filtering =
                        slot->sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT &&
                        slot->sampler_type == SDL_GPU_SHADERSAMPLERTYPE_FILTERING;
                }
                if (!resolved_to_filtering) {
                    continue;
                }
                SDL_IOprintf(
                    outputIO,
                    "// note: sampled texture slot %u resolved ambiguous float sampler policy to SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT with SDL_GPU_SHADERSAMPLERTYPE_FILTERING.\n",
                    evidence->slot);
                SDL_Log("resource layout note: sampled texture slot %u resolved ambiguous float sampler policy to filterable-float with filtering sampler", evidence->slot);
            }
        }
    }

    if ((output->notes & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY) != 0) {
        for (Uint32 i = 0; i < input->num_sampled_textures; i += 1) {
            const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence = &input->sampled_textures[i];
            if (!evidence->has_sdl_slot || !evidence->has_explicit_slot_policy) {
                continue;
            }
            SDL_IOprintf(
                outputIO,
                "// note: sampled texture slot %u uses explicit sampled-slot policy.\n",
                evidence->slot);
            SDL_Log("resource layout note: sampled texture slot %u uses explicit sampled-slot policy", evidence->slot);
        }
    }

    if ((output->notes & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_STORAGE_TEXTURE_SLOT_POLICY) != 0) {
        for (Uint32 i = 0; i < input->num_storage_texture_entries; i += 1) {
            const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence = &input->storage_textures[i];
            if (!evidence->has_sdl_slot || !evidence->has_explicit_slot_policy) {
                continue;
            }
            if (evidence->explicit_slot_policy_format_authority) {
                const char *reflected_format = wgsl_storage_texture_internal_format(evidence->format);
                const char *policy_format = wgsl_storage_texture_format(evidence->explicit_slot_policy.format);
                if (reflected_format && policy_format && SDL_strcmp(reflected_format, policy_format) == 0) {
                    SDL_IOprintf(
                        outputIO,
                        "// note: storage texture class %s slot %u uses explicit --storage-texture-slot policy with format_authority=explicit; final format %s matches the policy.\n",
                        storage_policy_class_name(evidence->resource_class),
                        evidence->slot,
                        policy_format);
                    SDL_Log(
                        "resource layout note: storage texture class %s slot %u uses explicit --storage-texture-slot policy with format_authority=explicit; final format %s matches the policy",
                        storage_policy_class_name(evidence->resource_class),
                        evidence->slot,
                        policy_format);
                } else {
                    SDL_IOprintf(
                        outputIO,
                        "// note: storage texture class %s slot %u uses explicit --storage-texture-slot policy with format_authority=explicit (%s -> %s).\n",
                        storage_policy_class_name(evidence->resource_class),
                        evidence->slot,
                        reflected_format ? reflected_format : "unknown",
                        policy_format ? policy_format : "unknown");
                    SDL_Log(
                        "resource layout note: storage texture class %s slot %u uses explicit --storage-texture-slot policy with format_authority=explicit (%s -> %s)",
                        storage_policy_class_name(evidence->resource_class),
                        evidence->slot,
                        reflected_format ? reflected_format : "unknown",
                        policy_format ? policy_format : "unknown");
                }
            } else {
                SDL_IOprintf(
                    outputIO,
                    "// note: storage texture class %s slot %u uses explicit --storage-texture-slot policy.\n",
                    storage_policy_class_name(evidence->resource_class),
                    evidence->slot);
                SDL_Log(
                    "resource layout note: storage texture class %s slot %u uses explicit --storage-texture-slot policy",
                    storage_policy_class_name(evidence->resource_class),
                    evidence->slot);
            }
        }
    }
}

static bool write_resource_layout_c_from_resources(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    const char *symbol_prefix,
    const char *input_filename,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies,
    Uint32 num_sampled_slot_policies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    bool allow_observed_storage_access)
{
    ShaderCross_CLIResourceLayoutEvidence evidence;
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots = NULL;
    SDL_GPUStorageTextureSlotDescription *storage_textures = NULL;
    SDL_GPUStorageTextureSlotDescription *readonly_storage_textures = NULL;
    SDL_GPUStorageTextureSlotDescription *readwrite_storage_textures = NULL;
    ShaderCross_CLIResourceLayoutEvidenceOptions evidence_options;
    bool succeeded = false;

    SDL_zero(evidence);
    SDL_zero(input);
    SDL_zero(output);
    SDL_zero(evidence_options);

    if (!resource_layout_kind_from_execution_model(slot_context->execution_model, &input.kind)) {
        return SDL_SetError("SPIR-V execution model is not supported for SDL_GPU resource layout C output");
    }

    fill_layout_facts_input_counts(slot_context, &input);
    if (slot_context->execution_model == SpvExecutionModelFragment) {
        if (!validate_resource_layout_buffer_slots_for_type(slot_context, compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER)) {
            goto done;
        }
    } else if (!validate_resource_layout_buffer_slots(slot_context, compiler, resources)) {
        goto done;
    }
    if (!fill_resource_layout_counts(slot_context, input.kind, &layout)) {
        SDL_SetError("SPIR-V execution model is not supported for SDL_GPU resource layout C output");
        goto done;
    }
    evidence_options.allow_observed_storage_access = allow_observed_storage_access;
    evidence_options.classify_fragment_writable_storage = true;
    evidence_options.collect_sampled_textures = true;
    if (!collect_resource_layout_evidence(
            &evidence,
            compiler,
            resources,
            slot_context,
            dref_usage,
            &evidence_options)) {
        goto done;
    }
    if (!apply_sampled_slot_policies(
            &evidence,
            input.num_sampled_texture_slots,
            sampled_slot_policies,
            num_sampled_slot_policies)) {
        goto done;
    }
    if (!apply_storage_texture_slot_policies(
            &evidence,
            &input,
            storage_texture_slot_policies,
            num_storage_texture_slot_policies)) {
        goto done;
    }
    if (!validate_fragment_storage_resource_layout(
            &evidence,
            compiler,
            resources,
            slot_context,
            dref_usage,
            &layout,
            true)) {
        goto done;
    }

    input.sampled_textures = evidence.sampled_textures;
    input.num_sampled_textures = evidence.num_sampled_textures;
    input.storage_textures = evidence.storage_textures;
    input.num_storage_texture_entries = evidence.num_storage_textures;
    input.has_samplerless_sampled_image = evidence.has_samplerless_sampled_image;
    if (slot_context->execution_model == SpvExecutionModelFragment) {
        input.num_storage_textures = layout.num_storage_textures;
    }

    if (input.num_sampled_texture_slots > 0) {
        sampled_texture_slots = (SDL_GPUSampledTextureSlotDescription *)SDL_calloc(input.num_sampled_texture_slots, sizeof(*sampled_texture_slots));
        if (!sampled_texture_slots) {
            SDL_OutOfMemory();
            goto done;
        }
        output.sampled_texture_slots = sampled_texture_slots;
        output.max_sampled_texture_slots = input.num_sampled_texture_slots;
    }
    if (input.num_storage_textures > 0) {
        storage_textures = (SDL_GPUStorageTextureSlotDescription *)SDL_calloc(input.num_storage_textures, sizeof(*storage_textures));
        if (!storage_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        output.storage_textures = storage_textures;
        output.max_storage_textures = input.num_storage_textures;
    }
    if (input.num_readonly_storage_textures > 0) {
        readonly_storage_textures = (SDL_GPUStorageTextureSlotDescription *)SDL_calloc(input.num_readonly_storage_textures, sizeof(*readonly_storage_textures));
        if (!readonly_storage_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        output.readonly_storage_textures = readonly_storage_textures;
        output.max_readonly_storage_textures = input.num_readonly_storage_textures;
    }
    if (input.num_readwrite_storage_textures > 0) {
        readwrite_storage_textures = (SDL_GPUStorageTextureSlotDescription *)SDL_calloc(input.num_readwrite_storage_textures, sizeof(*readwrite_storage_textures));
        if (!readwrite_storage_textures) {
            SDL_OutOfMemory();
            goto done;
        }
        output.readwrite_storage_textures = readwrite_storage_textures;
        output.max_readwrite_storage_textures = input.num_readwrite_storage_textures;
    }

    if (!SDL_ShaderCross_INTERNAL_MapResourceLayoutToLayoutFacts(&input, &output)) {
        goto done;
    }
    if (!output.accepted) {
        set_layout_facts_rejection_error(&input, output.reasons);
        goto done;
    }

    SDL_IOprintf(outputIO, "// Generated by SDL_shadercross %d.%d.%d from %s.\n",
        SDL_SHADERCROSS_MAJOR_VERSION,
        SDL_SHADERCROSS_MINOR_VERSION,
        SDL_SHADERCROSS_MICRO_VERSION,
        layout_input_basename(input_filename));
    SDL_IOprintf(outputIO, "// Regenerate after upgrading SDL or SDL_shadercross.\n");
    SDL_IOprintf(outputIO, "// Do not edit by hand.\n");
    write_resource_layout_policy_notes(outputIO, &input, &output);
    SDL_IOprintf(outputIO, "\n#include <SDL3/SDL_gpu.h>\n\n");

    if (!write_resource_layout_buffer_facts_c(outputIO, compiler, resources, slot_context, dref_usage, &layout, symbol_prefix)) {
        goto done;
    }

    if (!SDL_ShaderCross_INTERNAL_WriteResourceLayoutCInitializers(
            outputIO,
            symbol_prefix,
            &layout,
            &output)) {
        goto done;
    }

    succeeded = true;

done:
    free_resource_layout_evidence(&evidence);
    SDL_free(sampled_texture_slots);
    SDL_free(storage_textures);
    SDL_free(readonly_storage_textures);
    SDL_free(readwrite_storage_textures);
    return succeeded;
}

bool ShaderCross_CLI_WriteSPIRVResourceLayoutC(
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
    bool allow_observed_storage_access)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_set active_variables = NULL;
    spvc_resources resources = NULL;
    DrefUsageAnalysis dref_usage;
    SDLResourceLayoutContext slot_context;
    SpvExecutionModel execution_model = SpvExecutionModelMax;
    spvc_result result;
    bool succeeded = false;

    SDL_zero(dref_usage);
    SDL_zero(slot_context);

    if (!outputIO) {
        return SDL_InvalidParamError("outputIO");
    }

    result = spvc_context_create(&context);
    if (result < 0) {
        SDL_SetError("spvc_context_create failed: %d", (int)result);
        return false;
    }

    result = spvc_context_parse_spirv(context, (const SpvId *)bytecode, bytecode_size / sizeof(SpvId), &ir);
    if (result < 0) {
        SDL_SetError("spvc_context_parse_spirv failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);
    if (result < 0) {
        SDL_SetError("spvc_context_create_compiler failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!resource_layout_execution_model_for_entrypoint(compiler, entrypoint, shader_stage, &execution_model)) {
        goto done;
    }
    result = spvc_compiler_set_entry_point(compiler, entrypoint, execution_model);
    if (result < 0) {
        SDL_SetError("spvc_compiler_set_entry_point failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_get_active_interface_variables(compiler, &active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_get_active_interface_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_create_shader_resources_for_active_variables(compiler, &resources, active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_create_shader_resources_for_active_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!analyze_dref_usage(&dref_usage, bytecode, bytecode_size, entrypoint, execution_model)) {
        goto done;
    }
    if (!collect_sdl_resource_layout_context(&slot_context, compiler, resources)) {
        goto done;
    }
    if (!write_resource_layout_c_from_resources(
            outputIO,
            compiler,
            resources,
            &slot_context,
            &dref_usage,
            symbol_prefix,
            input_filename,
            sampled_slot_policies,
            num_sampled_slot_policies,
            storage_texture_slot_policies,
            num_storage_texture_slot_policies,
            allow_observed_storage_access)) {
        goto done;
    }

    succeeded = true;

done:
    free_dref_usage_analysis(&dref_usage);
    spvc_context_destroy(context);
    return succeeded;
}

bool ShaderCross_CLI_WriteSPIRVResourceLayoutPolicySuggestions(
    SDL_IOStream *outputIO,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy *sampled_slot_policies,
    Uint32 num_sampled_slot_policies,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    bool allow_observed_storage_access)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_set active_variables = NULL;
    spvc_resources resources = NULL;
    DrefUsageAnalysis dref_usage;
    SDLResourceLayoutContext slot_context;
    SpvExecutionModel execution_model = SpvExecutionModelMax;
    spvc_result result;
    bool succeeded = false;

    SDL_zero(dref_usage);
    SDL_zero(slot_context);

    if (!outputIO) {
        return SDL_InvalidParamError("outputIO");
    }

    result = spvc_context_create(&context);
    if (result < 0) {
        SDL_SetError("spvc_context_create failed: %d", (int)result);
        return false;
    }

    result = spvc_context_parse_spirv(context, (const SpvId *)bytecode, bytecode_size / sizeof(SpvId), &ir);
    if (result < 0) {
        SDL_SetError("spvc_context_parse_spirv failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);
    if (result < 0) {
        SDL_SetError("spvc_context_create_compiler failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!resource_layout_execution_model_for_entrypoint(compiler, entrypoint, shader_stage, &execution_model)) {
        goto done;
    }
    result = spvc_compiler_set_entry_point(compiler, entrypoint, execution_model);
    if (result < 0) {
        SDL_SetError("spvc_compiler_set_entry_point failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_get_active_interface_variables(compiler, &active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_get_active_interface_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_create_shader_resources_for_active_variables(compiler, &resources, active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_create_shader_resources_for_active_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!analyze_dref_usage(&dref_usage, bytecode, bytecode_size, entrypoint, execution_model)) {
        goto done;
    }
    if (!collect_sdl_resource_layout_context(&slot_context, compiler, resources)) {
        goto done;
    }
    if (!write_resource_layout_policy_suggestions_from_resources(
            outputIO,
            compiler,
            resources,
            &slot_context,
            &dref_usage,
            sampled_slot_policies,
            num_sampled_slot_policies,
            storage_texture_slot_policies,
            num_storage_texture_slot_policies,
            allow_observed_storage_access)) {
        goto done;
    }

    succeeded = true;

done:
    free_dref_usage_analysis(&dref_usage);
    spvc_context_destroy(context);
    return succeeded;
}

bool ShaderCross_CLI_UpdateStorageTextureDeclarationsForWGSL(
    char **wgsl_text,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_set active_variables = NULL;
    spvc_resources resources = NULL;
    DrefUsageAnalysis dref_usage;
    SDLResourceLayoutContext slot_context;
    ShaderCross_CLIResourceLayoutEvidence evidence;
    ShaderCross_CLIResourceLayoutEvidenceOptions evidence_options;
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SpvExecutionModel execution_model = SpvExecutionModelMax;
    spvc_result result;
    bool succeeded = false;

    SDL_zero(dref_usage);
    SDL_zero(slot_context);
    SDL_zero(evidence);
    SDL_zero(evidence_options);
    SDL_zero(input);

    if (wgsl_text == NULL || *wgsl_text == NULL) {
        return SDL_InvalidParamError("wgsl_text");
    }
    if (num_storage_texture_slot_policies > 0 && storage_texture_slot_policies == NULL) {
        return SDL_InvalidParamError("storage_texture_slot_policies");
    }

    result = spvc_context_create(&context);
    if (result < 0) {
        SDL_SetError("spvc_context_create failed: %d", (int)result);
        return false;
    }

    result = spvc_context_parse_spirv(context, (const SpvId *)bytecode, bytecode_size / sizeof(SpvId), &ir);
    if (result < 0) {
        SDL_SetError("spvc_context_parse_spirv failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);
    if (result < 0) {
        SDL_SetError("spvc_context_create_compiler failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!resource_layout_execution_model_for_entrypoint(compiler, entrypoint, shader_stage, &execution_model)) {
        goto done;
    }
    result = spvc_compiler_set_entry_point(compiler, entrypoint, execution_model);
    if (result < 0) {
        SDL_SetError("spvc_compiler_set_entry_point failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_get_active_interface_variables(compiler, &active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_get_active_interface_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_create_shader_resources_for_active_variables(compiler, &resources, active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_create_shader_resources_for_active_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!analyze_dref_usage(&dref_usage, bytecode, bytecode_size, entrypoint, execution_model)) {
        goto done;
    }
    if (!collect_sdl_resource_layout_context(&slot_context, compiler, resources)) {
        goto done;
    }
    if (!resource_layout_kind_from_execution_model(slot_context.execution_model, &input.kind)) {
        SDL_SetError("SPIR-V execution model is not supported for storage texture WGSL declaration updates");
        goto done;
    }
    fill_layout_facts_input_counts(&slot_context, &input);
    evidence_options.allow_observed_storage_access = true;
    evidence_options.classify_fragment_writable_storage = true;
    evidence_options.collect_sampled_textures = false;
    if (!collect_resource_layout_evidence(
            &evidence,
            compiler,
            resources,
            &slot_context,
            &dref_usage,
            &evidence_options)) {
        goto done;
    }
    if (!apply_storage_texture_slot_policies(
            &evidence,
            &input,
            storage_texture_slot_policies,
            num_storage_texture_slot_policies)) {
        goto done;
    }

    for (Uint32 i = 0; i < evidence.num_storage_textures; i += 1) {
        SDL_GPUStorageTextureSlotDescription slot;
        SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;
        if (!wgsl_storage_texture_declaration_needs_update(&evidence.storage_textures[i])) {
            continue;
        }
        if (!SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&evidence.storage_textures[i], &decision)) {
            goto done;
        }
        if (!decision.accepted) {
            char *message = NULL;
            if (!append_storage_texture_rejection_context(&message, &evidence.storage_textures[i])) {
                SDL_free(message);
                goto done;
            }
            SDL_SetError("storage texture slot layout facts are outside the current SDL_GPU support matrix%s", message ? message : "");
            SDL_free(message);
            goto done;
        }
        slot = decision.slot;
        if (!patch_wgsl_storage_texture_declaration(wgsl_text, &evidence.storage_textures[i], &slot)) {
            goto done;
        }
    }

    succeeded = true;

done:
    free_resource_layout_evidence(&evidence);
    free_dref_usage_analysis(&dref_usage);
    spvc_context_destroy(context);
    return succeeded;
}

typedef struct ShaderCross_CLISPIRVStorageTypePatch
{
    Uint32 image_type_id;
    Uint32 current_format;
    Uint32 target_format;
    Uint32 num_resources;
    Uint32 num_authoritative_resources;
    bool has_authority;
    bool needs_update;
    bool needs_extended_format_capability;
} ShaderCross_CLISPIRVStorageTypePatch;

static bool storage_texture_slot_spirv_format(SDL_GPUTextureFormat format, SpvImageFormat *spirv_format)
{
    /* Keep this table paired with spirv_storage_format_requires_extended_formats(). */
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        *spirv_format = SpvImageFormatRgba8;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
        *spirv_format = SpvImageFormatRgba8Snorm;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        *spirv_format = SpvImageFormatRgba16f;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
        *spirv_format = SpvImageFormatRg32f;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
        *spirv_format = SpvImageFormatRgba32f;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
        *spirv_format = SpvImageFormatRgba8ui;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
        *spirv_format = SpvImageFormatRgba16ui;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R32_UINT:
        *spirv_format = SpvImageFormatR32ui;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
        *spirv_format = SpvImageFormatRgba8i;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
        *spirv_format = SpvImageFormatRgba16i;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R32_INT:
        *spirv_format = SpvImageFormatR32i;
        return true;
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
        *spirv_format = SpvImageFormatR32f;
        return true;
    default:
        return false;
    }
}

static bool spirv_storage_format_requires_extended_formats(SpvImageFormat format)
{
    return format == SpvImageFormatRg32f;
}

static ShaderCross_CLISPIRVStorageTypePatch *find_spirv_storage_type_patch(
    ShaderCross_CLISPIRVStorageTypePatch *patches,
    Uint32 num_patches,
    Uint32 image_type_id)
{
    for (Uint32 i = 0; i < num_patches; i += 1) {
        if (patches[i].image_type_id == image_type_id) {
            return &patches[i];
        }
    }
    return NULL;
}

static bool append_spirv_storage_type_patch(
    ShaderCross_CLISPIRVStorageTypePatch **patches,
    Uint32 *num_patches,
    Uint32 image_type_id,
    Uint32 image_format,
    ShaderCross_CLISPIRVStorageTypePatch **out_patch)
{
    ShaderCross_CLISPIRVStorageTypePatch *entries = (ShaderCross_CLISPIRVStorageTypePatch *)SDL_realloc(
        *patches,
        sizeof(**patches) * ((size_t)*num_patches + 1));
    ShaderCross_CLISPIRVStorageTypePatch *patch;

    if (!entries) {
        return SDL_OutOfMemory();
    }

    *patches = entries;
    patch = &(*patches)[*num_patches];
    SDL_zero(*patch);
    patch->image_type_id = image_type_id;
    patch->current_format = image_format;
    patch->target_format = image_format;
    *num_patches += 1;
    *out_patch = patch;
    return true;
}

static bool collect_spirv_storage_format_patches(
    const ShaderCross_CLIResourceLayoutEvidence *evidence,
    ShaderCross_CLISPIRVStorageTypePatch **patches,
    Uint32 *num_patches)
{
    Uint32 write_count = 0;

    *patches = NULL;
    *num_patches = 0;

    for (Uint32 i = 0; i < evidence->num_storage_textures; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entry = &evidence->storage_textures[i];
        ShaderCross_CLISPIRVStorageTypePatch *patch = NULL;

        if (entry->image_type_id == 0) {
            return SDL_SetError("storage texture class '%s' slot %u has no SPIR-V image type id",
                storage_policy_class_name(entry->resource_class),
                entry->slot);
        }

        patch = find_spirv_storage_type_patch(*patches, *num_patches, entry->image_type_id);
        if (!patch) {
            if (!append_spirv_storage_type_patch(patches, num_patches, entry->image_type_id, entry->image_format, &patch)) {
                return false;
            }
        } else if (patch->current_format != entry->image_format) {
            return SDL_SetError("storage texture image type %u has inconsistent reflected SPIR-V formats", entry->image_type_id);
        }

        patch->num_resources += 1;

        if (entry->has_explicit_slot_policy && entry->explicit_slot_policy_format_authority) {
            SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;
            SDL_GPUStorageTextureSlotDescription slot;
            SpvImageFormat target_format;

            if (entry->uses_image_texel_pointer) {
                return SDL_SetError("SPIR-V storage texture format authority does not support image texel pointers or image atomics");
            }
            if (!SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(entry, &decision)) {
                return false;
            }
            if (!decision.accepted) {
                char *message = NULL;
                if (!append_storage_texture_rejection_context(&message, entry)) {
                    SDL_free(message);
                    return false;
                }
                SDL_SetError("storage texture slot layout facts are outside the current SDL_GPU support matrix%s", message ? message : "");
                SDL_free(message);
                return false;
            }
            slot = decision.slot;
            if (!storage_texture_slot_spirv_format(slot.format, &target_format)) {
                return SDL_SetError("storage texture class '%s' slot %u cannot be represented as a SPIR-V image format",
                    storage_policy_class_name(entry->resource_class),
                    entry->slot);
            }

            patch->num_authoritative_resources += 1;
            if (patch->has_authority && patch->target_format != (Uint32)target_format) {
                return SDL_SetError("SPIR-V storage texture format authority for image type %u requires type cloning for divergent formats", entry->image_type_id);
            }

            patch->has_authority = true;
            patch->target_format = (Uint32)target_format;
            patch->needs_update = patch->target_format != patch->current_format;
            patch->needs_extended_format_capability = spirv_storage_format_requires_extended_formats(target_format);
        }
    }

    for (Uint32 i = 0; i < *num_patches; i += 1) {
        ShaderCross_CLISPIRVStorageTypePatch *patch = &(*patches)[i];

        if (!patch->has_authority || !patch->needs_update) {
            continue;
        }
        if (patch->num_authoritative_resources != patch->num_resources) {
            return SDL_SetError("SPIR-V storage texture format authority requires every active storage texture sharing image type %u to provide the same explicit policy", patch->image_type_id);
        }
        (*patches)[write_count] = *patch;
        write_count += 1;
    }

    *num_patches = write_count;
    return true;
}

static bool append_spirv_storage_format_authority_note(
    char **provenance_note,
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entry)
{
    char *next_note = NULL;
    const char *reflected_format = wgsl_storage_texture_internal_format(entry->format);
    const char *policy_format = wgsl_storage_texture_format(entry->explicit_slot_policy.format);
    const char *class_name = storage_policy_class_name(entry->resource_class);

    if (provenance_note == NULL) {
        return true;
    }

    if (*provenance_note == NULL) {
        if (SDL_asprintf(
                provenance_note,
                "storage format authority: class %s slot %u %s -> %s",
                class_name,
                entry->slot,
                reflected_format ? reflected_format : "unknown",
                policy_format ? policy_format : "unknown") < 0) {
            return SDL_OutOfMemory();
        }
    } else {
        if (SDL_asprintf(
                &next_note,
                "%s; class %s slot %u %s -> %s",
                *provenance_note,
                class_name,
                entry->slot,
                reflected_format ? reflected_format : "unknown",
                policy_format ? policy_format : "unknown") < 0) {
            return SDL_OutOfMemory();
        }
        SDL_free(*provenance_note);
        *provenance_note = next_note;
    }

    SDL_Log(
        "resource layout note: storage texture class %s slot %u uses explicit --storage-texture-slot policy with format_authority=explicit (%s -> %s)",
        class_name,
        entry->slot,
        reflected_format ? reflected_format : "unknown",
        policy_format ? policy_format : "unknown");
    return true;
}

static bool collect_spirv_storage_format_authority_note(
    const ShaderCross_CLIResourceLayoutEvidence *evidence,
    char **provenance_note)
{
    if (provenance_note == NULL) {
        return true;
    }

    *provenance_note = NULL;
    for (Uint32 i = 0; i < evidence->num_storage_textures; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *entry = &evidence->storage_textures[i];

        if (!entry->has_explicit_slot_policy || !entry->explicit_slot_policy_format_authority) {
            continue;
        }
        if (!append_spirv_storage_format_authority_note(provenance_note, entry)) {
            return false;
        }
    }

    return true;
}

static bool spirv_bytecode_words(const Uint8 *bytecode, size_t bytecode_size, const Uint32 **words, size_t *word_count)
{
    if (bytecode == NULL) {
        return SDL_InvalidParamError("bytecode");
    }
    if (bytecode_size % sizeof(Uint32) != 0 || bytecode_size < sizeof(Uint32) * 5) {
        return SDL_SetError("invalid SPIR-V bytecode size");
    }

    *words = (const Uint32 *)bytecode;
    *word_count = bytecode_size / sizeof(Uint32);
    /* SPIR-V words are consumed in host order, matching analyze_dref_usage. */
    if ((*words)[0] != SpvMagicNumber) {
        return SDL_SetError("invalid SPIR-V magic number");
    }

    return true;
}

static bool spirv_has_capability(const Uint32 *words, size_t word_count, SpvCapability capability)
{
    for (size_t offset = 5; offset < word_count;) {
        Uint32 instruction = words[offset];
        Uint16 word_length = (Uint16)(instruction >> 16);
        SpvOp opcode = (SpvOp)(instruction & 0xffffu);

        if (word_length == 0 || offset + word_length > word_count) {
            return false;
        }
        if (opcode == SpvOpCapability && word_length >= 2 && words[offset + 1] == (Uint32)capability) {
            return true;
        }

        offset += word_length;
    }

    return false;
}

static bool copy_spirv_with_optional_extended_format_capability(
    const Uint32 *words,
    size_t word_count,
    bool needs_extended_format_capability,
    Uint32 **edited_words,
    size_t *edited_word_count)
{
    bool insert_capability = needs_extended_format_capability &&
        !spirv_has_capability(words, word_count, SpvCapabilityStorageImageExtendedFormats);
    size_t insertion_offset = 5;

    *edited_words = NULL;
    *edited_word_count = word_count;

    if (insert_capability) {
        for (size_t offset = 5; offset < word_count;) {
            Uint32 instruction = words[offset];
            Uint16 word_length = (Uint16)(instruction >> 16);
            SpvOp opcode = (SpvOp)(instruction & 0xffffu);

            if (word_length == 0 || offset + word_length > word_count) {
                return SDL_SetError("invalid SPIR-V instruction stream");
            }
            if (opcode != SpvOpCapability) {
                break;
            }
            insertion_offset = offset + word_length;
            offset += word_length;
        }

        *edited_word_count = word_count + 2;
    }

    *edited_words = (Uint32 *)SDL_malloc(*edited_word_count * sizeof(Uint32));
    if (!*edited_words) {
        return SDL_OutOfMemory();
    }

    if (insert_capability) {
        /* Capabilities are the first logical SPIR-V section. */
        SDL_memcpy(*edited_words, words, insertion_offset * sizeof(Uint32));
        (*edited_words)[insertion_offset] = ((Uint32)2 << 16) | (Uint32)SpvOpCapability;
        (*edited_words)[insertion_offset + 1] = (Uint32)SpvCapabilityStorageImageExtendedFormats;
        SDL_memcpy(
            *edited_words + insertion_offset + 2,
            words + insertion_offset,
            (word_count - insertion_offset) * sizeof(Uint32));
    } else {
        SDL_memcpy(*edited_words, words, word_count * sizeof(Uint32));
    }

    return true;
}

static bool patch_spirv_storage_texture_formats(
    Uint8 **bytecode,
    size_t *bytecode_size,
    const ShaderCross_CLISPIRVStorageTypePatch *patches,
    Uint32 num_patches)
{
    const Uint32 *words = NULL;
    size_t word_count = 0;
    Uint32 *edited_words = NULL;
    size_t edited_word_count = 0;
    bool needs_extended_format_capability = false;
    Uint32 patched_count = 0;

    if (num_patches == 0) {
        return true;
    }
    if (bytecode == NULL || *bytecode == NULL) {
        return SDL_InvalidParamError("bytecode");
    }
    if (bytecode_size == NULL) {
        return SDL_InvalidParamError("bytecode_size");
    }
    if (!spirv_bytecode_words(*bytecode, *bytecode_size, &words, &word_count)) {
        return false;
    }

    for (Uint32 i = 0; i < num_patches; i += 1) {
        needs_extended_format_capability |= patches[i].needs_extended_format_capability;
    }

    if (!copy_spirv_with_optional_extended_format_capability(
            words,
            word_count,
            needs_extended_format_capability,
            &edited_words,
            &edited_word_count)) {
        return false;
    }

    for (size_t offset = 5; offset < edited_word_count;) {
        Uint32 instruction = edited_words[offset];
        Uint16 word_length = (Uint16)(instruction >> 16);
        SpvOp opcode = (SpvOp)(instruction & 0xffffu);

        if (word_length == 0 || offset + word_length > edited_word_count) {
            SDL_free(edited_words);
            return SDL_SetError("invalid SPIR-V instruction stream");
        }

        if (opcode == SpvOpTypeImage && word_length >= 9) {
            Uint32 image_type_id = edited_words[offset + 1];

            for (Uint32 i = 0; i < num_patches; i += 1) {
                if (patches[i].image_type_id == image_type_id) {
                    /* OpTypeImage word 8 is the image format operand. */
                    if (edited_words[offset + 8] != patches[i].current_format) {
                        SDL_free(edited_words);
                        return SDL_SetError("SPIR-V image type %u format changed before storage texture authority could be applied", image_type_id);
                    }
                    edited_words[offset + 8] = patches[i].target_format;
                    patched_count += 1;
                    break;
                }
            }
        }

        offset += word_length;
    }

    if (patched_count != num_patches) {
        SDL_free(edited_words);
        return SDL_SetError("SPIR-V storage texture format authority could not find every reflected image type");
    }

    SDL_free(*bytecode);
    *bytecode = (Uint8 *)edited_words;
    *bytecode_size = edited_word_count * sizeof(Uint32);
    return true;
}

bool ShaderCross_CLI_UpdateStorageTextureFormatsForSPIRV(
    Uint8 **bytecode,
    size_t *bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy *storage_texture_slot_policies,
    Uint32 num_storage_texture_slot_policies,
    char **provenance_note)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_set active_variables = NULL;
    spvc_resources resources = NULL;
    DrefUsageAnalysis dref_usage;
    SDLResourceLayoutContext slot_context;
    ShaderCross_CLIResourceLayoutEvidence evidence;
    ShaderCross_CLIResourceLayoutEvidenceOptions evidence_options;
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    ShaderCross_CLISPIRVStorageTypePatch *patches = NULL;
    Uint32 num_patches = 0;
    SpvExecutionModel execution_model = SpvExecutionModelMax;
    spvc_result result;
    bool succeeded = false;

    SDL_zero(dref_usage);
    SDL_zero(slot_context);
    SDL_zero(evidence);
    SDL_zero(evidence_options);
    SDL_zero(input);

    if (provenance_note != NULL) {
        *provenance_note = NULL;
    }
    if (num_storage_texture_slot_policies == 0) {
        return true;
    }
    if (storage_texture_slot_policies == NULL) {
        return SDL_InvalidParamError("storage_texture_slot_policies");
    }
    if (bytecode == NULL || *bytecode == NULL) {
        return SDL_InvalidParamError("bytecode");
    }
    if (bytecode_size == NULL) {
        return SDL_InvalidParamError("bytecode_size");
    }

    result = spvc_context_create(&context);
    if (result < 0) {
        SDL_SetError("spvc_context_create failed: %d", (int)result);
        return false;
    }

    result = spvc_context_parse_spirv(context, (const SpvId *)*bytecode, *bytecode_size / sizeof(SpvId), &ir);
    if (result < 0) {
        SDL_SetError("spvc_context_parse_spirv failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);
    if (result < 0) {
        SDL_SetError("spvc_context_create_compiler failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!resource_layout_execution_model_for_entrypoint(compiler, entrypoint, shader_stage, &execution_model)) {
        goto done;
    }
    result = spvc_compiler_set_entry_point(compiler, entrypoint, execution_model);
    if (result < 0) {
        SDL_SetError("spvc_compiler_set_entry_point failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_get_active_interface_variables(compiler, &active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_get_active_interface_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_create_shader_resources_for_active_variables(compiler, &resources, active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_create_shader_resources_for_active_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!analyze_dref_usage(&dref_usage, *bytecode, *bytecode_size, entrypoint, execution_model)) {
        goto done;
    }
    if (!collect_sdl_resource_layout_context(&slot_context, compiler, resources)) {
        goto done;
    }
    if (!resource_layout_kind_from_execution_model(slot_context.execution_model, &input.kind)) {
        SDL_SetError("SPIR-V execution model is not supported for storage texture format authority");
        goto done;
    }
    fill_layout_facts_input_counts(&slot_context, &input);
    evidence_options.allow_observed_storage_access = true;
    evidence_options.classify_fragment_writable_storage = true;
    evidence_options.collect_sampled_textures = false;
    if (!collect_resource_layout_evidence(
            &evidence,
            compiler,
            resources,
            &slot_context,
            &dref_usage,
            &evidence_options)) {
        goto done;
    }
    if (!apply_storage_texture_slot_policies(
            &evidence,
            &input,
            storage_texture_slot_policies,
            num_storage_texture_slot_policies)) {
        goto done;
    }
    if (!collect_spirv_storage_format_patches(&evidence, &patches, &num_patches)) {
        goto done;
    }
    if (!collect_spirv_storage_format_authority_note(&evidence, provenance_note)) {
        goto done;
    }
    if (!patch_spirv_storage_texture_formats(bytecode, bytecode_size, patches, num_patches)) {
        goto done;
    }

    succeeded = true;

done:
    if (!succeeded && provenance_note != NULL) {
        SDL_free(*provenance_note);
        *provenance_note = NULL;
    }
    SDL_free(patches);
    free_resource_layout_evidence(&evidence);
    free_dref_usage_analysis(&dref_usage);
    spvc_context_destroy(context);
    return succeeded;
}

static const char *io_var_type_to_string(spvc_basetype base_type, Uint32 vector_size)
{
    switch (base_type) {
    case SPVC_BASETYPE_INT8:
        switch (vector_size) {
        case 1: return "byte";
        case 2: return "byte2";
        case 3: return "byte3";
        case 4: return "byte4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_UINT8:
        switch (vector_size) {
        case 1: return "ubyte";
        case 2: return "ubyte2";
        case 3: return "ubyte3";
        case 4: return "ubyte4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_INT16:
        switch (vector_size) {
        case 1: return "short";
        case 2: return "short2";
        case 3: return "short3";
        case 4: return "short4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_UINT16:
        switch (vector_size) {
        case 1: return "ushort";
        case 2: return "ushort2";
        case 3: return "ushort3";
        case 4: return "ushort4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_INT32:
        switch (vector_size) {
        case 1: return "int";
        case 2: return "int2";
        case 3: return "int3";
        case 4: return "int4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_UINT32:
        switch (vector_size) {
        case 1: return "uint";
        case 2: return "uint2";
        case 3: return "uint3";
        case 4: return "uint4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_INT64:
        switch (vector_size) {
        case 1: return "long";
        case 2: return "long2";
        case 3: return "long3";
        case 4: return "long4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_UINT64:
        switch (vector_size) {
        case 1: return "ulong";
        case 2: return "ulong2";
        case 3: return "ulong3";
        case 4: return "ulong4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_FP16:
        switch (vector_size) {
        case 1: return "half";
        case 2: return "half2";
        case 3: return "half3";
        case 4: return "half4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_FP32:
        switch (vector_size) {
        case 1: return "float";
        case 2: return "float2";
        case 3: return "float3";
        case 4: return "float4";
        default: break;
        }
        break;
    case SPVC_BASETYPE_FP64:
        switch (vector_size) {
        case 1: return "double";
        case 2: return "double2";
        case 3: return "double3";
        case 4: return "double4";
        default: break;
        }
        break;
    default:
        break;
    }

    return "unknown";
}

static bool write_stage_io_array_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    spvc_resource_type resource_type,
    const char *name)
{
    const spvc_reflected_resource *resource_list = NULL;
    size_t resource_count = 0;
    spvc_result result = spvc_resources_get_resource_list_for_type(resources, resource_type, &resource_list, &resource_count);

    if (result < 0) {
        return SDL_SetError("spvc_resources_get_resource_list_for_type failed");
    }

    SDL_IOprintf(outputIO, "\"%s\": [", name);
    for (size_t i = 0; i < resource_count; i += 1) {
        const spvc_reflected_resource *resource = &resource_list[i];
        spvc_type type = spvc_compiler_get_type_handle(compiler, resource->base_type_id);
        spvc_basetype base_type = type ? spvc_type_get_basetype(type) : SPVC_BASETYPE_UNKNOWN;
        Uint32 vector_size = type ? spvc_type_get_vector_size(type) : 0;
        bool has_location = spvc_compiler_has_decoration(compiler, resource->id, SpvDecorationLocation) == SPVC_TRUE;
        Uint32 location = has_location ? spvc_compiler_get_decoration(compiler, resource->id, SpvDecorationLocation) : 0;

        if (i > 0) {
            SDL_IOprintf(outputIO, ", ");
        }
        SDL_IOprintf(outputIO, "{ \"name\": ");
        write_json_string(outputIO, resource->name);
        SDL_IOprintf(outputIO, ", \"type\": ");
        write_json_string(outputIO, io_var_type_to_string(base_type, vector_size));
        SDL_IOprintf(outputIO, ", \"location\": ");
        write_json_null_or_uint(outputIO, location, has_location);
        SDL_IOprintf(outputIO, " }");
    }
    SDL_IOprintf(outputIO, "]");
    return true;
}

static bool write_spirv_reflection_summary_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access)
{
    SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind;
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;
    FragmentStorageBufferFactSlots fragment_slots;

    if (!resource_layout_kind_from_execution_model(slot_context->execution_model, &kind)) {
        return SDL_SetError("SPIR-V execution model is not supported for JSON reflection");
    }
    if (!fill_resource_layout_counts(slot_context, kind, &layout)) {
        return SDL_SetError("SPIR-V execution model is not supported for SDL resource layout counts");
    }

    if (kind == SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE) {
        SDL_IOprintf(
            outputIO,
            "\"samplers\": %u, \"readonly_storage_textures\": %u, \"readonly_storage_buffers\": %u, \"readwrite_storage_textures\": %u, \"readwrite_storage_buffers\": %u, \"uniform_buffers\": %u, \"threadcount_x\": %u, \"threadcount_y\": %u, \"threadcount_z\": %u",
            layout.num_samplers,
            layout.num_readonly_storage_textures,
            layout.num_readonly_storage_buffers,
            layout.num_readwrite_storage_textures,
            layout.num_readwrite_storage_buffers,
            layout.num_uniform_buffers,
            (unsigned)spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 0),
            (unsigned)spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 1),
            (unsigned)spvc_compiler_get_execution_mode_argument_by_index(compiler, SpvExecutionModeLocalSize, 2));
        return true;
    }

    if (slot_context->execution_model == SpvExecutionModelFragment) {
        if (!collect_fragment_storage_fact_slots_for_json(compiler, resources, slot_context, dref_usage, allow_observed_storage_access, &fragment_slots)) {
            return false;
        }
        layout.num_storage_textures = fragment_slots.num_readonly_storage_textures;
        layout.num_storage_buffers = fragment_slots.num_readonly_storage_buffers;
    }

    SDL_IOprintf(
        outputIO,
        "\"samplers\": %u, \"storage_textures\": %u, \"storage_buffers\": %u, \"uniform_buffers\": %u, ",
        layout.num_samplers,
        layout.num_storage_textures,
        layout.num_storage_buffers,
        layout.num_uniform_buffers);
    if (!write_stage_io_array_json(outputIO, compiler, resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, "inputs")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_stage_io_array_json(outputIO, compiler, resources, SPVC_RESOURCE_TYPE_STAGE_OUTPUT, "outputs")) {
        return false;
    }
    return true;
}

static bool write_detailed_spirv_resources_json(
    SDL_IOStream *outputIO,
    spvc_compiler compiler,
    spvc_resources resources,
    const SDLResourceLayoutContext *slot_context,
    const DrefUsageAnalysis *dref_usage,
    bool allow_observed_storage_access)
{
    SDL_IOprintf(outputIO, "\"resource_details\": { ");
    if (!write_resource_array_json(outputIO, compiler, resources, slot_context, dref_usage, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, "sampled_images")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_resource_array_json(outputIO, compiler, resources, slot_context, dref_usage, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, "separate_images")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_resource_array_json(outputIO, compiler, resources, slot_context, dref_usage, SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, "separate_samplers")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_resource_array_json(outputIO, compiler, resources, slot_context, dref_usage, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, "storage_images")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_resource_array_json(outputIO, compiler, resources, slot_context, dref_usage, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, "uniform_buffers")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_resource_array_json(outputIO, compiler, resources, slot_context, dref_usage, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, "storage_buffers")) {
        return false;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_sampled_texture_sampler_pairs_json(outputIO, compiler, resources, slot_context, dref_usage)) {
        return false;
    }
    SDL_IOprintf(outputIO, " }");
    SDL_IOprintf(outputIO, ", ");
    return write_sdl_resource_layout_json(outputIO, compiler, resources, slot_context, dref_usage, allow_observed_storage_access);
}

bool ShaderCross_CLI_WriteSPIRVReflectionJSON(
    SDL_IOStream *outputIO,
    const Uint8 *bytecode,
    size_t bytecode_size,
    const char *entrypoint,
    SDL_ShaderCross_ShaderStage shader_stage,
    bool allow_observed_storage_access)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_set active_variables = NULL;
    spvc_resources resources = NULL;
    DrefUsageAnalysis dref_usage;
    SDLResourceLayoutContext slot_context;
    SpvExecutionModel execution_model = SpvExecutionModelMax;
    spvc_result result;
    bool succeeded = false;

    SDL_zero(dref_usage);
    SDL_zero(slot_context);

    result = spvc_context_create(&context);
    if (result < 0) {
        SDL_SetError("spvc_context_create failed: %d", (int)result);
        return false;
    }

    result = spvc_context_parse_spirv(context, (const SpvId *)bytecode, bytecode_size / sizeof(SpvId), &ir);
    if (result < 0) {
        SDL_SetError("spvc_context_parse_spirv failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler);
    if (result < 0) {
        SDL_SetError("spvc_context_create_compiler failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!resource_layout_execution_model_for_entrypoint(compiler, entrypoint, shader_stage, &execution_model)) {
        goto done;
    }
    result = spvc_compiler_set_entry_point(compiler, entrypoint, execution_model);
    if (result < 0) {
        SDL_SetError("spvc_compiler_set_entry_point failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_get_active_interface_variables(compiler, &active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_get_active_interface_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    result = spvc_compiler_create_shader_resources_for_active_variables(compiler, &resources, active_variables);
    if (result < 0) {
        SDL_SetError("spvc_compiler_create_shader_resources_for_active_variables failed: %s", spvc_context_get_last_error_string(context));
        goto done;
    }

    if (!analyze_dref_usage(&dref_usage, bytecode, bytecode_size, entrypoint, execution_model)) {
        goto done;
    }
    if (!collect_sdl_resource_layout_context(&slot_context, compiler, resources)) {
        goto done;
    }

    SDL_IOprintf(outputIO, "{ ");
    if (!write_spirv_reflection_summary_json(outputIO, compiler, resources, &slot_context, &dref_usage, allow_observed_storage_access)) {
        goto done;
    }
    SDL_IOprintf(outputIO, ", ");
    if (!write_detailed_spirv_resources_json(outputIO, compiler, resources, &slot_context, &dref_usage, allow_observed_storage_access)) {
        goto done;
    }
    SDL_IOprintf(outputIO, " }\n");
    succeeded = true;

done:
    free_dref_usage_analysis(&dref_usage);
    spvc_context_destroy(context);
    return succeeded;
}
