#!/usr/bin/env python3

import shutil
import struct
import subprocess
import sys
from pathlib import Path


SPV_OP_CAPABILITY = 17
SPV_OP_TYPE_IMAGE = 25
SPV_CAPABILITY_STORAGE_IMAGE_EXTENDED_FORMATS = 49
SPV_IMAGE_FORMAT_R32F = 3
SPV_IMAGE_FORMAT_RG32F = 6
SPV_IMAGE_FORMAT_RGBA8 = 4
SPV_IMAGE_FORMAT_RGBA8I = 23
SPV_IMAGE_FORMAT_RGBA8UI = 32


HLSL_WRITEONLY_STORAGE = """
RWTexture2D<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_TWO_WRITEONLY_STORAGE = """
RWTexture2D<float4> OutputTexture0 : register(u0, space1);
RWTexture2D<float4> OutputTexture1 : register(u1, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture0[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
    OutputTexture1[threadID.xy] = float4(0.0, 1.0, 0.0, 1.0);
}
""".strip()


HLSL_UINT_WRITEONLY_STORAGE = """
RWTexture2D<uint4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = uint4(1, 2, 3, 4);
}
""".strip()


HLSL_SINT_WRITEONLY_STORAGE = """
RWTexture2D<int4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = int4(1, 2, 3, 4);
}
""".strip()


HLSL_IMAGE_ATOMIC_STORAGE = """
RWTexture2D<uint> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint previousValue;
    InterlockedAdd(OutputTexture[threadID.xy], 1, previousValue);
}
""".strip()


HLSL_MIXED_IMAGE_TYPE_STORAGE = """
RWTexture2D<float4> OutputTexture0 : register(u0, space1);
RWTexture2D<float> OutputTexture1 : register(u1, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture0[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
    OutputTexture1[threadID.xy] = 1.0;
}
""".strip()


HLSL_ARRAY_WRITEONLY_STORAGE = """
RWTexture2DArray<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[uint3(threadID.xy, 0)] = float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_READWRITE_STORAGE = """
[[vk::image_format("rgba8")]]
RWTexture2D<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    float4 value = OutputTexture[threadID.xy];
    OutputTexture[threadID.xy] = value + float4(0.1, 0.0, 0.0, 0.0);
}
""".strip()


def run(command, *, expect_success=True):
    completed = subprocess.run(
        command,
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


def require_contains(text, needle, message):
    if needle not in text:
        raise AssertionError(f"{message}\nmissing: {needle}\ntext:\n{text}")


def write_hlsl(out_dir, name, source):
    path = out_dir / name
    path.write_text(source + "\n", encoding="utf-8")
    return path


def hlsl_to_spirv_supported(shadercross, out_dir, hlsl_required):
    hlsl_probe = out_dir / "hlsl_probe.comp.hlsl"
    spirv_probe = out_dir / "hlsl_probe.spv"
    hlsl_probe.write_text(
        "[numthreads(1, 1, 1)] void main(uint3 threadID : SV_DispatchThreadID) {}\n",
        encoding="utf-8",
    )
    result = subprocess.run([
        shadercross,
        str(hlsl_probe),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(spirv_probe),
    ], check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode == 0:
        return True

    if hlsl_required:
        raise AssertionError(
            "HLSL-to-SPIR-V probe failed, but this test was configured with --hlsl-required.\n"
            f"{result.stdout}"
        )

    print("Skipping HLSL SPIR-V format-authority coverage because this build cannot compile HLSL to SPIR-V.")
    print(result.stdout)
    return False


def spirv_words(path):
    data = path.read_bytes()
    if len(data) % 4:
        raise AssertionError(f"{path} is not word-aligned SPIR-V")
    words = list(struct.unpack("<" + "I" * (len(data) // 4), data))
    if len(words) < 5 or words[0] != 0x07230203:
        raise AssertionError(f"{path} is not valid SPIR-V")
    return words


def iter_instructions(words):
    offset = 5
    while offset < len(words):
        instruction = words[offset]
        word_count = instruction >> 16
        opcode = instruction & 0xffff
        if word_count == 0 or offset + word_count > len(words):
            raise AssertionError("invalid SPIR-V instruction stream")
        yield opcode, words[offset:offset + word_count]
        offset += word_count


def image_formats(path):
    formats = []
    for opcode, inst in iter_instructions(spirv_words(path)):
        if opcode == SPV_OP_TYPE_IMAGE:
            formats.append(inst[8])
    return formats


def has_capability(path, capability):
    for opcode, inst in iter_instructions(spirv_words(path)):
        if opcode == SPV_OP_CAPABILITY and len(inst) >= 2 and inst[1] == capability:
            return True
    return False


def require_image_format(path, expected, message):
    formats = image_formats(path)
    if expected not in formats:
        raise AssertionError(f"{message}\nmissing image format {expected}; got {formats}")


def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <shadercross> <spirv-val> <output-dir> [--hlsl-required]", file=sys.stderr)
        return 2

    shadercross = sys.argv[1]
    spirv_val = sys.argv[2]
    out_dir = Path(sys.argv[3])
    hlsl_required = False

    i = 4
    while i < len(sys.argv):
        if sys.argv[i] == "--hlsl-required":
            hlsl_required = True
            i += 1
        else:
            print(f"Unexpected argument: {sys.argv[i]}", file=sys.stderr)
            return 2

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not hlsl_to_spirv_supported(shadercross, out_dir, hlsl_required):
        return 0

    writeonly_hlsl = write_hlsl(out_dir, "writeonly-storage.comp.hlsl", HLSL_WRITEONLY_STORAGE)
    writeonly_spv = out_dir / "writeonly-storage-rgba8.spv"
    writeonly_layout_facts = out_dir / "writeonly-storage-rgba8.c"
    result = run([
        shadercross,
        str(writeonly_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(writeonly_spv),
        "--resource-layout-c", str(writeonly_layout_facts),
        "--resource-layout-symbol-prefix", "writeonly_storage_rgba8",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(writeonly_spv)])
    require_image_format(writeonly_spv, SPV_IMAGE_FORMAT_RGBA8, "SPIR-V format authority should rewrite the image type to Rgba8")
    layout_facts_text = writeonly_layout_facts.read_text(encoding="utf-8")
    require_contains(layout_facts_text, "same final HLSL-to-SPIR-V bytecode used for CLI output", "SPIR-V authority layout facts should document same-bytecode output/layout provenance")
    require_contains(layout_facts_text, "storage format authority: class readwrite slot 0 rgba32float -> rgba8unorm", "SPIR-V authority layout facts should document the original reflected-to-policy format")
    require_contains(layout_facts_text, "final format rgba8unorm matches the policy", "SPIR-V authority layout facts should document that final reflected bytes match the policy")
    require_contains(layout_facts_text, ".num_readwrite_storage_textures = 1", "SPIR-V authority layout facts should emit the final read-write storage binding count")
    require_contains(result.stdout, "format_authority=explicit (rgba32float -> rgba8unorm)", "SPIR-V authority should log the reflected-to-policy format")

    rg32_spv = out_dir / "writeonly-storage-rg32.spv"
    rg32_layout_facts = out_dir / "writeonly-storage-rg32.c"
    run([
        shadercross,
        str(writeonly_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(rg32_spv),
        "--resource-layout-c", str(rg32_layout_facts),
        "--resource-layout-symbol-prefix", "writeonly_storage_rg32",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rg32float,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(rg32_spv)])
    require_image_format(rg32_spv, SPV_IMAGE_FORMAT_RG32F, "SPIR-V format authority should rewrite the image type to Rg32f")
    require_contains(rg32_layout_facts.read_text(encoding="utf-8"), ".format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT", "non-default SPIR-V authority layout facts should emit the final SDL storage texture format")
    if not has_capability(rg32_spv, SPV_CAPABILITY_STORAGE_IMAGE_EXTENDED_FORMATS):
        raise AssertionError("Rg32f storage format authority should add StorageImageExtendedFormats")

    uint_hlsl = write_hlsl(out_dir, "uint-writeonly-storage.comp.hlsl", HLSL_UINT_WRITEONLY_STORAGE)
    uint_spv = out_dir / "uint-writeonly-storage-rgba8ui.spv"
    result = run([
        shadercross,
        str(uint_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(uint_spv),
        "--resource-layout-c", str(out_dir / "uint-writeonly-storage-rgba8ui.c"),
        "--resource-layout-symbol-prefix", "uint_writeonly_storage_rgba8ui",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8uint,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(uint_spv)])
    require_image_format(uint_spv, SPV_IMAGE_FORMAT_RGBA8UI, "SPIR-V format authority should rewrite uint4 image types to Rgba8ui")
    require_contains(result.stdout, "format_authority=explicit (rgba32uint -> rgba8uint)", "SPIR-V authority should allow same-kind unsigned integer narrowing")

    sint_hlsl = write_hlsl(out_dir, "sint-writeonly-storage.comp.hlsl", HLSL_SINT_WRITEONLY_STORAGE)
    sint_spv = out_dir / "sint-writeonly-storage-rgba8i.spv"
    result = run([
        shadercross,
        str(sint_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(sint_spv),
        "--resource-layout-c", str(out_dir / "sint-writeonly-storage-rgba8i.c"),
        "--resource-layout-symbol-prefix", "sint_writeonly_storage_rgba8i",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8sint,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(sint_spv)])
    require_image_format(sint_spv, SPV_IMAGE_FORMAT_RGBA8I, "SPIR-V format authority should rewrite int4 image types to Rgba8i")
    require_contains(result.stdout, "format_authority=explicit (rgba32sint -> rgba8sint)", "SPIR-V authority should allow same-kind signed integer narrowing")

    mixed_hlsl = write_hlsl(out_dir, "mixed-image-type-storage.comp.hlsl", HLSL_MIXED_IMAGE_TYPE_STORAGE)
    mixed_spv = out_dir / "mixed-image-type-storage-selective.spv"
    run([
        shadercross,
        str(mixed_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(mixed_spv),
        "--resource-layout-c", str(out_dir / "mixed-image-type-storage-selective.c"),
        "--resource-layout-symbol-prefix", "mixed_image_type_storage_selective",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(mixed_spv)])
    require_image_format(mixed_spv, SPV_IMAGE_FORMAT_RGBA8, "SPIR-V format authority should rewrite only the authoritative image type")
    require_image_format(mixed_spv, SPV_IMAGE_FORMAT_R32F, "SPIR-V format authority should leave non-authoritative image types unchanged")

    two_hlsl = write_hlsl(out_dir, "two-writeonly-storage.comp.hlsl", HLSL_TWO_WRITEONLY_STORAGE)
    two_same_spv = out_dir / "two-writeonly-storage-same.spv"
    run([
        shadercross,
        str(two_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(two_same_spv),
        "--resource-layout-c", str(out_dir / "two-writeonly-storage-same.c"),
        "--resource-layout-symbol-prefix", "two_writeonly_storage_same",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
        "--storage-texture-slot", "class=readwrite,slot=1,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(two_same_spv)])
    require_image_format(two_same_spv, SPV_IMAGE_FORMAT_RGBA8, "shared image-type resources with the same policy format should rewrite in place")

    result = run([
        shadercross,
        str(two_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "two-writeonly-storage-partial.spv"),
        "--resource-layout-c", str(out_dir / "two-writeonly-storage-partial.c"),
        "--resource-layout-symbol-prefix", "two_writeonly_storage_partial",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "requires every active storage texture sharing image type", "partial shared image-type authority should reject clearly")

    result = run([
        shadercross,
        str(two_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "two-writeonly-storage-divergent.spv"),
        "--resource-layout-c", str(out_dir / "two-writeonly-storage-divergent.c"),
        "--resource-layout-symbol-prefix", "two_writeonly_storage_divergent",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
        "--storage-texture-slot", "class=readwrite,slot=1,texture=2d,format=rgba16float,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "requires type cloning for divergent formats", "divergent shared image-type authority should reject until cloning is implemented")

    array_hlsl = write_hlsl(out_dir, "array-writeonly-storage.comp.hlsl", HLSL_ARRAY_WRITEONLY_STORAGE)
    result = run([
        shadercross,
        str(array_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "array-writeonly-storage-authority.spv"),
        "--resource-layout-c", str(out_dir / "array-writeonly-storage-authority.c"),
        "--resource-layout-symbol-prefix", "array_writeonly_storage_authority",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d_array,format=rgba8unorm,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "shared storage resolver should reject non-2D format authority before SPIR-V patching")
    require_contains(result.stdout, "unsupported_boundary=format_authority_explicit_requires_2d_texture", "non-2D format authority diagnostics should name the unsupported boundary")

    result = run([
        shadercross,
        str(writeonly_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "writeonly-storage-bad-kind.spv"),
        "--resource-layout-c", str(out_dir / "writeonly-storage-bad-kind.c"),
        "--resource-layout-symbol-prefix", "writeonly_storage_bad_kind",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "SPIR-V authority should reject numeric-kind mismatches")
    require_contains(result.stdout, "storage texture 'OutputTexture' stage=compute source_set=1 source_binding=0 class=readwrite slot=0", "SPIR-V authority diagnostics should name the rejected resource")
    require_contains(result.stdout, "requested_policy=--storage-texture-slot class=readwrite,slot=0,texture=2d,format=r32uint,access=write,format_authority=explicit", "SPIR-V authority diagnostics should echo the rejected explicit policy")
    require_contains(result.stdout, "suggested_policy=--storage-texture-slot class=readwrite,slot=0,texture=2d,format=rgba32float,access=write", "SPIR-V authority diagnostics should include the reflected same-source policy")

    readwrite_hlsl = write_hlsl(out_dir, "readwrite-storage.comp.hlsl", HLSL_READWRITE_STORAGE)
    result = run([
        shadercross,
        str(readwrite_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "readwrite-storage-authority.spv"),
        "--resource-layout-c", str(out_dir / "readwrite-storage-authority.c"),
        "--resource-layout-symbol-prefix", "readwrite_storage_authority",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=read_write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "SPIR-V authority should reject read-write access in the first slice")
    require_contains(result.stdout, "unsupported_boundary=format_authority_explicit_requires_write_only_access", "SPIR-V read-write format authority diagnostics should name the unsupported boundary")

    image_atomic_hlsl = write_hlsl(out_dir, "image-atomic-storage.comp.hlsl", HLSL_IMAGE_ATOMIC_STORAGE)
    result = run([
        shadercross,
        str(image_atomic_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "image-atomic-storage-authority.spv"),
        "--resource-layout-c", str(out_dir / "image-atomic-storage-authority.c"),
        "--resource-layout-symbol-prefix", "image_atomic_storage_authority",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "SPIR-V storage texture format authority does not support image texel pointers or image atomics", "SPIR-V authority should reject image atomics")

    result = run([
        shadercross,
        str(writeonly_hlsl),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(out_dir / "writeonly-storage-no-layout_facts.spv"),
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "format_authority=explicit requires WGSL output or HLSL-to-SPIRV output with resource layout C output", "SPIR-V authority should require layout_facts")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
