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

#ifndef SDL_shadercross_layout_facts_h_
#define SDL_shadercross_layout_facts_h_

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_iostream.h>

typedef enum SDL_ShaderCross_INTERNAL_ResourceLayoutKind
{
    SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
    SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE
} SDL_ShaderCross_INTERNAL_ResourceLayoutKind;

typedef enum SDL_ShaderCross_INTERNAL_TextureDimension
{
    SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
    SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY,
    SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D,
    SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE,
    SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY
} SDL_ShaderCross_INTERNAL_TextureDimension;

typedef enum SDL_ShaderCross_INTERNAL_SampledTextureSampleKind
{
    SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT,
    SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH,
    SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT,
    SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT
} SDL_ShaderCross_INTERNAL_SampledTextureSampleKind;

typedef enum SDL_ShaderCross_INTERNAL_SamplerBindingKind
{
    SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE
} SDL_ShaderCross_INTERNAL_SamplerBindingKind;

typedef enum SDL_ShaderCross_INTERNAL_SamplerPolicy
{
    SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT,
    SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON
} SDL_ShaderCross_INTERNAL_SamplerPolicy;

typedef enum SDL_ShaderCross_INTERNAL_ImageDepthOperand
{
    SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH,
    SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_DEPTH
} SDL_ShaderCross_INTERNAL_ImageDepthOperand;

typedef enum SDL_ShaderCross_INTERNAL_StorageTextureResourceClass
{
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE
} SDL_ShaderCross_INTERNAL_StorageTextureResourceClass;

typedef enum SDL_ShaderCross_INTERNAL_StorageTextureAccess
{
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE
} SDL_ShaderCross_INTERNAL_StorageTextureAccess;

typedef enum SDL_ShaderCross_INTERNAL_StorageTextureFormat
{
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_UINT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_INT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT
} SDL_ShaderCross_INTERNAL_StorageTextureFormat;

typedef struct SDL_ShaderCross_INTERNAL_SampledTextureEvidence
{
    const char *name;
    bool has_sdl_slot;
    Uint32 slot;
    bool has_source_binding;
    Uint32 source_set;
    Uint32 source_binding;
    SDL_ShaderCross_INTERNAL_TextureDimension texture_dimension;
    SDL_ShaderCross_INTERNAL_SampledTextureSampleKind sample_kind;
    SDL_ShaderCross_INTERNAL_SamplerBindingKind sampler_binding;
    SDL_ShaderCross_INTERNAL_SamplerPolicy sampler_policy;
    bool multisampled;
    SDL_ShaderCross_INTERNAL_ImageDepthOperand image_depth;
    bool has_explicit_slot_policy;
    SDL_GPUSampledTextureSlotDescription explicit_slot_policy;
} SDL_ShaderCross_INTERNAL_SampledTextureEvidence;

typedef struct SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy
{
    Uint32 slot;
    SDL_GPUSampledTextureSlotDescription description;
} SDL_ShaderCross_INTERNAL_SampledTextureSlotPolicy;

typedef struct SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy
{
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class;
    Uint32 slot;
    SDL_GPUStorageTextureSlotDescription description;
    bool format_authority;
} SDL_ShaderCross_INTERNAL_StorageTextureSlotPolicy;

typedef struct SDL_ShaderCross_INTERNAL_StorageTextureEvidence
{
    const char *name;
    bool has_sdl_slot;
    Uint32 slot;
    Uint32 image_type_id;
    Uint32 image_format;
    bool has_source_binding;
    Uint32 source_set;
    Uint32 source_binding;
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class;
    SDL_ShaderCross_INTERNAL_TextureDimension texture_dimension;
    SDL_ShaderCross_INTERNAL_StorageTextureFormat format;
    SDL_ShaderCross_INTERNAL_StorageTextureAccess final_access;
    SDL_ShaderCross_INTERNAL_StorageTextureAccess observed_access;
    bool uses_image_texel_pointer;
    bool has_explicit_slot_policy;
    bool explicit_slot_policy_format_authority;
    SDL_GPUStorageTextureSlotDescription explicit_slot_policy;
} SDL_ShaderCross_INTERNAL_StorageTextureEvidence;

typedef enum SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority
{
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_NONE,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_OBSERVED_WRITE,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_POLICY,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_FORMAT_POLICY
} SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority;

typedef struct SDL_ShaderCross_INTERNAL_StorageTextureDecision
{
    bool accepted;
    Uint64 reasons;
    Uint64 notes;
    SDL_GPUStorageTextureSlotDescription slot;
    SDL_GPUTextureType reflected_texture_type;
    SDL_GPUTextureFormat reflected_format;
    SDL_GPUStorageTextureAccess reflected_access;
    bool has_reflected_texture_type;
    bool has_reflected_format;
    bool has_reflected_access;
    bool format_matches_reflection;
    SDL_ShaderCross_INTERNAL_StorageTextureAccess effective_access;
    SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority texture_authority;
    SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority format_authority;
    SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority access_authority;
} SDL_ShaderCross_INTERNAL_StorageTextureDecision;

typedef struct SDL_ShaderCross_INTERNAL_LayoutFactsInput
{
    SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind;
    Uint32 num_sampled_texture_slots;
    Uint32 num_storage_textures;
    Uint32 num_readonly_storage_textures;
    Uint32 num_readwrite_storage_textures;
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *sampled_textures;
    Uint32 num_sampled_textures;
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *storage_textures;
    Uint32 num_storage_texture_entries;
    bool has_samplerless_sampled_image;
} SDL_ShaderCross_INTERNAL_LayoutFactsInput;

#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLERLESS_SAMPLED_IMAGE (1ull << 0)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_UNMAPPED (1ull << 1)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_OUT_OF_RANGE (1ull << 2)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_DUPLICATE (1ull << 3)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED (1ull << 4)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED (1ull << 5)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_UNMAPPED (1ull << 6)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_OUT_OF_RANGE (1ull << 7)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_DUPLICATE (1ull << 8)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED (1ull << 9)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED (1ull << 10)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_ACCESS_AMBIGUOUS (1ull << 11)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED (1ull << 12)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED (1ull << 13)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DEPTH_AMBIGUOUS (1ull << 14)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH (1ull << 15)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH (1ull << 16)

#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_FLOAT_SAMPLER_POLICY (1ull << 0)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY (1ull << 1)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_STORAGE_TEXTURE_SLOT_POLICY (1ull << 2)
#define SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_STORAGE_TEXTURE_FORMAT_AUTHORITY (1ull << 3)

typedef struct SDL_ShaderCross_INTERNAL_LayoutFactsOutput
{
    bool accepted;
    Uint64 reasons;
    Uint64 notes;
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots;
    Uint32 max_sampled_texture_slots;
    Uint32 num_sampled_texture_slots;
    SDL_GPUStorageTextureSlotDescription *storage_textures;
    Uint32 max_storage_textures;
    Uint32 num_storage_textures;
    SDL_GPUStorageTextureSlotDescription *readonly_storage_textures;
    Uint32 max_readonly_storage_textures;
    Uint32 num_readonly_storage_textures;
    SDL_GPUStorageTextureSlotDescription *readwrite_storage_textures;
    Uint32 max_readwrite_storage_textures;
    Uint32 num_readwrite_storage_textures;
} SDL_ShaderCross_INTERNAL_LayoutFactsOutput;

typedef struct SDL_ShaderCross_INTERNAL_ResourceLayoutCounts
{
    SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind;
    SDL_GPUShaderStage shader_stage;
    Uint32 num_samplers;
    Uint32 num_storage_textures;
    Uint32 num_storage_buffers;
    Uint32 num_uniform_buffers;
    Uint32 num_readonly_storage_textures;
    Uint32 num_readonly_storage_buffers;
    Uint32 num_readwrite_storage_textures;
    Uint32 num_readwrite_storage_buffers;
} SDL_ShaderCross_INTERNAL_ResourceLayoutCounts;

bool SDL_ShaderCross_INTERNAL_MapResourceLayoutToLayoutFacts(
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output);

bool SDL_ShaderCross_INTERNAL_ValidateResourceLayoutCSymbolPrefix(
    const char *symbol_prefix);

bool SDL_ShaderCross_INTERNAL_WriteResourceLayoutCInitializers(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *layout_facts);

bool SDL_ShaderCross_INTERNAL_MapStorageTextureSlotDescription(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_GPUStorageTextureSlotDescription *slot);

bool SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_ShaderCross_INTERNAL_StorageTextureDecision *decision);

#endif
