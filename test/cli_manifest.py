#!/usr/bin/env python3

import json
import os
import re
import subprocess
import sys
from pathlib import Path

from cli_layout_facts_codegen import (  # noqa: E402
    READWRITE_R32_UINT_STORAGE_COMPUTE_SPVASM,
    SAMPLED_FRAGMENT_SPVASM,
    assemble,
)


HLSL_INCLUDE_SOURCE = """
#include "scale.hlsli"

struct Input
{
    float3 Position : POSITION;
};

struct Output
{
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.Position = float4(scale_position(input.Position), 1.0);
    return output;
}
""".strip()


HLSL_INCLUDE_FILE = """
float3 scale_position(float3 value)
{
#ifdef USE_DOUBLE_SCALE
    return value * 2.0;
#else
    return value;
#endif
}
""".strip()


HLSL_UNANNOTATED_WRITEONLY_STORAGE_COMPUTE = """
RWTexture2D<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


def run(command, *, cwd=None, env=None, expect_success=True):
    completed = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if expect_success and completed.returncode != 0:
        raise AssertionError(
            f"command failed with exit code {completed.returncode}: {' '.join(map(str, command))}\n{completed.stdout}"
        )
    if not expect_success and completed.returncode == 0:
        raise AssertionError(
            f"command unexpectedly succeeded: {' '.join(map(str, command))}\n{completed.stdout}"
        )
    return completed


def run_optional(command, unavailable_needle, skip_message, *, cwd=None, env=None):
    completed = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode == 0:
        return completed
    if unavailable_needle in completed.stdout:
        print(skip_message)
        return None
    raise AssertionError(
        f"optional command failed with exit code {completed.returncode}: {' '.join(map(str, command))}\n{completed.stdout}"
    )


def require_contains(text, needle, message):
    if needle not in text:
        raise AssertionError(f"{message}\nmissing: {needle}\ntext:\n{text}")


def write_json(path, payload):
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def manifest_base(source, stage, targets):
    return {
        "schema": "sdl-shadercross-cli-manifest",
        "version": 1,
        "source": source,
        "stage": stage,
        "targets": targets,
    }


def policy_from_suggestion(stdout, flag):
    match = re.search(re.escape(flag) + r"\s+([^\r\n]+)", stdout)
    if not match:
        raise AssertionError(f"did not find {flag} suggestion in:\n{stdout}")
    return match.group(1).strip()


def assert_same_bytes(a, b, message):
    a_bytes = a.read_bytes()
    b_bytes = b.read_bytes()
    if a_bytes != b_bytes:
        raise AssertionError(f"{message}: {a} and {b} differ")


def assert_same_text(a, b, message):
    a_text = a.read_text(encoding="utf-8")
    b_text = b.read_text(encoding="utf-8")
    if a_text != b_text:
        raise AssertionError(f"{message}: {a} and {b} differ\n--- {a}\n{a_text}\n--- {b}\n{b_text}")


def check_manifest(shadercross, manifest_path, *, env=None, expect_success=True):
    return run([shadercross, "--manifest", str(manifest_path), "--check"], env=env, expect_success=expect_success)


def assert_no_check_temps(paths):
    for path in paths:
        leftovers = sorted(path.parent.glob(path.name + ".shadercross-check-*.tmp"))
        if leftovers:
            raise AssertionError(f"manifest --check left temporary outputs for {path}: {leftovers}")


def check_manifest_preserves_outputs(shadercross, manifest_path, output_paths, *, env=None):
    before = {path: path.read_bytes() for path in output_paths}
    check_manifest(shadercross, manifest_path, env=env)
    for path, expected in before.items():
        actual = path.read_bytes()
        if actual != expected:
            raise AssertionError(f"manifest --check overwrote named output: {path}")
    assert_no_check_temps(output_paths)


def run_hlsl_manifest_equivalence(shadercross, out_dir, hlsl_required):
    if not hlsl_required:
        print("Skipping HLSL manifest equivalence because DXC/HLSL support is not required in this build.")
        return

    manifest_dir = out_dir / "hlsl-manifest"
    shader_dir = out_dir / "hlsl-shaders"
    include_dir = out_dir / "hlsl-include"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    shader_dir.mkdir(parents=True, exist_ok=True)
    include_dir.mkdir(parents=True, exist_ok=True)
    source = shader_dir / "include-define.vert.hlsl"
    include = include_dir / "scale.hlsli"
    source.write_text(HLSL_INCLUDE_SOURCE + "\n", encoding="utf-8")
    include.write_text(HLSL_INCLUDE_FILE + "\n", encoding="utf-8")

    direct_spv = out_dir / "hlsl-direct.spv"
    direct_hlsl = out_dir / "hlsl-direct.hlsl"
    direct_dxbc = out_dir / "hlsl-direct.dxbc"
    manifest_spv = out_dir / "hlsl-manifest.spv"
    manifest_hlsl = out_dir / "hlsl-manifest.hlsl"
    manifest_dxbc = out_dir / "hlsl-manifest.dxbc"
    run([
        shadercross,
        str(source),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "SPIRV",
        "-o", str(direct_spv),
        "-I", str(include_dir),
        "-DUSE_DOUBLE_SCALE=1",
    ])
    run([
        shadercross,
        str(source),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "HLSL",
        "-o", str(direct_hlsl),
        "-I", str(include_dir),
        "-DUSE_DOUBLE_SCALE=1",
    ])
    dxbc_result = run_optional([
        shadercross,
        str(source),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "DXBC",
        "-o", str(direct_dxbc),
        "-I", str(include_dir),
        "-DUSE_DOUBLE_SCALE=1",
    ], "Could not load D3DCompile", "Skipping DXBC manifest non-gating coverage because D3DCompile is unavailable.")

    targets = [
        {"kind": "shader", "format": "SPIRV", "path": "../hlsl-manifest.spv"},
        {"kind": "shader", "format": "HLSL", "path": "../hlsl-manifest.hlsl"},
    ]
    if dxbc_result is not None:
        targets.append({"kind": "shader", "format": "DXBC", "path": "../hlsl-manifest.dxbc"})
    manifest = manifest_base(
        {"path": "../hlsl-shaders/include-define.vert.hlsl", "format": "HLSL"},
        "vertex",
        targets,
    )
    manifest["includes"] = ["../hlsl-include"]
    manifest["defines"] = [{"name": "USE_DOUBLE_SCALE", "value": "1"}]
    manifest_path = manifest_dir / "hlsl.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_bytes(direct_spv, manifest_spv, "manifest HLSL source/define/include SPIR-V should match equivalent flags")
    assert_same_text(direct_hlsl, manifest_hlsl, "manifest HLSL source/define/include HLSL should match equivalent flags")
    check_manifest_preserves_outputs(shadercross, manifest_path, [manifest_spv, manifest_hlsl] + ([manifest_dxbc] if dxbc_result is not None else []))
    if dxbc_result is not None:
        assert_same_bytes(direct_dxbc, manifest_dxbc, "manifest HLSL source/define/include DXBC should match equivalent flags")
        manifest_dxbc.write_bytes(b"not a stable DXBC provenance gate\n")
        check_manifest(shadercross, manifest_path)
    manifest_spv.write_bytes(b"stale SPIR-V output\n")
    spirv_drift = check_manifest(shadercross, manifest_path, expect_success=False)
    require_contains(spirv_drift.stdout, "manifest check content-drift", "manifest --check should catch SPIR-V content drift")
    assert_no_check_temps([manifest_spv, manifest_hlsl] + ([manifest_dxbc] if dxbc_result is not None else []))

    direct_pssl_hlsl = out_dir / "hlsl-direct-pssl.hlsl"
    direct_pssl_dxil = out_dir / "hlsl-direct-pssl.dxil"
    manifest_pssl_hlsl = out_dir / "hlsl-manifest-pssl.hlsl"
    manifest_pssl_dxil = out_dir / "hlsl-manifest-pssl.dxil"
    run([
        shadercross,
        str(source),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "HLSL",
        "-o", str(direct_pssl_hlsl),
        "-I", str(include_dir),
        "-DUSE_DOUBLE_SCALE=1",
        "-p",
    ])
    run([
        shadercross,
        str(source),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "DXIL",
        "-o", str(direct_pssl_dxil),
        "-I", str(include_dir),
        "-DUSE_DOUBLE_SCALE=1",
        "-p",
    ])
    manifest = manifest_base(
        {"path": "../hlsl-shaders/include-define.vert.hlsl", "format": "HLSL"},
        "vertex",
        [
            {"kind": "shader", "format": "HLSL", "path": "../hlsl-manifest-pssl.hlsl"},
            {"kind": "shader", "format": "DXIL", "path": "../hlsl-manifest-pssl.dxil"},
        ],
    )
    manifest["includes"] = ["../hlsl-include"]
    manifest["defines"] = [{"name": "USE_DOUBLE_SCALE", "value": "1"}]
    manifest["options"] = {"pssl": True}
    manifest_path = manifest_dir / "hlsl-pssl-mixed.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_text(direct_pssl_hlsl, manifest_pssl_hlsl, "manifest PSSL HLSL target should match equivalent flags")
    assert_same_bytes(direct_pssl_dxil, manifest_pssl_dxil, "manifest PSSL sibling DXIL target should match equivalent flags")
    check_manifest_preserves_outputs(shadercross, manifest_path, [manifest_pssl_hlsl, manifest_pssl_dxil])
    manifest_pssl_dxil.write_bytes(b"not a stable DXIL provenance gate\n")
    check_manifest(shadercross, manifest_path)
    assert_no_check_temps([manifest_pssl_hlsl, manifest_pssl_dxil])
    manifest_pssl_hlsl.write_text(manifest_pssl_hlsl.read_text(encoding="utf-8") + "// stale exact-compared HLSL sibling\n", encoding="utf-8")
    pssl_hlsl_drift = check_manifest(shadercross, manifest_path, expect_success=False)
    require_contains(pssl_hlsl_drift.stdout, "manifest check content-drift", "manifest --check should still catch exact-compared siblings when DXIL is provenance-only")
    assert_no_check_temps([manifest_pssl_hlsl, manifest_pssl_dxil])


def run_hlsl_format_authority_manifest_equivalence(shadercross, spirv_val, tint, out_dir, hlsl_required):
    if not hlsl_required:
        print("Skipping HLSL format-authority manifest equivalence because DXC/HLSL support is not required in this build.")
        return
    if tint is None:
        print("Skipping HLSL format-authority manifest equivalence because Tint is not configured.")
        return

    manifest_dir = out_dir / "authority-manifest"
    shader_dir = out_dir / "authority-shaders"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    shader_dir.mkdir(parents=True, exist_ok=True)
    source = shader_dir / "unannotated-writeonly-storage.comp.hlsl"
    source.write_text(HLSL_UNANNOTATED_WRITEONLY_STORAGE_COMPUTE + "\n", encoding="utf-8")
    source_for_cli = str(manifest_dir / "../authority-shaders/unannotated-writeonly-storage.comp.hlsl")
    storage_policy = "class=readwrite,slot=0,texture=2d,format=rgba16float,access=write,format_authority=explicit"

    direct_spv = out_dir / "authority-direct.spv"
    direct_spv_c = out_dir / "authority-direct-spv.c"
    direct_wgsl = out_dir / "authority-direct.wgsl"
    direct_wgsl_c = out_dir / "authority-direct-wgsl.c"
    manifest_spv = out_dir / "authority-manifest.spv"
    manifest_wgsl = out_dir / "authority-manifest.wgsl"
    manifest_c = out_dir / "authority-manifest.c"

    run([
        shadercross,
        source_for_cli,
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(direct_spv),
        "--resource-layout-c", str(direct_spv_c),
        "--resource-layout-symbol-prefix", "authority_manifest",
        "--storage-texture-slot", storage_policy,
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(direct_spv)])

    run([
        shadercross,
        source_for_cli,
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "-o", str(direct_wgsl),
        "--resource-layout-c", str(direct_wgsl_c),
        "--resource-layout-symbol-prefix", "authority_manifest_wgsl",
        "--storage-texture-slot", storage_policy,
        "--tint", tint,
    ])

    manifest = manifest_base(
        {"path": "../authority-shaders/unannotated-writeonly-storage.comp.hlsl", "format": "HLSL"},
        "compute",
        [
            {"kind": "shader", "format": "SPIRV", "path": "../authority-manifest.spv"},
            {"kind": "shader", "format": "WGSL", "path": "../authority-manifest.wgsl"},
            {"kind": "resource_layout_c", "path": "../authority-manifest.c", "symbol_prefix": "authority_manifest"},
        ],
    )
    manifest["policies"] = {"storage_texture_slots": [storage_policy]}
    manifest_path = manifest_dir / "authority-mixed.json"
    write_json(manifest_path, manifest)
    env = os.environ.copy()
    env["SDL_SHADERCROSS_TINT"] = tint
    run([shadercross, "--manifest", str(manifest_path)], env=env)

    assert_same_bytes(direct_spv, manifest_spv, "manifest format-authority SPIR-V target should match equivalent flags")
    assert_same_text(direct_wgsl, manifest_wgsl, "manifest format-authority WGSL target should match equivalent flags even when SPIR-V is also emitted")
    assert_same_text(direct_spv_c, manifest_c, "manifest format-authority resource-layout C output should describe the final emitted SPIR-V bytes")
    check_manifest(shadercross, manifest_path, env=env)


def run_layout_and_json_manifest_equivalence(shadercross, spirv_as, spirv_val, out_dir):
    manifest_dir = out_dir / "layout-manifest"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    sampled_spv = assemble(spirv_as, spirv_val, out_dir, "manifest-sampled-fragment", SAMPLED_FRAGMENT_SPVASM)
    storage_spv = assemble(spirv_as, spirv_val, out_dir, "manifest-storage-compute", READWRITE_R32_UINT_STORAGE_COMPUTE_SPVASM)

    suggestion = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    sampled_policy = policy_from_suggestion(suggestion.stdout, "--resource-layout-sampled-slot")

    direct_sampled_c = out_dir / "sampled-direct.c"
    manifest_sampled_c = out_dir / "sampled-manifest.c"
    run([
        shadercross,
        str(manifest_dir / "../manifest-sampled-fragment.spv"),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(direct_sampled_c),
        "--resource-layout-symbol-prefix", "sampled_manifest",
        "--resource-layout-sampled-slot", sampled_policy,
    ])
    manifest = manifest_base(
        {"path": "../manifest-sampled-fragment.spv", "format": "SPIRV"},
        "fragment",
        [{"kind": "resource_layout_c", "path": "../sampled-manifest.c", "symbol_prefix": "sampled_manifest"}],
    )
    manifest["policies"] = {"sampled_slots": [sampled_policy]}
    manifest_path = manifest_dir / "sampled-layout.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_text(direct_sampled_c, manifest_sampled_c, "manifest sampled resource-layout C output should match equivalent flags")
    require_contains(manifest_sampled_c.read_text(encoding="utf-8"), "uses explicit sampled-slot policy", "manifest sampled policy should remove ambiguity")
    check_manifest(shadercross, manifest_path)

    direct_combined_json = out_dir / "combined-direct.json"
    direct_combined_hlsl = out_dir / "combined-direct.hlsl"
    direct_combined_c = out_dir / "combined-direct.c"
    manifest_combined_json = out_dir / "combined-manifest.json"
    manifest_combined_hlsl = out_dir / "combined-manifest.hlsl"
    manifest_combined_c = out_dir / "combined-manifest.c"
    run([
        shadercross,
        str(manifest_dir / "../manifest-sampled-fragment.spv"),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(direct_combined_json),
        "--resource-layout-c", str(direct_combined_c),
        "--resource-layout-symbol-prefix", "combined_manifest",
        "--resource-layout-sampled-slot", sampled_policy,
    ])
    run([
        shadercross,
        str(manifest_dir / "../manifest-sampled-fragment.spv"),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "HLSL",
        "-o", str(direct_combined_hlsl),
    ])
    manifest = manifest_base(
        {"path": "../manifest-sampled-fragment.spv", "format": "SPIRV"},
        "fragment",
        [
            {"kind": "shader", "format": "JSON", "path": "../combined-manifest.json"},
            {"kind": "shader", "format": "HLSL", "path": "../combined-manifest.hlsl"},
            {"kind": "resource_layout_c", "path": "../combined-manifest.c", "symbol_prefix": "combined_manifest"},
        ],
    )
    manifest["policies"] = {"sampled_slots": [sampled_policy]}
    manifest_path = manifest_dir / "combined-output-layout.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_text(direct_combined_json, manifest_combined_json, "manifest combined JSON output should match equivalent flags")
    assert_same_text(direct_combined_hlsl, manifest_combined_hlsl, "manifest combined HLSL output should match equivalent flags")
    assert_same_text(direct_combined_c, manifest_combined_c, "manifest combined resource-layout C output should match equivalent flags")
    check_manifest_preserves_outputs(shadercross, manifest_path, [manifest_combined_json, manifest_combined_hlsl, manifest_combined_c])
    manifest_combined_json.write_text("{}\n", encoding="utf-8")
    content_drift = check_manifest(shadercross, manifest_path, expect_success=False)
    require_contains(content_drift.stdout, "manifest check content-drift", "manifest --check should catch shader output content drift")
    require_contains(manifest_combined_json.read_text(encoding="utf-8"), "{}\n", "manifest --check must not overwrite stale shader outputs")
    assert_no_check_temps([manifest_combined_json, manifest_combined_hlsl, manifest_combined_c])

    storage_policy = "class=readwrite,slot=0,texture=2d,format=r32uint,access=read_write"
    direct_storage_c = out_dir / "storage-direct.c"
    manifest_storage_c = out_dir / "storage-manifest.c"
    run([
        shadercross,
        str(manifest_dir / "../manifest-storage-compute.spv"),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(direct_storage_c),
        "--resource-layout-symbol-prefix", "storage_manifest",
        "--storage-texture-slot", storage_policy,
    ])
    manifest = manifest_base(
        {"path": "../manifest-storage-compute.spv", "format": "SPIRV"},
        "compute",
        [{"kind": "resource_layout_c", "path": "../storage-manifest.c", "symbol_prefix": "storage_manifest"}],
    )
    manifest["policies"] = {"storage_texture_slots": [storage_policy]}
    manifest_path = manifest_dir / "storage-layout.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_text(direct_storage_c, manifest_storage_c, "manifest storage resource-layout C output should match equivalent flags")
    check_manifest(shadercross, manifest_path)
    manifest_storage_c.write_text(manifest_storage_c.read_text(encoding="utf-8") + "/* stale policy/resource-layout output */\n", encoding="utf-8")
    policy_drift = check_manifest(shadercross, manifest_path, expect_success=False)
    require_contains(policy_drift.stdout, "manifest check policy-drift", "manifest --check should catch resource-layout policy drift")
    require_contains(manifest_storage_c.read_text(encoding="utf-8"), "stale policy/resource-layout output", "manifest --check must not overwrite stale resource-layout outputs")
    assert_no_check_temps([manifest_storage_c])

    direct_json = out_dir / "sampled-direct.json"
    manifest_json = out_dir / "sampled-manifest.json"
    run([
        shadercross,
        str(manifest_dir / "../manifest-sampled-fragment.spv"),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(direct_json),
    ])
    manifest = manifest_base(
        {"path": "../manifest-sampled-fragment.spv", "format": "SPIRV"},
        "fragment",
        [{"kind": "shader", "format": "JSON", "path": "../sampled-manifest.json"}],
    )
    manifest_path = manifest_dir / "sampled-json.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_text(direct_json, manifest_json, "manifest JSON output should match equivalent flags")
    check_manifest(shadercross, manifest_path)
    manifest_json.unlink()
    missing_output = check_manifest(shadercross, manifest_path, expect_success=False)
    require_contains(missing_output.stdout, "manifest check missing-output", "manifest --check should catch missing named outputs")
    assert_no_check_temps([manifest_json])

    direct_msl = out_dir / "sampled-direct.msl"
    manifest_msl = out_dir / "sampled-manifest.msl"
    run([
        shadercross,
        str(manifest_dir / "../manifest-sampled-fragment.spv"),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "MSL",
        "-o", str(direct_msl),
    ])
    manifest = manifest_base(
        {"path": "../manifest-sampled-fragment.spv", "format": "SPIRV"},
        "fragment",
        [{"kind": "shader", "format": "MSL", "path": "../sampled-manifest.msl"}],
    )
    manifest_path = manifest_dir / "sampled-msl.json"
    write_json(manifest_path, manifest)
    run([shadercross, "--manifest", str(manifest_path)])
    assert_same_text(direct_msl, manifest_msl, "manifest MSL output should match equivalent flags")
    check_manifest(shadercross, manifest_path)
    manifest_msl.write_text(manifest_msl.read_text(encoding="utf-8") + "// stale MSL output\n", encoding="utf-8")
    msl_drift = check_manifest(shadercross, manifest_path, expect_success=False)
    require_contains(msl_drift.stdout, "manifest check content-drift", "manifest --check should catch MSL content drift")
    assert_no_check_temps([manifest_msl])


def run_wgsl_manifest_equivalence(shadercross, spirv_as, spirv_val, tint, out_dir):
    if tint is None:
        print("Skipping WGSL manifest equivalence because Tint is not configured.")
        return

    manifest_dir = out_dir / "wgsl-manifest"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    sampled_spv = assemble(spirv_as, spirv_val, out_dir, "manifest-wgsl-sampled-fragment", SAMPLED_FRAGMENT_SPVASM)
    direct_wgsl = out_dir / "sampled-direct.wgsl"
    manifest_wgsl = out_dir / "sampled-manifest.wgsl"

    run([
        shadercross,
        str(manifest_dir / "../manifest-wgsl-sampled-fragment.spv"),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "-o", str(direct_wgsl),
        "--tint", tint,
    ])
    manifest = manifest_base(
        {"path": "../manifest-wgsl-sampled-fragment.spv", "format": "SPIRV"},
        "fragment",
        [{"kind": "shader", "format": "WGSL", "path": "../sampled-manifest.wgsl"}],
    )
    manifest_path = manifest_dir / "sampled-wgsl.json"
    write_json(manifest_path, manifest)
    env = os.environ.copy()
    env["SDL_SHADERCROSS_TINT"] = tint
    run([shadercross, "--manifest", str(manifest_path)], env=env)
    assert_same_text(direct_wgsl, manifest_wgsl, "manifest WGSL output should match equivalent flags")
    check_manifest_preserves_outputs(shadercross, manifest_path, [manifest_wgsl], env=env)
    manifest_wgsl.write_text(manifest_wgsl.read_text(encoding="utf-8") + "// stale WGSL output\n", encoding="utf-8")
    wgsl_drift = check_manifest(shadercross, manifest_path, env=env, expect_success=False)
    require_contains(wgsl_drift.stdout, "manifest check content-drift", "manifest --check should catch WGSL content drift")
    assert_no_check_temps([manifest_wgsl])


def run_negative_manifest_cases(shadercross, out_dir):
    manifest_dir = out_dir / "negative-manifest"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    source = out_dir / "dummy.frag.spv"
    source.write_bytes(b"\x03\x02\x23\x07")

    valid_minimal = manifest_base(
        {"path": "../dummy.frag.spv", "format": "SPIRV"},
        "fragment",
        [{"kind": "shader", "format": "JSON", "path": "../dummy.json"}],
    )
    manifest_path = manifest_dir / "minimal.json"
    write_json(manifest_path, valid_minimal)
    mixed = run([shadercross, "--manifest", str(manifest_path), "-t", "fragment"], expect_success=False)
    require_contains(mixed.stdout, "--manifest cannot be combined", "mixed manifest and flags should fail clearly")
    direct_check = run([shadercross, "--check"], expect_success=False)
    require_contains(direct_check.stdout, "--check requires --manifest", "direct --check should fail clearly")
    (out_dir / "dummy.json").write_text("{}\n", encoding="utf-8")
    reversed_check = run([shadercross, "--check", "--manifest", str(manifest_path)], expect_success=False)
    require_contains(reversed_check.stdout, "manifest check option-drift", "reversed --check --manifest order should be accepted as manifest check mode")
    option_drift_manifest = {**valid_minimal, "source": {"path": "../does-not-exist.frag.spv", "format": "SPIRV"}}
    option_drift_path = manifest_dir / "option-drift.json"
    write_json(option_drift_path, option_drift_manifest)
    option_drift = check_manifest(shadercross, option_drift_path, expect_success=False)
    require_contains(option_drift.stdout, "manifest check option-drift", "manifest --check should catch generation option drift")
    duplicate_direct_path = run([
        shadercross,
        str(source),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(out_dir / "duplicate-direct.c"),
        "--resource-layout-c", str(out_dir / "duplicate-direct.c"),
        "--resource-layout-symbol-prefix", "duplicate_direct",
    ], expect_success=False)
    require_contains(duplicate_direct_path.stdout, "used by more than one shadercross target", "direct CLI should reject shared shader/layout output paths")

    bad_cases = [
        ("unknown-version.json", {**valid_minimal, "version": 2}, "unsupported manifest version"),
        ("unknown-key.json", {**valid_minimal, "unexpected_field": True}, "unknown key 'unexpected_field'"),
        ("bad-policy.json", {
            **valid_minimal,
            "targets": [{"kind": "resource_layout_c", "path": "../bad-policy.c", "symbol_prefix": "bad_policy"}],
            "policies": {"sampled_slots": ["slot=0,texture=2d,sample=not_a_sample,sampler=non_filtering"]},
        }, "invalid --resource-layout-sampled-slot"),
        ("duplicate-slot.json", {
            **valid_minimal,
            "targets": [{"kind": "resource_layout_c", "path": "../duplicate-slot.c", "symbol_prefix": "duplicate_slot"}],
            "policies": {
                "sampled_slots": [
                    "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
                    "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
                ]
            },
        }, "duplicate slot 0"),
        ("multiple-includes.json", {**valid_minimal, "includes": ["../a", "../b"]}, "only one include directory"),
        ("multiple-layout-targets.json", {
            **valid_minimal,
            "targets": [
                {"kind": "resource_layout_c", "path": "../a.c", "symbol_prefix": "a"},
                {"kind": "resource_layout_c", "path": "../b.c", "symbol_prefix": "b"},
            ],
        }, "only one resource_layout_c target"),
        ("duplicate-shader-target-path.json", {
            **valid_minimal,
            "targets": [
                {"kind": "shader", "format": "JSON", "path": "../duplicate-shader.out"},
                {"kind": "shader", "format": "HLSL", "path": "../duplicate-shader.out"},
            ],
        }, "used by more than one shadercross target"),
        ("duplicate-shader-layout-path.json", {
            **valid_minimal,
            "targets": [
                {"kind": "shader", "format": "JSON", "path": "../duplicate-layout.c"},
                {"kind": "resource_layout_c", "path": "../duplicate-layout.c", "symbol_prefix": "duplicate_layout"},
            ],
        }, "used by more than one shadercross target"),
        ("missing-prefix.json", {
            **valid_minimal,
            "targets": [{"kind": "resource_layout_c", "path": "../missing-prefix.c"}],
        }, "missing required key 'symbol_prefix'"),
    ]
    for name, payload, expected in bad_cases:
        path = manifest_dir / name
        write_json(path, payload)
        result = run([shadercross, "--manifest", str(path)], expect_success=False)
        require_contains(result.stdout, expected, f"{name} should fail clearly")

    duplicate_key_path = manifest_dir / "duplicate-key.json"
    duplicate_key_path.write_text(
        '{"schema":"sdl-shadercross-cli-manifest","version":1,"version":1,"source":{"path":"../dummy.frag.spv","format":"SPIRV"},"stage":"fragment","targets":[{"kind":"shader","format":"JSON","path":"../dummy.json"}]}\n',
        encoding="utf-8",
    )
    result = run([shadercross, "--manifest", str(duplicate_key_path)], expect_success=False)
    require_contains(result.stdout, "duplicate object key 'version'", "duplicate manifest keys should fail clearly")

    nul_cases = [
        (
            "nul-key.json",
            '{"schema":"sdl-shadercross-cli-manifest","version":1,"source":{"path":"../dummy.frag.spv","format":"SPIRV"},"stage":"fragment","targets":[{"kind":"shader","format":"JSON","path":"../dummy.json"}],"schema\\u0000x":"sdl-shadercross-cli-manifest"}\n',
        ),
        (
            "nul-schema.json",
            '{"schema":"sdl-shadercross-cli-manifest\\u0000x","version":1,"source":{"path":"../dummy.frag.spv","format":"SPIRV"},"stage":"fragment","targets":[{"kind":"shader","format":"JSON","path":"../dummy.json"}]}\n',
        ),
        (
            "nul-target-kind.json",
            '{"schema":"sdl-shadercross-cli-manifest","version":1,"source":{"path":"../dummy.frag.spv","format":"SPIRV"},"stage":"fragment","targets":[{"kind":"shader\\u0000x","format":"JSON","path":"../dummy.json"}]}\n',
        ),
        (
            "nul-target-format.json",
            '{"schema":"sdl-shadercross-cli-manifest","version":1,"source":{"path":"../dummy.frag.spv","format":"SPIRV"},"stage":"fragment","targets":[{"kind":"shader","format":"JSON\\u0000x","path":"../dummy.json"}]}\n',
        ),
        (
            "nul-path.json",
            '{"schema":"sdl-shadercross-cli-manifest","version":1,"source":{"path":"../dummy.frag.spv\\u0000ignored","format":"SPIRV"},"stage":"fragment","targets":[{"kind":"shader","format":"JSON","path":"../dummy.json"}]}\n',
        ),
    ]
    for name, text in nul_cases:
        path = manifest_dir / name
        path.write_text(text, encoding="utf-8")
        result = run([shadercross, "--manifest", str(path)], expect_success=False)
        require_contains(result.stdout, "NUL Unicode escape is not allowed", f"{name} should reject escaped NUL")

    malformed_cases = [
        ("malformed-object.json", '{"schema":"sdl-shadercross-cli-manifest","version":{"bad":}\n'),
        ("malformed-array.json", '{"schema":"sdl-shadercross-cli-manifest","version":[1,]}\n'),
    ]
    for name, text in malformed_cases:
        path = manifest_dir / name
        path.write_text(text, encoding="utf-8")
        result = run([shadercross, "--manifest", str(path)], expect_success=False)
        require_contains(result.stdout, "manifest JSON parse error", f"{name} should fail in the parser")

    huge_path = manifest_dir / "huge.json"
    huge_path.write_text(" " * (1024 * 1024 + 1), encoding="utf-8")
    result = run([shadercross, "--manifest", str(huge_path)], expect_success=False)
    require_contains(result.stdout, "is too large", "oversized manifest should fail before parsing")

    escaped_path = manifest_dir / "escaped.json"
    escaped_path.write_text(
        '{"schema":"sdl-shadercross-cli-manifest","version":1,"source":{"path":"../dummy.frag.spv","format":"SPIRV"},"stage":"fragment","entrypoint":"ma\\u0069n","targets":[{"kind":"shader","format":"JSON","path":"../escaped.json"}]}\n',
        encoding="utf-8",
    )
    result = run([shadercross, "--manifest", str(escaped_path)], expect_success=False)
    require_contains(result.stdout, "SPIRV file too small", "escaped string parsing should produce a normal parsed manifest path before shader validation")


def main(argv):
    if len(argv) < 5:
        print("usage: cli_manifest.py <shadercross> <spirv-as> <spirv-val> <out-dir> [--hlsl-required] [--tint <path>]")
        return 2
    shadercross = argv[1]
    spirv_as = argv[2]
    spirv_val = argv[3]
    out_dir = Path(argv[4])
    hlsl_required = "--hlsl-required" in argv[5:]
    tint = None
    if "--tint" in argv[5:]:
        index = argv.index("--tint")
        tint = argv[index + 1]
    out_dir.mkdir(parents=True, exist_ok=True)

    run_hlsl_manifest_equivalence(shadercross, out_dir, hlsl_required)
    run_hlsl_format_authority_manifest_equivalence(shadercross, spirv_val, tint, out_dir, hlsl_required)
    run_layout_and_json_manifest_equivalence(shadercross, spirv_as, spirv_val, out_dir)
    run_wgsl_manifest_equivalence(shadercross, spirv_as, spirv_val, tint, out_dir)
    run_negative_manifest_cases(shadercross, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
