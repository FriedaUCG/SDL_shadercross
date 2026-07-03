#!/usr/bin/env python3

import json
import re
import shutil
import os
import struct
import subprocess
import sys
from pathlib import Path


SPV_OP_TYPE_IMAGE = 25
SPV_IMAGE_FORMAT_RGBA16F = 2


SAMPLED_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
OpDecorate %sampler DescriptorSet 2
OpDecorate %sampler Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%v2float = OpTypeVector %float 2
%v4float = OpTypeVector %float 4
%float_0 = OpConstant %float 0
%coord = OpConstantComposite %v2float %float_0 %float_0
%image = OpTypeImage %float 2D 0 0 0 1 Unknown
%sampler_type = OpTypeSampler
%sampled_image = OpTypeSampledImage %image
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_uc_sampler = OpTypePointer UniformConstant %sampler_type
%ptr_out_v4float = OpTypePointer Output %v4float
%texture = OpVariable %ptr_uc_image UniformConstant
%sampler = OpVariable %ptr_uc_sampler UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%loaded_sampler = OpLoad %sampler_type %sampler
%combined = OpSampledImage %sampled_image %loaded_texture %loaded_sampler
%color = OpImageSampleImplicitLod %v4float %combined %coord
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


HLSL_VERTEX_WITH_UNUSED_RESOURCE = """
Texture2D<float4> UnusedTexture : register(t0, space2);
SamplerState UnusedSampler : register(s0, space2);

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 Matrix;
};

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
    output.Position = mul(Matrix, float4(input.Position, 1.0));
    return output;
}
""".strip()


HLSL_MATRIX_ORDER_VERTEX = """
cbuffer VertexUniforms : register(b0, space1)
{
    column_major float4x4 Matrix;
};

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
    output.Position = mul(Matrix, float4(input.Position, 1.0));
    return output;
}
""".strip()


HLSL_INSTANCED_MATRIX_ORDER_VERTEX = """
cbuffer VertUniforms : register(b0, space1)
{
    column_major float4x4 ViewProj;
};

struct Input
{
    float3 Position : POSITION;
    float4 Col0 : TEXCOORD0;
    float4 Col1 : TEXCOORD1;
    float4 Col2 : TEXCOORD2;
    float4 Col3 : TEXCOORD3;
};

struct Output
{
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    float4x4 model = transpose(float4x4(input.Col0, input.Col1, input.Col2, input.Col3));
    float4 world = mul(model, float4(input.Position, 1.0));
    output.Position = mul(ViewProj, world);
    return output;
}
""".strip()


HLSL_MSL_RESERVED_OBJECT_DATA_VERTEX = """
StructuredBuffer<float4> object_data : register(t0, space0);

struct Output
{
    float4 Position : SV_Position;
};

Output main(uint vertexID : SV_VertexID)
{
    Output output;
    output.Position = object_data[vertexID];
    return output;
}
""".strip()


HLSL_TEXTURED_FRAGMENT = """
Texture2D<float4> ColorTexture : register(t0, space2);
SamplerState ColorSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    return ColorTexture.Sample(ColorSampler, uv);
}
""".strip()


HLSL_TEXTURE_QUERY_AND_SAMPLE_FRAGMENT = """
Texture2D<float> HeightTexture : register(t0, space2);
SamplerState HeightSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    uint width;
    uint height;
    HeightTexture.GetDimensions(width, height);
    float value = HeightTexture.SampleLevel(HeightSampler, uv, 0.0);
    return float4(value, (float)width / 4096.0, (float)height / 4096.0, 1.0);
}
""".strip()


HLSL_EXPLICIT_LOD_CONTROL_FLOW_FRAGMENT = """
Texture2D<float4> ColorTexture : register(t0, space2);
SamplerState ColorSampler : register(s0, space2);

cbuffer Params : register(b0, space3)
{
    float threshold;
    float3 _pad0;
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 color = float4(0.0, 0.0, 0.0, 1.0);
    float samples = 0.0;

    for (int i = 0; i < 4; i++)
    {
        float offset = (float)i * 0.01;
        if (uv.x + offset < threshold)
        {
            continue;
        }

        color += ColorTexture.SampleLevel(ColorSampler, uv + float2(offset, 0.0), 0.0);
        samples += 1.0;
        if (samples >= 2.0)
        {
            break;
        }
    }

    if (samples > 0.0)
    {
        color /= samples;
    }
    return color;
}
""".strip()


HLSL_WRITEONLY_STORAGE_COMPUTE = """
[[vk::image_format("rgba8")]]
RWTexture2D<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
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


HLSL_UNANNOTATED_ARRAY_WRITEONLY_STORAGE_COMPUTE = """
RWTexture2DArray<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID] = float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_READWRITE_STORAGE_COMPUTE = """
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


def run_with_env(command, env, *, expect_success=True):
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
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


def require_not_contains(text, needle, message):
    if needle in text:
        raise AssertionError(f"{message}\nunexpected: {needle}\ntext:\n{text}")


def require_regex(text, pattern, message):
    if not re.search(pattern, text):
        raise AssertionError(f"{message}\nmissing pattern: {pattern}\ntext:\n{text}")


def require_binding(text, group, binding, message):
    pattern = re.compile(
        r"@group\(\s*" + str(group) + r"u?\s*\)\s*@binding\(\s*" + str(binding) + r"u?\s*\)"
    )
    if not pattern.search(text):
        raise AssertionError(f"{message}\nmissing binding: group={group} binding={binding}\ntext:\n{text}")


def require_not_binding(text, group, binding, message):
    pattern = re.compile(
        r"@group\(\s*" + str(group) + r"u?\s*\)\s*@binding\(\s*" + str(binding) + r"u?\s*\)"
    )
    if pattern.search(text):
        raise AssertionError(f"{message}\nunexpected binding: group={group} binding={binding}\ntext:\n{text}")


def assemble(spirv_as, spirv_val, out_dir, name, source):
    asm_path = out_dir / f"{name}.spvasm"
    spv_path = out_dir / f"{name}.spv"
    asm_path.write_text(source + "\n", encoding="utf-8")
    run([spirv_as, "--target-env", "vulkan1.0", str(asm_path), "-o", str(spv_path)])
    run([spirv_val, "--target-env", "vulkan1.0", str(spv_path)])
    return spv_path


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


def require_image_format(path, expected, message):
    formats = image_formats(path)
    if expected not in formats:
        raise AssertionError(f"{message}\nmissing image format {expected}; got {formats}")


def validate_wgsl(tint, wgsl_path, out_dir, name):
    validated = out_dir / f"{name}.validated.wgsl"
    run([
        tint,
        "--format=wgsl",
        "--validate=true",
        "--output-name",
        str(validated),
        str(wgsl_path),
    ])
    if not validated.exists():
        raise AssertionError(f"Tint validation did not write {validated}")


def make_tint_no_input_format_wrapper(tint, out_dir):
    script_path = out_dir / "tint_no_input_format_wrapper.py"
    script_path.write_text(
        "\n".join([
            "#!/usr/bin/env python3",
            "import subprocess",
            "import sys",
            "",
            "real_tint = sys.argv[1]",
            "args = sys.argv[2:]",
            "for arg in args:",
            "    if arg == '--input-format' or arg == '-if' or arg.startswith('--input-format='):",
            "        print('error: unknown option: --input-format', file=sys.stderr)",
            "        sys.exit(2)",
            "completed = subprocess.run([real_tint, *args], check=False)",
            "sys.exit(completed.returncode)",
            "",
        ]),
        encoding="utf-8",
    )

    if os.name == "nt":
        wrapper_path = out_dir / "tint-no-input-format.cmd"
        wrapper_path.write_text(
            f"@echo off\r\n\"{sys.executable}\" \"{script_path}\" \"{tint}\" %*\r\n",
            encoding="utf-8",
        )
    else:
        wrapper_path = out_dir / "tint-no-input-format"
        wrapper_path.write_text(
            f"#!/bin/sh\nexec \"{sys.executable}\" \"{script_path}\" \"{tint}\" \"$@\"\n",
            encoding="utf-8",
        )
        wrapper_path.chmod(0o755)

    return str(wrapper_path)


def make_tint_matrix_order_broken_wrapper(tint, out_dir):
    script_path = out_dir / "tint_matrix_order_broken_wrapper.py"
    script_path.write_text(
        "\n".join([
            "#!/usr/bin/env python3",
            "import subprocess",
            "import sys",
            "",
            "real_tint = sys.argv[1]",
            "args = sys.argv[2:]",
            "completed = subprocess.run([real_tint, *args], check=False)",
            "if completed.returncode != 0:",
            "    sys.exit(completed.returncode)",
            "try:",
            "    output = args[args.index('--output-name') + 1]",
            "except (ValueError, IndexError):",
            "    print('missing --output-name', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'r', encoding='utf-8') as f:",
            "    text = f.read()",
            "text = text.replace('transpose(VertexUniforms.Matrix)', 'VertexUniforms.Matrix')",
            "with open(output, 'w', encoding='utf-8') as f:",
            "    f.write(text)",
            "sys.exit(0)",
            "",
        ]),
        encoding="utf-8",
    )

    if os.name == "nt":
        wrapper_path = out_dir / "tint-matrix-order-broken.cmd"
        wrapper_path.write_text(
            f"@echo off\r\n\"{sys.executable}\" \"{script_path}\" \"{tint}\" %*\r\n",
            encoding="utf-8",
        )
    else:
        wrapper_path = out_dir / "tint-matrix-order-broken"
        wrapper_path.write_text(
            f"#!/bin/sh\nexec \"{sys.executable}\" \"{script_path}\" \"{tint}\" \"$@\"\n",
            encoding="utf-8",
        )
        wrapper_path.chmod(0o755)

    return str(wrapper_path)


def make_tint_storage_access_wrapper(tint, out_dir):
    script_path = out_dir / "tint_storage_access_wrapper.py"
    script_path.write_text(
        "\n".join([
            "#!/usr/bin/env python3",
            "import subprocess",
            "import sys",
            "",
            "real_tint = sys.argv[1]",
            "args = sys.argv[2:]",
            "completed = subprocess.run([real_tint, *args], check=False)",
            "if completed.returncode != 0:",
            "    sys.exit(completed.returncode)",
            "try:",
            "    output = args[args.index('--output-name') + 1]",
            "except (ValueError, IndexError):",
            "    print('missing --output-name', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'r', encoding='utf-8') as f:",
            "    text = f.read()",
            "old = 'texture_storage_2d<rgba8unorm, read_write>'",
            "new = 'texture_storage_2d<rgba8unorm, read>'",
            "if old not in text:",
            "    print('expected storage texture declaration was not found', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'w', encoding='utf-8') as f:",
            "    f.write(text.replace(old, new, 1))",
            "sys.exit(0)",
            "",
        ]),
        encoding="utf-8",
    )

    if os.name == "nt":
        wrapper_path = out_dir / "tint-storage-access.cmd"
        wrapper_path.write_text(
            f"@echo off\r\n\"{sys.executable}\" \"{script_path}\" \"{tint}\" %*\r\n",
            encoding="utf-8",
        )
    else:
        wrapper_path = out_dir / "tint-storage-access"
        wrapper_path.write_text(
            f"#!/bin/sh\nexec \"{sys.executable}\" \"{script_path}\" \"{tint}\" \"$@\"\n",
            encoding="utf-8",
        )
        wrapper_path.chmod(0o755)

    return str(wrapper_path)


def make_tint_storage_format_wrapper(tint, out_dir):
    script_path = out_dir / "tint_storage_format_wrapper.py"
    script_path.write_text(
        "\n".join([
            "#!/usr/bin/env python3",
            "import subprocess",
            "import sys",
            "",
            "real_tint = sys.argv[1]",
            "args = sys.argv[2:]",
            "completed = subprocess.run([real_tint, *args], check=False)",
            "if completed.returncode != 0:",
            "    sys.exit(completed.returncode)",
            "try:",
            "    output = args[args.index('--output-name') + 1]",
            "except (ValueError, IndexError):",
            "    print('missing --output-name', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'r', encoding='utf-8') as f:",
            "    text = f.read()",
            "old = 'texture_storage_2d<rgba32float, read_write>'",
            "new = 'texture_storage_2d<r32float, read_write>'",
            "if old not in text:",
            "    print('expected storage texture declaration was not found', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'w', encoding='utf-8') as f:",
            "    f.write(text.replace(old, new, 1))",
            "sys.exit(0)",
            "",
        ]),
        encoding="utf-8",
    )

    if os.name == "nt":
        wrapper_path = out_dir / "tint-storage-format.cmd"
        wrapper_path.write_text(
            f"@echo off\r\n\"{sys.executable}\" \"{script_path}\" \"{tint}\" %*\r\n",
            encoding="utf-8",
        )
    else:
        wrapper_path = out_dir / "tint-storage-format"
        wrapper_path.write_text(
            f"#!/bin/sh\nexec \"{sys.executable}\" \"{script_path}\" \"{tint}\" \"$@\"\n",
            encoding="utf-8",
        )
        wrapper_path.chmod(0o755)

    return str(wrapper_path)


def make_tint_split_storage_attributes_wrapper(tint, out_dir):
    script_path = out_dir / "tint_split_storage_attributes_wrapper.py"
    script_path.write_text(
        "\n".join([
            "#!/usr/bin/env python3",
            "import re",
            "import subprocess",
            "import sys",
            "",
            "real_tint = sys.argv[1]",
            "args = sys.argv[2:]",
            "completed = subprocess.run([real_tint, *args], check=False)",
            "if completed.returncode != 0:",
            "    sys.exit(completed.returncode)",
            "try:",
            "    output = args[args.index('--output-name') + 1]",
            "except (ValueError, IndexError):",
            "    print('missing --output-name', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'r', encoding='utf-8') as f:",
            "    text = f.read()",
            "updated, count = re.subn(",
            "    r'@group\\(\\s*1u?\\s*\\)\\s*@binding\\(\\s*0u?\\s*\\)\\s*var',",
            "    '@group ( 1u )\\n@binding ( 0 )\\nvar',",
            "    text,",
            "    count=1,",
            ")",
            "if count != 1:",
            "    print('expected storage texture binding declaration was not found', file=sys.stderr)",
            "    sys.exit(2)",
            "with open(output, 'w', encoding='utf-8') as f:",
            "    f.write(updated)",
            "sys.exit(0)",
            "",
        ]),
        encoding="utf-8",
    )

    if os.name == "nt":
        wrapper_path = out_dir / "tint-split-storage-attributes.cmd"
        wrapper_path.write_text(
            f"@echo off\r\n\"{sys.executable}\" \"{script_path}\" \"{tint}\" %*\r\n",
            encoding="utf-8",
        )
    else:
        wrapper_path = out_dir / "tint-split-storage-attributes"
        wrapper_path.write_text(
            f"#!/bin/sh\nexec \"{sys.executable}\" \"{script_path}\" \"{tint}\" \"$@\"\n",
            encoding="utf-8",
        )
        wrapper_path.chmod(0o755)

    return str(wrapper_path)


def hlsl_to_wgsl_supported(shadercross, tint, out_dir, hlsl_required):
    hlsl_probe = out_dir / "hlsl_probe.vert.hlsl"
    wgsl_probe = out_dir / "hlsl_probe.wgsl"
    hlsl_probe.write_text("float4 main(float4 position : POSITION) : SV_Position { return position; }\n", encoding="utf-8")
    result = subprocess.run([
        shadercross,
        str(hlsl_probe),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(wgsl_probe),
    ], check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode == 0:
        validate_wgsl(tint, wgsl_probe, out_dir, "hlsl-probe")
        return True

    if hlsl_required:
        raise AssertionError(
            "HLSL-to-WGSL probe failed, but this test was configured with --hlsl-required.\n"
            f"{result.stdout}"
        )

    print("Skipping HLSL WGSL codegen coverage because this build cannot compile HLSL to SPIR-V.")
    print(result.stdout)
    return False


def main():
    if len(sys.argv) < 6:
        print(f"Usage: {sys.argv[0]} <shadercross> <spirv-as> <spirv-val> <tint> <output-dir> [--hlsl-required]", file=sys.stderr)
        return 2

    shadercross = sys.argv[1]
    spirv_as = sys.argv[2]
    spirv_val = sys.argv[3]
    tint = sys.argv[4]
    out_dir = Path(sys.argv[5])
    hlsl_required = False

    i = 6
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

    sampled_spv = assemble(spirv_as, spirv_val, out_dir, "sampled-fragment", SAMPLED_FRAGMENT_SPVASM)
    sampled_wgsl = out_dir / "sampled-fragment.wgsl"
    sampled_result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(sampled_wgsl),
    ])
    sampled_text = sampled_wgsl.read_text(encoding="utf-8")
    require_contains(sampled_result.stdout, "Tint note: shadercross: WGSL produced via Tint and SDL_shadercross", "SPIR-V to WGSL should record the selected Tint provenance")
    require_contains(sampled_result.stdout, "tint_manifest=", "SPIR-V to WGSL should record bundled Tint manifest provenance when available")
    require_contains(sampled_result.stdout, "tint_conformance=matrix-order-row-major-square:passed", "SPIR-V to WGSL should record the selected Tint conformance result")
    require_not_contains(sampled_result.stdout, "tint_path=", "SPIR-V to WGSL should not print local Tint paths in normal diagnostics")
    require_not_contains(sampled_text, "tint_path=", "SPIR-V to WGSL should not embed local Tint paths in generated WGSL")
    require_not_contains(sampled_text, "tint_manifest=", "SPIR-V to WGSL should not embed Tint manifest diagnostics in generated WGSL")
    require_not_contains(sampled_text, "tint_conformance=", "SPIR-V to WGSL should not embed Tint conformance diagnostics in generated WGSL")
    require_not_contains(sampled_text, "WGSL produced via Tint", "SPIR-V to WGSL should keep Tint provenance in diagnostics, not shader text")
    require_contains(sampled_text, "@fragment", "SPIR-V to WGSL should emit a fragment entry point")
    require_binding(sampled_text, 2, 0, "SPIR-V to WGSL should preserve SDL sampled binding group")
    require_binding(sampled_text, 2, 1, "SPIR-V to WGSL should preserve SDL paired sampler binding")
    validate_wgsl(tint, sampled_wgsl, out_dir, "sampled-fragment")

    compatibility_tint = make_tint_no_input_format_wrapper(tint, out_dir)
    compatibility_wgsl = out_dir / "sampled-fragment-no-input-format.wgsl"
    run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", compatibility_tint,
        "-o", str(compatibility_wgsl),
    ])
    require_binding(compatibility_wgsl.read_text(encoding="utf-8"), 2, 0, "WGSL output should retry Tint without --input-format when the executable rejects that option")
    validate_wgsl(tint, compatibility_wgsl, out_dir, "sampled-fragment-no-input-format")

    env_wgsl = out_dir / "sampled-fragment-env.wgsl"
    env = os.environ.copy()
    env["SDL_SHADERCROSS_TINT"] = tint
    run_with_env([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "-o", str(env_wgsl),
    ], env)
    require_binding(env_wgsl.read_text(encoding="utf-8"), 2, 0, "SDL_SHADERCROSS_TINT fallback should produce WGSL")
    validate_wgsl(tint, env_wgsl, out_dir, "sampled-fragment-env")

    explicit_wins_wgsl = out_dir / "sampled-fragment-explicit-wins.wgsl"
    bad_env = os.environ.copy()
    bad_env["SDL_SHADERCROSS_TINT"] = str(out_dir / "wrong-env-tint")
    run_with_env([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(explicit_wins_wgsl),
    ], bad_env)
    require_binding(explicit_wins_wgsl.read_text(encoding="utf-8"), 2, 0, "explicit --tint should override SDL_SHADERCROSS_TINT")
    validate_wgsl(tint, explicit_wins_wgsl, out_dir, "sampled-fragment-explicit-wins")

    bad_tint_output = out_dir / "bad-tint.wgsl"
    bad_tint_result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", str(out_dir / "missing-tint-executable"),
        "-o", str(bad_tint_output),
    ], expect_success=False)
    require_contains(bad_tint_result.stdout, "Failed to run Tint executable", "missing Tint executable should fail clearly")
    if bad_tint_output.exists():
        raise AssertionError("missing Tint executable should not leave a partial WGSL output")

    broken_matrix_tint = make_tint_matrix_order_broken_wrapper(tint, out_dir)
    broken_matrix_output = out_dir / "broken-matrix-canary.wgsl"
    broken_matrix_result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", broken_matrix_tint,
        "-o", str(broken_matrix_output),
    ], expect_success=False)
    require_contains(broken_matrix_result.stdout, "failed SDL_shadercross WGSL conformance canary 'matrix-order-row-major-square'", "matrix-order-broken Tint wrapper should fail the WGSL conformance gate")
    if broken_matrix_output.exists():
        raise AssertionError("failed Tint conformance should not leave a partial WGSL output")

    unvalidated_output = out_dir / "unvalidated-matrix-wrapper.wgsl"
    unvalidated_result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", broken_matrix_tint,
        "--allow-unvalidated-tint",
        "-o", str(unvalidated_output),
    ])
    require_contains(unvalidated_result.stdout, "tint_conformance=not_checked:allow-unvalidated-tint", "development-only unvalidated WGSL should record that the conformance gate was skipped")
    require_contains(unvalidated_result.stdout, "WGSL output is using unvalidated Tint", "development-only unvalidated WGSL should warn that the conformance gate was skipped")
    validate_wgsl(tint, unvalidated_output, out_dir, "unvalidated-matrix-wrapper")

    if not hlsl_to_wgsl_supported(shadercross, tint, out_dir, hlsl_required):
        return 0

    hlsl_vertex = out_dir / "unused-resource.vert.hlsl"
    hlsl_vertex.write_text(HLSL_VERTEX_WITH_UNUSED_RESOURCE + "\n", encoding="utf-8")
    default_wgsl = out_dir / "unused-resource-default.wgsl"
    culled_wgsl = out_dir / "unused-resource-culled.wgsl"
    culled_layout_c = out_dir / "unused-resource-culled-layout.c"
    run([
        shadercross,
        str(hlsl_vertex),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(default_wgsl),
    ])
    run([
        shadercross,
        str(hlsl_vertex),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "WGSL",
        "--tint", tint,
        "-c",
        "-o", str(culled_wgsl),
        "--resource-layout-c", str(culled_layout_c),
        "--resource-layout-symbol-prefix", "unused_resource_culled",
    ])
    default_text = default_wgsl.read_text(encoding="utf-8")
    culled_text = culled_wgsl.read_text(encoding="utf-8")
    culled_layout_text = culled_layout_c.read_text(encoding="utf-8")
    require_binding(default_text, 1, 0, "default HLSL WGSL should preserve used uniform binding")
    require_binding(default_text, 2, 0, "default HLSL WGSL should not silently cull unused shared resources")
    require_binding(culled_text, 1, 0, "culled HLSL WGSL should preserve used uniform binding")
    require_not_binding(culled_text, 2, 0, "culled HLSL WGSL should honor the explicit cull option")
    require_contains(culled_layout_text, "same HLSL-to-SPIR-V bytecode used for CLI output; cull_unused_bindings=true", "culled HLSL WGSL layout should document same-bytecode cull provenance")
    require_contains(culled_layout_text, ".num_uniform_buffers = 1", "culled HLSL WGSL layout should preserve the used uniform buffer")
    require_contains(culled_layout_text, ".num_samplers = 0", "culled HLSL WGSL layout should omit the unused sampled texture/sampler")
    validate_wgsl(tint, default_wgsl, out_dir, "unused-resource-default")
    validate_wgsl(tint, culled_wgsl, out_dir, "unused-resource-culled")

    # Row-major SPIR-V matrices should be transposed before WGSL row-vector
    # multiplication so HLSL mul(matrix, vector) stays equivalent to native.
    matrix_order_vertex = out_dir / "matrix-order.vert.hlsl"
    matrix_order_vertex.write_text(HLSL_MATRIX_ORDER_VERTEX + "\n", encoding="utf-8")
    matrix_order_wgsl = out_dir / "matrix-order.wgsl"
    matrix_order_msl = out_dir / "matrix-order.msl"
    run([
        shadercross,
        str(matrix_order_vertex),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(matrix_order_wgsl),
    ])
    run([
        shadercross,
        str(matrix_order_vertex),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "MSL",
        "-o", str(matrix_order_msl),
    ])
    matrix_order_wgsl_text = matrix_order_wgsl.read_text(encoding="utf-8")
    matrix_order_msl_text = matrix_order_msl.read_text(encoding="utf-8")
    require_regex(
        matrix_order_wgsl_text,
        r"vec4<f32>\([^\n;]*\)\s*\*\s*transpose\(VertexUniforms\.Matrix\)",
        "WGSL row-major lowering should transpose the uniform matrix before multiplication; check that the configured Tint includes the row-major fix",
    )
    require_contains(
        matrix_order_msl_text,
        "VertexUniforms.Matrix * float4(",
        "MSL output should preserve the native matrix-vector order for the same HLSL source",
    )
    validate_wgsl(tint, matrix_order_wgsl, out_dir, "matrix-order")

    instanced_matrix_order_vertex = out_dir / "instanced-matrix-order.vert.hlsl"
    instanced_matrix_order_vertex.write_text(HLSL_INSTANCED_MATRIX_ORDER_VERTEX + "\n", encoding="utf-8")
    instanced_matrix_order_wgsl = out_dir / "instanced-matrix-order.wgsl"
    run([
        shadercross,
        str(instanced_matrix_order_vertex),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(instanced_matrix_order_wgsl),
    ])
    instanced_matrix_order_text = instanced_matrix_order_wgsl.read_text(encoding="utf-8")
    require_regex(
        instanced_matrix_order_text,
        r"vec4<f32>\([^\n;]*\)\s*\*\s*transpose\(mat4x4<f32>",
        "WGSL row-major lowering should preserve the constructed model matrix semantics; check that the configured Tint includes the row-major fix",
    )
    require_regex(
        instanced_matrix_order_text,
        r"\)\s*\*\s*transpose\(VertUniforms\.ViewProj\)",
        "WGSL row-major lowering should transpose the uniform view-projection matrix after instancing; check that the configured Tint includes the row-major fix",
    )
    validate_wgsl(tint, instanced_matrix_order_wgsl, out_dir, "instanced-matrix-order")

    msl_reserved_vertex = out_dir / "msl-reserved-object-data.vert.hlsl"
    msl_reserved_vertex.write_text(HLSL_MSL_RESERVED_OBJECT_DATA_VERTEX + "\n", encoding="utf-8")
    msl_reserved_output = out_dir / "msl-reserved-object-data.msl"
    run([
        shadercross,
        str(msl_reserved_vertex),
        "-s", "HLSL",
        "-t", "vertex",
        "-d", "MSL",
        "--msl-version", "3.0.0",
        "-o", str(msl_reserved_output),
    ])
    msl_reserved_text = msl_reserved_output.read_text(encoding="utf-8")
    require_contains(
        msl_reserved_text,
        "object_data0",
        "MSL output should rename resource identifiers that collide with newer MSL address spaces",
    )
    require_not_contains(
        msl_reserved_text,
        " object_data [[buffer",
        "MSL output should not emit the reserved object_data identifier as a buffer parameter",
    )

    hlsl_fragment = out_dir / "textured.frag.hlsl"
    hlsl_fragment.write_text(HLSL_TEXTURED_FRAGMENT + "\n", encoding="utf-8")
    textured_wgsl = out_dir / "textured.wgsl"
    textured_layout_facts_c = out_dir / "textured_layout_facts.c"
    result = run([
        shadercross,
        str(hlsl_fragment),
        "-s", "HLSL",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(textured_wgsl),
        "--resource-layout-c", str(textured_layout_facts_c),
        "--resource-layout-symbol-prefix", "textured",
    ])
    textured_text = textured_wgsl.read_text(encoding="utf-8")
    layout_facts_text = textured_layout_facts_c.read_text(encoding="utf-8")
    require_contains(textured_text, "@fragment", "HLSL to WGSL should emit a fragment entry point")
    require_binding(textured_text, 2, 0, "HLSL to WGSL should preserve sampled texture binding")
    require_binding(textured_text, 2, 1, "HLSL to WGSL should preserve paired sampler binding")
    require_contains(layout_facts_text, "static const SDL_GPUShaderResourceLayout textured_resource_layout", "HLSL WGSL plus layout facts should emit layout facts")
    require_contains(layout_facts_text, ".stage = SDL_GPU_SHADERSTAGE_FRAGMENT", "HLSL WGSL layout facts should preserve fragment stage")
    require_contains(layout_facts_text, ".num_samplers = 1,", "HLSL WGSL layout facts should preserve the sampled texture/sampler pair count")
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 resolved ambiguous float sampler policy", "HLSL WGSL layout facts should keep existing sampled policy diagnostics")
    validate_wgsl(tint, textured_wgsl, out_dir, "textured")

    hlsl_query_sample_fragment = out_dir / "texture-query-and-sample.frag.hlsl"
    hlsl_query_sample_fragment.write_text(HLSL_TEXTURE_QUERY_AND_SAMPLE_FRAGMENT + "\n", encoding="utf-8")
    query_sample_wgsl = out_dir / "texture-query-and-sample.wgsl"
    query_sample_layout_facts_c = out_dir / "texture-query-and-sample.c"
    query_sample_json = out_dir / "texture-query-and-sample.json"
    run([
        shadercross,
        str(hlsl_query_sample_fragment),
        "-s", "HLSL",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(query_sample_wgsl),
        "--resource-layout-c", str(query_sample_layout_facts_c),
        "--resource-layout-symbol-prefix", "texture_query_and_sample",
    ])
    query_sample_text = query_sample_wgsl.read_text(encoding="utf-8")
    query_sample_layout_facts_text = query_sample_layout_facts_c.read_text(encoding="utf-8")
    require_contains(query_sample_text, "textureDimensions", "HLSL texture query plus sampling should preserve texture dimension queries in WGSL")
    require_contains(query_sample_text, "textureSampleLevel", "HLSL texture query plus sampling should preserve explicit sampled texture reads in WGSL")
    require_binding(query_sample_text, 2, 0, "HLSL texture query plus sampling should preserve sampled texture binding")
    require_binding(query_sample_text, 2, 1, "HLSL texture query plus sampling should preserve paired sampler binding")
    require_contains(query_sample_layout_facts_text, ".stage = SDL_GPU_SHADERSTAGE_FRAGMENT", "HLSL texture query plus sampling layout facts should preserve fragment stage")
    require_contains(query_sample_layout_facts_text, ".num_samplers = 1,", "HLSL texture query plus sampling layout facts should preserve the sampled texture/sampler pair count")
    require_contains(query_sample_layout_facts_text, "resolved ambiguous float sampler policy", "HLSL texture query plus sampling layout facts should resolve the sampled texture policy")
    validate_wgsl(tint, query_sample_wgsl, out_dir, "texture-query-and-sample")
    run([
        shadercross,
        str(hlsl_query_sample_fragment),
        "-s", "HLSL",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(query_sample_json),
    ])
    query_sample_json_text = query_sample_json.read_text(encoding="utf-8")
    query_sample_json_data = json.loads(query_sample_json_text)
    require_contains(query_sample_json_text, '"sampled_texture_sampler_pairs": [{ "combined_id": null', "HLSL texture query plus sampling JSON should recover sampled pairs from SPIR-V scan")
    require_contains(query_sample_json_text, '"image_name": "HeightTexture"', "HLSL texture query plus sampling JSON should name the sampled texture")
    require_contains(query_sample_json_text, '"sampler_name": "HeightSampler"', "HLSL texture query plus sampling JSON should name the paired sampler")
    require_contains(query_sample_json_text, '"sampled_textures": [{ "stage": "fragment", "slot": 0', "HLSL texture query plus sampling JSON should emit SDL sampled texture layout")
    require_not_contains(query_sample_json_text, "spvc_compiler_build_combined_image_samplers failed", "HLSL texture query plus sampling JSON should not report the raw SPIRV-Cross failure after recovery")
    query_sample_pairs = query_sample_json_data["resource_details"]["sampled_texture_sampler_pairs"]
    query_sample_layout = query_sample_json_data["sdl_resource_layout"]["sampled_textures"]
    if len(query_sample_pairs) != 1:
        raise AssertionError(f"HLSL texture query plus sampling JSON should emit exactly one sampled pair\n{query_sample_json_text}")
    if query_sample_pairs[0]["pair_derivation"] != "spirv_scan":
        raise AssertionError(f"HLSL texture query plus sampling JSON should identify SPIR-V scan pair recovery\n{query_sample_json_text}")
    if len(query_sample_layout) != 1:
        raise AssertionError(f"HLSL texture query plus sampling JSON should emit exactly one SDL sampled texture layout entry\n{query_sample_json_text}")
    if query_sample_layout[0]["pair_derivation"] != "spirv_scan":
        raise AssertionError(f"HLSL texture query plus sampling JSON layout should identify SPIR-V scan pair recovery\n{query_sample_json_text}")

    # This data-dependent branch shape must keep explicit-LOD sampling; implicit
    # derivatives would be invalid under WGSL uniformity rules.
    hlsl_control_flow_fragment = out_dir / "explicit-lod-control-flow.frag.hlsl"
    hlsl_control_flow_fragment.write_text(HLSL_EXPLICIT_LOD_CONTROL_FLOW_FRAGMENT + "\n", encoding="utf-8")
    control_flow_wgsl = out_dir / "explicit-lod-control-flow.wgsl"
    run([
        shadercross,
        str(hlsl_control_flow_fragment),
        "-s", "HLSL",
        "-t", "fragment",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(control_flow_wgsl),
    ])
    control_flow_text = control_flow_wgsl.read_text(encoding="utf-8")
    require_contains(control_flow_text, "textureSampleLevel", "HLSL explicit-LOD sampling in data-dependent control flow should emit WGSL explicit-LOD sampling")
    require_not_contains(control_flow_text, "textureSample(", "HLSL explicit-LOD sampling in data-dependent control flow should not emit implicit-LOD WGSL sampling")
    require_binding(control_flow_text, 2, 0, "HLSL explicit-LOD control-flow WGSL should preserve sampled texture binding")
    require_binding(control_flow_text, 2, 1, "HLSL explicit-LOD control-flow WGSL should preserve paired sampler binding")
    require_binding(control_flow_text, 3, 0, "HLSL explicit-LOD control-flow WGSL should preserve uniform binding")
    require_contains(control_flow_text, "Params.threshold", "HLSL explicit-LOD control-flow WGSL should keep data-dependent uniform control flow")
    validate_wgsl(tint, control_flow_wgsl, out_dir, "explicit-lod-control-flow")

    hlsl_writeonly_storage = out_dir / "writeonly-storage.comp.hlsl"
    hlsl_writeonly_storage.write_text(HLSL_WRITEONLY_STORAGE_COMPUTE + "\n", encoding="utf-8")
    writeonly_storage_wgsl = out_dir / "writeonly-storage.wgsl"
    run([
        shadercross,
        str(hlsl_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(writeonly_storage_wgsl),
    ])
    writeonly_storage_text = writeonly_storage_wgsl.read_text(encoding="utf-8")
    require_binding(writeonly_storage_text, 1, 0, "storage texture WGSL should preserve the SDL compute read-write binding")
    require_contains(writeonly_storage_text, "texture_storage_2d<rgba8unorm, write>", "storage texture WGSL should narrow write-only observed use to WGSL write access")
    require_not_contains(writeonly_storage_text, "texture_storage_2d<rgba8unorm, read_write>", "storage texture WGSL should not leave Tint's broad read_write declaration")
    validate_wgsl(tint, writeonly_storage_wgsl, out_dir, "writeonly-storage")

    split_storage_attributes_tint = make_tint_split_storage_attributes_wrapper(tint, out_dir)
    split_storage_attributes_wgsl = out_dir / "writeonly-storage-split-attributes.wgsl"
    run([
        shadercross,
        str(hlsl_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", split_storage_attributes_tint,
        "-o", str(split_storage_attributes_wgsl),
    ])
    split_storage_attributes_text = split_storage_attributes_wgsl.read_text(encoding="utf-8")
    require_contains(split_storage_attributes_text, "texture_storage_2d<rgba8unorm, write>", "storage texture WGSL patching should handle split group/binding attributes")
    require_contains(split_storage_attributes_text, "@group ( 1u )\n@binding ( 0 )\nvar", "storage texture WGSL patching should preserve split attributes from Tint")
    validate_wgsl(tint, split_storage_attributes_wgsl, out_dir, "writeonly-storage-split-attributes")

    hlsl_unannotated_writeonly_storage = out_dir / "unannotated-writeonly-storage.comp.hlsl"
    hlsl_unannotated_writeonly_storage.write_text(HLSL_UNANNOTATED_WRITEONLY_STORAGE_COMPUTE + "\n", encoding="utf-8")
    unannotated_authority_wgsl = out_dir / "unannotated-writeonly-storage-authority.wgsl"
    unannotated_authority_layout_facts = out_dir / "unannotated-writeonly-storage-authority.c"
    result = run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(unannotated_authority_wgsl),
        "--resource-layout-c", str(unannotated_authority_layout_facts),
        "--resource-layout-symbol-prefix", "unannotated_storage_authority",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba16float,access=write,format_authority=explicit",
    ])
    unannotated_authority_text = unannotated_authority_wgsl.read_text(encoding="utf-8")
    unannotated_authority_layout_facts_text = unannotated_authority_layout_facts.read_text(encoding="utf-8")
    require_contains(unannotated_authority_text, "texture_storage_2d<rgba16float, write>", "storage texture format authority should patch WGSL to the policy format")
    require_contains(unannotated_authority_layout_facts_text, "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT", "storage texture format authority should patch generated layout facts to the policy format")
    require_contains(unannotated_authority_layout_facts_text, "format_authority=explicit (rgba32float -> rgba16float)", "storage texture format authority should document the reflected-to-policy format")
    require_contains(result.stdout, "format_authority=explicit (rgba32float -> rgba16float)", "storage texture format authority should log the reflected-to-policy format")
    validate_wgsl(tint, unannotated_authority_wgsl, out_dir, "unannotated-writeonly-storage-authority")

    unannotated_authority_spv = out_dir / "unannotated-writeonly-storage-authority.spv"
    unannotated_authority_spv_layout_facts = out_dir / "unannotated-writeonly-storage-authority-spirv.c"
    run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "SPIRV",
        "-o", str(unannotated_authority_spv),
        "--resource-layout-c", str(unannotated_authority_spv_layout_facts),
        "--resource-layout-symbol-prefix", "unannotated_storage_authority_spirv",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba16float,access=write,format_authority=explicit",
    ])
    run([spirv_val, "--target-env", "vulkan1.0", str(unannotated_authority_spv)])
    require_image_format(unannotated_authority_spv, SPV_IMAGE_FORMAT_RGBA16F, "SPIR-V storage texture format authority should patch the same policy format as WGSL")
    unannotated_authority_spv_layout_facts_text = unannotated_authority_spv_layout_facts.read_text(encoding="utf-8")
    for needle in [
        ".texture_type = SDL_GPU_TEXTURETYPE_2D",
        ".format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT",
        ".access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY",
        ".num_readwrite_storage_textures = 1",
    ]:
        require_contains(unannotated_authority_layout_facts_text, needle, "WGSL authority layout facts should carry the shared storage decision")
        require_contains(unannotated_authority_spv_layout_facts_text, needle, "SPIR-V authority layout facts should carry the same storage decision")

    result = run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(out_dir / "unannotated-writeonly-storage-authority-no-layout_facts.wgsl"),
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "format_authority=explicit requires WGSL output or HLSL-to-SPIRV output with resource layout C output", "storage texture format authority should require same-command layout_facts")

    result = run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(out_dir / "unannotated-writeonly-storage-bad-policy.wgsl"),
        "--resource-layout-c", str(out_dir / "unannotated-writeonly-storage-bad-policy.c"),
        "--resource-layout-symbol-prefix", "unannotated_storage_bad_policy",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "storage texture policy should keep exact format matching by default")

    result = run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(out_dir / "unannotated-writeonly-storage-bad-kind.wgsl"),
        "--resource-layout-c", str(out_dir / "unannotated-writeonly-storage-bad-kind.c"),
        "--resource-layout-symbol-prefix", "unannotated_storage_bad_kind",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "storage texture format authority should reject numeric-kind mismatches")

    hlsl_unannotated_array_writeonly_storage = out_dir / "unannotated-array-writeonly-storage.comp.hlsl"
    hlsl_unannotated_array_writeonly_storage.write_text(HLSL_UNANNOTATED_ARRAY_WRITEONLY_STORAGE_COMPUTE + "\n", encoding="utf-8")
    result = run([
        shadercross,
        str(hlsl_unannotated_array_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(out_dir / "unannotated-array-writeonly-storage-authority.wgsl"),
        "--resource-layout-c", str(out_dir / "unannotated-array-writeonly-storage-authority.c"),
        "--resource-layout-symbol-prefix", "unannotated_array_storage_authority",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d_array,format=rgba8unorm,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "WGSL storage texture format authority should reject non-2D textures at the shared resolver")
    require_contains(result.stdout, "unsupported_boundary=format_authority_explicit_requires_2d_texture", "WGSL non-2D format authority diagnostics should name the unsupported boundary")

    result = run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(out_dir / "unannotated-writeonly-storage-readwrite-authority.wgsl"),
        "--resource-layout-c", str(out_dir / "unannotated-writeonly-storage-readwrite-authority.c"),
        "--resource-layout-symbol-prefix", "unannotated_storage_readwrite_authority",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=read_write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "storage texture format authority should reject read-write access in the first slice")
    require_contains(result.stdout, "unsupported_boundary=format_authority_explicit_requires_write_only_access", "WGSL read-write format authority diagnostics should name the unsupported boundary")

    bad_storage_format_tint = make_tint_storage_format_wrapper(tint, out_dir)
    result = run([
        shadercross,
        str(hlsl_unannotated_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", bad_storage_format_tint,
        "--allow-unvalidated-tint",
        "-o", str(out_dir / "unannotated-writeonly-storage-bad-tint-format.wgsl"),
        "--resource-layout-c", str(out_dir / "unannotated-writeonly-storage-bad-tint-format.c"),
        "--resource-layout-symbol-prefix", "unannotated_storage_bad_tint_format",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ], expect_success=False)
    require_contains(result.stdout, "uses a format that does not match storage texture evidence", "storage texture format authority should reject third-format WGSL surprises")

    bad_storage_access_tint = make_tint_storage_access_wrapper(tint, out_dir)
    result = run([
        shadercross,
        str(hlsl_writeonly_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", bad_storage_access_tint,
        "--allow-unvalidated-tint",
        "-o", str(out_dir / "writeonly-storage-bad-tint-access.wgsl"),
    ], expect_success=False)
    require_contains(result.stdout, "uses an access that does not match storage texture evidence", "storage texture WGSL should reject an unexpected Tint access")

    hlsl_readwrite_storage = out_dir / "readwrite-storage.comp.hlsl"
    hlsl_readwrite_storage.write_text(HLSL_READWRITE_STORAGE_COMPUTE + "\n", encoding="utf-8")
    result = run([
        shadercross,
        str(hlsl_readwrite_storage),
        "-s", "HLSL",
        "-t", "compute",
        "-d", "WGSL",
        "--tint", tint,
        "-o", str(out_dir / "readwrite-storage-bad-policy.wgsl"),
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture slot layout facts are outside the current SDL_GPU support matrix", "storage texture policy WGSL should reject a write-only policy for observed read-write use")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
