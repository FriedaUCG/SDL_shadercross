/*
  Simple DirectMedia Layer Shader Cross Compiler
  Copyright (C) 2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be liable for any damages
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

#include <SDL3/SDL.h>

#include "../src/SDL_shadercross_layout_facts.h"

static int failures;

static void Check(bool condition, const char *message)
{
    if (!condition) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FAIL: %s", message);
        failures += 1;
    }
}

static SDL_ShaderCross_INTERNAL_LayoutFactsOutput MakeOutput(
    SDL_GPUSampledTextureSlotDescription *sampled_texture_slots,
    Uint32 max_sampled_texture_slots,
    SDL_GPUStorageTextureSlotDescription *storage_textures,
    Uint32 max_storage_textures,
    SDL_GPUStorageTextureSlotDescription *readonly_storage_textures,
    Uint32 max_readonly_storage_textures,
    SDL_GPUStorageTextureSlotDescription *readwrite_storage_textures,
    Uint32 max_readwrite_storage_textures)
{
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_zero(output);
    output.sampled_texture_slots = sampled_texture_slots;
    output.max_sampled_texture_slots = max_sampled_texture_slots;
    output.storage_textures = storage_textures;
    output.max_storage_textures = max_storage_textures;
    output.readonly_storage_textures = readonly_storage_textures;
    output.max_readonly_storage_textures = max_readonly_storage_textures;
    output.readwrite_storage_textures = readwrite_storage_textures;
    output.max_readwrite_storage_textures = max_readwrite_storage_textures;
    return output;
}

static bool Map(
    const SDL_ShaderCross_INTERNAL_LayoutFactsInput *input,
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output)
{
    bool result = SDL_ShaderCross_INTERNAL_MapResourceLayoutToLayoutFacts(input, output);
    Check(result, "mapper call should complete");
    return result;
}

static bool WriteResourceLayoutC(
    char *buffer,
    size_t buffer_size,
    const char *symbol_prefix,
    const SDL_ShaderCross_INTERNAL_ResourceLayoutCounts *layout,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output)
{
    bool result;
    SDL_IOStream *stream = SDL_IOFromMem(buffer, buffer_size);

    Check(stream != NULL, "memory stream should be created for resource layout C output");
    if (stream == NULL) {
        return false;
    }

    result = SDL_ShaderCross_INTERNAL_WriteResourceLayoutCInitializers(stream, symbol_prefix, layout, output);
    SDL_CloseIO(stream);
    buffer[buffer_size - 1] = '\0';
    Check(result, "resource layout initializer writer should complete");
    return result;
}

static bool WriteShaderResourceLayoutCFromLayoutFacts(
    char *buffer,
    size_t buffer_size,
    const char *symbol_prefix,
    SDL_GPUShaderStage shader_stage,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output)
{
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;

    SDL_zero(layout);
    layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    layout.shader_stage = shader_stage;
    layout.num_samplers = output->max_sampled_texture_slots;
    layout.num_storage_textures = output->max_storage_textures;

    return WriteResourceLayoutC(buffer, buffer_size, symbol_prefix, &layout, output);
}

static bool WriteComputeResourceLayoutCFromLayoutFacts(
    char *buffer,
    size_t buffer_size,
    const char *symbol_prefix,
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output)
{
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;

    SDL_zero(layout);
    layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    layout.num_samplers = output->max_sampled_texture_slots;
    layout.num_readonly_storage_textures = output->max_readonly_storage_textures;
    layout.num_readwrite_storage_textures = output->max_readwrite_storage_textures;

    return WriteResourceLayoutC(buffer, buffer_size, symbol_prefix, &layout, output);
}

static SDL_ShaderCross_INTERNAL_SampledTextureEvidence MakeSampledTextureEvidence(
    bool has_sdl_slot,
    Uint32 slot,
    SDL_ShaderCross_INTERNAL_TextureDimension texture_dimension,
    SDL_ShaderCross_INTERNAL_SampledTextureSampleKind sample_kind,
    SDL_ShaderCross_INTERNAL_SamplerBindingKind sampler_binding,
    SDL_ShaderCross_INTERNAL_SamplerPolicy sampler_policy)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence evidence;
    SDL_zero(evidence);
    evidence.has_sdl_slot = has_sdl_slot;
    evidence.slot = slot;
    evidence.texture_dimension = texture_dimension;
    evidence.sample_kind = sample_kind;
    evidence.sampler_binding = sampler_binding;
    evidence.sampler_policy = sampler_policy;
    return evidence;
}

static SDL_ShaderCross_INTERNAL_StorageTextureEvidence MakeStorageTextureEvidence(
    bool has_sdl_slot,
    Uint32 slot,
    SDL_ShaderCross_INTERNAL_StorageTextureResourceClass resource_class,
    SDL_ShaderCross_INTERNAL_TextureDimension texture_dimension,
    SDL_ShaderCross_INTERNAL_StorageTextureFormat format,
    SDL_ShaderCross_INTERNAL_StorageTextureAccess access)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence evidence;
    SDL_zero(evidence);
    evidence.has_sdl_slot = has_sdl_slot;
    evidence.slot = slot;
    evidence.resource_class = resource_class;
    evidence.texture_dimension = texture_dimension;
    evidence.format = format;
    evidence.final_access = access;
    evidence.observed_access = access;
    return evidence;
}

static void SetExplicitSampledTexturePolicy(
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence *evidence,
    SDL_GPUTextureType texture_type,
    SDL_GPUShaderTextureSampleType sample_type,
    SDL_GPUShaderSamplerType sampler_type)
{
    evidence->has_explicit_slot_policy = true;
    evidence->explicit_slot_policy.texture_type = texture_type;
    evidence->explicit_slot_policy.sample_type = sample_type;
    evidence->explicit_slot_policy.sampler_type = sampler_type;
}

static void CheckContains(const char *text, const char *needle, const char *message)
{
    Check(SDL_strstr(text, needle) != NULL, message);
}

static void CheckNotContains(const char *text, const char *needle, const char *message)
{
    Check(SDL_strstr(text, needle) == NULL, message);
}

static void CheckRejectedOutput(
    const SDL_ShaderCross_INTERNAL_LayoutFactsOutput *output,
    Uint64 expected_reasons,
    const char *message)
{
    Check(!output->accepted, message);
    Check(output->reasons == expected_reasons, "rejected layout facts should match the expected reason bits");
    Check(output->num_sampled_texture_slots == 0, "rejected output should not publish sampled texture layout_facts");
    Check(output->num_storage_textures == 0, "rejected output should not publish graphics storage texture layout_facts");
    Check(output->num_readonly_storage_textures == 0, "rejected output should not publish read-only storage texture layout_facts");
    Check(output->num_readwrite_storage_textures == 0, "rejected output should not publish read-write storage texture layout_facts");
}

static void TestStorageTextureDecisionResolver(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence reflected =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE);
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence observed_write =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE);
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence explicit_format =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY);
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence bad_kind =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY);
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence authority_array =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY);
    SDL_ShaderCross_INTERNAL_StorageTextureDecision decision;

    SDL_zero(decision);
    Check(SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&reflected, &decision), "reflected storage texture decision should resolve");
    Check(decision.accepted, "reflected read-write storage texture decision should be accepted");
    Check(decision.slot.texture_type == SDL_GPU_TEXTURETYPE_2D, "reflected decision should map texture type");
    Check(decision.slot.format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT, "reflected decision should map format");
    Check(decision.slot.access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "reflected decision should map access");
    Check(decision.format_authority == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED, "reflected decision should name reflected format authority");
    Check(decision.access_authority == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_REFLECTED, "reflected decision should name reflected access authority");

    observed_write.observed_access = SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY;
    SDL_zero(decision);
    Check(SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&observed_write, &decision), "observed-write storage texture decision should resolve");
    Check(decision.accepted, "observed-write storage texture decision should be accepted");
    Check(decision.slot.access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY, "observed write-only use should narrow read-write reflection to write-only");
    Check(decision.access_authority == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_OBSERVED_WRITE, "observed-write decision should name observed access authority");

    explicit_format.has_explicit_slot_policy = true;
    explicit_format.explicit_slot_policy_format_authority = true;
    explicit_format.explicit_slot_policy.texture_type = SDL_GPU_TEXTURETYPE_2D;
    explicit_format.explicit_slot_policy.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    explicit_format.explicit_slot_policy.access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY;
    SDL_zero(decision);
    Check(SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&explicit_format, &decision), "explicit-format storage texture decision should resolve");
    Check(decision.accepted, "explicit same-kind format authority should be accepted");
    Check(decision.slot.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, "explicit format authority should choose the policy format");
    Check(decision.format_authority == SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_DECISION_AUTHORITY_EXPLICIT_FORMAT_POLICY, "explicit format authority should be recorded");
    Check((decision.notes & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_STORAGE_TEXTURE_FORMAT_AUTHORITY) != 0, "explicit format authority should emit a layout fact note");

    bad_kind.has_explicit_slot_policy = true;
    bad_kind.explicit_slot_policy_format_authority = true;
    bad_kind.explicit_slot_policy.texture_type = SDL_GPU_TEXTURETYPE_2D;
    bad_kind.explicit_slot_policy.format = SDL_GPU_TEXTUREFORMAT_R32_UINT;
    bad_kind.explicit_slot_policy.access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY;
    SDL_zero(decision);
    Check(SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&bad_kind, &decision), "bad-kind storage texture decision should resolve as a rejection");
    Check(!decision.accepted, "explicit numeric-kind mismatch should be rejected");
    Check((decision.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH) != 0, "numeric-kind mismatch should report policy mismatch");

    authority_array.has_explicit_slot_policy = true;
    authority_array.explicit_slot_policy_format_authority = true;
    authority_array.explicit_slot_policy.texture_type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    authority_array.explicit_slot_policy.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    authority_array.explicit_slot_policy.access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY;
    SDL_zero(decision);
    Check(SDL_ShaderCross_INTERNAL_ResolveStorageTextureSlotDecision(&authority_array, &decision), "non-2D format authority decision should resolve as a rejection");
    Check(!decision.accepted, "format authority should reject non-2D storage texture policy at the shared resolver");
    Check((decision.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_POLICY_MISMATCH) != 0, "non-2D format authority should report policy mismatch");
}

static void TestEmptyShaderEmitsNoLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    output = MakeOutput(NULL, 0, NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "empty shader layout facts should be accepted");
    Check(output.reasons == 0, "empty shader should have no rejection reasons");
    Check(output.num_sampled_texture_slots == 0, "empty shader should not emit sampled slots");
    Check(output.num_storage_textures == 0, "empty shader should not emit storage texture slots");
}

static void TestMixedSampledTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
        MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT),
        MakeSampledTextureEvidence(true, 2, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[3];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "mixed sampled texture layout facts should be accepted");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "mixed sampled class should emit the complete slot array");
    Check((output.notes & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_FLOAT_SAMPLER_POLICY) != 0, "ambiguous float sampler policy should be noted");
    Check(sampled_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D, "default sampled slot should remain 2D");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT, "default sampled slot should remain filterable float");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_FILTERING, "default sampled slot should remain filtering");
    Check(sampled_slots[1].texture_type == SDL_GPU_TEXTURETYPE_3D, "3D float sampled slot should map to 3D texture type");
    Check(sampled_slots[1].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT, "3D float sampled slot should map to filterable float");
    Check(sampled_slots[1].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_FILTERING, "3D float sampled slot should resolve to filtering sampler");
    Check(sampled_slots[2].texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY, "depth comparison slot should keep 2D array type");
    Check(sampled_slots[2].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH, "depth comparison slot should map to depth sample type");
    Check(sampled_slots[2].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_COMPARISON, "depth comparison slot should map to comparison sampler");
}

static void TestNonFilteringSampledTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT),
        MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[4096];

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "0021 sampled texture layout facts should be accepted");
    Check(output.notes == 0, "explicit non-filtering sampled texture layout facts should not emit an ambiguity note");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "0021 sampled layout facts should emit the complete slot array");
    Check(sampled_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY, "non-filtering float sampled slot should keep 2D array type");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT, "non-filtering float sampled slot should map to filterable float");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING, "non-filtering float sampled slot should map to non-filtering sampler");
    Check(sampled_slots[1].texture_type == SDL_GPU_TEXTURETYPE_2D, "normal depth sampled slot should keep 2D type");
    Check(sampled_slots[1].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH, "normal depth sampled slot should map to depth sample type");
    Check(sampled_slots[1].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING, "normal depth sampled slot should map to non-filtering sampler");

    if (!WriteShaderResourceLayoutCFromLayoutFacts(c_output, sizeof(c_output), "nonfiltering_shader", SDL_GPU_SHADERSTAGE_FRAGMENT, &output)) {
        return;
    }
    CheckContains(c_output, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING", "resource layout C output should emit non-filtering sampler layout_facts");
}

static void TestDepthCubeSampledTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON),
        MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[8192];

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "0060 depth cube sampled texture layout facts should be accepted");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "0060 depth cube layout facts should emit the complete slot array");
    Check(sampled_slots[0].texture_type == SDL_GPU_TEXTURETYPE_CUBE, "depth comparison cube slot should preserve cube type");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH, "depth comparison cube slot should map to depth sample type");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_COMPARISON, "depth comparison cube slot should map to comparison sampler");
    Check(sampled_slots[1].texture_type == SDL_GPU_TEXTURETYPE_CUBE_ARRAY, "non-filtering depth cube-array slot should preserve cube-array type");
    Check(sampled_slots[1].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH, "non-filtering depth cube-array slot should map to depth sample type");
    Check(sampled_slots[1].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING, "non-filtering depth cube-array slot should map to non-filtering sampler");

    {
        SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;

        SDL_zero(layout);
        SDL_zeroa(c_output);
        layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
        layout.shader_stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        layout.num_samplers = SDL_arraysize(sampled_slots);

        if (!WriteResourceLayoutC(c_output, sizeof(c_output), "depth_cube_layout", &layout, &output)) {
            return;
        }

        CheckContains(c_output, "static const SDL_GPUShaderResourceLayout depth_cube_layout_resource_layout", "depth cube layout C output should emit shader layout facts");
        CheckContains(c_output, ".sampled_texture_slots = depth_cube_layout_sampled_texture_slots", "depth cube layout C output should reference sampled texture slots directly");
        CheckContains(c_output, ".texture_type = SDL_GPU_TEXTURETYPE_CUBE", "depth cube layout C output should preserve cube type");
        CheckContains(c_output, ".texture_type = SDL_GPU_TEXTURETYPE_CUBE_ARRAY", "depth cube layout C output should preserve cube-array type");
    }
}

static void TestSamplerlessIntegerSampledTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN),
        MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[4096];

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "0023 samplerless integer sampled texture layout facts should be accepted");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "0023 samplerless sampled layout facts should emit the complete slot array");
    Check(sampled_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D, "samplerless signed integer slot should keep 2D type");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT, "samplerless signed integer slot should map to SINT sample type");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONE, "samplerless signed integer slot should map to NONE sampler type");
    Check(sampled_slots[1].texture_type == SDL_GPU_TEXTURETYPE_3D, "samplerless unsigned integer slot should keep 3D type");
    Check(sampled_slots[1].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT, "samplerless unsigned integer slot should map to UINT sample type");
    Check(sampled_slots[1].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONE, "samplerless unsigned integer slot should map to NONE sampler type");

    if (!WriteShaderResourceLayoutCFromLayoutFacts(c_output, sizeof(c_output), "samplerless_integer_shader", SDL_GPU_SHADERSTAGE_FRAGMENT, &output)) {
        return;
    }
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT", "resource layout C output should emit signed integer sample layout_facts");
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT", "resource layout C output should emit unsigned integer sample layout_facts");
    CheckContains(c_output, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE", "resource layout C output should emit samplerless layout_facts");
}

static void TestMultisampledSampledTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        {
            .has_sdl_slot = true,
            .slot = 0,
            .texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
            .sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT,
            .sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE,
            .sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
            .multisampled = true,
            .image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH
        },
        {
            .has_sdl_slot = true,
            .slot = 1,
            .texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
            .sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH,
            .sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE,
            .sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
            .multisampled = true,
            .image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_DEPTH
        }
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[4096];

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "0036 MSAA sampled texture layout facts should be accepted for concrete SPIR-V depth facts");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "0036 MSAA sampled layout facts should emit the complete slot array");
    Check(sampled_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D, "MSAA color sampled slot should keep 2D type");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT, "MSAA color sampled slot should map to multisampled unfilterable float");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONE, "MSAA color sampled slot should be samplerless");
    Check(sampled_slots[1].texture_type == SDL_GPU_TEXTURETYPE_2D, "MSAA depth sampled slot should keep 2D type");
    Check(sampled_slots[1].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH, "MSAA depth sampled slot should map to multisampled depth");
    Check(sampled_slots[1].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONE, "MSAA depth sampled slot should be samplerless");

    if (!WriteShaderResourceLayoutCFromLayoutFacts(c_output, sizeof(c_output), "msaa_sampled_shader", SDL_GPU_SHADERSTAGE_FRAGMENT, &output)) {
        return;
    }
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT", "resource layout C output should emit MSAA color sample layout_facts");
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH", "resource layout C output should emit MSAA depth sample layout_facts");
    CheckContains(c_output, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE", "resource layout C output should emit samplerless MSAA layout_facts");
}

static void TestExplicitSampledSlotPolicyLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT),
        {
            .has_sdl_slot = true,
            .slot = 1,
            .texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
            .sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT,
            .sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE,
            .sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
            .multisampled = true,
            .image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN
        },
        {
            .has_sdl_slot = true,
            .slot = 2,
            .texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
            .sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT,
            .sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE,
            .sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
            .multisampled = true,
            .image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN
        }
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[3];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[4096];

    SetExplicitSampledTexturePolicy(
        &sampled[0],
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT,
        SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING);
    SetExplicitSampledTexturePolicy(
        &sampled[1],
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT,
        SDL_GPU_SHADERSAMPLERTYPE_NONE);
    SetExplicitSampledTexturePolicy(
        &sampled[2],
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH,
        SDL_GPU_SHADERSAMPLERTYPE_NONE);

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "explicit sampled slot policy layout facts should be accepted when it matches reflection evidence");
    Check((output.notes & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_NOTE_RESOLVED_SAMPLED_TEXTURE_SLOT_POLICY) != 0, "explicit sampled slot policy should be noted");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "explicit sampled slot policy should emit the complete slot array");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT, "explicit policy should map ordinary unfilterable float");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING, "explicit unfilterable float policy should use non-filtering sampler");
    Check(sampled_slots[1].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT, "explicit policy should resolve ambiguous MSAA color");
    Check(sampled_slots[2].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH, "explicit policy should resolve ambiguous MSAA depth");

    if (!WriteShaderResourceLayoutCFromLayoutFacts(c_output, sizeof(c_output), "explicit_policy_shader", SDL_GPU_SHADERSTAGE_FRAGMENT, &output)) {
        return;
    }
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT", "resource layout C output should emit ordinary unfilterable float sample layout_facts");
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT", "resource layout C output should emit explicit MSAA color layout_facts");
    CheckContains(c_output, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH", "resource layout C output should emit explicit MSAA depth layout_facts");
}

static void TestExplicitSampledSlotPolicyRejections(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled = {
        .has_sdl_slot = true,
        .slot = 0,
        .texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
        .sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT,
        .sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE,
        .sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
        .multisampled = true,
        .image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH
    };
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_GPUSampledTextureSlotDescription sampled_slots[1];

    SetExplicitSampledTexturePolicy(
        &sampled,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH,
        SDL_GPU_SHADERSAMPLERTYPE_NONE);

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = &sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH, "explicit MSAA depth policy should reject concrete color evidence");

    sampled = MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN);
    SetExplicitSampledTexturePolicy(
        &sampled,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT,
        SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING);
    input.sampled_textures = &sampled;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH, "explicit unfilterable float policy should reject integer evidence");

    sampled = MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_UNKNOWN, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING_OR_NONFILTERING_FLOAT);
    SetExplicitSampledTexturePolicy(
        &sampled,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT,
        SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING);
    input.sampled_textures = &sampled;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_POLICY_MISMATCH, "explicit sampled slot policy should reject texture-type mismatch");
}

static void TestMultisampledSampledTextureRejections(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence ambiguous = {
        .has_sdl_slot = true,
        .slot = 0,
        .texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D,
        .sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT,
        .sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE,
        .sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN,
        .multisampled = true,
        .image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_UNKNOWN
    };
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence arrayed = ambiguous;
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence paired = ambiguous;
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence integer = ambiguous;
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_GPUSampledTextureSlotDescription sampled_slots[1];

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = &ambiguous;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DEPTH_AMBIGUOUS, "ambiguous MSAA image depth should be rejected");

    arrayed.texture_dimension = SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY;
    arrayed.image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH;
    input.sampled_textures = &arrayed;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED, "arrayed MSAA sampled texture layout facts should be rejected");

    paired.sampler_binding = SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING;
    paired.sampler_policy = SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING;
    paired.image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH;
    input.sampled_textures = &paired;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED, "paired MSAA sampled texture layout facts should be rejected");

    integer.sample_kind = SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT;
    integer.image_depth = SDL_SHADERCROSS_INTERNAL_IMAGE_DEPTH_NOT_DEPTH;
    input.sampled_textures = &integer;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    if (!Map(&input, &output)) {
        return;
    }
    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED, "integer MSAA sampled texture layout facts should be rejected until a carrier shape exists");
}

static void TestSparseSampledTextureClassEmitsDefaults(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled = MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING);
    SDL_GPUSampledTextureSlotDescription sampled_slots[3];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    SDL_zeroa(sampled_slots);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = &sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "sparse non-default sampled class should be accepted");
    Check(output.num_sampled_texture_slots == SDL_arraysize(sampled_slots), "sparse sampled class should emit the complete slot array");
    Check(sampled_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D, "sparse sampled gap before non-default slot should default to 2D");
    Check(sampled_slots[0].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT, "sparse sampled gap before non-default slot should default to filterable float");
    Check(sampled_slots[0].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_FILTERING, "sparse sampled gap before non-default slot should default to filtering");
    Check(sampled_slots[1].texture_type == SDL_GPU_TEXTURETYPE_3D, "sparse sampled non-default slot should map to 3D");
    Check(sampled_slots[2].texture_type == SDL_GPU_TEXTURETYPE_2D, "sparse sampled gap after non-default slot should default to 2D");
    Check(sampled_slots[2].sample_type == SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT, "sparse sampled gap after non-default slot should default to filterable float");
    Check(sampled_slots[2].sampler_type == SDL_GPU_SHADERSAMPLERTYPE_FILTERING, "sparse sampled gap after non-default slot should default to filtering");
}

static void TestShaderResourceLayoutLayoutFactsOutput(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled = MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING);
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[4096];

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = &sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output) ||
        !WriteShaderResourceLayoutCFromLayoutFacts(c_output, sizeof(c_output), "shader_case", SDL_GPU_SHADERSTAGE_FRAGMENT, &output)) {
        return;
    }

    CheckContains(c_output, "static const SDL_GPUSampledTextureSlotDescription shader_case_sampled_texture_slots[]", "shader layout C output should emit sampled texture slot array");
    CheckContains(c_output, ".texture_type = SDL_GPU_TEXTURETYPE_3D", "shader layout C output should include 3D texture layout_facts");
    CheckContains(c_output, "static const SDL_GPUShaderResourceLayout shader_case_resource_layout", "shader layout C output should emit shader layout facts");
    CheckNotContains(c_output, ".version = sizeof(SDL_GPU", "shader layout C output should not emit SDL_GPU version initializers");
    CheckContains(c_output, ".sampled_texture_slots = shader_case_sampled_texture_slots", "shader layout C output should reference sampled texture slots");
}

static void TestComputeStorageTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_textures[] = {
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 2, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 3, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 4, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 5, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 6, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 7, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 8, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 9, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 10, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 11, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 12, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 2, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 3, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 4, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 5, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 6, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 7, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 8, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 9, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 10, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 11, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 12, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 13, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 14, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 15, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 16, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 17, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 18, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 19, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 20, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 21, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 22, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 23, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE)
    };
    static const SDL_GPUTextureFormat expected_broad_writeonly_formats[] = {
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT
    };
    static const SDL_GPUTextureFormat expected_broad_readonly_formats[] = {
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT
    };
    static const SDL_GPUTextureType expected_broad_readonly_types[] = {
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY
    };
    static const SDL_GPUTextureFormat expected_broad_readwrite_formats[] = {
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT
    };
    SDL_GPUStorageTextureSlotDescription readonly_slots[13];
    SDL_GPUStorageTextureSlotDescription readwrite_slots[24];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readonly_storage_textures = SDL_arraysize(readonly_slots);
    input.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    input.storage_textures = storage_textures;
    input.num_storage_texture_entries = SDL_arraysize(storage_textures);
    output = MakeOutput(NULL, 0, NULL, 0, readonly_slots, SDL_arraysize(readonly_slots), readwrite_slots, SDL_arraysize(readwrite_slots));

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "compute storage texture layout facts should be accepted");
    Check(output.num_readonly_storage_textures == SDL_arraysize(readonly_slots), "non-default read-only storage texture class should emit layout_facts");
    Check(readonly_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D, "default read-only storage texture should stay 2D");
    Check(readonly_slots[0].format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, "default read-only storage texture should stay rgba8unorm");
    Check(readonly_slots[1].texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY, "read-only storage texture should map to 2D-array");
    Check(readonly_slots[1].format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, "read-only array storage texture should map to rgba8unorm");
    Check(readonly_slots[1].access == SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY, "read-only array storage texture should preserve read-only access");
    Check(readonly_slots[2].texture_type == SDL_GPU_TEXTURETYPE_3D, "read-only storage texture should map to 3D");
    Check(readonly_slots[2].format == SDL_GPU_TEXTUREFORMAT_R32_UINT, "read-only storage texture should map to R32_UINT");
    Check(readonly_slots[2].access == SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY, "read-only R32_UINT storage texture should preserve read-only access");
    Check(readonly_slots[3].texture_type == SDL_GPU_TEXTURETYPE_2D_ARRAY, "read-only signed storage texture should keep 2D-array type");
    Check(readonly_slots[3].format == SDL_GPU_TEXTUREFORMAT_R32_INT, "read-only signed storage texture should map to R32_INT");
    Check(readonly_slots[3].access == SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY, "read-only R32_INT storage texture should preserve read-only access");
    Check(readonly_slots[4].texture_type == SDL_GPU_TEXTURETYPE_3D, "read-only float storage texture should keep 3D type");
    Check(readonly_slots[4].format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT, "read-only float storage texture should map to R32_FLOAT");
    Check(readonly_slots[4].access == SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY, "read-only R32_FLOAT storage texture should preserve read-only access");
    for (Uint32 i = 0; i < SDL_arraysize(expected_broad_readonly_formats); i += 1) {
        Check(readonly_slots[5 + i].texture_type == expected_broad_readonly_types[i], "broad read-only storage texture should preserve its texture type");
        Check(readonly_slots[5 + i].format == expected_broad_readonly_formats[i], "broad read-only storage texture should map to the expected format");
        Check(readonly_slots[5 + i].access == SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY, "broad read-only storage texture should preserve read-only access");
    }
    Check(output.num_readwrite_storage_textures == SDL_arraysize(readwrite_slots), "non-default read-write storage texture should emit layout_facts");
    Check(readwrite_slots[0].texture_type == SDL_GPU_TEXTURETYPE_2D, "read-write storage texture should map to 2D");
    Check(readwrite_slots[0].format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, "read-write storage texture should map to rgba8unorm");
    Check(readwrite_slots[0].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "read-write storage texture should preserve read-write access");
    Check(readwrite_slots[1].texture_type == SDL_GPU_TEXTURETYPE_2D, "write-only scalar storage texture should map to 2D");
    Check(readwrite_slots[1].format == SDL_GPU_TEXTUREFORMAT_R32_UINT, "write-only scalar storage texture should map to R32_UINT");
    Check(readwrite_slots[1].access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY, "write-only scalar storage texture should preserve write-only access");
    Check(readwrite_slots[2].texture_type == SDL_GPU_TEXTURETYPE_2D, "read-write signed scalar storage texture should map to 2D");
    Check(readwrite_slots[2].format == SDL_GPU_TEXTUREFORMAT_R32_INT, "read-write signed scalar storage texture should map to R32_INT");
    Check(readwrite_slots[2].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "read-write signed scalar storage texture should preserve read-write access");
    Check(readwrite_slots[3].texture_type == SDL_GPU_TEXTURETYPE_2D, "read-write float scalar storage texture should map to 2D");
    Check(readwrite_slots[3].format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT, "read-write float scalar storage texture should map to R32_FLOAT");
    Check(readwrite_slots[3].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "read-write float scalar storage texture should preserve read-write access");
    Check(readwrite_slots[4].texture_type == SDL_GPU_TEXTURETYPE_3D, "write-only 3D storage texture should map to 3D");
    Check(readwrite_slots[4].format == SDL_GPU_TEXTUREFORMAT_R32_UINT, "write-only 3D storage texture should map to R32_UINT");
    Check(readwrite_slots[4].access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY, "write-only 3D storage texture should preserve write-only access");
    Check(readwrite_slots[5].texture_type == SDL_GPU_TEXTURETYPE_3D, "read-write 3D storage texture should map to 3D");
    Check(readwrite_slots[5].format == SDL_GPU_TEXTUREFORMAT_R32_UINT, "read-write 3D storage texture should map to R32_UINT");
    Check(readwrite_slots[5].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "read-write 3D storage texture should preserve read-write access");
    Check(readwrite_slots[6].texture_type == SDL_GPU_TEXTURETYPE_3D, "write-only signed 3D storage texture should map to 3D");
    Check(readwrite_slots[6].format == SDL_GPU_TEXTUREFORMAT_R32_INT, "write-only signed 3D storage texture should map to R32_INT");
    Check(readwrite_slots[6].access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY, "write-only signed 3D storage texture should preserve write-only access");
    Check(readwrite_slots[7].texture_type == SDL_GPU_TEXTURETYPE_3D, "read-write signed 3D storage texture should map to 3D");
    Check(readwrite_slots[7].format == SDL_GPU_TEXTUREFORMAT_R32_INT, "read-write signed 3D storage texture should map to R32_INT");
    Check(readwrite_slots[7].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "read-write signed 3D storage texture should preserve read-write access");
    Check(readwrite_slots[8].texture_type == SDL_GPU_TEXTURETYPE_3D, "write-only float 3D storage texture should map to 3D");
    Check(readwrite_slots[8].format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT, "write-only float 3D storage texture should map to R32_FLOAT");
    Check(readwrite_slots[8].access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY, "write-only float 3D storage texture should preserve write-only access");
    Check(readwrite_slots[9].texture_type == SDL_GPU_TEXTURETYPE_3D, "read-write float 3D storage texture should map to 3D");
    Check(readwrite_slots[9].format == SDL_GPU_TEXTUREFORMAT_R32_FLOAT, "read-write float 3D storage texture should map to R32_FLOAT");
    Check(readwrite_slots[9].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "read-write float 3D storage texture should preserve read-write access");
    for (Uint32 i = 0; i < SDL_arraysize(expected_broad_writeonly_formats); i += 1) {
        Check(readwrite_slots[10 + i].texture_type == SDL_GPU_TEXTURETYPE_2D, "broad write-only storage texture should map to 2D");
        Check(readwrite_slots[10 + i].format == expected_broad_writeonly_formats[i], "broad write-only storage texture should map to the expected format");
        Check(readwrite_slots[10 + i].access == SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY, "broad write-only storage texture should preserve write-only access");
    }
    for (Uint32 i = 0; i < SDL_arraysize(expected_broad_readwrite_formats); i += 1) {
        Check(readwrite_slots[18 + i].texture_type == SDL_GPU_TEXTURETYPE_2D, "broad read-write storage texture should map to 2D");
        Check(readwrite_slots[18 + i].format == expected_broad_readwrite_formats[i], "broad read-write storage texture should map to the expected format");
        Check(readwrite_slots[18 + i].access == SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE, "broad read-write storage texture should preserve read-write access");
    }
}

static void TestFragmentWritableStorageTextureLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence writeonly =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY);
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence readwrite =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_FRAGMENT_WRITABLE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE);
    SDL_GPUStorageTextureSlotDescription slot;

    SDL_zero(slot);
    SDL_ClearError();
    Check(!SDL_ShaderCross_INTERNAL_MapStorageTextureSlotDescription(&writeonly, &slot), "fragment writable 2D-array write-only storage texture should not map to SDL layout facts");
    Check(SDL_strstr(SDL_GetError(), "storage texture slot layout facts are outside") != NULL, "fragment write-only storage texture rejection should explain unsupported layout facts");

    SDL_zero(slot);
    SDL_ClearError();
    Check(!SDL_ShaderCross_INTERNAL_MapStorageTextureSlotDescription(&readwrite, &slot), "fragment writable 2D-array read-write storage texture should not map to SDL layout facts");
    Check(SDL_strstr(SDL_GetError(), "storage texture slot layout facts are outside") != NULL, "fragment read-write storage texture rejection should explain unsupported layout facts");
}

static void TestGraphicsStorageTextureLayoutFacts(void)
{
    static const SDL_GPUTextureType expected_texture_types[] = {
        SDL_GPU_TEXTURETYPE_2D_ARRAY,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY,
        SDL_GPU_TEXTURETYPE_3D,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTURETYPE_2D_ARRAY
    };
    static const SDL_GPUTextureFormat expected_formats[] = {
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREFORMAT_R32_UINT,
        SDL_GPU_TEXTUREFORMAT_R32_INT,
        SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT
    };
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_textures[] = {
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 2, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 3, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 4, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 5, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 6, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 7, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32B32A32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 8, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 9, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 10, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 11, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY)
    };
    SDL_GPUStorageTextureSlotDescription storage_slots[12];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    Uint32 i;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_storage_textures = SDL_arraysize(storage_slots);
    input.storage_textures = storage_textures;
    input.num_storage_texture_entries = SDL_arraysize(storage_textures);
    output = MakeOutput(NULL, 0, storage_slots, SDL_arraysize(storage_slots), NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "graphics storage texture layout facts should be accepted");
    Check(output.num_storage_textures == SDL_arraysize(storage_slots), "graphics storage texture layout facts should emit full slot array");
    Check(SDL_arraysize(storage_slots) == SDL_arraysize(expected_texture_types), "graphics storage texture test should keep expected type count aligned");
    Check(SDL_arraysize(storage_slots) == SDL_arraysize(expected_formats), "graphics storage texture test should keep expected format count aligned");
    for (i = 0; i < SDL_arraysize(storage_slots); i += 1) {
        Check(storage_slots[i].texture_type == expected_texture_types[i], "graphics storage texture should preserve the expected texture type");
        Check(storage_slots[i].format == expected_formats[i], "graphics storage texture should map to the expected format");
        Check(storage_slots[i].access == SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY, "graphics storage texture should preserve read-only access");
    }
}

static void TestDefaultComputeWriteOnlyStorageTextureEmitsNoLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_texture =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY);
    SDL_GPUStorageTextureSlotDescription readwrite_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    input.storage_textures = &storage_texture;
    input.num_storage_texture_entries = 1;
    output = MakeOutput(NULL, 0, NULL, 0, NULL, 0, readwrite_slots, SDL_arraysize(readwrite_slots));

    if (!Map(&input, &output)) {
        return;
    }

    Check(output.accepted, "default compute write-only storage texture should be accepted");
    Check(output.num_readwrite_storage_textures == 0, "default compute write-only storage texture should not emit layout_facts");
}

static void TestComputeResourceLayoutLayoutFactsOutput(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_textures[] = {
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
        MakeStorageTextureEvidence(true, 2, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE)
    };
    SDL_GPUStorageTextureSlotDescription readonly_slots[1];
    SDL_GPUStorageTextureSlotDescription readwrite_slots[3];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    char c_output[8192];

    SDL_zero(input);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readonly_storage_textures = SDL_arraysize(readonly_slots);
    input.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    input.storage_textures = storage_textures;
    input.num_storage_texture_entries = SDL_arraysize(storage_textures);
    output = MakeOutput(NULL, 0, NULL, 0, readonly_slots, SDL_arraysize(readonly_slots), readwrite_slots, SDL_arraysize(readwrite_slots));

    if (!Map(&input, &output) ||
        !WriteComputeResourceLayoutCFromLayoutFacts(c_output, sizeof(c_output), "compute_case", &output)) {
        return;
    }

    CheckContains(c_output, "static const SDL_GPUStorageTextureSlotDescription compute_case_readonly_storage_textures[]", "compute layout C output should emit read-only storage texture slot array");
    CheckContains(c_output, ".texture_type = SDL_GPU_TEXTURETYPE_3D", "compute layout C output should include 3D read-only storage texture layout_facts");
    CheckContains(c_output, ".format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT", "compute layout C output should include R32_FLOAT storage texture layout_facts");
    CheckContains(c_output, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY", "compute layout C output should include read-only storage texture access");
    CheckContains(c_output, "static const SDL_GPUStorageTextureSlotDescription compute_case_readwrite_storage_textures[]", "compute layout C output should emit read-write storage texture slot array");
    CheckContains(c_output, ".texture_type = SDL_GPU_TEXTURETYPE_3D", "compute layout C output should include 3D read-write storage texture layout_facts");
    CheckContains(c_output, ".format = SDL_GPU_TEXTUREFORMAT_R32_UINT", "compute layout C output should include writable R32_UINT storage texture layout_facts");
    CheckContains(c_output, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "compute layout C output should include read-write access layout_facts");
    CheckContains(c_output, ".format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT", "compute layout C output should include broad write-only storage texture layout_facts");
    CheckContains(c_output, ".access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", "compute layout C output should include write-only access layout_facts");
    CheckContains(c_output, "static const SDL_GPUComputePipelineResourceLayout compute_case_resource_layout", "compute layout C output should emit compute layout facts");
    CheckNotContains(c_output, ".version = sizeof(SDL_GPU", "compute layout C output should not emit SDL_GPU version initializers");
    CheckContains(c_output, ".readonly_storage_texture_slots = compute_case_readonly_storage_textures", "compute layout C output should reference read-only storage texture slots directly");
    CheckContains(c_output, ".readwrite_storage_texture_slots = compute_case_readwrite_storage_textures", "compute layout C output should reference read-write storage texture slots directly");
}

static void TestResourceLayoutCInitializerOutput(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence default_sampled = MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING);
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence nondefault_sampled = MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING);
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence compute_storage_textures[] = {
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_GPUStorageTextureSlotDescription readonly_slots[1];
    SDL_GPUStorageTextureSlotDescription readwrite_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;
    char c_output[8192];

    SDL_zero(input);
    SDL_zero(layout);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = 1;
    input.sampled_textures = &default_sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    layout.shader_stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    layout.num_samplers = 1;
    layout.num_storage_buffers = 2;
    layout.num_uniform_buffers = 3;

    if (!Map(&input, &output) ||
        !WriteResourceLayoutC(c_output, sizeof(c_output), "default_shader", &layout, &output)) {
        return;
    }

    Check(output.num_sampled_texture_slots == 0, "default-only sampled layout should not need sampled layout_facts");
    CheckContains(c_output, "static const SDL_GPUShaderResourceLayout default_shader_resource_layout", "default shader layout C output should emit shader layout facts");
    CheckContains(c_output, ".stage = SDL_GPU_SHADERSTAGE_FRAGMENT", "default shader layout C output should preserve shader stage");
    CheckContains(c_output, ".num_samplers = 1", "default shader layout C output should preserve sampler count");
    CheckContains(c_output, ".num_storage_buffers = 2", "default shader layout C output should preserve storage buffer count");
    CheckContains(c_output, ".num_uniform_buffers = 3", "default shader layout C output should preserve uniform buffer count");

    SDL_zero(input);
    SDL_zero(layout);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = &nondefault_sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);
    layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    layout.shader_stage = SDL_GPU_SHADERSTAGE_VERTEX;
    layout.num_samplers = SDL_arraysize(sampled_slots);
    layout.num_storage_buffers = 1;
    layout.num_uniform_buffers = 4;

    if (!Map(&input, &output) ||
        !WriteResourceLayoutC(c_output, sizeof(c_output), "shader_layout", &layout, &output)) {
        return;
    }

    CheckContains(c_output, "static const SDL_GPUSampledTextureSlotDescription shader_layout_sampled_texture_slots[]", "non-default shader layout C output should emit sampled slot layout_facts");
    CheckContains(c_output, "static const SDL_GPUShaderResourceLayout shader_layout_resource_layout", "non-default shader layout C output should emit shader layout facts");
    CheckContains(c_output, ".stage = SDL_GPU_SHADERSTAGE_VERTEX", "non-default shader layout C output should preserve vertex stage");
    CheckContains(c_output, ".num_samplers = 2", "non-default shader layout C output should preserve full sampler count");
    CheckContains(c_output, ".sampled_texture_slots = shader_layout_sampled_texture_slots", "non-default shader layout C output should reference sampled slots directly");

    SDL_zero(input);
    SDL_zero(layout);
    SDL_zeroa(c_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readonly_storage_textures = SDL_arraysize(readonly_slots);
    input.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    input.storage_textures = compute_storage_textures;
    input.num_storage_texture_entries = SDL_arraysize(compute_storage_textures);
    output = MakeOutput(NULL, 0, NULL, 0, readonly_slots, SDL_arraysize(readonly_slots), readwrite_slots, SDL_arraysize(readwrite_slots));
    layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    layout.num_samplers = 1;
    layout.num_readonly_storage_textures = SDL_arraysize(readonly_slots);
    layout.num_readonly_storage_buffers = 3;
    layout.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    layout.num_readwrite_storage_buffers = 4;
    layout.num_uniform_buffers = 5;

    if (!Map(&input, &output) ||
        !WriteResourceLayoutC(c_output, sizeof(c_output), "compute_layout", &layout, &output)) {
        return;
    }

    CheckContains(c_output, "static const SDL_GPUComputePipelineResourceLayout compute_layout_resource_layout", "non-default compute layout C output should emit compute layout facts");
    CheckContains(c_output, ".num_samplers = 1", "non-default compute layout C output should preserve sampler count");
    CheckContains(c_output, ".num_readonly_storage_textures = 1", "non-default compute layout C output should preserve read-only storage texture count");
    CheckContains(c_output, ".num_readonly_storage_buffers = 3", "non-default compute layout C output should preserve read-only storage buffer count");
    CheckContains(c_output, ".num_readwrite_storage_textures = 1", "non-default compute layout C output should preserve read-write storage texture count");
    CheckContains(c_output, ".num_readwrite_storage_buffers = 4", "non-default compute layout C output should preserve read-write storage buffer count");
    CheckContains(c_output, ".num_uniform_buffers = 5", "non-default compute layout C output should preserve uniform buffer count");
    CheckContains(c_output, ".readonly_storage_texture_slots = compute_layout_readonly_storage_textures", "non-default compute layout C output should reference read-only storage slots directly");
    CheckContains(c_output, ".readwrite_storage_texture_slots = compute_layout_readwrite_storage_textures", "non-default compute layout C output should reference read-write storage slots directly");
}

static void TestCInitializerWriterFailures(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled = MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING);
    SDL_GPUSampledTextureSlotDescription sampled_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;
    SDL_ShaderCross_INTERNAL_ResourceLayoutCounts layout;
    SDL_IOStream *stream;
    char c_output[1024];
    char small_output[16];
    bool result;

    SDL_zero(input);
    SDL_zero(layout);
    SDL_zeroa(c_output);
    SDL_zeroa(small_output);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = &sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }
    layout.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    layout.shader_stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    layout.num_samplers = output.max_sampled_texture_slots;

    stream = SDL_IOFromMem(c_output, sizeof(c_output));
    Check(stream != NULL, "memory stream should be created for invalid-prefix test");
    if (stream != NULL) {
        SDL_ClearError();
        result = SDL_ShaderCross_INTERNAL_WriteResourceLayoutCInitializers(stream, "shader-case", &layout, &output);
        SDL_CloseIO(stream);
        Check(!result, "resource layout C writer should reject invalid symbol prefixes");
        Check(SDL_strstr(SDL_GetError(), "symbol prefix") != NULL, "invalid symbol prefix should set a useful error");
    }

    stream = SDL_IOFromMem(small_output, sizeof(small_output));
    Check(stream != NULL, "memory stream should be created for too-small output test");
    if (stream != NULL) {
        SDL_ClearError();
        result = SDL_ShaderCross_INTERNAL_WriteResourceLayoutCInitializers(stream, "shader_case", &layout, &output);
        SDL_CloseIO(stream);
        Check(!result, "resource layout C writer should reject too-small output streams");
    }
}

static void TestCoarseSamplerlessSampledImageRejected(void)
{
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.has_samplerless_sampled_image = true;
    output = MakeOutput(NULL, 0, NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(!output.accepted, "coarse samplerless sampled image evidence should be rejected");
    Check((output.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLERLESS_SAMPLED_IMAGE) != 0, "coarse samplerless rejection reason should be set");
}

static void TestDuplicateSampledSlotRejected(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled;
    input.num_sampled_textures = SDL_arraysize(sampled);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(!output.accepted, "duplicate sampled slots should be rejected");
    Check((output.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_DUPLICATE) != 0, "duplicate sampled-slot reason should be set");
}

static void TestSampledTextureRejectionMatrix(void)
{
    typedef struct SampledRejectCase
    {
        const char *message;
        SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled;
        Uint32 num_sampled_texture_slots;
        Uint64 expected_reasons;
    } SampledRejectCase;
    SampledRejectCase cases[] = {
        {
            "unmapped sampled texture should be rejected",
            MakeSampledTextureEvidence(false, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_UNMAPPED
        },
        {
            "out-of-range sampled texture slot should be rejected",
            MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_SLOT_OUT_OF_RANGE
        },
        {
            "unsupported sampled texture dimension should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_UNKNOWN, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "signed integer sampled texture with sampler should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_SINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED
        },
        {
            "unsigned integer sampled texture with sampler should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED
        },
        {
            "float samplerless sampled texture should be rejected without explicit exact-load policy support",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED
        },
        {
            "samplerless cube sampled texture should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONE, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "depth with filtering sampler should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED
        },
        {
            "3D normal depth sampled texture should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED
        },
        {
            "3D depth comparison sampled texture should be rejected",
            MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_DEPTH, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_COMPARISON, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_COMPARISON),
            1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED
        }
    };

    for (Uint32 i = 0; i < SDL_arraysize(cases); i += 1) {
        SDL_GPUSampledTextureSlotDescription sampled_slots[2];
        SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
        SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

        SDL_zero(input);
        SDL_zeroa(sampled_slots);
        input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
        input.num_sampled_texture_slots = cases[i].num_sampled_texture_slots;
        input.sampled_textures = &cases[i].sampled;
        input.num_sampled_textures = 1;
        output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

        if (!Map(&input, &output)) {
            continue;
        }

        CheckRejectedOutput(&output, cases[i].expected_reasons, cases[i].message);
    }
}

static void TestUnsupportedStorageTextureRejected(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_texture =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_UNKNOWN, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN);
    SDL_GPUStorageTextureSlotDescription readwrite_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    input.storage_textures = &storage_texture;
    input.num_storage_texture_entries = 1;
    output = MakeOutput(NULL, 0, NULL, 0, NULL, 0, readwrite_slots, SDL_arraysize(readwrite_slots));

    if (!Map(&input, &output)) {
        return;
    }

    Check(!output.accepted, "unsupported and ambiguous storage texture evidence should be rejected");
    Check((output.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED) != 0, "unsupported storage texture format reason should be set");
    Check((output.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_ACCESS_AMBIGUOUS) != 0, "ambiguous storage texture access reason should be set");
}

static void TestStorageTextureRejectionMatrix(void)
{
    typedef struct StorageRejectCase
    {
        const char *message;
        SDL_ShaderCross_INTERNAL_ResourceLayoutKind kind;
        SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_texture;
        Uint32 num_storage_textures;
        Uint32 num_readonly_storage_textures;
        Uint32 num_readwrite_storage_textures;
        Uint64 expected_reasons;
    } StorageRejectCase;
    const StorageRejectCase cases[] = {
        {
            "unmapped graphics storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(false, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_UNMAPPED
        },
        {
            "out-of-range graphics storage texture slot should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_OUT_OF_RANGE
        },
        {
            "unsupported graphics storage texture dimension should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "unsupported graphics storage texture cube-array dimension should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "cube compute read-only storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            0, 1, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "cube-array compute read-only storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R16G16B16A16_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            0, 1, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "unsupported graphics storage texture format should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_UNKNOWN, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED
        },
        {
            "read-write graphics storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED
        },
        {
            "cube compute read-write storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_INT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
            0, 0, 1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "cube-array compute read-write storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
            0, 0, 1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "ambiguous graphics storage texture access should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_UNKNOWN),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_ACCESS_AMBIGUOUS
        },
        {
            "write-only graphics storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
            1, 0, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED
        },
        {
            "write-only compute read-only storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_WRITE_ONLY),
            0, 1, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED
        },
        {
            "read-only compute read-write storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            0, 0, 1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED
        },
        {
            "write-only-only SNORM storage format should be rejected for compute read-write access",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_SNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
            0, 0, 1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED
        },
        {
            "write-only/read-only RG32 float storage format should be rejected for compute read-write access",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32G32_FLOAT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
            0, 0, 1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_FORMAT_UNSUPPORTED
        },
        {
            "2D-array compute read-write storage texture should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
            0, 0, 1,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED
        },
        {
            "compute storage texture class in shader layout facts should be rejected",
            SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER,
            MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READONLY, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
            0, 1, 0,
            SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_GROUP_UNSUPPORTED
        }
    };

    for (Uint32 i = 0; i < SDL_arraysize(cases); i += 1) {
        SDL_GPUStorageTextureSlotDescription storage_slots[1];
        SDL_GPUStorageTextureSlotDescription readonly_storage_slots[1];
        SDL_GPUStorageTextureSlotDescription readwrite_storage_slots[1];
        SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
        SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

        SDL_zero(input);
        SDL_zeroa(storage_slots);
        SDL_zeroa(readonly_storage_slots);
        SDL_zeroa(readwrite_storage_slots);
        input.kind = cases[i].kind;
        input.num_storage_textures = cases[i].num_storage_textures;
        input.num_readonly_storage_textures = cases[i].num_readonly_storage_textures;
        input.num_readwrite_storage_textures = cases[i].num_readwrite_storage_textures;
        input.storage_textures = &cases[i].storage_texture;
        input.num_storage_texture_entries = 1;
        output = MakeOutput(
            NULL, 0,
            storage_slots, SDL_arraysize(storage_slots),
            readonly_storage_slots, SDL_arraysize(readonly_storage_slots),
            readwrite_storage_slots, SDL_arraysize(readwrite_storage_slots));

        if (!Map(&input, &output)) {
            continue;
        }

        CheckRejectedOutput(&output, cases[i].expected_reasons, cases[i].message);
    }
}

static void TestDuplicateStorageSlotRejected(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_textures[] = {
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY),
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_GRAPHICS_STORAGE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_ONLY)
    };
    SDL_GPUStorageTextureSlotDescription storage_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_storage_textures = SDL_arraysize(storage_slots);
    input.storage_textures = storage_textures;
    input.num_storage_texture_entries = SDL_arraysize(storage_textures);
    output = MakeOutput(NULL, 0, storage_slots, SDL_arraysize(storage_slots), NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_SLOT_DUPLICATE, "duplicate storage texture slots should be rejected");
}

static void TestReadWriteStorageOutputCapacityExceeded(void)
{
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_texture =
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE);
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readwrite_storage_textures = 1;
    input.storage_textures = &storage_texture;
    input.num_storage_texture_entries = 1;
    output = MakeOutput(NULL, 0, NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED, "too-small read-write storage output buffer should be rejected");
}

static void TestRejectedPartialClassesPublishNoLayoutFacts(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled_textures[] = {
        MakeSampledTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING),
        MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_UINT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_NONFILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_UNKNOWN)
    };
    SDL_ShaderCross_INTERNAL_StorageTextureEvidence storage_textures[] = {
        MakeStorageTextureEvidence(true, 0, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_2D, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R8G8B8A8_UNORM, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE),
        MakeStorageTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_CLASS_COMPUTE_READWRITE, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_CUBE_ARRAY, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_FORMAT_R32_UINT, SDL_SHADERCROSS_INTERNAL_STORAGE_TEXTURE_ACCESS_READ_WRITE)
    };
    SDL_GPUSampledTextureSlotDescription sampled_slots[2];
    SDL_GPUStorageTextureSlotDescription readwrite_slots[2];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    SDL_zeroa(sampled_slots);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = SDL_arraysize(sampled_slots);
    input.sampled_textures = sampled_textures;
    input.num_sampled_textures = SDL_arraysize(sampled_textures);
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (Map(&input, &output)) {
        CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_SAMPLED_TEXTURE_PAIR_UNSUPPORTED, "invalid sampled class should not publish partial layout_facts");
    }

    SDL_zero(input);
    SDL_zeroa(readwrite_slots);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_COMPUTE_PIPELINE;
    input.num_readwrite_storage_textures = SDL_arraysize(readwrite_slots);
    input.storage_textures = storage_textures;
    input.num_storage_texture_entries = SDL_arraysize(storage_textures);
    output = MakeOutput(NULL, 0, NULL, 0, NULL, 0, readwrite_slots, SDL_arraysize(readwrite_slots));

    if (Map(&input, &output)) {
        CheckRejectedOutput(&output, SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_STORAGE_TEXTURE_DIMENSION_UNSUPPORTED, "invalid storage class should not publish partial layout_facts");
    }
}

static void TestOutputCapacityExceeded(void)
{
    SDL_ShaderCross_INTERNAL_SampledTextureEvidence sampled = MakeSampledTextureEvidence(true, 1, SDL_SHADERCROSS_INTERNAL_TEXTUREDIMENSION_3D, SDL_SHADERCROSS_INTERNAL_SAMPLED_TEXTURE_SAMPLE_FLOAT, SDL_SHADERCROSS_INTERNAL_SAMPLER_BINDING_FILTERING, SDL_SHADERCROSS_INTERNAL_SAMPLER_POLICY_FILTERING);
    SDL_GPUSampledTextureSlotDescription sampled_slots[1];
    SDL_ShaderCross_INTERNAL_LayoutFactsInput input;
    SDL_ShaderCross_INTERNAL_LayoutFactsOutput output;

    SDL_zero(input);
    input.kind = SDL_SHADERCROSS_INTERNAL_RESOURCE_LAYOUT_KIND_SHADER;
    input.num_sampled_texture_slots = 2;
    input.sampled_textures = &sampled;
    input.num_sampled_textures = 1;
    output = MakeOutput(sampled_slots, SDL_arraysize(sampled_slots), NULL, 0, NULL, 0, NULL, 0);

    if (!Map(&input, &output)) {
        return;
    }

    Check(!output.accepted, "too-small output buffer should be rejected");
    Check((output.reasons & SDL_SHADERCROSS_INTERNAL_LAYOUT_FACTS_REASON_OUTPUT_CAPACITY_EXCEEDED) != 0, "output-capacity rejection reason should be set");
    Check(output.num_sampled_texture_slots == 0, "rejected output should not publish partial sampled layout_facts");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    TestStorageTextureDecisionResolver();
    TestEmptyShaderEmitsNoLayoutFacts();
    TestMixedSampledTextureLayoutFacts();
    TestNonFilteringSampledTextureLayoutFacts();
    TestDepthCubeSampledTextureLayoutFacts();
    TestSamplerlessIntegerSampledTextureLayoutFacts();
    TestMultisampledSampledTextureLayoutFacts();
    TestExplicitSampledSlotPolicyLayoutFacts();
    TestExplicitSampledSlotPolicyRejections();
    TestMultisampledSampledTextureRejections();
    TestSparseSampledTextureClassEmitsDefaults();
    TestShaderResourceLayoutLayoutFactsOutput();
    TestComputeStorageTextureLayoutFacts();
    TestFragmentWritableStorageTextureLayoutFacts();
    TestGraphicsStorageTextureLayoutFacts();
    TestDefaultComputeWriteOnlyStorageTextureEmitsNoLayoutFacts();
    TestComputeResourceLayoutLayoutFactsOutput();
    TestResourceLayoutCInitializerOutput();
    TestCInitializerWriterFailures();
    TestCoarseSamplerlessSampledImageRejected();
    TestDuplicateSampledSlotRejected();
    TestSampledTextureRejectionMatrix();
    TestUnsupportedStorageTextureRejected();
    TestStorageTextureRejectionMatrix();
    TestDuplicateStorageSlotRejected();
    TestReadWriteStorageOutputCapacityExceeded();
    TestRejectedPartialClassesPublishNoLayoutFacts();
    TestOutputCapacityExceeded();

    if (failures != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%d layout facts mapper checks failed", failures);
        return 1;
    }

    SDL_Log("layout facts mapper checks passed");
    return 0;
}
