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

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include <stdarg.h>

static SDL_GPUSampledTextureSlotDescription DefaultSampledTextureSlot(void)
{
    SDL_GPUSampledTextureSlotDescription result;
    result.texture_type = SDL_GPU_TEXTURETYPE_2D;
    result.sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT;
    result.sampler_type = SDL_GPU_SHADERSAMPLERTYPE_FILTERING;
    return result;
}

static SDL_GPUStorageTextureSlotDescription DefaultStorageTextureSlot(SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class)
{
    SDL_GPUStorageTextureSlotDescription result;
    result.texture_type = SDL_GPU_TEXTURETYPE_2D;
    result.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    result.access = (resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE) ?
        SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY :
        SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY;
    return result;
}

static bool SameSampledTextureSlot(SDL_GPUSampledTextureSlotDescription a, SDL_GPUSampledTextureSlotDescription b)
{
    return a.texture_type == b.texture_type &&
           a.sample_type == b.sample_type &&
           a.sampler_type == b.sampler_type;
}

static bool SameStorageTextureSlot(SDL_GPUStorageTextureSlotDescription a, SDL_GPUStorageTextureSlotDescription b)
{
    return a.texture_type == b.texture_type &&
           a.format == b.format &&
           a.access == b.access;
}

static bool TextureDimensionToType(SDL_ShaderCross_INTERNAL_TextureDimension dimension, SDL_GPUTextureType *texture_type)
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

static bool StorageTextureFormatToSDL(SDL_ShaderCross_INTERNAL_StorageTextureFormat format, SDL_GPUTextureFormat *texture_format)
{
    switch (format) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_UINT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32_UINT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_INT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32_INT;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT:
        *texture_format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        return true;
    default:
        return false;
    }
}

static bool StorageTextureAccessToSDL(SDL_ShaderCross_INTERNAL_StorageTextureAccess access, SDL_GPUStorageTextureAccess *storage_access)
{
    switch (access) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY:
        *storage_access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY:
        *storage_access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY;
        return true;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE:
        *storage_access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE;
        return true;
    default:
        return false;
    }
}

static bool StorageTextureTypeSupportsReadOnly(SDL_GPUTextureType texture_type)
{
    return texture_type == SDL_GPU_TEXTURETYPE_2D ||
           texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY ||
           texture_type == SDL_GPU_TEXTURETYPE_3D;
}

static bool StorageTextureFormatSupportsReadOnly(SDL_GPUTextureFormat format)
{
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
}

static bool StorageTextureFormatSupportsComputeReadOnly(SDL_GPUTextureFormat format)
{
    return StorageTextureFormatSupportsReadOnly(format);
}

static bool StorageTextureFormatSupportsWriteOnly(SDL_GPUTextureFormat format)
{
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
}

static bool StorageTextureTypeSupportsReadWrite(SDL_GPUTextureType texture_type)
{
    return texture_type == SDL_GPU_TEXTURETYPE_2D ||
           texture_type == SDL_GPU_TEXTURETYPE_3D;
}

static bool StorageTextureFormatSupportsReadWrite(SDL_GPUTextureFormat format)
{
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_INT ||
           format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
}

typedef enum SDL_ShaderCross_INTERNAL_StorageTextureFormatKind
{
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_UNKNOWN,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_FLOAT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_UINT,
    SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_SINT
} SDL_ShaderCross_INTERNAL_StorageTextureFormatKind;

static SDL_ShaderCross_INTERNAL_StorageTextureFormatKind StorageTextureFormatKind(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_FLOAT;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT:
    case SDL_GPU_TEXTUREFORMAT_R32_UINT:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_UINT;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT:
    case SDL_GPU_TEXTUREFORMAT_R32_INT:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_SINT;
    default:
        return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_UNKNOWN;
    }
}

static bool StorageTextureFormatsHaveSameKind(SDL_GPUTextureFormat a, SDL_GPUTextureFormat b)
{
    const SDL_ShaderCross_INTERNAL_StorageTextureFormatKind a_kind = StorageTextureFormatKind(a);
    return a_kind != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_KIND_UNKNOWN &&
           a_kind == StorageTextureFormatKind(b);
}

static bool SampledTextureTypeSupportsSamplerlessInteger(SDL_GPUTextureType texture_type)
{
    return texture_type == SDL_GPU_TEXTURETYPE_2D ||
           texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY ||
           texture_type == SDL_GPU_TEXTURETYPE_3D;
}

static bool SampledTextureTypeSupportsDepth(SDL_GPUTextureType texture_type)
{
    return texture_type == SDL_GPU_TEXTURETYPE_2D ||
           texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY ||
           texture_type == SDL_GPU_TEXTURETYPE_CUBE ||
           texture_type == SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
}

static bool ValidateExplicitSampledTextureSlotPolicy(
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence,
    SDL_GPUSampledTextureSlotDescription *slot,
    Uint64 *reasons,
    Uint64 *notes)
{
    const SDL_GPUSampledTextureSlotDescription policy = evidence->explicit_slot_policy;

    if (policy.texture_type != slot->texture_type) {
        *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH;
        return false;
    }

    if (evidence->multisampled) {
        if (policy.texture_type != SDL_GPU_TEXTURETYPE_2D ||
            policy.sampler_type != SDL_GPU_SHADERSAMPLERTYPE_NONE ||
            evidence->sampler_binding != SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE) {
            *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH;
            return false;
        }

        if (policy.sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT) {
            if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT &&
                evidence->image_depth != SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_DEPTH) {
                *slot = policy;
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
                return true;
            }
        } else if (policy.sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH) {
            if ((evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT ||
                 evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH) &&
                evidence->image_depth != SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH) {
                *slot = policy;
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
                return true;
            }
        }

        *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH;
        return false;
    }

    if (policy.sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT ||
        policy.sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH) {
        *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH;
        return false;
    }

    switch (policy.sample_type) {
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT:
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT) {
            if (policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_FILTERING &&
                (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN ||
                 evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING) &&
                (evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING ||
                 evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT)) {
                *slot = policy;
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
                return true;
            }
            if (policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING &&
                (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN ||
                 evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING) &&
                evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT) {
                *slot = policy;
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
                return true;
            }
        }
        break;
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT:
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT &&
            policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING &&
            (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN ||
             evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING) &&
            evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT) {
            *slot = policy;
            *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
            return true;
        }
        break;
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH:
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH &&
            SampledTextureTypeSupportsDepth(policy.texture_type)) {
            if (policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_COMPARISON &&
                evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON &&
                evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON) {
                *slot = policy;
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
                return true;
            }
            if (policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING &&
                evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING &&
                evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN) {
                *slot = policy;
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
                return true;
            }
        }
        break;
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT:
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT &&
            policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONE &&
            evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE &&
            SampledTextureTypeSupportsSamplerlessInteger(policy.texture_type)) {
            *slot = policy;
            *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
            return true;
        }
        break;
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT:
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT &&
            policy.sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONE &&
            evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE &&
            SampledTextureTypeSupportsSamplerlessInteger(policy.texture_type)) {
            *slot = policy;
            *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY;
            return true;
        }
        break;
    default:
        break;
    }

    *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH;
    return false;
}

static SDL_ShaderCross_INTERNAL_StorageTextureAccess StorageTextureEffectiveAccess(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority *authority)
{
    if (evidence->final_access != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN) {
        if (evidence->final_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE &&
            evidence->observed_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY) {
            if (authority != NULL) {
                *authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_OBSERVED_WRITE;
            }
            return evidence->observed_access;
        }
        if (authority != NULL) {
            *authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED;
        }
        return evidence->final_access;
    }
    if (evidence->observed_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY) {
        if (authority != NULL) {
            *authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_OBSERVED_WRITE;
        }
        return evidence->observed_access;
    }
    if (authority != NULL) {
        *authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_NONE;
    }
    return SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN;
}

static void StorageTextureReject(SDL_ShaderCross_INTERNAL_StorageTextureDecision *decision, Uint64 reason)
{
    decision->reasons |= reason;
}

static bool StorageTextureClassSupportsSlot(
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class,
    const SDL_GPUStorageTextureSlotDescription *slot,
    Uint64 *reasons,
    bool explicit_policy)
{
    const Uint64 dimension_reason = explicit_policy ?
        SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH :
        SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED;
    const Uint64 format_reason = explicit_policy ?
        SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH :
        SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED;
    const Uint64 group_reason = explicit_policy ?
        SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH :
        SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED;
    Uint64 entry_reasons = 0;

    switch (resource_class) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
        if (!StorageTextureTypeSupportsReadOnly(slot->texture_type)) {
            entry_reasons |= dimension_reason;
        }
        if (!StorageTextureFormatSupportsReadOnly(slot->format)) {
            entry_reasons |= format_reason;
        }
        if (slot->access != SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY) {
            entry_reasons |= group_reason;
        }
        break;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
        if (!StorageTextureTypeSupportsReadOnly(slot->texture_type)) {
            entry_reasons |= dimension_reason;
        }
        if (!StorageTextureFormatSupportsComputeReadOnly(slot->format)) {
            entry_reasons |= format_reason;
        }
        if (slot->access != SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY) {
            entry_reasons |= group_reason;
        }
        break;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
        if (!StorageTextureTypeSupportsReadWrite(slot->texture_type)) {
            entry_reasons |= dimension_reason;
        }
        if (slot->access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY) {
            if (!StorageTextureFormatSupportsWriteOnly(slot->format)) {
                entry_reasons |= format_reason;
            }
        } else if (slot->access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE) {
            if (!StorageTextureFormatSupportsReadWrite(slot->format)) {
                entry_reasons |= format_reason;
            }
        } else {
            entry_reasons |= group_reason;
        }
        break;
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE:
        entry_reasons |= group_reason;
        break;
    default:
        entry_reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED;
        break;
    }

    *reasons |= entry_reasons;
    return entry_reasons == 0;
}

bool SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_ShaderCross_INTERNAL_StorageTextureDecision *decision)
{
    SDL_ShaderCross_INTERNAL_StorageTextureDecisionAuthority effective_access_authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_NONE;

    if (evidence == NULL) {
        return SDL_InvalidParamError("evidence");
    }
    if (decision == NULL) {
        return SDL_InvalidParamError("decision");
    }

    SDL_zero(*decision);
    decision->slot = DefaultStorageTextureSlot(evidence->resource_class);
    decision->has_reflected_texture_type = TextureDimensionToType(evidence->texture_dimension, &decision->reflected_texture_type);
    decision->has_reflected_format = StorageTextureFormatToSDL(evidence->format, &decision->reflected_format);
    decision->has_reflected_access = StorageTextureAccessToSDL(evidence->final_access, &decision->reflected_access);
    decision->effective_access = StorageTextureEffectiveAccess(evidence, &effective_access_authority);
    decision->texture_authority = decision->has_reflected_texture_type ?
        SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED :
        SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_NONE;
    decision->format_authority = decision->has_reflected_format ?
        SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED :
        SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_NONE;
    decision->access_authority = effective_access_authority;

    if (evidence->has_explicit_slot_policy) {
        SDL_GPUStorageTextureAccess expected_access;
        decision->slot = evidence->explicit_slot_policy;
        decision->texture_authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_POLICY;
        decision->format_authority = evidence->explicit_slot_policy_format_authority ?
            SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_FORMAT_POLICY :
            SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_POLICY;
        decision->access_authority = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_POLICY;
        decision->format_matches_reflection =
            decision->has_reflected_format &&
            decision->slot.format == decision->reflected_format;

        if (!decision->has_reflected_texture_type) {
            StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED);
        } else if (decision->slot.texture_type != decision->reflected_texture_type) {
            StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH);
        }

        if (evidence->explicit_slot_policy_format_authority &&
            (evidence->resource_class != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE ||
             decision->slot.access != SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY ||
             decision->slot.texture_type != SDL_GPU_TEXTURETYPE_2D)) {
            StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH);
        }
        if (!decision->format_matches_reflection &&
            (!evidence->explicit_slot_policy_format_authority ||
             !decision->has_reflected_format ||
             !StorageTextureFormatsHaveSameKind(decision->slot.format, decision->reflected_format))) {
            StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH);
        }

        if (evidence->observed_access != SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN) {
            if (!StorageTextureAccessToSDL(evidence->observed_access, &expected_access) ||
                decision->slot.access != expected_access) {
                StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH);
            }
        } else if (decision->has_reflected_access) {
            if (decision->slot.access != decision->reflected_access) {
                StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH);
            }
        } else {
            StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH);
        }

        StorageTextureClassSupportsSlot(evidence->resource_class, &decision->slot, &decision->reasons, true);
        if (decision->reasons == 0) {
            decision->notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_STORAGE_TEXTURE_SLOT_POLICY;
            if (evidence->explicit_slot_policy_format_authority) {
                decision->notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_STORAGE_TEXTURE_FORMAT_AUTHORITY;
            }
        }
        decision->accepted = decision->reasons == 0;
        return true;
    }

    if (!decision->has_reflected_texture_type) {
        StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED);
    } else {
        decision->slot.texture_type = decision->reflected_texture_type;
    }
    if (!decision->has_reflected_format) {
        StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED);
    } else {
        decision->slot.format = decision->reflected_format;
    }
    if (decision->effective_access == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN ||
        !StorageTextureAccessToSDL(decision->effective_access, &decision->slot.access)) {
        StorageTextureReject(decision, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_ACCESS_AMBIGUOUS);
    }

    if (decision->reasons == 0) {
        StorageTextureClassSupportsSlot(evidence->resource_class, &decision->slot, &decision->reasons, false);
    }
    decision->accepted = decision->reasons == 0;
    return true;
}

static bool MapSampledTextureSlot(
    const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence,
    SDL_GPUSampledTextureSlotDescription *slot,
    Uint64 *reasons,
    Uint64 *notes)
{
    if (!TextureDimensionToType(evidence->texture_dimension, &slot->texture_type)) {
        *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED;
        return false;
    }

    if (evidence->has_explicit_slot_policy) {
        return ValidateExplicitSampledTextureSlotPolicy(evidence, slot, reasons, notes);
    }

    if (evidence->multisampled) {
        if (slot->texture_type != SDL_GPU_TEXTURETYPE_2D) {
            *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED;
            return false;
        }
        if (evidence->sampler_binding != SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE) {
            *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED;
            return false;
        }

        switch (evidence->image_depth) {
        case SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH:
            if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT) {
                slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT;
                slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE;
                return true;
            }
            break;
        case SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_DEPTH:
            if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT ||
                evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH) {
                slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH;
                slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE;
                return true;
            }
            break;
        case SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN:
        default:
            *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DEPTH_AMBIGUOUS;
            return false;
        }

        *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED;
        return false;
    }

    if (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE) {
        if (!SampledTextureTypeSupportsSamplerlessInteger(slot->texture_type)) {
            *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED;
            return false;
        }
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT) {
            slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT;
            slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE;
            return true;
        }
        if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT) {
            slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT;
            slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE;
            return true;
        }

        *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED;
        return false;
    }

    if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT) {
        if ((evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN ||
             evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING) &&
            (evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING ||
             evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT)) {
            slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT;
            slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_FILTERING;
            if (evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT) {
                *notes |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_FLOAT_SAMPLER_POLICY;
            }
            return true;
        }
        if (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING &&
            evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT) {
            slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT;
            slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING;
            return true;
        }
    }

    if (evidence->sample_kind == SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH &&
        SampledTextureTypeSupportsDepth(slot->texture_type)) {
        if (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON &&
            evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON) {
            slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH;
            slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_COMPARISON;
            return true;
        }
        if (evidence->sampler_binding == SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING &&
            evidence->sampler_policy == SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN) {
            slot->sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH;
            slot->sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING;
            return true;
        }
    }

    *reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED;
    return false;
}

static bool MapStorageTextureSlot(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_GPUStorageTextureSlotDescription *slot,
    Uint64 *reasons,
    Uint64 *notes)
{
    SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;

    if (!SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(evidence, &decision)) {
        return false;
    }

    *reasons |= decision.reasons;
    if (notes != NULL) {
        *notes |= decision.notes;
    }
    if (!decision.accepted) {
        return false;
    }

    *slot = decision.slot;
    return true;
}

bool SDL_ShaderCross_INTERNAL_MapStorageTextureSlotDescription(
    const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence,
    SDL_GPUStorageTextureSlotDescription *slot)
{
    Uint64 reasons = 0;

    if (evidence == NULL) {
        return SDL_InvalidParamError("evidence");
    }
    if (slot == NULL) {
        return SDL_InvalidParamError("slot");
    }

    *slot = DefaultStorageTextureSlot(evidence->resource_class);
    if (!MapStorageTextureSlot(evidence, slot, &reasons, NULL)) {
        return SDL_SetError("storage texture slot layout facts are outside the current SDL_GPU support matrix");
    }
    return true;
}

static bool GetStorageTarget(
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class,
    Uint32 *count)
{
    switch (resource_class) {
    case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
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

static void RestoreOutputBuffers(
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output,
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots,
    Uint32 max_sampled_texture_slots,
    SDL_GPUStorageTextureSlotDescription *storage_textures,
    Uint32 max_storage_textures,
    SDL_GPUStorageTextureSlotDescription *readonly_storage_textures,
    Uint32 max_readonly_storage_textures,
    SDL_GPUStorageTextureSlotDescription *readwrite_storage_textures,
    Uint32 max_readwrite_storage_textures)
{
    output->sampled_texture_slots = sampled_texture_slots;
    output->max_sampled_texture_slots = max_sampled_texture_slots;
    output->storage_textures = storage_textures;
    output->max_storage_textures = max_storage_textures;
    output->readonly_storage_textures = readonly_storage_textures;
    output->max_readonly_storage_textures = max_readonly_storage_textures;
    output->readwrite_storage_textures = readwrite_storage_textures;
    output->max_readwrite_storage_textures = max_readwrite_storage_textures;
}

bool SDL_ShaderCross_INTERNAL_MapResourceLayoutToLayoutFacts(
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output)
{
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots;
    SDL_GPUStorageTextureSlotDescription *storage_textures;
    SDL_GPUStorageTextureSlotDescription *readonly_storage_textures;
    SDL_GPUStorageTextureSlotDescription *readwrite_storage_textures;
    Uint32 max_sampled_texture_slots;
    Uint32 max_storage_textures;
    Uint32 max_readonly_storage_textures;
    Uint32 max_readwrite_storage_textures;
    bool *sampled_seen = NULL;
    bool *storage_seen = NULL;
    bool *readonly_seen = NULL;
    bool *readwrite_seen = NULL;
    bool sampled_needs_layout_facts = false;
    bool storage_needs_layout_facts = false;
    bool readonly_needs_layout_facts = false;
    bool readwrite_needs_layout_facts = false;

    if (input == NULL) {
        return SDL_InvalidParamError("input");
    }
    if (output == NULL) {
        return SDL_InvalidParamError("output");
    }
    if (input->num_sampled_textures > 0 && input->sampled_textures == NULL) {
        return SDL_InvalidParamError("input->sampled_textures");
    }
    if (input->num_storage_texture_entries > 0 && input->storage_textures == NULL) {
        return SDL_InvalidParamError("input->storage_textures");
    }

    sampled_texture_slots = output->sampled_texture_slots;
    max_sampled_texture_slots = output->max_sampled_texture_slots;
    storage_textures = output->storage_textures;
    max_storage_textures = output->max_storage_textures;
    readonly_storage_textures = output->readonly_storage_textures;
    max_readonly_storage_textures = output->max_readonly_storage_textures;
    readwrite_storage_textures = output->readwrite_storage_textures;
    max_readwrite_storage_textures = output->max_readwrite_storage_textures;
    SDL_zero(*output);
    RestoreOutputBuffers(
        output,
        sampled_texture_slots,
        max_sampled_texture_slots,
        storage_textures,
        max_storage_textures,
        readonly_storage_textures,
        max_readonly_storage_textures,
        readwrite_storage_textures,
        max_readwrite_storage_textures);

    if (input->has_samplerless_sampled_image) {
        output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLERLESS_SAMPLED_IMAGE;
    }

    if (input->num_sampled_texture_slots > 0) {
        sampled_seen = (bool *)SDL_calloc(input->num_sampled_texture_slots, sizeof(*sampled_seen));
        if (sampled_seen == NULL) {
            return SDL_OutOfMemory();
        }
    }
    if (input->num_storage_textures > 0) {
        storage_seen = (bool *)SDL_calloc(input->num_storage_textures, sizeof(*storage_seen));
        if (storage_seen == NULL) {
            goto alloc_failed;
        }
    }
    if (input->num_readonly_storage_textures > 0) {
        readonly_seen = (bool *)SDL_calloc(input->num_readonly_storage_textures, sizeof(*readonly_seen));
        if (readonly_seen == NULL) {
            goto alloc_failed;
        }
    }
    if (input->num_readwrite_storage_textures > 0) {
        readwrite_seen = (bool *)SDL_calloc(input->num_readwrite_storage_textures, sizeof(*readwrite_seen));
        if (readwrite_seen == NULL) {
            goto alloc_failed;
        }
    }

    for (Uint32 i = 0; i < input->num_sampled_textures; i += 1) {
        const SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence = &input->sampled_textures[i];
        SDL_GPUSampledTextureSlotDescription slot = DefaultSampledTextureSlot();

        if (!evidence->has_sdl_slot) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_UNMAPPED;
            continue;
        }
        if (evidence->slot >= input->num_sampled_texture_slots) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_OUT_OF_RANGE;
            continue;
        }
        if (sampled_seen[evidence->slot]) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_DUPLICATE;
            continue;
        }
        sampled_seen[evidence->slot] = true;

        if (!MapSampledTextureSlot(evidence, &slot, &output->reasons, &output->notes)) {
            continue;
        }
        if (!SameSampledTextureSlot(slot, DefaultSampledTextureSlot())) {
            sampled_needs_layout_facts = true;
        }
    }

    for (Uint32 i = 0; i < input->num_storage_texture_entries; i += 1) {
        const SDL_ShaderCross_INTERNAL_StorageTextureEvidence *evidence = &input->storage_textures[i];
        SDL_GPUStorageTextureSlotDescription slot;
        SDL_GPUStorageTextureSlotDescription default_slot;
        Uint32 count = 0;
        bool *seen = NULL;

        if (!GetStorageTarget(input, evidence->resource_class, &count)) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED;
            continue;
        }
        switch (evidence->resource_class) {
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
            seen = storage_seen;
            break;
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
            seen = readonly_seen;
            break;
        case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
            seen = readwrite_seen;
            break;
        default:
            break;
        }

        if (!evidence->has_sdl_slot) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_UNMAPPED;
            continue;
        }
        if (evidence->slot >= count) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_OUT_OF_RANGE;
            continue;
        }
        if (seen != NULL && seen[evidence->slot]) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_DUPLICATE;
            continue;
        }
        if (seen != NULL) {
            seen[evidence->slot] = true;
        }

        slot = DefaultStorageTextureSlot(evidence->resource_class);
        if (!MapStorageTextureSlot(evidence, &slot, &output->reasons, &output->notes)) {
            continue;
        }
        default_slot = DefaultStorageTextureSlot(evidence->resource_class);
        if (!SameStorageTextureSlot(slot, default_slot)) {
            if (evidence->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE) {
                storage_needs_layout_facts = true;
            } else if (evidence->resource_class == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY) {
                readonly_needs_layout_facts = true;
            } else {
                readwrite_needs_layout_facts = true;
            }
        }
    }

    if (output->reasons == 0 && sampled_needs_layout_facts) {
        if (sampled_texture_slots == NULL || max_sampled_texture_slots < input->num_sampled_texture_slots) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED;
        } else {
            for (Uint32 i = 0; i < input->num_sampled_texture_slots; i += 1) {
                sampled_texture_slots[i] = DefaultSampledTextureSlot();
            }
            output->num_sampled_texture_slots = input->num_sampled_texture_slots;
        }
    }
    if (output->reasons == 0 && storage_needs_layout_facts) {
        if (storage_textures == NULL || max_storage_textures < input->num_storage_textures) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED;
        } else {
            for (Uint32 i = 0; i < input->num_storage_textures; i += 1) {
                storage_textures[i] = DefaultStorageTextureSlot(SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE);
            }
            output->num_storage_textures = input->num_storage_textures;
        }
    }
    if (output->reasons == 0 && readonly_needs_layout_facts) {
        if (readonly_storage_textures == NULL || max_readonly_storage_textures < input->num_readonly_storage_textures) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED;
        } else {
            for (Uint32 i = 0; i < input->num_readonly_storage_textures; i += 1) {
                readonly_storage_textures[i] = DefaultStorageTextureSlot(SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY);
            }
            output->num_readonly_storage_textures = input->num_readonly_storage_textures;
        }
    }
    if (output->reasons == 0 && readwrite_needs_layout_facts) {
        if (readwrite_storage_textures == NULL || max_readwrite_storage_textures < input->num_readwrite_storage_textures) {
            output->reasons |= SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED;
        } else {
            for (Uint32 i = 0; i < input->num_readwrite_storage_textures; i += 1) {
                readwrite_storage_textures[i] = DefaultStorageTextureSlot(SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE);
            }
            output->num_readwrite_storage_textures = input->num_readwrite_storage_textures;
        }
    }

    if (output->reasons == 0) {
        for (Uint32 i = 0; i < input->num_sampled_textures && output->num_sampled_texture_slots > 0; i += 1) {
            SDL_GPUSampledTextureSlotDescription slot = DefaultSampledTextureSlot();
            if (MapSampledTextureSlot(&input->sampled_textures[i], &slot, &output->reasons, &output->notes)) {
                sampled_texture_slots[input->sampled_textures[i].slot] = slot;
            }
        }
        for (Uint32 i = 0; i < input->num_storage_texture_entries; i += 1) {
            SDL_GPUStorageTextureSlotDescription slot;
            if (!MapStorageTextureSlot(&input->storage_textures[i], &slot, &output->reasons, &output->notes)) {
                continue;
            }
            switch (input->storage_textures[i].resource_class) {
            case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE:
                if (output->num_storage_textures > 0) {
                    storage_textures[input->storage_textures[i].slot] = slot;
                }
                break;
            case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY:
                if (output->num_readonly_storage_textures > 0) {
                    readonly_storage_textures[input->storage_textures[i].slot] = slot;
                }
                break;
            case SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE:
                if (output->num_readwrite_storage_textures > 0) {
                    readwrite_storage_textures[input->storage_textures[i].slot] = slot;
                }
                break;
            default:
                break;
            }
        }
    }

    output->accepted = output->reasons == 0;
    if (!output->accepted) {
        output->num_sampled_texture_slots = 0;
        output->num_storage_textures = 0;
        output->num_readonly_storage_textures = 0;
        output->num_readwrite_storage_textures = 0;
    }

    SDL_free(sampled_seen);
    SDL_free(storage_seen);
    SDL_free(readonly_seen);
    SDL_free(readwrite_seen);
    return true;

alloc_failed:
    SDL_free(sampled_seen);
    SDL_free(storage_seen);
    SDL_free(readonly_seen);
    SDL_free(readwrite_seen);
    return SDL_OutOfMemory();
}

static bool IsCIdentifierStart(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static bool IsCIdentifierContinue(char ch)
{
    return IsCIdentifierStart(ch) || (ch >= '0' && ch <= '9');
}

bool SDL_ShaderCross_INTERNAL_ValidateResourceLayoutCSymbolPrefix(const char *symbol_prefix)
{
    if (symbol_prefix == NULL || symbol_prefix[0] == '\0') {
        return SDL_InvalidParamError("symbol_prefix");
    }
    if (!IsCIdentifierStart(symbol_prefix[0])) {
        return SDL_SetError("C symbol prefix must start with an ASCII letter or underscore");
    }
    for (const char *ch = symbol_prefix + 1; *ch != '\0'; ch += 1) {
        if (!IsCIdentifierContinue(*ch)) {
            return SDL_SetError("C symbol prefix must contain only ASCII letters, digits, and underscores");
        }
    }
    return true;
}

static const char *TextureTypeName(SDL_GPUTextureType type)
{
    switch (type) {
    case SDL_GPU_TEXTURETYPE_2D:
        return "SDL_GPU_TEXTURETYPE_2D";
    case SDL_GPU_TEXTURETYPE_2D_ARRAY:
        return "SDL_GPU_TEXTURETYPE_2D_ARRAY";
    case SDL_GPU_TEXTURETYPE_3D:
        return "SDL_GPU_TEXTURETYPE_3D";
    case SDL_GPU_TEXTURETYPE_CUBE:
        return "SDL_GPU_TEXTURETYPE_CUBE";
    case SDL_GPU_TEXTURETYPE_CUBE_ARRAY:
        return "SDL_GPU_TEXTURETYPE_CUBE_ARRAY";
    default:
        return NULL;
    }
}

static const char *SampleTypeName(SDL_GPUShaderTextureSampleType type)
{
    switch (type) {
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT";
    case SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH:
        return "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH";
    default:
        return NULL;
    }
}

static const char *SamplerTypeName(SDL_GPUShaderSamplerType type)
{
    switch (type) {
    case SDL_GPU_SHADERSAMPLERTYPE_FILTERING:
        return "SDL_GPU_SHADERSAMPLERTYPE_FILTERING";
    case SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING:
        return "SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING";
    case SDL_GPU_SHADERSAMPLERTYPE_COMPARISON:
        return "SDL_GPU_SHADERSAMPLERTYPE_COMPARISON";
    case SDL_GPU_SHADERSAMPLERTYPE_NONE:
        return "SDL_GPU_SHADERSAMPLERTYPE_NONE";
    default:
        return NULL;
    }
}

static const char *StorageTextureFormatName(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
        return "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
        return "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
        return "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT:
        return "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
        return "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT";
    case SDL_GPU_TEXTUREFORMAT_R32_UINT:
        return "SDL_GPU_TEXTUREFORMAT_R32_UINT";
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT:
        return "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
        return "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT";
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
        return "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT";
    case SDL_GPU_TEXTUREFORMAT_R32_INT:
        return "SDL_GPU_TEXTUREFORMAT_R32_INT";
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
        return "SDL_GPU_TEXTUREFORMAT_R32_FLOAT";
    default:
        return NULL;
    }
}

static const char *StorageTextureAccessName(SDL_GPUStorageTextureAccess access)
{
    switch (access) {
    case SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY:
        return "SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY";
    case SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY:
        return "SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY";
    case SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE:
        return "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE";
    default:
        return NULL;
    }
}

static bool WriteIOPrintf(SDL_IOStream *output, const char *fmt, ...)
{
    va_list ap;
    int size;
    char *text = NULL;
    size_t bytes;

    va_start(ap, fmt);
    size = SDL_vasprintf(&text, fmt, ap);
    va_end(ap);
    if (size < 0) {
        return SDL_OutOfMemory();
    }

    bytes = SDL_WriteIO(output, text, (size_t)size);
    SDL_free(text);
    if (bytes != (size_t)size) {
        if (*SDL_GetError() == '\0') {
            SDL_SetError("failed to write SDL_GPU C initializer");
        }
        return false;
    }

    return true;
}

static bool WriteSampledTextureSlotArray(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const char *array_suffix,
    const SDL_GPUSampledTextureSlotDescription *slots,
    Uint32 count)
{
    if (count == 0) {
        return true;
    }
    if (slots == NULL) {
        return SDL_InvalidParamError("slots");
    }

    if (!WriteIOPrintf(output, "static const SDL_GPUSampledTextureSlotDescription %s_%s[] = {\n", symbol_prefix, array_suffix)) {
        return false;
    }
    for (Uint32 i = 0; i < count; i += 1) {
        const char *texture_type = TextureTypeName(slots[i].texture_type);
        const char *sample_type = SampleTypeName(slots[i].sample_type);
        const char *sampler_type = SamplerTypeName(slots[i].sampler_type);
        if (texture_type == NULL || sample_type == NULL || sampler_type == NULL) {
            return SDL_SetError("unsupported sampled texture resource layout slot");
        }
        if (!WriteIOPrintf(output, "    {\n") ||
            !WriteIOPrintf(output, "        .texture_type = %s,\n", texture_type) ||
            !WriteIOPrintf(output, "        .sample_type = %s,\n", sample_type) ||
            !WriteIOPrintf(output, "        .sampler_type = %s,\n", sampler_type) ||
            !WriteIOPrintf(output, "    }%s\n", i + 1 < count ? "," : "")) {
            return false;
        }
    }
    return WriteIOPrintf(output, "};\n\n");
}

static bool WriteStorageTextureSlotArray(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const char *array_suffix,
    const SDL_GPUStorageTextureSlotDescription *slots,
    Uint32 count)
{
    if (count == 0) {
        return true;
    }
    if (slots == NULL) {
        return SDL_InvalidParamError("slots");
    }

    if (!WriteIOPrintf(output, "static const SDL_GPUStorageTextureSlotDescription %s_%s[] = {\n", symbol_prefix, array_suffix)) {
        return false;
    }
    for (Uint32 i = 0; i < count; i += 1) {
        const char *texture_type = TextureTypeName(slots[i].texture_type);
        const char *format = StorageTextureFormatName(slots[i].format);
        const char *access = StorageTextureAccessName(slots[i].access);
        if (texture_type == NULL || format == NULL || access == NULL) {
            return SDL_SetError("unsupported storage texture resource layout slot");
        }
        if (!WriteIOPrintf(output, "    {\n") ||
            !WriteIOPrintf(output, "        .texture_type = %s,\n", texture_type) ||
            !WriteIOPrintf(output, "        .format = %s,\n", format) ||
            !WriteIOPrintf(output, "        .access = %s,\n", access) ||
            !WriteIOPrintf(output, "    }%s\n", i + 1 < count ? "," : "")) {
            return false;
        }
    }
    return WriteIOPrintf(output, "};\n\n");
}

static bool WriteLayoutArrayPointer(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const char *array_suffix,
    const char *pointer_field,
    Uint32 count)
{
    if (count == 0) {
        return true;
    }

    return WriteIOPrintf(output, "    .%s = %s_%s,\n", pointer_field, symbol_prefix, array_suffix);
}

static bool ValidateResourceLayoutCWriterInputs(
    SDL_IOStream *output,
    const char *symbol_prefix,
    SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *layout_facts)
{
    const bool shader_layout = kind == SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    const bool compute_layout = kind == SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;

    if (output == NULL) {
        return SDL_InvalidParamError("output");
    }
    if (layout_facts == NULL) {
        return SDL_InvalidParamError("layout_facts");
    }
    if (!SDL_ShaderCross_INTERNAL_ValidateResourceLayoutCSymbolPrefix(symbol_prefix)) {
        return false;
    }
    if (!layout_facts->accepted) {
        return SDL_SetError("cannot write rejected resource layout facts");
    }
    if (!shader_layout && !compute_layout) {
        return SDL_InvalidParamError("kind");
    }
    if (shader_layout && (layout_facts->num_readonly_storage_textures != 0 || layout_facts->num_readwrite_storage_textures != 0)) {
        return SDL_SetError("compute storage texture layout facts cannot be written as shader layout facts");
    }
    if (compute_layout && layout_facts->num_storage_textures != 0) {
        return SDL_SetError("shader storage texture layout facts cannot be written as compute layout facts");
    }

    return true;
}

static bool WriteResourceLayoutArrays(
    SDL_IOStream *output,
    const char *symbol_prefix,
    SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *layout_facts)
{
    const bool shader_layout = kind == SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;

    if (!WriteSampledTextureSlotArray(output, symbol_prefix, "sampled_texture_slots", layout_facts->sampled_texture_slots, layout_facts->num_sampled_texture_slots)) {
        return false;
    }
    if (shader_layout) {
        if (!WriteStorageTextureSlotArray(output, symbol_prefix, "storage_textures", layout_facts->storage_textures, layout_facts->num_storage_textures)) {
            return false;
        }
        return true;
    }

    if (!WriteStorageTextureSlotArray(output, symbol_prefix, "readonly_storage_textures", layout_facts->readonly_storage_textures, layout_facts->num_readonly_storage_textures)) {
        return false;
    }
    if (!WriteStorageTextureSlotArray(output, symbol_prefix, "readwrite_storage_textures", layout_facts->readwrite_storage_textures, layout_facts->num_readwrite_storage_textures)) {
        return false;
    }
    return true;
}

static const char *ShaderStageName(SDL_GPUShaderStage stage)
{
    switch (stage) {
    case SDL_GPU_SHADERSTAGE_VERTEX:
        return "SDL_GPU_SHADERSTAGE_VERTEX";
    case SDL_GPU_SHADERSTAGE_FRAGMENT:
        return "SDL_GPU_SHADERSTAGE_FRAGMENT";
    default:
        return NULL;
    }
}

static bool WriteShaderResourceLayout(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *layout_facts)
{
    const char *stage = ShaderStageName(layout->shader_stage);

    if (stage == NULL) {
        return SDL_SetError("unsupported shader stage for resource layout C output");
    }

    return WriteIOPrintf(output, "\nstatic const SDL_GPUShaderResourceLayout %s_resource_layout = {\n", symbol_prefix) &&
           WriteIOPrintf(output, "    .stage = %s,\n", stage) &&
           WriteIOPrintf(output, "    .num_samplers = %u,\n", (unsigned)layout->num_samplers) &&
           WriteIOPrintf(output, "    .num_storage_textures = %u,\n", (unsigned)layout->num_storage_textures) &&
           WriteIOPrintf(output, "    .num_storage_buffers = %u,\n", (unsigned)layout->num_storage_buffers) &&
           WriteIOPrintf(output, "    .num_uniform_buffers = %u,\n", (unsigned)layout->num_uniform_buffers) &&
           WriteLayoutArrayPointer(output, symbol_prefix, "sampled_texture_slots", "sampled_texture_slots", layout_facts->num_sampled_texture_slots) &&
           WriteLayoutArrayPointer(output, symbol_prefix, "storage_textures", "storage_texture_slots", layout_facts->num_storage_textures) &&
           WriteIOPrintf(output, "};\n");
}

static bool WriteComputeResourceLayout(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *layout_facts)
{
    return WriteIOPrintf(output, "\nstatic const SDL_GPUComputePipelineResourceLayout %s_resource_layout = {\n", symbol_prefix) &&
           WriteIOPrintf(output, "    .num_samplers = %u,\n", (unsigned)layout->num_samplers) &&
           WriteIOPrintf(output, "    .num_readonly_storage_textures = %u,\n", (unsigned)layout->num_readonly_storage_textures) &&
           WriteIOPrintf(output, "    .num_readonly_storage_buffers = %u,\n", (unsigned)layout->num_readonly_storage_buffers) &&
           WriteIOPrintf(output, "    .num_readwrite_storage_textures = %u,\n", (unsigned)layout->num_readwrite_storage_textures) &&
           WriteIOPrintf(output, "    .num_readwrite_storage_buffers = %u,\n", (unsigned)layout->num_readwrite_storage_buffers) &&
           WriteIOPrintf(output, "    .num_uniform_buffers = %u,\n", (unsigned)layout->num_uniform_buffers) &&
           WriteLayoutArrayPointer(output, symbol_prefix, "sampled_texture_slots", "sampled_texture_slots", layout_facts->num_sampled_texture_slots) &&
           WriteLayoutArrayPointer(output, symbol_prefix, "readonly_storage_textures", "readonly_storage_texture_slots", layout_facts->num_readonly_storage_textures) &&
           WriteLayoutArrayPointer(output, symbol_prefix, "readwrite_storage_textures", "readwrite_storage_texture_slots", layout_facts->num_readwrite_storage_textures) &&
           WriteIOPrintf(output, "};\n");
}

bool SDL_ShaderCross_INTERNAL_WriteResourceLayoutCInitializers(
    SDL_IOStream *output,
    const char *symbol_prefix,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *layout_facts)
{
    if (layout == NULL) {
        return SDL_InvalidParamError("layout");
    }
    if (!ValidateResourceLayoutCWriterInputs(output, symbol_prefix, layout->kind, layout_facts)) {
        return false;
    }

    if (!WriteResourceLayoutArrays(output, symbol_prefix, layout->kind, layout_facts)) {
        return false;
    }

    if (layout->kind == SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER) {
        return WriteShaderResourceLayout(output, symbol_prefix, layout, layout_facts);
    }
    return WriteComputeResourceLayout(output, symbol_prefix, layout, layout_facts);
}
