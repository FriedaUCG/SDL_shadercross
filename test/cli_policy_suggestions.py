#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli_layout_facts_codegen import (  # noqa: E402
    MSAA_UINT_FRAGMENT_SPVASM,
    MSAA_UNKNOWN_DEPTH_FRAGMENT_SPVASM,
    SAMPLED_FRAGMENT_SPVASM,
    assemble,
)


HLSL_COMPUTE_SAMPLED = """
Texture2DArray<float4> InputTexture : register(t0, space0);
SamplerState InputSampler : register(s0, space0);
RWStructuredBuffer<uint> OutputWords : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    float4 color = InputTexture.SampleLevel(InputSampler, float3(0.5, 0.5, 0.0), 0.0);
    OutputWords[0] = (uint)round(color.x * 255.0);
}
""".strip()


HLSL_MSAA_SAMPLED = """
Texture2DMS<float4> InputTexture : register(t0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    return InputTexture.Load(int2(0, 0), 0);
}
""".strip()


HLSL_MSAA_UINT_SAMPLED = """
Texture2DMS<uint4> InputTexture : register(t0, space0);
RWStructuredBuffer<uint> OutputWords : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint4 color = InputTexture.Load(int2(0, 0), 0);
    OutputWords[0] = color.x;
}
""".strip()


HLSL_UNANNOTATED_WRITEONLY_STORAGE = """
RWTexture2D<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_FRAGMENT_WRITEONLY_STORAGE = """
RWTexture2D<float4> OutputTexture : register(u0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    OutputTexture[uint2(position.xy)] = float4(1.0, 0.0, 0.0, 1.0);
    return float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_READWRITE_3D_STORAGE = """
RWTexture3D<uint> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint3 coord = uint3(0, 0, 0);
    uint value = OutputTexture[coord];
    OutputTexture[coord] = value + 1;
}
""".strip()


def run(command, *, expect_success=True):
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if expect_success and completed.returncode != 0:
        raise AssertionError(
            f"command failed with exit code {completed.returncode}: {' '.join(map(str, command))}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    if not expect_success and completed.returncode == 0:
        raise AssertionError(
            f"command unexpectedly succeeded: {' '.join(map(str, command))}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return completed


def combined_output(completed):
    return completed.stdout + completed.stderr


def require_empty_stderr(completed, message):
    if completed.stderr:
        raise AssertionError(f"{message}\nstderr:\n{completed.stderr}\nstdout:\n{completed.stdout}")


def require_contains(text, needle, message):
    if needle not in text:
        raise AssertionError(f"{message}\nmissing: {needle}\ntext:\n{text}")


def require_not_contains(text, needle, message):
    if needle in text:
        raise AssertionError(f"{message}\nunexpected: {needle}\ntext:\n{text}")


def write_hlsl(out_dir, name, source):
    path = out_dir / name
    path.write_text(source + "\n", encoding="utf-8")
    return path


def take_optional_path_argument(args, option):
    if option not in args:
        return None
    index = args.index(option)
    if index + 1 >= len(args):
        raise AssertionError(f"{option} requires a path argument")
    value = args[index + 1]
    del args[index:index + 2]
    return value


def hlsl_to_spirv_supported(shadercross, out_dir, hlsl_required):
    hlsl_probe = write_hlsl(
        out_dir,
        "hlsl_probe.comp.hlsl",
        "[numthreads(1, 1, 1)] void main(uint3 threadID : SV_DispatchThreadID) {}",
    )
    spirv_probe = out_dir / "hlsl_probe.spv"
    result = subprocess.run([
        shadercross,
        str(hlsl_probe),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(spirv_probe),
    ], check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode == 0:
        return True

    if hlsl_required:
        raise AssertionError(
            "HLSL-to-SPIR-V probe failed, but this test was configured with --hlsl-required.\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    print("Skipping resource layout policy suggestion coverage because this build cannot compile HLSL to SPIR-V.")
    print(combined_output(result))
    return False


def run_spirv_suggestion_coverage(shadercross, out_dir, spirv_as, spirv_val):
    if not spirv_as or not spirv_val:
        print("Skipping SPIR-V resource layout policy suggestion coverage because spirv-as/spirv-val were not configured.")
        return

    sampled_spv = assemble(spirv_as, spirv_val, out_dir, "sampled-fragment-suggestions", SAMPLED_FRAGMENT_SPVASM)
    sampled_result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(sampled_result, "SPIR-V sampled suggestion mode should write only stdout")
    require_contains(sampled_result.stdout, "sampled texture '<unnamed>' stage=fragment source_set=2 source_binding=0 slot=0", "SPIR-V sampled suggestions should report stage, source binding, and SDL slot even when the SPIR-V variable is unnamed")
    require_contains(sampled_result.stdout, "ambiguity=float_filtering_or_non_filtering", "SPIR-V sampled suggestions should report float sampler ambiguity")
    require_contains(sampled_result.stdout, "--resource-layout-sampled-slot slot=0,texture=2d,sample=filterable_float,sampler=filtering", "SPIR-V sampled suggestions should include the default filterable candidate")
    require_contains(sampled_result.stdout, "--resource-layout-sampled-slot slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering", "SPIR-V sampled suggestions should include the unfilterable candidate")

    sampled_bad_policy = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
        "--resource-layout-sampled-slot", "slot=0,texture=3d,sample=filterable_float,sampler=filtering",
    ], expect_success=False)
    require_contains(combined_output(sampled_bad_policy), "explicit sampled-slot policy does not match the reflected sampled texture evidence", "SPIR-V suggestion mode should reject invalid sampled policies instead of reporting convergence")

    msaa_unknown_depth_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-unknown-depth-suggestions", MSAA_UNKNOWN_DEPTH_FRAGMENT_SPVASM)
    msaa_result = run([
        shadercross,
        str(msaa_unknown_depth_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(msaa_result, "SPIR-V MSAA suggestion mode should write only stdout")
    require_contains(msaa_result.stdout, "ambiguity=msaa_depth_or_color", "SPIR-V MSAA suggestions should label depth/color ambiguity")
    require_contains(msaa_result.stdout, "--resource-layout-sampled-slot slot=0,texture=2d,sample=multisampled_unfilterable_float,sampler=none", "SPIR-V MSAA suggestions should include the color candidate")
    require_contains(msaa_result.stdout, "--resource-layout-sampled-slot slot=0,texture=2d,sample=multisampled_depth,sampler=none", "SPIR-V MSAA suggestions should include the depth candidate")

    msaa_uint_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-uint-suggestions", MSAA_UINT_FRAGMENT_SPVASM)
    msaa_uint_result = run([
        shadercross,
        str(msaa_uint_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(msaa_uint_result, "SPIR-V integer MSAA suggestion mode should write only stdout")
    require_contains(msaa_uint_result.stdout, "# No resource layout policy suggestions.", "SPIR-V integer MSAA sampled textures should not receive unsupported float/depth candidates")
    require_not_contains(msaa_uint_result.stdout, "--resource-layout-sampled-slot", "SPIR-V sampled suggestions must not print candidates that do not round-trip")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <shadercross> <output-dir> [--hlsl-required] [--spirv-as <path> --spirv-val <path>]", file=sys.stderr)
        return 2

    shadercross = sys.argv[1]
    out_dir = Path(sys.argv[2])
    extra_args = sys.argv[3:]
    hlsl_required = "--hlsl-required" in extra_args
    if hlsl_required:
        extra_args.remove("--hlsl-required")
    spirv_as = take_optional_path_argument(extra_args, "--spirv-as")
    spirv_val = take_optional_path_argument(extra_args, "--spirv-val")
    if extra_args:
        print(f"Unknown arguments: {' '.join(extra_args)}", file=sys.stderr)
        return 2
    out_dir.mkdir(parents=True, exist_ok=True)

    run_spirv_suggestion_coverage(shadercross, out_dir, spirv_as, spirv_val)

    if not hlsl_to_spirv_supported(shadercross, out_dir, hlsl_required):
        return 0

    sampled_hlsl = write_hlsl(out_dir, "sampled.comp.hlsl", HLSL_COMPUTE_SAMPLED)
    sampled_result = run([
        shadercross,
        str(sampled_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(sampled_result, "successful suggestion mode should write suggestions to stdout without stderr noise")
    sampled_suggestions = sampled_result.stdout
    require_contains(sampled_suggestions, "# SDL_shadercross resource layout policy suggestions", "suggestion mode should print a stable header")
    require_contains(sampled_suggestions, "sampled texture 'InputTexture' stage=compute source_set=0 source_binding=0 slot=0", "sampled suggestions should name resource, stage, source binding, and SDL slot")
    require_contains(sampled_suggestions, "inferred_texture=2d_array inferred_sample=float inferred_sampler=unknown ambiguity=float_filtering_or_non_filtering", "sampled suggestions should report inferred facts and ambiguity")
    require_contains(sampled_suggestions, "--resource-layout-sampled-slot slot=0,texture=2d_array,sample=filterable_float,sampler=filtering", "sampled suggestions should include the default filterable candidate")
    require_contains(sampled_suggestions, "--resource-layout-sampled-slot slot=0,texture=2d_array,sample=unfilterable_float,sampler=non_filtering", "sampled suggestions should include the unfilterable-float candidate")
    require_not_contains(sampled_suggestions, "downstream-sentinel", "SDL_shadercross suggestions must not leak downstream workflow names")

    sampled_layout = out_dir / "sampled_policy.c"
    run([
        shadercross,
        str(sampled_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--resource-layout-c", str(sampled_layout),
        "--resource-layout-symbol-prefix", "sampled_policy",
        "--resource-layout-sampled-slot", "slot=0,texture=2d_array,sample=filterable_float,sampler=filtering",
    ])
    require_contains(sampled_layout.read_text(encoding="utf-8"), "note: sampled texture slot 0 uses explicit sampled-slot policy", "sampled suggestions should round-trip into existing resource-layout policy flags")
    sampled_converged_result = run([
        shadercross,
        str(sampled_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
        "--resource-layout-sampled-slot", "slot=0,texture=2d_array,sample=filterable_float,sampler=filtering",
    ])
    require_empty_stderr(sampled_converged_result, "converged suggestion mode should write only stdout")
    sampled_converged = sampled_converged_result.stdout
    require_not_contains(sampled_converged, "--resource-layout-sampled-slot", "suggestion mode should stop suggesting sampled policies already supplied on the command line")

    sampled_bad_policy = run([
        shadercross,
        str(sampled_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ], expect_success=False)
    require_contains(combined_output(sampled_bad_policy), "explicit sampled-slot policy does not match the reflected sampled texture evidence", "suggestion mode should reject invalid sampled policies instead of reporting convergence")

    msaa_hlsl = write_hlsl(out_dir, "msaa.frag.hlsl", HLSL_MSAA_SAMPLED)
    msaa_result = run([
        shadercross,
        str(msaa_hlsl),
        "-s", "HLSL",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(msaa_result, "MSAA suggestion mode should write only stdout")
    msaa_suggestions = msaa_result.stdout
    require_contains(msaa_suggestions, "ambiguity=msaa_depth_or_color", "MSAA suggestions should label depth/color ambiguity")
    require_contains(msaa_suggestions, "--resource-layout-sampled-slot slot=0,texture=2d,sample=multisampled_unfilterable_float,sampler=none", "MSAA suggestions should include the color candidate")
    require_contains(msaa_suggestions, "--resource-layout-sampled-slot slot=0,texture=2d,sample=multisampled_depth,sampler=none", "MSAA suggestions should include the depth candidate")

    msaa_uint_hlsl = write_hlsl(out_dir, "msaa-uint.comp.hlsl", HLSL_MSAA_UINT_SAMPLED)
    msaa_uint_result = run([
        shadercross,
        str(msaa_uint_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(msaa_uint_result, "unsupported integer MSAA suggestion mode should write only stdout")
    require_contains(msaa_uint_result.stdout, "# No resource layout policy suggestions.", "integer MSAA sampled textures should not receive unsupported float/depth candidates")
    require_not_contains(msaa_uint_result.stdout, "--resource-layout-sampled-slot", "sampled suggestions must not print candidates that do not round-trip")

    writeonly_hlsl = write_hlsl(out_dir, "writeonly-storage.comp.hlsl", HLSL_UNANNOTATED_WRITEONLY_STORAGE)
    writeonly_result = run([
        shadercross,
        str(writeonly_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(writeonly_result, "storage suggestion mode should write only stdout")
    writeonly_suggestions = writeonly_result.stdout
    require_contains(writeonly_suggestions, "storage texture 'OutputTexture' stage=compute source_set=1 source_binding=0 class=readwrite slot=0", "storage suggestions should name resource, stage, source binding, class, and SDL slot")
    require_contains(writeonly_suggestions, "observed_access=write_only ambiguity=observed_write_access", "storage suggestions should report observed-write access authority")
    require_contains(writeonly_suggestions, "--storage-texture-slot class=readwrite,slot=0,texture=2d,format=rgba32float,access=write", "storage suggestions should include the reflected write-only policy")

    fragment_writeonly_hlsl = write_hlsl(out_dir, "fragment-writeonly-storage.frag.hlsl", HLSL_FRAGMENT_WRITEONLY_STORAGE)
    fragment_writeonly_result = run([
        shadercross,
        str(fragment_writeonly_hlsl),
        "-s", "HLSL",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(fragment_writeonly_result, "fragment storage suggestion mode should write only stdout")
    fragment_writeonly_suggestions = fragment_writeonly_result.stdout
    require_contains(fragment_writeonly_suggestions, "# No resource layout policy suggestions.", "ambiguous fragment storage writes should not produce an unsupported regular-storage write policy")
    require_not_contains(fragment_writeonly_suggestions, "--storage-texture-slot class=storage,slot=0,texture=2d,format=rgba32float,access=write", "suggestion mode must not print storage policies that do not round-trip")

    storage_3d_hlsl = write_hlsl(out_dir, "readwrite-3d-storage.comp.hlsl", HLSL_READWRITE_3D_STORAGE)
    storage_3d_result = run([
        shadercross,
        str(storage_3d_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
    ])
    require_empty_stderr(storage_3d_result, "3D storage suggestion mode should write only stdout")
    storage_3d_suggestions = storage_3d_result.stdout
    require_contains(storage_3d_suggestions, "reflected_dimension=3d reflected_format=r32uint", "3D storage suggestions should report reflected facts")
    require_contains(storage_3d_suggestions, "--storage-texture-slot class=readwrite,slot=0,texture=3d,format=r32uint,access=read_write", "3D storage suggestions should include the read-write policy candidate")

    storage_3d_layout = out_dir / "storage_3d_policy.c"
    run([
        shadercross,
        str(storage_3d_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--resource-layout-c", str(storage_3d_layout),
        "--resource-layout-symbol-prefix", "storage_3d_policy",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=3d,format=r32uint,access=read_write",
    ])
    storage_3d_text = storage_3d_layout.read_text(encoding="utf-8")
    require_contains(storage_3d_text, "note: storage texture class readwrite slot 0 uses explicit --storage-texture-slot policy", "storage suggestions should round-trip into existing storage policy flags")
    require_contains(storage_3d_text, "SDL_GPU_TEXTURETYPE_3D", "3D storage policy round-trip should preserve texture type")
    require_contains(storage_3d_text, "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "3D storage policy round-trip should preserve read-write access")

    storage_3d_bad_policy = run([
        shadercross,
        str(storage_3d_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=read_write",
    ], expect_success=False)
    require_contains(combined_output(storage_3d_bad_policy), "--storage-texture-slot does not match the reflected storage texture evidence", "suggestion mode should reject invalid storage policies instead of reporting convergence")

    bad_mixed_mode = run([
        shadercross,
        str(sampled_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "--suggest-resource-layout-policy",
        "-d", "SPIRV",
        "-o", str(out_dir / "bad.spv"),
    ], expect_success=False)
    require_contains(combined_output(bad_mixed_mode), "--suggest-resource-layout-policy cannot be combined", "suggestion mode should reject mixed shader-output commands")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
