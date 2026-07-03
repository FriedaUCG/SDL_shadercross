#!/usr/bin/env python3

import json
import hashlib
import subprocess
import sys
import shutil
import re
from pathlib import Path


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


SPARSE_SAMPLED_FRAGMENT_SPVASM = SAMPLED_FRAGMENT_SPVASM.replace(
    "OpDecorate %texture Binding 0\nOpDecorate %sampler DescriptorSet 2\nOpDecorate %sampler Binding 0",
    "OpDecorate %texture Binding 1\nOpDecorate %sampler DescriptorSet 2\nOpDecorate %sampler Binding 1",
)


SAMPLERLESS_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%v4float = OpTypeVector %float 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%image = OpTypeImage %float 2D 0 0 0 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4float = OpTypePointer Output %v4float
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4float %loaded_texture %coord Lod %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


UINT_SAMPLERLESS_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%v2int = OpTypeVector %int 2
%v4uint = OpTypeVector %uint 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%image = OpTypeImage %uint 2D 0 0 0 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4uint = OpTypePointer Output %v4uint
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4uint Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4uint %loaded_texture %coord Lod %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


SINT_SAMPLERLESS_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%v4int = OpTypeVector %int 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%image = OpTypeImage %int 2D 0 0 0 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4int = OpTypePointer Output %v4int
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4int Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4int %loaded_texture %coord Lod %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


MSAA_SAMPLED_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %color_texture DescriptorSet 2
OpDecorate %color_texture Binding 0
OpDecorate %depth_texture DescriptorSet 2
OpDecorate %depth_texture Binding 1
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%v4float = OpTypeVector %float 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%color_image = OpTypeImage %float 2D 0 0 1 1 Unknown
%depth_image = OpTypeImage %float 2D 1 0 1 1 Unknown
%ptr_uc_color_image = OpTypePointer UniformConstant %color_image
%ptr_uc_depth_image = OpTypePointer UniformConstant %depth_image
%ptr_out_v4float = OpTypePointer Output %v4float
%color_texture = OpVariable %ptr_uc_color_image UniformConstant
%depth_texture = OpVariable %ptr_uc_depth_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_color_texture = OpLoad %color_image %color_texture
%loaded_depth_texture = OpLoad %depth_image %depth_texture
%color = OpImageFetch %v4float %loaded_color_texture %coord Sample %int_0
%depth_color = OpImageFetch %v4float %loaded_depth_texture %coord Sample %int_0
%combined = OpFAdd %v4float %color %depth_color
OpStore %out_color %combined
OpReturn
OpFunctionEnd
""".strip()


MSAA_UNKNOWN_DEPTH_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%v4float = OpTypeVector %float 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%image = OpTypeImage %float 2D 2 0 1 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4float = OpTypePointer Output %v4float
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4float %loaded_texture %coord Sample %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


MSAA_ARRAY_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%v3int = OpTypeVector %int 3
%v4float = OpTypeVector %float 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v3int %int_0 %int_0 %int_0
%image = OpTypeImage %float 2D 0 1 1 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4float = OpTypePointer Output %v4float
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4float %loaded_texture %coord Sample %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


MSAA_UINT_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 2
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%v2int = OpTypeVector %int 2
%v4uint = OpTypeVector %uint 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%image = OpTypeImage %uint 2D 0 0 1 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4uint = OpTypePointer Output %v4uint
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4uint Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4uint %loaded_texture %coord Sample %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


MSAA_WRONG_SET_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %texture DescriptorSet 3
OpDecorate %texture Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%v4float = OpTypeVector %float 4
%int_0 = OpConstant %int 0
%coord = OpConstantComposite %v2int %int_0 %int_0
%image = OpTypeImage %float 2D 0 0 1 1 Unknown
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_out_v4float = OpTypePointer Output %v4float
%texture = OpVariable %ptr_uc_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%color = OpImageFetch %v4float %loaded_texture %coord Sample %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


MIXED_PAIRED_AND_SAMPLERLESS_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %sampled_texture DescriptorSet 2
OpDecorate %sampled_texture Binding 0
OpDecorate %sampled_sampler DescriptorSet 2
OpDecorate %sampled_sampler Binding 0
OpDecorate %fetch_texture DescriptorSet 2
OpDecorate %fetch_texture Binding 1
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%v2float = OpTypeVector %float 2
%v2int = OpTypeVector %int 2
%v4float = OpTypeVector %float 4
%v4uint = OpTypeVector %uint 4
%float_0 = OpConstant %float 0
%int_0 = OpConstant %int 0
%sample_coord = OpConstantComposite %v2float %float_0 %float_0
%fetch_coord = OpConstantComposite %v2int %int_0 %int_0
%sampled_image_type = OpTypeImage %float 2D 0 0 0 1 Unknown
%fetch_image_type = OpTypeImage %uint 2D 0 0 0 1 Unknown
%sampler_type = OpTypeSampler
%combined_type = OpTypeSampledImage %sampled_image_type
%ptr_uc_sampled_image = OpTypePointer UniformConstant %sampled_image_type
%ptr_uc_fetch_image = OpTypePointer UniformConstant %fetch_image_type
%ptr_uc_sampler = OpTypePointer UniformConstant %sampler_type
%ptr_out_v4float = OpTypePointer Output %v4float
%sampled_texture = OpVariable %ptr_uc_sampled_image UniformConstant
%sampled_sampler = OpVariable %ptr_uc_sampler UniformConstant
%fetch_texture = OpVariable %ptr_uc_fetch_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_sampled_texture = OpLoad %sampled_image_type %sampled_texture
%loaded_sampler = OpLoad %sampler_type %sampled_sampler
%combined = OpSampledImage %combined_type %loaded_sampled_texture %loaded_sampler
%color = OpImageSampleImplicitLod %v4float %combined %sample_coord
%loaded_fetch_texture = OpLoad %fetch_image_type %fetch_texture
%unused_fetch = OpImageFetch %v4uint %loaded_fetch_texture %fetch_coord Lod %int_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


NORMAL_DEPTH_FRAGMENT_SPVASM = """
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
%image = OpTypeImage %float 2D 1 0 0 1 Unknown
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
%sampled_color = OpImageSampleImplicitLod %v4float %combined %coord
%sample = OpCompositeExtract %float %sampled_color 0
%color = OpCompositeConstruct %v4float %sample %sample %sample %sample
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


DEPTH_DREF_GATHER_FRAGMENT_SPVASM = """
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
%image = OpTypeImage %float 2D 1 0 0 1 Unknown
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
%color = OpImageDrefGather %v4float %combined %coord %float_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


DEPTH_CUBE_DREF_FRAGMENT_SPVASM = """
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
%v3float = OpTypeVector %float 3
%v4float = OpTypeVector %float 4
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v3float %float_1 %float_0 %float_0
%image = OpTypeImage %float Cube 1 0 0 1 Unknown
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
%color = OpImageDrefGather %v4float %combined %coord %float_0
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


DEPTH_CUBE_ARRAY_FRAGMENT_SPVASM = """
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
%v4float = OpTypeVector %float 4
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v4float %float_1 %float_0 %float_0 %float_0
%image = OpTypeImage %float Cube 1 1 0 1 Unknown
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
%sampled_color = OpImageSampleImplicitLod %v4float %combined %coord
%sample = OpCompositeExtract %float %sampled_color 0
%color = OpCompositeConstruct %v4float %sample %sample %sample %sample
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


UINT_SAMPLED_FRAGMENT_SPVASM = """
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
%uint = OpTypeInt 32 0
%v4uint = OpTypeVector %uint 4
%float_0 = OpConstant %float 0
%coord = OpConstantComposite %v2float %float_0 %float_0
%image = OpTypeImage %uint 2D 0 0 0 1 Unknown
%sampler_type = OpTypeSampler
%sampled_image = OpTypeSampledImage %image
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_uc_sampler = OpTypePointer UniformConstant %sampler_type
%ptr_out_v4uint = OpTypePointer Output %v4uint
%texture = OpVariable %ptr_uc_image UniformConstant
%sampler = OpVariable %ptr_uc_sampler UniformConstant
%out_color = OpVariable %ptr_out_v4uint Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %image %texture
%loaded_sampler = OpLoad %sampler_type %sampler
%combined = OpSampledImage %sampled_image %loaded_texture %loaded_sampler
%color = OpImageSampleImplicitLod %v4uint %combined %coord
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


WRONG_SET_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %valid_texture DescriptorSet 2
OpDecorate %valid_texture Binding 1
OpDecorate %valid_sampler DescriptorSet 2
OpDecorate %valid_sampler Binding 1
OpDecorate %wrong_texture DescriptorSet 3
OpDecorate %wrong_texture Binding 0
OpDecorate %wrong_sampler DescriptorSet 3
OpDecorate %wrong_sampler Binding 0
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
%valid_texture = OpVariable %ptr_uc_image UniformConstant
%valid_sampler = OpVariable %ptr_uc_sampler UniformConstant
%wrong_texture = OpVariable %ptr_uc_image UniformConstant
%wrong_sampler = OpVariable %ptr_uc_sampler UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_valid_texture = OpLoad %image %valid_texture
%loaded_valid_sampler = OpLoad %sampler_type %valid_sampler
%combined_valid = OpSampledImage %sampled_image %loaded_valid_texture %loaded_valid_sampler
%valid_color = OpImageSampleImplicitLod %v4float %combined_valid %coord
%loaded_wrong_texture = OpLoad %image %wrong_texture
%loaded_wrong_sampler = OpLoad %sampler_type %wrong_sampler
%combined_wrong = OpSampledImage %sampled_image %loaded_wrong_texture %loaded_wrong_sampler
%wrong_color = OpImageSampleImplicitLod %v4float %combined_wrong %coord
%color = OpFAdd %v4float %valid_color %wrong_color
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


MULTI_ENTRYPOINT_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpEntryPoint Fragment %alt "alt" %out_color
OpExecutionMode %main OriginUpperLeft
OpExecutionMode %alt OriginUpperLeft
OpName %main_texture "MainTexture"
OpName %main_sampler "MainSampler"
OpName %alt_texture "AltTexture"
OpName %alt_sampler "AltSampler"
OpName %alt_extra_texture "AltExtraTexture"
OpName %alt_extra_sampler "AltExtraSampler"
OpDecorate %out_color Location 0
OpDecorate %main_texture DescriptorSet 2
OpDecorate %main_texture Binding 0
OpDecorate %main_sampler DescriptorSet 2
OpDecorate %main_sampler Binding 0
OpDecorate %alt_texture DescriptorSet 2
OpDecorate %alt_texture Binding 1
OpDecorate %alt_sampler DescriptorSet 2
OpDecorate %alt_sampler Binding 1
OpDecorate %alt_extra_texture DescriptorSet 2
OpDecorate %alt_extra_texture Binding 2
OpDecorate %alt_extra_sampler DescriptorSet 2
OpDecorate %alt_extra_sampler Binding 2
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
%main_texture = OpVariable %ptr_uc_image UniformConstant
%main_sampler = OpVariable %ptr_uc_sampler UniformConstant
%alt_texture = OpVariable %ptr_uc_image UniformConstant
%alt_sampler = OpVariable %ptr_uc_sampler UniformConstant
%alt_extra_texture = OpVariable %ptr_uc_image UniformConstant
%alt_extra_sampler = OpVariable %ptr_uc_sampler UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%main_entry = OpLabel
%loaded_main_texture = OpLoad %image %main_texture
%loaded_main_sampler = OpLoad %sampler_type %main_sampler
%combined_main = OpSampledImage %sampled_image %loaded_main_texture %loaded_main_sampler
%main_color = OpImageSampleImplicitLod %v4float %combined_main %coord
OpStore %out_color %main_color
OpReturn
OpFunctionEnd
%alt = OpFunction %void None %main_type
%alt_entry = OpLabel
%loaded_alt_texture = OpLoad %image %alt_texture
%loaded_alt_sampler = OpLoad %sampler_type %alt_sampler
%combined_alt = OpSampledImage %sampled_image %loaded_alt_texture %loaded_alt_sampler
%alt_color = OpImageSampleImplicitLod %v4float %combined_alt %coord
%loaded_alt_extra_texture = OpLoad %image %alt_extra_texture
%loaded_alt_extra_sampler = OpLoad %sampler_type %alt_extra_sampler
%combined_alt_extra = OpSampledImage %sampled_image %loaded_alt_extra_texture %loaded_alt_extra_sampler
%alt_extra_color = OpImageSampleImplicitLod %v4float %combined_alt_extra %coord
%alt_total_color = OpFAdd %v4float %alt_color %alt_extra_color
OpStore %out_color %alt_total_color
OpReturn
OpFunctionEnd
""".strip()


MULTI_ENTRYPOINT_SHARED_DEPTH_DREF_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpEntryPoint Fragment %alt "alt" %out_color
OpExecutionMode %main OriginUpperLeft
OpExecutionMode %alt OriginUpperLeft
OpName %texture "SharedDepthTexture"
OpName %sampler "SharedDepthSampler"
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
%image = OpTypeImage %float 2D 1 0 0 1 Unknown
%sampler_type = OpTypeSampler
%sampled_image = OpTypeSampledImage %image
%ptr_uc_image = OpTypePointer UniformConstant %image
%ptr_uc_sampler = OpTypePointer UniformConstant %sampler_type
%ptr_out_v4float = OpTypePointer Output %v4float
%texture = OpVariable %ptr_uc_image UniformConstant
%sampler = OpVariable %ptr_uc_sampler UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%main_entry = OpLabel
%loaded_main_texture = OpLoad %image %texture
%loaded_main_sampler = OpLoad %sampler_type %sampler
%combined_main = OpSampledImage %sampled_image %loaded_main_texture %loaded_main_sampler
%sampled_color = OpImageSampleImplicitLod %v4float %combined_main %coord
%sample = OpCompositeExtract %float %sampled_color 0
%main_color = OpCompositeConstruct %v4float %sample %sample %sample %sample
OpStore %out_color %main_color
OpReturn
OpFunctionEnd
%alt = OpFunction %void None %main_type
%alt_entry = OpLabel
%loaded_alt_texture = OpLoad %image %texture
%loaded_alt_sampler = OpLoad %sampler_type %sampler
%combined_alt = OpSampledImage %sampled_image %loaded_alt_texture %loaded_alt_sampler
%alt_color = OpImageDrefGather %v4float %combined_alt %coord %float_0
OpStore %out_color %alt_color
OpReturn
OpFunctionEnd
""".strip()


HELPER_PARAMETER_SAMPLED_PAIR_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpName %texture "HelperTexture"
OpName %sampler "HelperSampler"
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
%helper_type = OpTypeFunction %v4float %image %sampler_type
%texture = OpVariable %ptr_uc_image UniformConstant
%sampler = OpVariable %ptr_uc_sampler UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%main_entry = OpLabel
%loaded_texture = OpLoad %image %texture
%loaded_sampler = OpLoad %sampler_type %sampler
%color = OpFunctionCall %v4float %sample_helper %loaded_texture %loaded_sampler
OpStore %out_color %color
OpReturn
OpFunctionEnd
%sample_helper = OpFunction %v4float None %helper_type
%helper_texture = OpFunctionParameter %image
%helper_sampler = OpFunctionParameter %sampler_type
%helper_entry = OpLabel
%combined_helper = OpSampledImage %sampled_image %helper_texture %helper_sampler
%helper_color = OpImageSampleImplicitLod %v4float %combined_helper %coord
OpReturnValue %helper_color
OpFunctionEnd
""".strip()


READWRITE_R32_UINT_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_image DescriptorSet 1
OpDecorate %storage_image Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4uint = OpTypeVector %uint 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%storage_image_type = OpTypeImage %uint 2D 0 0 0 2 R32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
%color = OpImageRead %v4uint %loaded_storage %coord
OpImageWrite %loaded_storage %coord %color
OpReturn
OpFunctionEnd
""".strip()


READWRITE_R32_UINT_STORAGE_3D_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_image DescriptorSet 1
OpDecorate %storage_image Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%uint = OpTypeInt 32 0
%v3uint = OpTypeVector %uint 3
%v4uint = OpTypeVector %uint 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%storage_image_type = OpTypeImage %uint 3D 0 0 0 2 R32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
%color = OpImageRead %v4uint %loaded_storage %coord
OpImageWrite %loaded_storage %coord %color
OpReturn
OpFunctionEnd
""".strip()


READWRITE_R32_UINT_STORAGE_2D_ARRAY_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %write_storage_image DescriptorSet 1
OpDecorate %write_storage_image Binding 0
OpDecorate %write_storage_image NonReadable
OpDecorate %readwrite_storage_image DescriptorSet 1
OpDecorate %readwrite_storage_image Binding 1
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%uint = OpTypeInt 32 0
%v3uint = OpTypeVector %uint 3
%v4uint = OpTypeVector %uint 4
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%coord = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%value = OpConstantComposite %v4uint %uint_1 %uint_1 %uint_1 %uint_1
%storage_image_type = OpTypeImage %uint 2D 0 1 0 2 R32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%write_storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%readwrite_storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_write_storage = OpLoad %storage_image_type %write_storage_image
OpImageWrite %loaded_write_storage %coord %value
%loaded_readwrite_storage = OpLoad %storage_image_type %readwrite_storage_image
%color = OpImageRead %v4uint %loaded_readwrite_storage %coord
OpImageWrite %loaded_readwrite_storage %coord %color
OpReturn
OpFunctionEnd
""".strip()


WRITEONLY_STORAGE_TEXTURE_RESOURCE_ARRAY_COMPUTE_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_images DescriptorSet 1
OpDecorate %storage_images Binding 0
OpDecorate %storage_images NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%uint_2 = OpConstant %uint 2
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%storage_image_array_type = OpTypeArray %storage_image_type %uint_2
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_uc_storage_image_array = OpTypePointer UniformConstant %storage_image_array_type
%storage_images = OpVariable %ptr_uc_storage_image_array UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_image_ptr = OpAccessChain %ptr_uc_storage_image %storage_images %uint_0
%loaded_storage = OpLoad %storage_image_type %storage_image_ptr
OpImageWrite %loaded_storage %coord %color
OpReturn
OpFunctionEnd
""".strip()


READWRITE_R32_SCALAR_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %sint_storage_image DescriptorSet 1
OpDecorate %sint_storage_image Binding 0
OpDecorate %sint_storage_image NonReadable
OpDecorate %float_storage_image DescriptorSet 1
OpDecorate %float_storage_image Binding 1
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4int = OpTypeVector %int 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%int_1 = OpConstant %int 1
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%sint_value = OpConstantComposite %v4int %int_1 %int_1 %int_1 %int_1
%sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 R32i
%float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 R32f
%ptr_uc_sint_storage_image = OpTypePointer UniformConstant %sint_storage_image_type
%ptr_uc_float_storage_image = OpTypePointer UniformConstant %float_storage_image_type
%sint_storage_image = OpVariable %ptr_uc_sint_storage_image UniformConstant
%float_storage_image = OpVariable %ptr_uc_float_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_sint_storage = OpLoad %sint_storage_image_type %sint_storage_image
OpImageWrite %loaded_sint_storage %coord %sint_value
%loaded_float_storage = OpLoad %float_storage_image_type %float_storage_image
%float_color = OpImageRead %v4float %loaded_float_storage %coord
%updated_float_color = OpFAdd %v4float %float_color %float_color
OpImageWrite %loaded_float_storage %coord %updated_float_color
OpReturn
OpFunctionEnd
""".strip()


WRITABLE_R32_SCALAR_STORAGE_3D_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %uint_write_storage_image DescriptorSet 1
OpDecorate %uint_write_storage_image Binding 0
OpDecorate %uint_write_storage_image NonReadable
OpDecorate %uint_readwrite_storage_image DescriptorSet 1
OpDecorate %uint_readwrite_storage_image Binding 1
OpDecorate %sint_write_storage_image DescriptorSet 1
OpDecorate %sint_write_storage_image Binding 2
OpDecorate %sint_write_storage_image NonReadable
OpDecorate %sint_readwrite_storage_image DescriptorSet 1
OpDecorate %sint_readwrite_storage_image Binding 3
OpDecorate %float_write_storage_image DescriptorSet 1
OpDecorate %float_write_storage_image Binding 4
OpDecorate %float_write_storage_image NonReadable
OpDecorate %float_readwrite_storage_image DescriptorSet 1
OpDecorate %float_readwrite_storage_image Binding 5
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v3uint = OpTypeVector %uint 3
%v4uint = OpTypeVector %uint 4
%v4int = OpTypeVector %int 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%int_1 = OpConstant %int 1
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%uint_value = OpConstantComposite %v4uint %uint_1 %uint_1 %uint_1 %uint_1
%sint_value = OpConstantComposite %v4int %int_1 %int_1 %int_1 %int_1
%float_value = OpConstantComposite %v4float %float_1 %float_1 %float_1 %float_1
%uint_storage_image_type = OpTypeImage %uint 3D 0 0 0 2 R32ui
%sint_storage_image_type = OpTypeImage %int 3D 0 0 0 2 R32i
%float_storage_image_type = OpTypeImage %float 3D 0 0 0 2 R32f
%ptr_uc_uint_storage_image = OpTypePointer UniformConstant %uint_storage_image_type
%ptr_uc_sint_storage_image = OpTypePointer UniformConstant %sint_storage_image_type
%ptr_uc_float_storage_image = OpTypePointer UniformConstant %float_storage_image_type
%uint_write_storage_image = OpVariable %ptr_uc_uint_storage_image UniformConstant
%uint_readwrite_storage_image = OpVariable %ptr_uc_uint_storage_image UniformConstant
%sint_write_storage_image = OpVariable %ptr_uc_sint_storage_image UniformConstant
%sint_readwrite_storage_image = OpVariable %ptr_uc_sint_storage_image UniformConstant
%float_write_storage_image = OpVariable %ptr_uc_float_storage_image UniformConstant
%float_readwrite_storage_image = OpVariable %ptr_uc_float_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_uint_write_storage = OpLoad %uint_storage_image_type %uint_write_storage_image
OpImageWrite %loaded_uint_write_storage %coord %uint_value
%loaded_uint_readwrite_storage = OpLoad %uint_storage_image_type %uint_readwrite_storage_image
%uint_color = OpImageRead %v4uint %loaded_uint_readwrite_storage %coord
OpImageWrite %loaded_uint_readwrite_storage %coord %uint_color
%loaded_sint_write_storage = OpLoad %sint_storage_image_type %sint_write_storage_image
OpImageWrite %loaded_sint_write_storage %coord %sint_value
%loaded_sint_readwrite_storage = OpLoad %sint_storage_image_type %sint_readwrite_storage_image
%sint_color = OpImageRead %v4int %loaded_sint_readwrite_storage %coord
OpImageWrite %loaded_sint_readwrite_storage %coord %sint_color
%loaded_float_write_storage = OpLoad %float_storage_image_type %float_write_storage_image
OpImageWrite %loaded_float_write_storage %coord %float_value
%loaded_float_readwrite_storage = OpLoad %float_storage_image_type %float_readwrite_storage_image
%float_color = OpImageRead %v4float %loaded_float_readwrite_storage %coord
OpImageWrite %loaded_float_readwrite_storage %coord %float_color
OpReturn
OpFunctionEnd
""".strip()


WRITABLE_BROAD_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %rgba8snorm_storage_image DescriptorSet 1
OpDecorate %rgba8snorm_storage_image Binding 0
OpDecorate %rgba8snorm_storage_image NonReadable
OpDecorate %rgba16float_storage_image DescriptorSet 1
OpDecorate %rgba16float_storage_image Binding 1
OpDecorate %rgba16float_storage_image NonReadable
OpDecorate %rg32float_storage_image DescriptorSet 1
OpDecorate %rg32float_storage_image Binding 2
OpDecorate %rg32float_storage_image NonReadable
OpDecorate %rgba32float_storage_image DescriptorSet 1
OpDecorate %rgba32float_storage_image Binding 3
OpDecorate %rgba32float_storage_image NonReadable
OpDecorate %rgba8uint_storage_image DescriptorSet 1
OpDecorate %rgba8uint_storage_image Binding 4
OpDecorate %rgba8uint_storage_image NonReadable
OpDecorate %rgba16uint_storage_image DescriptorSet 1
OpDecorate %rgba16uint_storage_image Binding 5
OpDecorate %rgba16uint_storage_image NonReadable
OpDecorate %rgba8sint_storage_image DescriptorSet 1
OpDecorate %rgba8sint_storage_image Binding 6
OpDecorate %rgba8sint_storage_image NonReadable
OpDecorate %rgba16sint_storage_image DescriptorSet 1
OpDecorate %rgba16sint_storage_image Binding 7
OpDecorate %rgba16sint_storage_image NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4int = OpTypeVector %int 4
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%int_1 = OpConstant %int 1
%uint_1 = OpConstant %uint 1
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%sint_value = OpConstantComposite %v4int %int_1 %int_1 %int_1 %int_1
%uint_value = OpConstantComposite %v4uint %uint_1 %uint_1 %uint_1 %uint_1
%float_value = OpConstantComposite %v4float %float_1 %float_1 %float_1 %float_1
%rgba8snorm_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba8Snorm
%rgba16float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%rg32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rg32f
%rgba32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba32f
%rgba8uint_storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba8ui
%rgba16uint_storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba16ui
%rgba8sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba8i
%rgba16sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba16i
%ptr_uc_rgba8snorm_storage_image = OpTypePointer UniformConstant %rgba8snorm_storage_image_type
%ptr_uc_rgba16float_storage_image = OpTypePointer UniformConstant %rgba16float_storage_image_type
%ptr_uc_rg32float_storage_image = OpTypePointer UniformConstant %rg32float_storage_image_type
%ptr_uc_rgba32float_storage_image = OpTypePointer UniformConstant %rgba32float_storage_image_type
%ptr_uc_rgba8uint_storage_image = OpTypePointer UniformConstant %rgba8uint_storage_image_type
%ptr_uc_rgba16uint_storage_image = OpTypePointer UniformConstant %rgba16uint_storage_image_type
%ptr_uc_rgba8sint_storage_image = OpTypePointer UniformConstant %rgba8sint_storage_image_type
%ptr_uc_rgba16sint_storage_image = OpTypePointer UniformConstant %rgba16sint_storage_image_type
%rgba8snorm_storage_image = OpVariable %ptr_uc_rgba8snorm_storage_image UniformConstant
%rgba16float_storage_image = OpVariable %ptr_uc_rgba16float_storage_image UniformConstant
%rg32float_storage_image = OpVariable %ptr_uc_rg32float_storage_image UniformConstant
%rgba32float_storage_image = OpVariable %ptr_uc_rgba32float_storage_image UniformConstant
%rgba8uint_storage_image = OpVariable %ptr_uc_rgba8uint_storage_image UniformConstant
%rgba16uint_storage_image = OpVariable %ptr_uc_rgba16uint_storage_image UniformConstant
%rgba8sint_storage_image = OpVariable %ptr_uc_rgba8sint_storage_image UniformConstant
%rgba16sint_storage_image = OpVariable %ptr_uc_rgba16sint_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_rgba8snorm_storage = OpLoad %rgba8snorm_storage_image_type %rgba8snorm_storage_image
OpImageWrite %loaded_rgba8snorm_storage %coord %float_value
%loaded_rgba16float_storage = OpLoad %rgba16float_storage_image_type %rgba16float_storage_image
OpImageWrite %loaded_rgba16float_storage %coord %float_value
%loaded_rg32float_storage = OpLoad %rg32float_storage_image_type %rg32float_storage_image
OpImageWrite %loaded_rg32float_storage %coord %float_value
%loaded_rgba32float_storage = OpLoad %rgba32float_storage_image_type %rgba32float_storage_image
OpImageWrite %loaded_rgba32float_storage %coord %float_value
%loaded_rgba8uint_storage = OpLoad %rgba8uint_storage_image_type %rgba8uint_storage_image
OpImageWrite %loaded_rgba8uint_storage %coord %uint_value
%loaded_rgba16uint_storage = OpLoad %rgba16uint_storage_image_type %rgba16uint_storage_image
OpImageWrite %loaded_rgba16uint_storage %coord %uint_value
%loaded_rgba8sint_storage = OpLoad %rgba8sint_storage_image_type %rgba8sint_storage_image
OpImageWrite %loaded_rgba8sint_storage %coord %sint_value
%loaded_rgba16sint_storage = OpLoad %rgba16sint_storage_image_type %rgba16sint_storage_image
OpImageWrite %loaded_rgba16sint_storage %coord %sint_value
OpReturn
OpFunctionEnd
""".strip()


READWRITE_BROAD_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %rgba16float_storage_image DescriptorSet 1
OpDecorate %rgba16float_storage_image Binding 0
OpDecorate %rgba32float_storage_image DescriptorSet 1
OpDecorate %rgba32float_storage_image Binding 1
OpDecorate %rgba8uint_storage_image DescriptorSet 1
OpDecorate %rgba8uint_storage_image Binding 2
OpDecorate %rgba16uint_storage_image DescriptorSet 1
OpDecorate %rgba16uint_storage_image Binding 3
OpDecorate %rgba8sint_storage_image DescriptorSet 1
OpDecorate %rgba8sint_storage_image Binding 4
OpDecorate %rgba16sint_storage_image DescriptorSet 1
OpDecorate %rgba16sint_storage_image Binding 5
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4int = OpTypeVector %int 4
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%rgba16float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%rgba32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba32f
%rgba8uint_storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba8ui
%rgba16uint_storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba16ui
%rgba8sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba8i
%rgba16sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba16i
%ptr_uc_rgba16float_storage_image = OpTypePointer UniformConstant %rgba16float_storage_image_type
%ptr_uc_rgba32float_storage_image = OpTypePointer UniformConstant %rgba32float_storage_image_type
%ptr_uc_rgba8uint_storage_image = OpTypePointer UniformConstant %rgba8uint_storage_image_type
%ptr_uc_rgba16uint_storage_image = OpTypePointer UniformConstant %rgba16uint_storage_image_type
%ptr_uc_rgba8sint_storage_image = OpTypePointer UniformConstant %rgba8sint_storage_image_type
%ptr_uc_rgba16sint_storage_image = OpTypePointer UniformConstant %rgba16sint_storage_image_type
%rgba16float_storage_image = OpVariable %ptr_uc_rgba16float_storage_image UniformConstant
%rgba32float_storage_image = OpVariable %ptr_uc_rgba32float_storage_image UniformConstant
%rgba8uint_storage_image = OpVariable %ptr_uc_rgba8uint_storage_image UniformConstant
%rgba16uint_storage_image = OpVariable %ptr_uc_rgba16uint_storage_image UniformConstant
%rgba8sint_storage_image = OpVariable %ptr_uc_rgba8sint_storage_image UniformConstant
%rgba16sint_storage_image = OpVariable %ptr_uc_rgba16sint_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_rgba16float_storage = OpLoad %rgba16float_storage_image_type %rgba16float_storage_image
%rgba16float_value = OpImageRead %v4float %loaded_rgba16float_storage %coord
OpImageWrite %loaded_rgba16float_storage %coord %rgba16float_value
%loaded_rgba32float_storage = OpLoad %rgba32float_storage_image_type %rgba32float_storage_image
%rgba32float_value = OpImageRead %v4float %loaded_rgba32float_storage %coord
OpImageWrite %loaded_rgba32float_storage %coord %rgba32float_value
%loaded_rgba8uint_storage = OpLoad %rgba8uint_storage_image_type %rgba8uint_storage_image
%rgba8uint_value = OpImageRead %v4uint %loaded_rgba8uint_storage %coord
OpImageWrite %loaded_rgba8uint_storage %coord %rgba8uint_value
%loaded_rgba16uint_storage = OpLoad %rgba16uint_storage_image_type %rgba16uint_storage_image
%rgba16uint_value = OpImageRead %v4uint %loaded_rgba16uint_storage %coord
OpImageWrite %loaded_rgba16uint_storage %coord %rgba16uint_value
%loaded_rgba8sint_storage = OpLoad %rgba8sint_storage_image_type %rgba8sint_storage_image
%rgba8sint_value = OpImageRead %v4int %loaded_rgba8sint_storage %coord
OpImageWrite %loaded_rgba8sint_storage %coord %rgba8sint_value
%loaded_rgba16sint_storage = OpLoad %rgba16sint_storage_image_type %rgba16sint_storage_image
%rgba16sint_value = OpImageRead %v4int %loaded_rgba16sint_storage %coord
OpImageWrite %loaded_rgba16sint_storage %coord %rgba16sint_value
OpReturn
OpFunctionEnd
""".strip()


READWRITE_RG32_FLOAT_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %rg32float_storage_image DescriptorSet 1
OpDecorate %rg32float_storage_image Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%rg32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rg32f
%ptr_uc_rg32float_storage_image = OpTypePointer UniformConstant %rg32float_storage_image_type
%rg32float_storage_image = OpVariable %ptr_uc_rg32float_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_rg32float_storage = OpLoad %rg32float_storage_image_type %rg32float_storage_image
%rg32float_value = OpImageRead %v4float %loaded_rg32float_storage %coord
OpImageWrite %loaded_rg32float_storage %coord %rg32float_value
OpReturn
OpFunctionEnd
""".strip()


READONLY_BROAD_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %rgba8snorm_storage_image DescriptorSet 0
OpDecorate %rgba8snorm_storage_image Binding 0
OpDecorate %rgba8snorm_storage_image NonWritable
OpDecorate %rgba16float_storage_image DescriptorSet 0
OpDecorate %rgba16float_storage_image Binding 1
OpDecorate %rgba16float_storage_image NonWritable
OpDecorate %rg32float_storage_image DescriptorSet 0
OpDecorate %rg32float_storage_image Binding 2
OpDecorate %rg32float_storage_image NonWritable
OpDecorate %rgba32float_storage_image DescriptorSet 0
OpDecorate %rgba32float_storage_image Binding 3
OpDecorate %rgba32float_storage_image NonWritable
OpDecorate %rgba8uint_storage_image DescriptorSet 0
OpDecorate %rgba8uint_storage_image Binding 4
OpDecorate %rgba8uint_storage_image NonWritable
OpDecorate %rgba16uint_storage_image DescriptorSet 0
OpDecorate %rgba16uint_storage_image Binding 5
OpDecorate %rgba16uint_storage_image NonWritable
OpDecorate %rgba8sint_storage_image DescriptorSet 0
OpDecorate %rgba8sint_storage_image Binding 6
OpDecorate %rgba8sint_storage_image NonWritable
OpDecorate %rgba16sint_storage_image DescriptorSet 0
OpDecorate %rgba16sint_storage_image Binding 7
OpDecorate %rgba16sint_storage_image NonWritable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4int = OpTypeVector %int 4
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%rgba8snorm_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba8Snorm
%rgba16float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%rg32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rg32f
%rgba32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba32f
%rgba8uint_storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba8ui
%rgba16uint_storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba16ui
%rgba8sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba8i
%rgba16sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba16i
%ptr_uc_rgba8snorm_storage_image = OpTypePointer UniformConstant %rgba8snorm_storage_image_type
%ptr_uc_rgba16float_storage_image = OpTypePointer UniformConstant %rgba16float_storage_image_type
%ptr_uc_rg32float_storage_image = OpTypePointer UniformConstant %rg32float_storage_image_type
%ptr_uc_rgba32float_storage_image = OpTypePointer UniformConstant %rgba32float_storage_image_type
%ptr_uc_rgba8uint_storage_image = OpTypePointer UniformConstant %rgba8uint_storage_image_type
%ptr_uc_rgba16uint_storage_image = OpTypePointer UniformConstant %rgba16uint_storage_image_type
%ptr_uc_rgba8sint_storage_image = OpTypePointer UniformConstant %rgba8sint_storage_image_type
%ptr_uc_rgba16sint_storage_image = OpTypePointer UniformConstant %rgba16sint_storage_image_type
%rgba8snorm_storage_image = OpVariable %ptr_uc_rgba8snorm_storage_image UniformConstant
%rgba16float_storage_image = OpVariable %ptr_uc_rgba16float_storage_image UniformConstant
%rg32float_storage_image = OpVariable %ptr_uc_rg32float_storage_image UniformConstant
%rgba32float_storage_image = OpVariable %ptr_uc_rgba32float_storage_image UniformConstant
%rgba8uint_storage_image = OpVariable %ptr_uc_rgba8uint_storage_image UniformConstant
%rgba16uint_storage_image = OpVariable %ptr_uc_rgba16uint_storage_image UniformConstant
%rgba8sint_storage_image = OpVariable %ptr_uc_rgba8sint_storage_image UniformConstant
%rgba16sint_storage_image = OpVariable %ptr_uc_rgba16sint_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_rgba8snorm_storage = OpLoad %rgba8snorm_storage_image_type %rgba8snorm_storage_image
%rgba8snorm_value = OpImageRead %v4float %loaded_rgba8snorm_storage %coord
%loaded_rgba16float_storage = OpLoad %rgba16float_storage_image_type %rgba16float_storage_image
%rgba16float_value = OpImageRead %v4float %loaded_rgba16float_storage %coord
%loaded_rg32float_storage = OpLoad %rg32float_storage_image_type %rg32float_storage_image
%rg32float_value = OpImageRead %v4float %loaded_rg32float_storage %coord
%loaded_rgba32float_storage = OpLoad %rgba32float_storage_image_type %rgba32float_storage_image
%rgba32float_value = OpImageRead %v4float %loaded_rgba32float_storage %coord
%loaded_rgba8uint_storage = OpLoad %rgba8uint_storage_image_type %rgba8uint_storage_image
%rgba8uint_value = OpImageRead %v4uint %loaded_rgba8uint_storage %coord
%loaded_rgba16uint_storage = OpLoad %rgba16uint_storage_image_type %rgba16uint_storage_image
%rgba16uint_value = OpImageRead %v4uint %loaded_rgba16uint_storage %coord
%loaded_rgba8sint_storage = OpLoad %rgba8sint_storage_image_type %rgba8sint_storage_image
%rgba8sint_value = OpImageRead %v4int %loaded_rgba8sint_storage %coord
%loaded_rgba16sint_storage = OpLoad %rgba16sint_storage_image_type %rgba16sint_storage_image
%rgba16sint_value = OpImageRead %v4int %loaded_rgba16sint_storage %coord
OpReturn
OpFunctionEnd
""".strip()


def readonly_broad_storage_graphics_spvasm(stage, descriptor_set):
    if stage == "Vertex":
        entry_interface = "%out_position"
        execution_mode = ""
        output_decorations = "OpDecorate %out_position BuiltIn Position"
        output_variable_name = "%out_position"
    elif stage == "Fragment":
        entry_interface = "%out_color"
        execution_mode = "OpExecutionMode %main OriginUpperLeft"
        output_decorations = "OpDecorate %out_color Location 0"
        output_variable_name = "%out_color"
    else:
        raise ValueError(f"unsupported graphics stage: {stage}")

    return f"""
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint {stage} %main "main" {entry_interface}
{execution_mode}
{output_decorations}
OpDecorate %rgba8snorm_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba8snorm_storage_image Binding 0
OpDecorate %rgba8snorm_storage_image NonWritable
OpDecorate %rgba16float_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba16float_storage_image Binding 1
OpDecorate %rgba16float_storage_image NonWritable
OpDecorate %rg32float_storage_image DescriptorSet {descriptor_set}
OpDecorate %rg32float_storage_image Binding 2
OpDecorate %rg32float_storage_image NonWritable
OpDecorate %rgba32float_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba32float_storage_image Binding 3
OpDecorate %rgba32float_storage_image NonWritable
OpDecorate %rgba8uint_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba8uint_storage_image Binding 4
OpDecorate %rgba8uint_storage_image NonWritable
OpDecorate %rgba16uint_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba16uint_storage_image Binding 5
OpDecorate %rgba16uint_storage_image NonWritable
OpDecorate %rgba8sint_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba8sint_storage_image Binding 6
OpDecorate %rgba8sint_storage_image NonWritable
OpDecorate %rgba16sint_storage_image DescriptorSet {descriptor_set}
OpDecorate %rgba16sint_storage_image Binding 7
OpDecorate %rgba16sint_storage_image NonWritable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v3uint = OpTypeVector %uint 3
%v4int = OpTypeVector %int 4
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord2d = OpConstantComposite %v2uint %uint_0 %uint_0
%coord3d = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%output_value = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%rgba8snorm_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba8Snorm
%rgba16float_storage_image_type = OpTypeImage %float 2D 0 1 0 2 Rgba16f
%rg32float_storage_image_type = OpTypeImage %float 3D 0 0 0 2 Rg32f
%rgba32float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba32f
%rgba8uint_storage_image_type = OpTypeImage %uint 2D 0 1 0 2 Rgba8ui
%rgba16uint_storage_image_type = OpTypeImage %uint 3D 0 0 0 2 Rgba16ui
%rgba8sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 Rgba8i
%rgba16sint_storage_image_type = OpTypeImage %int 2D 0 1 0 2 Rgba16i
%ptr_uc_rgba8snorm_storage_image = OpTypePointer UniformConstant %rgba8snorm_storage_image_type
%ptr_uc_rgba16float_storage_image = OpTypePointer UniformConstant %rgba16float_storage_image_type
%ptr_uc_rg32float_storage_image = OpTypePointer UniformConstant %rg32float_storage_image_type
%ptr_uc_rgba32float_storage_image = OpTypePointer UniformConstant %rgba32float_storage_image_type
%ptr_uc_rgba8uint_storage_image = OpTypePointer UniformConstant %rgba8uint_storage_image_type
%ptr_uc_rgba16uint_storage_image = OpTypePointer UniformConstant %rgba16uint_storage_image_type
%ptr_uc_rgba8sint_storage_image = OpTypePointer UniformConstant %rgba8sint_storage_image_type
%ptr_uc_rgba16sint_storage_image = OpTypePointer UniformConstant %rgba16sint_storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%rgba8snorm_storage_image = OpVariable %ptr_uc_rgba8snorm_storage_image UniformConstant
%rgba16float_storage_image = OpVariable %ptr_uc_rgba16float_storage_image UniformConstant
%rg32float_storage_image = OpVariable %ptr_uc_rg32float_storage_image UniformConstant
%rgba32float_storage_image = OpVariable %ptr_uc_rgba32float_storage_image UniformConstant
%rgba8uint_storage_image = OpVariable %ptr_uc_rgba8uint_storage_image UniformConstant
%rgba16uint_storage_image = OpVariable %ptr_uc_rgba16uint_storage_image UniformConstant
%rgba8sint_storage_image = OpVariable %ptr_uc_rgba8sint_storage_image UniformConstant
%rgba16sint_storage_image = OpVariable %ptr_uc_rgba16sint_storage_image UniformConstant
{output_variable_name} = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_rgba8snorm_storage = OpLoad %rgba8snorm_storage_image_type %rgba8snorm_storage_image
%rgba8snorm_value = OpImageRead %v4float %loaded_rgba8snorm_storage %coord2d
%loaded_rgba16float_storage = OpLoad %rgba16float_storage_image_type %rgba16float_storage_image
%rgba16float_value = OpImageRead %v4float %loaded_rgba16float_storage %coord3d
%loaded_rg32float_storage = OpLoad %rg32float_storage_image_type %rg32float_storage_image
%rg32float_value = OpImageRead %v4float %loaded_rg32float_storage %coord3d
%loaded_rgba32float_storage = OpLoad %rgba32float_storage_image_type %rgba32float_storage_image
%rgba32float_value = OpImageRead %v4float %loaded_rgba32float_storage %coord2d
%loaded_rgba8uint_storage = OpLoad %rgba8uint_storage_image_type %rgba8uint_storage_image
%rgba8uint_value = OpImageRead %v4uint %loaded_rgba8uint_storage %coord3d
%loaded_rgba16uint_storage = OpLoad %rgba16uint_storage_image_type %rgba16uint_storage_image
%rgba16uint_value = OpImageRead %v4uint %loaded_rgba16uint_storage %coord3d
%loaded_rgba8sint_storage = OpLoad %rgba8sint_storage_image_type %rgba8sint_storage_image
%rgba8sint_value = OpImageRead %v4int %loaded_rgba8sint_storage %coord2d
%loaded_rgba16sint_storage = OpLoad %rgba16sint_storage_image_type %rgba16sint_storage_image
%rgba16sint_value = OpImageRead %v4int %loaded_rgba16sint_storage %coord3d
OpStore {output_variable_name} %output_value
OpReturn
OpFunctionEnd
""".strip()


WRITEONLY_GRAPHICS_STORAGE_FRAGMENT_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 0
OpDecorate %storage_image NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
OpImageWrite %loaded_storage %coord %color
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


WRITEONLY_WRONG_SET_GRAPHICS_STORAGE_FRAGMENT_SPVASM = WRITEONLY_GRAPHICS_STORAGE_FRAGMENT_SPVASM.replace(
    "OpDecorate %storage_image DescriptorSet 2",
    "OpDecorate %storage_image DescriptorSet 0",
)


WRITEONLY_SPARSE_GRAPHICS_STORAGE_FRAGMENT_SPVASM = WRITEONLY_GRAPHICS_STORAGE_FRAGMENT_SPVASM.replace(
    "OpDecorate %storage_image Binding 0",
    "OpDecorate %storage_image Binding 1",
)


READWRITE_GRAPHICS_STORAGE_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %uint 2D 0 0 0 2 R32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
%read_value = OpImageRead %v4uint %loaded_storage %coord
OpImageWrite %loaded_storage %coord %read_value
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


WRITEONLY_GRAPHICS_STORAGE_VERTEX_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Vertex %main "main" %out_position
OpDecorate %out_position BuiltIn Position
OpDecorate %storage_image DescriptorSet 0
OpDecorate %storage_image Binding 0
OpDecorate %storage_image NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%position = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_position = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
OpImageWrite %loaded_storage %coord %color
OpStore %out_position %position
OpReturn
OpFunctionEnd
""".strip()


WRITEONLY_ARRAYED_GRAPHICS_STORAGE_FRAGMENT_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 0
OpDecorate %storage_image NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v3uint = OpTypeVector %uint 3
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %float 2D 0 1 0 2 Rgba16f
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
OpImageWrite %loaded_storage %coord %color
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


READWRITE_ARRAYED_GRAPHICS_STORAGE_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v3uint = OpTypeVector %uint 3
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%out_value = OpConstantComposite %v4float %float_1 %float_1 %float_1 %float_1
%storage_image_type = OpTypeImage %uint 2D 0 1 0 2 R32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
%color = OpImageRead %v4uint %loaded_storage %coord
OpImageWrite %loaded_storage %coord %color
OpStore %out_color %out_value
OpReturn
OpFunctionEnd
""".strip()


WRITEONLY_STORAGE_TEXTURE_RESOURCE_ARRAY_FRAGMENT_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_images DescriptorSet 2
OpDecorate %storage_images Binding 0
OpDecorate %storage_images NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%uint_2 = OpConstant %uint 2
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%storage_image_array_type = OpTypeArray %storage_image_type %uint_2
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_uc_storage_image_array = OpTypePointer UniformConstant %storage_image_array_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_images = OpVariable %ptr_uc_storage_image_array UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_image_ptr = OpAccessChain %ptr_uc_storage_image %storage_images %uint_0
%loaded_storage = OpLoad %storage_image_type %storage_image_ptr
OpImageWrite %loaded_storage %coord %color
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


FRAGMENT_WRITABLE_STORAGE_BUFFER_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_array ArrayStride 4
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %storage_buffer DescriptorSet 2
OpDecorate %storage_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%float_1 = OpConstant %float 1
%storage_array = OpTypeArray %float %uint_1
%storage_block = OpTypeStruct %storage_array
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_float = OpTypePointer Uniform %float
%ptr_output_v4float = OpTypePointer Output %v4float
%storage_buffer = OpVariable %ptr_storage_block Uniform
%out_color = OpVariable %ptr_output_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0 %uint_0
OpStore %storage_ptr %float_1
%color = OpCompositeConstruct %v4float %float_1 %float_1 %float_1 %float_1
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


FRAGMENT_READONLY_BUFFER_WRITABLE_TEXTURE_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %storage_buffer DescriptorSet 2
OpDecorate %storage_buffer Binding 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 1
OpDecorate %storage_image NonReadable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%storage_block = OpTypeStruct %float
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_float = OpTypePointer Uniform %float
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_output_v4float = OpTypePointer Output %v4float
%storage_buffer = OpVariable %ptr_storage_block Uniform
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_color = OpVariable %ptr_output_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0
%value = OpLoad %float %storage_ptr
%color = OpCompositeConstruct %v4float %value %value %value %float_1
%loaded_storage = OpLoad %storage_image_type %storage_image
OpImageWrite %loaded_storage %coord %color
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


FRAGMENT_WRITABLE_TEXTURE_WRITABLE_BUFFER_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 0
OpDecorate %storage_image NonReadable
OpDecorate %storage_array ArrayStride 4
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %storage_buffer DescriptorSet 2
OpDecorate %storage_buffer Binding 1
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%float_1 = OpConstant %float 1
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%color = OpConstantComposite %v4float %float_1 %float_1 %float_1 %float_1
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba16f
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%storage_array = OpTypeArray %float %uint_1
%storage_block = OpTypeStruct %storage_array
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_float = OpTypePointer Uniform %float
%ptr_output_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%storage_buffer = OpVariable %ptr_storage_block Uniform
%out_color = OpVariable %ptr_output_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0 %uint_0
OpStore %storage_ptr %float_1
%loaded_storage = OpLoad %storage_image_type %storage_image
OpImageWrite %loaded_storage %coord %color
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


VERTEX_WRITABLE_STORAGE_BUFFER_SPVASM = FRAGMENT_WRITABLE_STORAGE_BUFFER_SPVASM.replace(
    "OpEntryPoint Fragment %main \"main\" %out_color\nOpExecutionMode %main OriginUpperLeft\nOpDecorate %out_color Location 0",
    "OpEntryPoint Vertex %main \"main\" %out_color\nOpDecorate %out_color BuiltIn Position",
).replace(
    "OpDecorate %storage_buffer DescriptorSet 2",
    "OpDecorate %storage_buffer DescriptorSet 0",
)


FRAGMENT_WRITABLE_STORAGE_BUFFER_ARRAY_SPVASM = FRAGMENT_WRITABLE_STORAGE_BUFFER_SPVASM.replace(
    "OpDecorate %storage_array ArrayStride 4\nOpDecorate %storage_block BufferBlock",
    "OpDecorate %storage_array ArrayStride 4\nOpDecorate %storage_block BufferBlock",
).replace(
    "%ptr_storage_block = OpTypePointer Uniform %storage_block",
    "%storage_block_array = OpTypeArray %storage_block %uint_1\n%ptr_storage_block = OpTypePointer Uniform %storage_block_array",
).replace(
    "%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0 %uint_0",
    "%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0 %uint_0 %uint_0",
)


FRAGMENT_ATOMIC_STORAGE_BUFFER_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %storage_buffer DescriptorSet 2
OpDecorate %storage_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%float_1 = OpConstant %float 1
%storage_block = OpTypeStruct %uint
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_uint = OpTypePointer Uniform %uint
%ptr_output_v4float = OpTypePointer Output %v4float
%storage_buffer = OpVariable %ptr_storage_block Uniform
%out_color = OpVariable %ptr_output_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_ptr = OpAccessChain %ptr_uniform_uint %storage_buffer %uint_0
%old_value = OpAtomicIAdd %uint %storage_ptr %uint_1 %uint_0 %uint_1
%color = OpCompositeConstruct %v4float %float_1 %float_1 %float_1 %float_1
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


UNSUPPORTED_GRAPHICS_STORAGE_FRAGMENT_SPVASM = """
OpCapability Shader
OpCapability StorageImageExtendedFormats
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_image DescriptorSet 2
OpDecorate %storage_image Binding 0
OpDecorate %storage_image NonWritable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4uint = OpTypeVector %uint 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%float_0 = OpConstant %float 0
%float_1 = OpConstant %float 1
%color = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_1
%storage_image_type = OpTypeImage %uint 2D 0 0 0 2 Rgba32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_out_v4float = OpTypePointer Output %v4float
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%out_color = OpVariable %ptr_out_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
%read_value = OpImageRead %v4uint %loaded_storage %coord
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


READONLY_R32_UINT_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_image DescriptorSet 0
OpDecorate %storage_image Binding 0
OpDecorate %storage_image NonWritable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%uint = OpTypeInt 32 0
%v3uint = OpTypeVector %uint 3
%v4uint = OpTypeVector %uint 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v3uint %uint_0 %uint_0 %uint_0
%storage_image_type = OpTypeImage %uint 3D 0 0 0 2 R32ui
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_storage = OpLoad %storage_image_type %storage_image
%color = OpImageRead %v4uint %loaded_storage %coord
OpReturn
OpFunctionEnd
""".strip()


READONLY_R32_SCALAR_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %sint_storage_image DescriptorSet 0
OpDecorate %sint_storage_image Binding 0
OpDecorate %sint_storage_image NonWritable
OpDecorate %float_storage_image DescriptorSet 0
OpDecorate %float_storage_image Binding 1
OpDecorate %float_storage_image NonWritable
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%int = OpTypeInt 32 1
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v2uint = OpTypeVector %uint 2
%v4int = OpTypeVector %int 4
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%sint_storage_image_type = OpTypeImage %int 2D 0 0 0 2 R32i
%float_storage_image_type = OpTypeImage %float 2D 0 0 0 2 R32f
%ptr_uc_sint_storage_image = OpTypePointer UniformConstant %sint_storage_image_type
%ptr_uc_float_storage_image = OpTypePointer UniformConstant %float_storage_image_type
%sint_storage_image = OpVariable %ptr_uc_sint_storage_image UniformConstant
%float_storage_image = OpVariable %ptr_uc_float_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_sint_storage = OpLoad %sint_storage_image_type %sint_storage_image
%sint_color = OpImageRead %v4int %loaded_sint_storage %coord
%loaded_float_storage = OpLoad %float_storage_image_type %float_storage_image
%float_color = OpImageRead %v4float %loaded_float_storage %coord
OpReturn
OpFunctionEnd
""".strip()


COMPUTE_LAYOUT_FACTS_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %texture DescriptorSet 0
OpDecorate %texture Binding 0
OpDecorate %sampler DescriptorSet 0
OpDecorate %sampler Binding 0
OpDecorate %storage_image DescriptorSet 1
OpDecorate %storage_image Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%v3float = OpTypeVector %float 3
%v4float = OpTypeVector %float 4
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%float_0 = OpConstant %float 0
%int_0 = OpConstant %int 0
%sample_coord = OpConstantComposite %v3float %float_0 %float_0 %float_0
%storage_coord = OpConstantComposite %v2int %int_0 %int_0
%sampled_image_type = OpTypeImage %float 2D 0 1 0 1 Unknown
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba8
%sampler_type = OpTypeSampler
%combined_type = OpTypeSampledImage %sampled_image_type
%ptr_uc_sampled_image = OpTypePointer UniformConstant %sampled_image_type
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_uc_sampler = OpTypePointer UniformConstant %sampler_type
%texture = OpVariable %ptr_uc_sampled_image UniformConstant
%sampler = OpVariable %ptr_uc_sampler UniformConstant
%storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_texture = OpLoad %sampled_image_type %texture
%loaded_sampler = OpLoad %sampler_type %sampler
%combined = OpSampledImage %combined_type %loaded_texture %loaded_sampler
%sampled_color = OpImageSampleExplicitLod %v4float %combined %sample_coord Lod %float_0
%loaded_storage = OpLoad %storage_image_type %storage_image
%stored_color = OpImageRead %v4float %loaded_storage %storage_coord
%color = OpFAdd %v4float %sampled_color %stored_color
OpImageWrite %loaded_storage %storage_coord %color
OpReturn
OpFunctionEnd
""".strip()


LAYOUT_COUNTS_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_array ArrayStride 4
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %uniform_block Block
OpMemberDecorate %uniform_block 0 Offset 0
OpDecorate %readonly_storage_buffer DescriptorSet 0
OpDecorate %readonly_storage_buffer Binding 0
OpDecorate %readwrite_storage_buffer DescriptorSet 1
OpDecorate %readwrite_storage_buffer Binding 0
OpDecorate %uniform_buffer DescriptorSet 2
OpDecorate %uniform_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%storage_array = OpTypeArray %float %uint_1
%storage_block = OpTypeStruct %storage_array
%uniform_block = OpTypeStruct %float
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_block = OpTypePointer Uniform %uniform_block
%ptr_uniform_float = OpTypePointer Uniform %float
%readonly_storage_buffer = OpVariable %ptr_storage_block Uniform
%readwrite_storage_buffer = OpVariable %ptr_storage_block Uniform
%uniform_buffer = OpVariable %ptr_uniform_block Uniform
%main = OpFunction %void None %main_type
%entry = OpLabel
%readonly_ptr = OpAccessChain %ptr_uniform_float %readonly_storage_buffer %uint_0 %uint_0
%readonly_value = OpLoad %float %readonly_ptr
%uniform_ptr = OpAccessChain %ptr_uniform_float %uniform_buffer %uint_0
%uniform_value = OpLoad %float %uniform_ptr
%sum = OpFAdd %float %readonly_value %uniform_value
%readwrite_ptr = OpAccessChain %ptr_uniform_float %readwrite_storage_buffer %uint_0 %uint_0
OpStore %readwrite_ptr %sum
OpReturn
OpFunctionEnd
""".strip()


LAYOUT_COUNTS_FRAGMENT_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out_color
OpExecutionMode %main OriginUpperLeft
OpDecorate %out_color Location 0
OpDecorate %storage_array ArrayStride 4
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %uniform_block Block
OpMemberDecorate %uniform_block 0 Offset 0
OpDecorate %storage_buffer DescriptorSet 2
OpDecorate %storage_buffer Binding 0
OpDecorate %uniform_buffer DescriptorSet 3
OpDecorate %uniform_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%float_1 = OpConstant %float 1
%storage_array = OpTypeArray %float %uint_1
%storage_block = OpTypeStruct %storage_array
%uniform_block = OpTypeStruct %float
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_block = OpTypePointer Uniform %uniform_block
%ptr_uniform_float = OpTypePointer Uniform %float
%ptr_output_v4float = OpTypePointer Output %v4float
%storage_buffer = OpVariable %ptr_storage_block Uniform
%uniform_buffer = OpVariable %ptr_uniform_block Uniform
%out_color = OpVariable %ptr_output_v4float Output
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0 %uint_0
%storage_value = OpLoad %float %storage_ptr
%uniform_ptr = OpAccessChain %ptr_uniform_float %uniform_buffer %uint_0
%uniform_value = OpLoad %float %uniform_ptr
%sum = OpFAdd %float %storage_value %uniform_value
%color = OpCompositeConstruct %v4float %sum %sum %sum %float_1
OpStore %out_color %color
OpReturn
OpFunctionEnd
""".strip()


RUNTIME_ARRAY_STORAGE_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_array ArrayStride 4
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %storage_buffer DescriptorSet 0
OpDecorate %storage_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%storage_array = OpTypeRuntimeArray %float
%storage_block = OpTypeStruct %storage_array
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_float = OpTypePointer Uniform %float
%storage_buffer = OpVariable %ptr_storage_block Uniform
%main = OpFunction %void None %main_type
%entry = OpLabel
%storage_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_0 %uint_0
%value = OpLoad %float %storage_ptr
OpReturn
OpFunctionEnd
""".strip()


STORAGE_FLOAT3_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpMemberDecorate %storage_block 1 Offset 16
OpDecorate %storage_buffer DescriptorSet 0
OpDecorate %storage_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%v3float = OpTypeVector %float 3
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%storage_block = OpTypeStruct %v3float %float
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_v3float = OpTypePointer Uniform %v3float
%ptr_uniform_float = OpTypePointer Uniform %float
%storage_buffer = OpVariable %ptr_storage_block Uniform
%main = OpFunction %void None %main_type
%entry = OpLabel
%vec_ptr = OpAccessChain %ptr_uniform_v3float %storage_buffer %uint_0
%vec_value = OpLoad %v3float %vec_ptr
%scalar_ptr = OpAccessChain %ptr_uniform_float %storage_buffer %uint_1
%scalar_value = OpLoad %float %scalar_ptr
OpReturn
OpFunctionEnd
""".strip()


BAD_LAYOUT_UNIFORM_SET_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %uniform_block Block
OpMemberDecorate %uniform_block 0 Offset 0
OpDecorate %uniform_buffer DescriptorSet 0
OpDecorate %uniform_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%uniform_block = OpTypeStruct %float
%ptr_uniform_block = OpTypePointer Uniform %uniform_block
%ptr_uniform_float = OpTypePointer Uniform %float
%uniform_buffer = OpVariable %ptr_uniform_block Uniform
%main = OpFunction %void None %main_type
%entry = OpLabel
%uniform_ptr = OpAccessChain %ptr_uniform_float %uniform_buffer %uint_0
%uniform_value = OpLoad %float %uniform_ptr
OpReturn
OpFunctionEnd
""".strip()


BAD_LAYOUT_STORAGE_BUFFER_OVERLAP_COMPUTE_SPVASM = """
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %readonly_storage_image DescriptorSet 0
OpDecorate %readonly_storage_image Binding 0
OpDecorate %storage_array ArrayStride 4
OpDecorate %storage_block BufferBlock
OpMemberDecorate %storage_block 0 Offset 0
OpDecorate %readonly_storage_buffer DescriptorSet 0
OpDecorate %readonly_storage_buffer Binding 0
OpDecorate %readwrite_storage_buffer DescriptorSet 1
OpDecorate %readwrite_storage_buffer Binding 0
%void = OpTypeVoid
%main_type = OpTypeFunction %void
%float = OpTypeFloat 32
%uint = OpTypeInt 32 0
%v2uint = OpTypeVector %uint 2
%v4float = OpTypeVector %float 4
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%coord = OpConstantComposite %v2uint %uint_0 %uint_0
%storage_array = OpTypeArray %float %uint_1
%storage_block = OpTypeStruct %storage_array
%storage_image_type = OpTypeImage %float 2D 0 0 0 2 Rgba8
%ptr_uc_storage_image = OpTypePointer UniformConstant %storage_image_type
%ptr_storage_block = OpTypePointer Uniform %storage_block
%ptr_uniform_float = OpTypePointer Uniform %float
%readonly_storage_image = OpVariable %ptr_uc_storage_image UniformConstant
%readonly_storage_buffer = OpVariable %ptr_storage_block Uniform
%readwrite_storage_buffer = OpVariable %ptr_storage_block Uniform
%main = OpFunction %void None %main_type
%entry = OpLabel
%loaded_image = OpLoad %storage_image_type %readonly_storage_image
%image_value = OpImageRead %v4float %loaded_image %coord
%image_x = OpCompositeExtract %float %image_value 0
%readonly_ptr = OpAccessChain %ptr_uniform_float %readonly_storage_buffer %uint_0 %uint_0
%readonly_value = OpLoad %float %readonly_ptr
%sum = OpFAdd %float %image_x %readonly_value
%readwrite_ptr = OpAccessChain %ptr_uniform_float %readwrite_storage_buffer %uint_0 %uint_0
OpStore %readwrite_ptr %sum
OpReturn
OpFunctionEnd
""".strip()


HLSL_FRAGMENT_LAYOUT_FACTS = """
Texture2D<float4> InputTexture : register(t0, space2);
SamplerState InputSampler : register(s0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    return InputTexture.Sample(InputSampler, float2(0.5, 0.5));
}
""".strip()


HLSL_BUFFER_LAYOUT_FACTS = """
cbuffer LayoutParams : register(b0, space3)
{
    float3 eye_pos;
    float exposure;
    float values[2];
    row_major float3x3 basis;
};

float4 main(float4 position : SV_Position) : SV_Target0
{
    float value = eye_pos.x + exposure + values[1] + basis[0][0];
    return float4(value, value, value, 1.0);
}
""".strip()


HLSL_FRAGMENT_UNANNOTATED_WRITEONLY_STORAGE_LAYOUT_FACTS = """
RWTexture2D<float4> OutputTexture : register(u0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    OutputTexture[uint2(position.xy)] = float4(1.0, 0.0, 0.0, 1.0);
    return float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_DEPTH_CUBE_LAYOUT_FACTS = """
TextureCube<float> ShadowCube : register(t0, space2);
SamplerComparisonState ShadowSampler : register(s0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    float visibility = ShadowCube.SampleCmpLevelZero(ShadowSampler, float3(1.0, 0.0, 0.0), 0.5);
    return float4(visibility, visibility, visibility, 1.0);
}
""".strip()


HLSL_COMPUTE_LAYOUT_FACTS = """
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


HLSL_COMPUTE_WRITEONLY_STORAGE_LAYOUT_FACTS = """
[[vk::image_format("r32f")]]
RWTexture2D<float> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = 1.0;
}
""".strip()


HLSL_COMPUTE_UNANNOTATED_WRITEONLY_STORAGE_LAYOUT_FACTS = """
RWTexture2D<float4> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    OutputTexture[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
""".strip()


HLSL_COMPUTE_READWRITE_3D_STORAGE_LAYOUT_FACTS = """
RWTexture3D<uint> OutputTexture : register(u0, space1);

[numthreads(1, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint3 coord = uint3(0, 0, 0);
    uint value = OutputTexture[coord];
    OutputTexture[coord] = value + 1;
}
""".strip()


HLSL_MSAA_SAMPLED_LAYOUT_FACTS = """
Texture2DMS<float4> InputTexture : register(t0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    return InputTexture.Load(int2(0, 0), 0);
}
""".strip()


HLSL_MSAA_SCALAR_SAMPLED_LAYOUT_FACTS = """
Texture2DMS<float> InputTexture : register(t0, space2);

float4 main(float4 position : SV_Position) : SV_Target0
{
    float value = InputTexture.Load(int2(0, 0), 0);
    return float4(value, value, value, 1.0);
}
""".strip()


MSAA_DEPTH_AMBIGUOUS_DIAGNOSTIC = "automatic MSAA sampled texture layout facts require concrete final SPIR-V OpTypeImage Depth=0 or Depth=1 evidence; use an explicit sampled-slot policy when the intended SDL_GPU sampled slot shape is known"


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
            f"command failed with exit code {completed.returncode}: {' '.join(command)}\n{completed.stdout}"
        )
    if not expect_success and completed.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {' '.join(command)}\n{completed.stdout}")
    return completed


def require_contains(text, needle, message):
    if needle not in text:
        raise AssertionError(f"{message}\nmissing: {needle}\ntext:\n{text}")


def require_not_contains(text, needle, message):
    if needle in text:
        raise AssertionError(f"{message}\nunexpected: {needle}\ntext:\n{text}")


def require_count(text, needle, expected, message):
    actual = text.count(needle)
    if actual != expected:
        raise AssertionError(f"{message}\nexpected {expected} occurrences of: {needle}\nactual: {actual}\ntext:\n{text}")


def require_storage_texture_initializer(text, texture_type, storage_format, access, message):
    pattern = (
        r"\{\s*"
        rf"\.texture_type = {re.escape(texture_type)},\s*"
        rf"\.format = {re.escape(storage_format)},\s*"
        rf"\.access = {re.escape(access)},\s*"
        r"\}"
    )
    if not re.search(pattern, text):
        raise AssertionError(
            f"{message}\nmissing initializer for {texture_type}, {storage_format}, {access}\ntext:\n{text}"
        )


BROAD_READONLY_GRAPHICS_STORAGE_INITIALIZERS = [
    ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM"),
    ("SDL_GPU_TEXTURETYPE_2D_ARRAY", "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT"),
    ("SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT"),
    ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT"),
    ("SDL_GPU_TEXTURETYPE_2D_ARRAY", "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT"),
    ("SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT"),
    ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT"),
    ("SDL_GPU_TEXTURETYPE_2D_ARRAY", "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT"),
]


def require_broad_readonly_graphics_storage_layout_facts(text, symbol_prefix, message_prefix):
    require_contains(
        text,
        f"static const SDL_GPUStorageTextureSlotDescription {symbol_prefix}_storage_textures[]",
        f"{message_prefix} should emit shader storage texture layout_facts",
    )
    require_contains(
        text,
        f"static const SDL_GPUShaderResourceLayout {symbol_prefix}_resource_layout",
        f"{message_prefix} should emit shader resource layout facts",
    )
    require_count(text, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY", 8, f"{message_prefix} should preserve read-only access for every slot")
    require_not_contains(text, "readonly_storage_textures", f"{message_prefix} should use graphics shader storage texture layout_facts, not compute read-only layout_facts")
    require_not_contains(text, "readwrite_storage_textures", f"{message_prefix} should not emit compute read-write storage layout_facts")
    for texture_type, storage_format in BROAD_READONLY_GRAPHICS_STORAGE_INITIALIZERS:
        require_storage_texture_initializer(
            text,
            texture_type,
            storage_format,
            "SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY",
            f"{message_prefix} should preserve {texture_type} {storage_format}",
        )


def require_sampled_texture_initializers(text, initializers, message):
    index = 0
    for texture_type, sample_type, sampler_type in initializers:
        pattern = (
            r"\{\s*"
            rf"\.texture_type = {re.escape(texture_type)},\s*"
            rf"\.sample_type = {re.escape(sample_type)},\s*"
            rf"\.sampler_type = {re.escape(sampler_type)},\s*"
            r"\}"
        )
        match = re.search(pattern, text[index:])
        if not match:
            raise AssertionError(
                f"{message}\nmissing ordered initializer for {texture_type}, {sample_type}, {sampler_type}\ntext:\n{text}"
            )
        index += match.end()


def assemble(spirv_as, spirv_val, out_dir, name, source):
    asm_path = out_dir / f"{name}.spvasm"
    spv_path = out_dir / f"{name}.spv"
    asm_path.write_text(source + "\n", encoding="utf-8")
    run([spirv_as, "--target-env", "vulkan1.0", str(asm_path), "-o", str(spv_path)])
    run([spirv_val, "--target-env", "vulkan1.0", str(spv_path)])
    return spv_path


def cmake_string(value):
    return str(value).replace("\\", "/").replace('"', '\\"')


def compile_generated_c(cmake, sdl3_dir, source, out_dir, name, cmake_generator=None, cmake_generator_platform=None, cmake_generator_toolset=None, cmake_make_program=None, cmake_defines=None):
    name_hash = hashlib.sha1(name.encode("utf-8")).hexdigest()[:12]
    if sys.platform == "win32":
        compile_root = out_dir.parent / "_c" / name_hash
        if compile_root.exists():
            shutil.rmtree(compile_root)
        project_dir = compile_root / "p"
        build_dir = compile_root / "b"
    else:
        compile_root = None
        project_dir = out_dir / f"compile-{name_hash}-project"
        build_dir = out_dir / f"compile-{name_hash}-build"

    try:
        project_dir.mkdir(parents=True)
        (project_dir / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 3.16)
project(shadercross_layout_facts_compile C)
find_package(SDL3 REQUIRED CONFIG)
add_library(layout_facts OBJECT "{cmake_string(source)}")
if(TARGET SDL3::Headers)
    target_link_libraries(layout_facts PRIVATE SDL3::Headers)
elseif(TARGET SDL3::SDL3)
    target_link_libraries(layout_facts PRIVATE SDL3::SDL3)
else()
    message(FATAL_ERROR "SDL3 headers target not found")
endif()
""", encoding="utf-8")

        configure_command = [cmake, "-S", str(project_dir), "-B", str(build_dir)]
        if cmake_generator:
            configure_command.extend(["-G", cmake_generator])
        if cmake_generator_platform:
            configure_command.extend(["-A", cmake_generator_platform])
        if cmake_generator_toolset:
            configure_command.extend(["-T", cmake_generator_toolset])
        if cmake_make_program:
            configure_command.append(f"-DCMAKE_MAKE_PROGRAM={cmake_make_program}")
        if cmake_defines:
            for cmake_define in cmake_defines:
                configure_command.append(f"-D{cmake_define}")
        if sdl3_dir:
            configure_command.append(f"-DSDL3_DIR={sdl3_dir}")
        run(configure_command)
        run([cmake, "--build", str(build_dir), "--target", "layout_facts"])
    finally:
        if compile_root is not None:
            shutil.rmtree(compile_root, ignore_errors=True)


def hlsl_to_spirv_supported(shadercross, out_dir, hlsl_required):
    hlsl_probe = out_dir / "hlsl_probe.vert.hlsl"
    spirv_probe = out_dir / "hlsl_probe.spv"
    hlsl_probe.write_text("float4 main(float4 position : POSITION) : SV_Position { return position; }\n", encoding="utf-8")
    result = subprocess.run([
        shadercross,
        str(hlsl_probe),
        "-s", "HLSL",
        "-t", "vertex",
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

    print("Skipping HLSL layout facts C codegen coverage because this build cannot compile HLSL to SPIR-V.")
    print(result.stdout)
    return False


def main():
    if len(sys.argv) < 6:
        print(f"Usage: {sys.argv[0]} <shadercross> <spirv-as> <spirv-val> <output-dir> <cmake> [--sdl3-dir <path>] [--cmake-generator <name>] [--cmake-generator-platform <name>] [--cmake-generator-toolset <name>] [--cmake-make-program <path>] [--cmake-define <name=value>]", file=sys.stderr)
        return 2

    shadercross = sys.argv[1]
    spirv_as = sys.argv[2]
    spirv_val = sys.argv[3]
    out_dir = Path(sys.argv[4])
    cmake = sys.argv[5]
    sdl3_dir = None
    cmake_generator = None
    cmake_generator_platform = None
    cmake_generator_toolset = None
    cmake_make_program = None
    cmake_defines = []
    hlsl_required = False
    i = 6
    while i < len(sys.argv):
        if sys.argv[i] == "--sdl3-dir" and i + 1 < len(sys.argv):
            sdl3_dir = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--cmake-generator" and i + 1 < len(sys.argv):
            cmake_generator = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--cmake-generator-platform" and i + 1 < len(sys.argv):
            cmake_generator_platform = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--cmake-generator-toolset" and i + 1 < len(sys.argv):
            cmake_generator_toolset = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--cmake-make-program" and i + 1 < len(sys.argv):
            cmake_make_program = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--cmake-define" and i + 1 < len(sys.argv):
            cmake_defines.append(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "--hlsl-required":
            hlsl_required = True
            i += 1
        else:
            print(f"Unexpected argument: {sys.argv[i]}", file=sys.stderr)
            return 2

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    help_result = run([shadercross, "--help"])
    require_contains(help_result.stdout, "--resource-layout-c", "help should advertise resource layout C output")
    require_contains(help_result.stdout, "--resource-layout-sampled-slot", "help should advertise resource layout sampled-slot policy")

    sampled_spv = assemble(spirv_as, spirv_val, out_dir, "sampled-fragment", SAMPLED_FRAGMENT_SPVASM)
    samplerless_spv = assemble(spirv_as, spirv_val, out_dir, "samplerless-fragment", SAMPLERLESS_FRAGMENT_SPVASM)
    uint_samplerless_spv = assemble(spirv_as, spirv_val, out_dir, "uint-samplerless-fragment", UINT_SAMPLERLESS_FRAGMENT_SPVASM)
    sint_samplerless_spv = assemble(spirv_as, spirv_val, out_dir, "sint-samplerless-fragment", SINT_SAMPLERLESS_FRAGMENT_SPVASM)
    msaa_sampled_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-sampled-fragment", MSAA_SAMPLED_FRAGMENT_SPVASM)
    msaa_unknown_depth_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-unknown-depth-fragment", MSAA_UNKNOWN_DEPTH_FRAGMENT_SPVASM)
    msaa_array_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-array-fragment", MSAA_ARRAY_FRAGMENT_SPVASM)
    msaa_uint_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-uint-fragment", MSAA_UINT_FRAGMENT_SPVASM)
    msaa_wrong_set_spv = assemble(spirv_as, spirv_val, out_dir, "msaa-wrong-set-fragment", MSAA_WRONG_SET_FRAGMENT_SPVASM)
    mixed_paired_and_samplerless_spv = assemble(spirv_as, spirv_val, out_dir, "mixed-paired-and-samplerless-fragment", MIXED_PAIRED_AND_SAMPLERLESS_FRAGMENT_SPVASM)
    normal_depth_spv = assemble(spirv_as, spirv_val, out_dir, "normal-depth-fragment", NORMAL_DEPTH_FRAGMENT_SPVASM)
    depth_dref_gather_spv = assemble(spirv_as, spirv_val, out_dir, "depth-dref-gather-fragment", DEPTH_DREF_GATHER_FRAGMENT_SPVASM)
    depth_cube_dref_spv = assemble(spirv_as, spirv_val, out_dir, "depth-cube-dref-fragment", DEPTH_CUBE_DREF_FRAGMENT_SPVASM)
    depth_cube_array_spv = assemble(spirv_as, spirv_val, out_dir, "depth-cube-array-fragment", DEPTH_CUBE_ARRAY_FRAGMENT_SPVASM)
    uint_sampled_spv = assemble(spirv_as, spirv_val, out_dir, "uint-sampled-fragment", UINT_SAMPLED_FRAGMENT_SPVASM)
    wrong_set_spv = assemble(spirv_as, spirv_val, out_dir, "wrong-set-fragment", WRONG_SET_FRAGMENT_SPVASM)
    multi_entrypoint_spv = assemble(spirv_as, spirv_val, out_dir, "multi-entrypoint-fragment", MULTI_ENTRYPOINT_FRAGMENT_SPVASM)
    multi_entrypoint_shared_depth_dref_spv = assemble(spirv_as, spirv_val, out_dir, "multi-entrypoint-shared-depth-dref-fragment", MULTI_ENTRYPOINT_SHARED_DEPTH_DREF_SPVASM)
    helper_parameter_sampled_pair_spv = assemble(spirv_as, spirv_val, out_dir, "helper-parameter-sampled-pair-fragment", HELPER_PARAMETER_SAMPLED_PAIR_FRAGMENT_SPVASM)
    readwrite_r32_uint_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-r32-uint-storage-compute", READWRITE_R32_UINT_STORAGE_COMPUTE_SPVASM)
    readwrite_r32_uint_storage_3d_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-r32-uint-storage-3d-compute", READWRITE_R32_UINT_STORAGE_3D_COMPUTE_SPVASM)
    readwrite_r32_uint_storage_2d_array_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-r32-uint-storage-2d-array-compute", READWRITE_R32_UINT_STORAGE_2D_ARRAY_COMPUTE_SPVASM)
    writeonly_storage_texture_resource_array_compute_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-storage-texture-resource-array-compute", WRITEONLY_STORAGE_TEXTURE_RESOURCE_ARRAY_COMPUTE_SPVASM)
    readwrite_r32_scalar_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-r32-scalar-storage-compute", READWRITE_R32_SCALAR_STORAGE_COMPUTE_SPVASM)
    writable_r32_scalar_storage_3d_spv = assemble(spirv_as, spirv_val, out_dir, "writable-r32-scalar-storage-3d-compute", WRITABLE_R32_SCALAR_STORAGE_3D_COMPUTE_SPVASM)
    writable_broad_storage_spv = assemble(spirv_as, spirv_val, out_dir, "writable-broad-storage-compute", WRITABLE_BROAD_STORAGE_COMPUTE_SPVASM)
    readwrite_broad_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-broad-storage-compute", READWRITE_BROAD_STORAGE_COMPUTE_SPVASM)
    readwrite_rg32_float_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-rg32-float-storage-compute", READWRITE_RG32_FLOAT_STORAGE_COMPUTE_SPVASM)
    readonly_broad_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readonly-broad-storage-compute", READONLY_BROAD_STORAGE_COMPUTE_SPVASM)
    readonly_broad_storage_vertex_spv = assemble(spirv_as, spirv_val, out_dir, "readonly-broad-storage-vertex", readonly_broad_storage_graphics_spvasm("Vertex", 0))
    readonly_broad_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "readonly-broad-storage-fragment", readonly_broad_storage_graphics_spvasm("Fragment", 2))
    writeonly_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-graphics-storage-fragment", WRITEONLY_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    writeonly_wrong_set_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-wrong-set-graphics-storage-fragment", WRITEONLY_WRONG_SET_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    writeonly_sparse_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-sparse-graphics-storage-fragment", WRITEONLY_SPARSE_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    readwrite_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-graphics-storage-fragment", READWRITE_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    writeonly_graphics_storage_vertex_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-graphics-storage-vertex", WRITEONLY_GRAPHICS_STORAGE_VERTEX_SPVASM)
    writeonly_arrayed_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-arrayed-graphics-storage-fragment", WRITEONLY_ARRAYED_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    readwrite_arrayed_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "readwrite-arrayed-graphics-storage-fragment", READWRITE_ARRAYED_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    writeonly_storage_texture_resource_array_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "writeonly-storage-texture-resource-array-fragment", WRITEONLY_STORAGE_TEXTURE_RESOURCE_ARRAY_FRAGMENT_SPVASM)
    fragment_writable_storage_buffer_spv = assemble(spirv_as, spirv_val, out_dir, "fragment-writable-storage-buffer", FRAGMENT_WRITABLE_STORAGE_BUFFER_SPVASM)
    fragment_readonly_buffer_writable_texture_spv = assemble(spirv_as, spirv_val, out_dir, "fragment-readonly-buffer-writable-texture", FRAGMENT_READONLY_BUFFER_WRITABLE_TEXTURE_SPVASM)
    fragment_writable_texture_writable_buffer_spv = assemble(spirv_as, spirv_val, out_dir, "fragment-writable-texture-writable-buffer", FRAGMENT_WRITABLE_TEXTURE_WRITABLE_BUFFER_SPVASM)
    vertex_writable_storage_buffer_spv = assemble(spirv_as, spirv_val, out_dir, "vertex-writable-storage-buffer", VERTEX_WRITABLE_STORAGE_BUFFER_SPVASM)
    fragment_writable_storage_buffer_array_spv = assemble(spirv_as, spirv_val, out_dir, "fragment-writable-storage-buffer-array", FRAGMENT_WRITABLE_STORAGE_BUFFER_ARRAY_SPVASM)
    fragment_atomic_storage_buffer_spv = assemble(spirv_as, spirv_val, out_dir, "fragment-atomic-storage-buffer", FRAGMENT_ATOMIC_STORAGE_BUFFER_SPVASM)
    unsupported_graphics_storage_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "unsupported-graphics-storage-fragment", UNSUPPORTED_GRAPHICS_STORAGE_FRAGMENT_SPVASM)
    readonly_r32_uint_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readonly-r32-uint-storage-compute", READONLY_R32_UINT_STORAGE_COMPUTE_SPVASM)
    readonly_r32_scalar_storage_spv = assemble(spirv_as, spirv_val, out_dir, "readonly-r32-scalar-storage-compute", READONLY_R32_SCALAR_STORAGE_COMPUTE_SPVASM)
    compute_spv = assemble(spirv_as, spirv_val, out_dir, "compute-layout_facts", COMPUTE_LAYOUT_FACTS_SPVASM)
    layout_counts_compute_spv = assemble(spirv_as, spirv_val, out_dir, "layout-counts-compute", LAYOUT_COUNTS_COMPUTE_SPVASM)
    layout_counts_fragment_spv = assemble(spirv_as, spirv_val, out_dir, "layout-counts-fragment", LAYOUT_COUNTS_FRAGMENT_SPVASM)
    runtime_array_storage_compute_spv = assemble(spirv_as, spirv_val, out_dir, "runtime-array-storage-compute", RUNTIME_ARRAY_STORAGE_COMPUTE_SPVASM)
    storage_float3_compute_spv = assemble(spirv_as, spirv_val, out_dir, "storage-float3-compute", STORAGE_FLOAT3_COMPUTE_SPVASM)
    bad_layout_uniform_set_compute_spv = assemble(spirv_as, spirv_val, out_dir, "bad-layout-uniform-set-compute", BAD_LAYOUT_UNIFORM_SET_COMPUTE_SPVASM)
    bad_layout_storage_buffer_overlap_compute_spv = assemble(spirv_as, spirv_val, out_dir, "bad-layout-storage-buffer-overlap-compute", BAD_LAYOUT_STORAGE_BUFFER_OVERLAP_COMPUTE_SPVASM)
    sparse_sampled_spv = assemble(spirv_as, spirv_val, out_dir, "sparse-sampled-fragment", SPARSE_SAMPLED_FRAGMENT_SPVASM)
    layout_facts_c = out_dir / "sampled_layout_facts.c"
    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(layout_facts_c),
        "--resource-layout-symbol-prefix", "sampled_shader",
    ])
    generated = layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "// Generated by SDL_shadercross", "resource layout C output should include generated header")
    require_contains(generated, "#include <SDL3/SDL_gpu.h>", "resource layout C output should include SDL_gpu.h")
    require_contains(generated, "note: sampled texture slot 0 resolved ambiguous float sampler policy", "resource layout C output should document float sampler policy resolution")
    require_not_contains(generated, "static const SDL_GPUSampledTextureSlotDescription sampled_shader_sampled_texture_slots[]", "default sampled texture layout facts should not emit an unnecessary slot array")
    require_contains(generated, "static const SDL_GPUShaderResourceLayout sampled_shader_resource_layout", "resource layout C output should emit layout facts")
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 resolved ambiguous float sampler policy", "resource layout C output should log float sampler policy resolution")
    def compile_layout_facts_source(source, name):
        compile_generated_c(cmake, sdl3_dir, source, out_dir, name, cmake_generator, cmake_generator_platform, cmake_generator_toolset, cmake_make_program, cmake_defines)

    compile_layout_facts_source(layout_facts_c, "sampled_layout_facts")

    sampled_layout_c = out_dir / "sampled_layout.c"
    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(sampled_layout_c),
        "--resource-layout-symbol-prefix", "sampled_layout",
    ])
    generated = sampled_layout_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUShaderResourceLayout sampled_layout_resource_layout", "layout C output should emit shader layout facts")
    require_not_contains(generated, ".version = sizeof(SDL_GPU", "layout C output should not emit SDL_GPU version initializers")
    require_contains(generated, ".stage = SDL_GPU_SHADERSTAGE_FRAGMENT", "layout C output should preserve shader stage")
    require_contains(generated, ".num_samplers = 1", "layout C output should preserve default sampled texture count")
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 resolved ambiguous float sampler policy", "layout C output should keep layout facts policy diagnostics")
    compile_layout_facts_source(sampled_layout_c, "sampled_layout")

    unfilterable_policy_layout_c = out_dir / "unfilterable_policy_layout.c"
    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(unfilterable_policy_layout_c),
        "--resource-layout-symbol-prefix", "unfilterable_policy_layout",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ])
    generated = unfilterable_policy_layout_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription unfilterable_policy_layout_sampled_texture_slots[]", "resource-layout sampled policy should emit sampled texture layout_facts")
    require_contains(generated, "static const SDL_GPUShaderResourceLayout unfilterable_policy_layout_resource_layout", "resource-layout sampled policy should emit shader layout facts")
    require_contains(generated, ".sampled_texture_slots = unfilterable_policy_layout_sampled_texture_slots", "resource-layout sampled policy should reference generated sampled texture layout facts")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT", "SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING"),
        ],
        "resource-layout sampled policy should preserve the requested slot shape",
    )
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 uses explicit sampled-slot policy", "resource-layout sampled policy should log its policy note")
    compile_layout_facts_source(unfilterable_policy_layout_c, "unfilterable_policy_layout")

    unfilterable_policy_layout_facts_c = out_dir / "unfilterable_policy_layout_facts.c"
    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(unfilterable_policy_layout_facts_c),
        "--resource-layout-symbol-prefix", "unfilterable_policy_shader",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ])
    generated = unfilterable_policy_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "note: sampled texture slot 0 uses explicit sampled-slot policy", "explicit unfilterable sampled policy should be documented")
    require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription unfilterable_policy_shader_sampled_texture_slots[]", "explicit unfilterable sampled policy should emit sampled texture layout_facts")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_UNFILTERABLE_FLOAT", "SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING"),
        ],
        "explicit unfilterable sampled policy should preserve the requested slot shape",
    )
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 uses explicit sampled-slot policy", "explicit unfilterable sampled policy should log its policy note")
    compile_layout_facts_source(unfilterable_policy_layout_facts_c, "unfilterable_policy_layout_facts")

    compute_layout_facts_c = out_dir / "compute_layout_facts.c"
    result = run([
        shadercross,
        str(compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(compute_layout_facts_c),
        "--resource-layout-symbol-prefix", "compute_pipeline",
    ])
    generated = compute_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription compute_pipeline_sampled_texture_slots[]", "compute resource layout C output should emit sampled texture layout_facts")
    require_contains(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D_ARRAY", "compute resource layout C output should preserve sampled 2D-array texture facts")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription compute_pipeline_readwrite_storage_textures[]", "compute resource layout C output should emit read-write storage texture layout_facts")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "compute resource layout C output should preserve read-write storage texture access")
    require_contains(generated, ".sampled_texture_slots = compute_pipeline_sampled_texture_slots", "compute resource layout C output should reference generated sampled texture layout facts")
    require_contains(generated, ".readwrite_storage_texture_slots = compute_pipeline_readwrite_storage_textures", "compute resource layout C output should reference generated read-write storage texture layout facts")
    require_not_contains(generated, ".version = sizeof(SDL_GPU", "compute resource layout C output should not emit SDL_GPU version initializers")
    require_contains(generated, "static const SDL_GPUComputePipelineResourceLayout compute_pipeline_resource_layout", "compute resource layout C output should emit layout facts")
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 resolved ambiguous float sampler policy", "compute sampled texture layout facts should log float sampler policy resolution")
    compile_layout_facts_source(compute_layout_facts_c, "compute_layout_facts")

    compute_layout_c = out_dir / "compute_layout.c"
    result = run([
        shadercross,
        str(compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(compute_layout_c),
        "--resource-layout-symbol-prefix", "compute_layout",
    ])
    generated = compute_layout_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUComputePipelineResourceLayout compute_layout_resource_layout", "compute layout C output should emit compute layout facts")
    require_not_contains(generated, ".version = sizeof(SDL_GPU", "compute layout C output should not emit SDL_GPU version initializers")
    require_contains(generated, ".num_samplers = 1", "compute layout C output should preserve sampled texture count")
    require_contains(generated, ".num_readwrite_storage_textures = 1", "compute layout C output should preserve read-write storage texture count")
    require_contains(generated, ".sampled_texture_slots = compute_layout_sampled_texture_slots", "non-default compute layout C output should reference generated sampled texture layout facts")
    require_contains(generated, ".readwrite_storage_texture_slots = compute_layout_readwrite_storage_textures", "non-default compute layout C output should reference generated storage texture layout facts")
    compile_layout_facts_source(compute_layout_c, "compute_layout")

    layout_counts_c = out_dir / "layout_counts.c"
    run([
        shadercross,
        str(layout_counts_compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(layout_counts_c),
        "--resource-layout-symbol-prefix", "layout_counts",
    ])
    generated = layout_counts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUComputePipelineResourceLayout layout_counts_resource_layout", "buffer-count layout C output should emit compute layout facts")
    require_contains(generated, ".num_readonly_storage_buffers = 1", "buffer-count layout C output should preserve read-only storage buffer count")
    require_contains(generated, ".num_readwrite_storage_buffers = 1", "buffer-count layout C output should preserve read-write storage buffer count")
    require_contains(generated, ".num_uniform_buffers = 1", "buffer-count layout C output should preserve uniform buffer count")
    require_contains(generated, "enum { layout_counts_readonly_storage_buffer_0_size = 4 };", "buffer-count layout C output should emit read-only storage buffer size constant")
    require_contains(generated, "enum { layout_counts_readwrite_storage_buffer_0_size = 4 };", "buffer-count layout C output should emit read-write storage buffer size constant")
    require_contains(generated, "enum { layout_counts_uniform_buffer_0_size = 4 };", "buffer-count layout C output should emit uniform buffer size constant")
    require_contains(generated, "member 0 <unnamed>: offset=0 size=4 type=float32", "buffer-count layout C output should include storage/uniform member layout facts")
    require_contains(generated, "array_stride=4", "buffer-count layout C output should include storage buffer array stride facts")
    compile_layout_facts_source(layout_counts_c, "layout_counts")

    layout_counts_fragment_c = out_dir / "layout_counts_fragment.c"
    run([
        shadercross,
        str(layout_counts_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(layout_counts_fragment_c),
        "--resource-layout-symbol-prefix", "layout_counts_fragment",
    ])
    generated = layout_counts_fragment_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUShaderResourceLayout layout_counts_fragment_resource_layout", "graphics buffer-count layout C output should emit shader layout facts")
    require_contains(generated, ".stage = SDL_GPU_SHADERSTAGE_FRAGMENT", "graphics buffer-count layout C output should preserve shader stage")
    require_contains(generated, ".num_storage_buffers = 1", "graphics buffer-count layout C output should preserve storage buffer count")
    require_contains(generated, ".num_uniform_buffers = 1", "graphics buffer-count layout C output should preserve uniform buffer count")
    require_contains(generated, "enum { layout_counts_fragment_storage_buffer_0_size = 4 };", "graphics buffer-count layout C output should emit storage buffer size constant")
    require_contains(generated, "enum { layout_counts_fragment_uniform_buffer_0_size = 4 };", "graphics buffer-count layout C output should emit uniform buffer size constant")
    require_contains(generated, "member 0 <unnamed>: offset=0 size=4 type=float32", "graphics buffer-count layout C output should include buffer member layout facts")
    compile_layout_facts_source(layout_counts_fragment_c, "layout_counts_fragment")

    runtime_array_layout_c = out_dir / "runtime_array_storage_layout.c"
    runtime_array_layout_json = out_dir / "runtime_array_storage_layout.json"
    run([
        shadercross,
        str(runtime_array_storage_compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "-d", "JSON",
        "-o", str(runtime_array_layout_json),
        "--resource-layout-c", str(runtime_array_layout_c),
        "--resource-layout-symbol-prefix", "runtime_array_storage_layout",
    ])
    generated = runtime_array_layout_c.read_text(encoding="utf-8")
    layout_json = json.loads(runtime_array_layout_json.read_text(encoding="utf-8"))
    storage_buffers = layout_json["sdl_resource_layout"]["storage_buffers"]
    if len(storage_buffers) != 1:
        raise AssertionError(f"runtime-array storage fixture should reflect one storage buffer, got {len(storage_buffers)}")
    storage_buffer = storage_buffers[0]
    if storage_buffer["fixed_size"]:
        raise AssertionError(f"runtime-array storage fixture must not report a fixed binding size: {storage_buffer}")
    if not storage_buffer["runtime_or_dynamic_array"]:
        raise AssertionError(f"runtime-array storage fixture should report runtime/dynamic array facts: {storage_buffer}")
    require_not_contains(generated, "runtime_array_storage_layout_readonly_storage_buffer_0_size", "runtime-array storage layout C output must not emit a misleading fixed size constant")
    require_contains(generated, "no fixed binding-size constant emitted; block includes a runtime or specialization-sized array", "runtime-array storage layout C output should explain why no fixed size constant was emitted")
    require_contains(generated, "runtime_or_dynamic_array=true array_stride=4", "runtime-array storage layout C output should include stride facts for the runtime array")
    compile_layout_facts_source(runtime_array_layout_c, "runtime_array_storage_layout")

    storage_float3_layout_c = out_dir / "storage_float3_layout.c"
    storage_float3_layout_json = out_dir / "storage_float3_layout.json"
    run([
        shadercross,
        str(storage_float3_compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "-d", "JSON",
        "-o", str(storage_float3_layout_json),
        "--resource-layout-c", str(storage_float3_layout_c),
        "--resource-layout-symbol-prefix", "storage_float3_layout",
    ])
    generated = storage_float3_layout_c.read_text(encoding="utf-8")
    layout_json = json.loads(storage_float3_layout_json.read_text(encoding="utf-8"))
    storage_buffers = layout_json["sdl_resource_layout"]["storage_buffers"]
    if len(storage_buffers) != 1:
        raise AssertionError(f"storage-buffer float3 fixture should reflect one storage buffer, got {len(storage_buffers)}")
    storage_buffer = storage_buffers[0]
    if not storage_buffer["fixed_size"] or storage_buffer["runtime_or_dynamic_array"]:
        raise AssertionError(f"storage-buffer float3 fixture should report a fixed binding size: {storage_buffer}")
    first_member = storage_buffer["members"][0]
    if "storage_buffer_float3_align16" not in first_member["hazards"] or "float3_tail_or_following_padding" not in first_member["hazards"]:
        raise AssertionError(f"storage-buffer float3 hazards were not reflected as expected: {first_member}")
    require_contains(generated, "enum { storage_float3_layout_readonly_storage_buffer_0_size = 20 };", "storage-buffer float3 layout C output should emit fixed binding size")
    require_contains(generated, "member 0 <unnamed>: offset=0 size=12 type=float32 vec=3", "storage-buffer float3 layout C output should include vec3 member facts")
    require_contains(generated, "storage-buffer vec3 alignment", "storage-buffer float3 layout C output should warn about storage-buffer vec3 alignment")
    require_contains(generated, "reflected layout leaves padding after the three float components", "storage-buffer float3 layout C output should distinguish tail/following padding")
    compile_layout_facts_source(storage_float3_layout_c, "storage_float3_layout")

    result = run([
        shadercross,
        str(bad_layout_uniform_set_compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(out_dir / "bad_layout_uniform_set.c"),
        "--resource-layout-symbol-prefix", "bad_layout_uniform_set",
    ], expect_success=False)
    require_contains(result.stdout, "uniform buffer binding does not match SDL's resource layout slot convention", "layout C output should reject uniform buffers outside SDL's resource layout convention")

    result = run([
        shadercross,
        str(bad_layout_storage_buffer_overlap_compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(out_dir / "bad_layout_storage_buffer_overlap.c"),
        "--resource-layout-symbol-prefix", "bad_layout_storage_buffer_overlap",
    ], expect_success=False)
    require_contains(result.stdout, "storage buffer binding does not match SDL's resource layout slot convention", "layout C output should reject storage buffers that overlap earlier SDL resource classes")

    layout_facts_with_json_c = out_dir / "sampled_with_json_layout_facts.c"
    json_output = out_dir / "sampled.json"
    run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(json_output),
        "--resource-layout-c", str(layout_facts_with_json_c),
        "--resource-layout-symbol-prefix", "sampled_json_shader",
    ])
    require_contains(json_output.read_text(encoding="utf-8"), '"sdl_resource_layout"', "SPIR-V output plus resource layout mode should still write the requested output file")
    require_contains(layout_facts_with_json_c.read_text(encoding="utf-8"), "sampled_json_shader_resource_layout", "SPIR-V output plus resource layout mode should write resource layout C")

    missing_prefix_c = out_dir / "missing_prefix.c"
    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "missing_resource_layout_prefix.c"),
    ], expect_success=False)
    require_contains(result.stdout, "--resource-layout-symbol-prefix is required with --resource-layout-c", "missing resource-layout prefix should fail clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "duplicate_resource_layout_a.c"),
        "--resource-layout-c", str(out_dir / "duplicate_resource_layout_b.c"),
    ], expect_success=False)
    require_contains(result.stdout, "--resource-layout-c cannot be specified more than once", "duplicate resource-layout output should fail clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-symbol-prefix", "sampled_layout_a",
        "--resource-layout-symbol-prefix", "sampled_layout_b",
    ], expect_success=False)
    require_contains(result.stdout, "--resource-layout-symbol-prefix cannot be specified more than once", "duplicate resource-layout prefix should fail clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(missing_prefix_c),
    ], expect_success=False)
    require_contains(result.stdout, "--resource-layout-symbol-prefix is required with --resource-layout-c", "missing resource-layout prefix should fail clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "bad_prefix.c"),
        "--resource-layout-symbol-prefix", "bad-prefix",
    ], expect_success=False)
    require_contains(result.stdout, "C symbol prefix", "invalid resource-layout prefix should fail clearly")
    if (out_dir / "bad_prefix.c").exists():
        raise AssertionError("invalid resource-layout prefix should not create a partial resource layout C output")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(out_dir / "sampled_policy_requires_layout_facts.json"),
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ], expect_success=False)
    require_contains(result.stdout, "--resource-layout-sampled-slot requires --resource-layout-c", "resource-layout sampled slot policy should require resource layout C output")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "bad_sampled_policy.c"),
        "--resource-layout-symbol-prefix", "bad_sampled_policy",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=not_a_sample,sampler=non_filtering",
    ], expect_success=False)
    require_contains(result.stdout, "invalid --resource-layout-sampled-slot", "invalid sampled slot policy token should fail clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "duplicate_sampled_policy.c"),
        "--resource-layout-symbol-prefix", "duplicate_sampled_policy",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ], expect_success=False)
    require_contains(result.stdout, "duplicate slot 0", "duplicate sampled slot policy should fail clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "unsupported_sampled_policy_tuple.c"),
        "--resource-layout-symbol-prefix", "unsupported_sampled_policy_tuple",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=filtering",
    ], expect_success=False)
    require_contains(result.stdout, "explicit sampled-slot policy does not match the reflected sampled texture evidence", "unsupported sampled slot policy tuple should fail clearly")

    result = run([
        shadercross,
        str(readwrite_r32_uint_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(out_dir / "bad_storage_policy_tuple.c"),
        "--resource-layout-symbol-prefix", "bad_storage_policy_tuple",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=write",
    ], expect_success=False)
    require_contains(result.stdout, "--storage-texture-slot does not match the reflected storage texture evidence", "storage texture policy should reject contradictory read-write evidence")

    result = run([
        shadercross,
        str(readwrite_r32_uint_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(out_dir / "duplicate_storage_policy.c"),
        "--resource-layout-symbol-prefix", "duplicate_storage_policy",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=read_write",
        "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=r32uint,access=read_write",
    ], expect_success=False)
    require_contains(result.stdout, "duplicate class/slot pair", "duplicate storage texture policy should fail clearly")

    result = run([
        shadercross,
        str(readwrite_r32_uint_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(out_dir / "out_of_range_storage_policy.c"),
        "--resource-layout-symbol-prefix", "out_of_range_storage_policy",
        "--storage-texture-slot", "class=readwrite,slot=2,texture=2d,format=r32uint,access=read_write",
    ], expect_success=False)
    require_contains(result.stdout, "slot 2 exceeds the reflected SDL storage texture count", "out-of-range storage texture policy should fail clearly")

    result = run([
        shadercross,
        str(samplerless_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "samplerless_layout_facts.c"),
        "--resource-layout-symbol-prefix", "samplerless_shader",
    ], expect_success=False)
    require_contains(result.stdout, "sampled texture sample kind and sampler binding kind", "float samplerless sampled images should remain rejected clearly")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "out_of_range_sampled_policy.c"),
        "--resource-layout-symbol-prefix", "out_of_range_sampled_policy",
        "--resource-layout-sampled-slot", "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ], expect_success=False)
    require_contains(result.stdout, "sampled slot policy slot 2 exceeds the reflected SDL sampled texture count", "out-of-range sampled slot policy should fail clearly")

    result = run([
        shadercross,
        str(sparse_sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "unreflected_sampled_policy.c"),
        "--resource-layout-symbol-prefix", "unreflected_sampled_policy",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ], expect_success=False)
    require_contains(result.stdout, "sampled slot policy slot 0 does not match a reflected sampled texture", "unreflected sampled slot policy should fail clearly")

    uint_samplerless_layout_facts_c = out_dir / "uint_samplerless_layout_facts.c"
    run([
        shadercross,
        str(uint_samplerless_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(uint_samplerless_layout_facts_c),
        "--resource-layout-symbol-prefix", "uint_samplerless_shader",
    ])
    generated = uint_samplerless_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription uint_samplerless_shader_sampled_texture_slots[]", "integer samplerless resource layout C output should emit sampled texture layout_facts")
    require_contains(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D", "integer samplerless resource layout C output should preserve 2D texture type")
    require_contains(generated, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT", "integer samplerless resource layout C output should preserve uint sample type")
    require_contains(generated, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE", "integer samplerless resource layout C output should emit samplerless layout_facts")
    compile_layout_facts_source(uint_samplerless_layout_facts_c, "uint_samplerless_layout_facts")

    sint_samplerless_layout_facts_c = out_dir / "sint_samplerless_layout_facts.c"
    run([
        shadercross,
        str(sint_samplerless_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(sint_samplerless_layout_facts_c),
        "--resource-layout-symbol-prefix", "sint_samplerless_shader",
    ])
    generated = sint_samplerless_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_SINT", "signed integer samplerless resource layout C output should preserve sint sample type")
    require_contains(generated, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE", "signed integer samplerless resource layout C output should emit samplerless layout_facts")
    compile_layout_facts_source(sint_samplerless_layout_facts_c, "sint_samplerless_layout_facts")

    msaa_sampled_layout_facts_c = out_dir / "msaa_sampled_layout_facts.c"
    run([
        shadercross,
        str(msaa_sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(msaa_sampled_layout_facts_c),
        "--resource-layout-symbol-prefix", "msaa_sampled_shader",
    ])
    generated = msaa_sampled_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription msaa_sampled_shader_sampled_texture_slots[]", "MSAA sampled resource layout C output should emit sampled texture layout_facts")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT", "SDL_GPU_SHADERSAMPLERTYPE_NONE"),
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH", "SDL_GPU_SHADERSAMPLERTYPE_NONE"),
        ],
        "MSAA sampled resource layout C output should preserve raw Depth=0 for slot 0 and raw Depth=1 for slot 1",
    )
    require_count(generated, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONE", 2, "MSAA sampled layout facts should be samplerless for both slots")
    compile_layout_facts_source(msaa_sampled_layout_facts_c, "msaa_sampled_layout_facts")

    result = run([
        shadercross,
        str(msaa_unknown_depth_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "msaa_unknown_depth_layout_facts.c"),
        "--resource-layout-symbol-prefix", "msaa_unknown_depth_shader",
    ], expect_success=False)
    require_contains(result.stdout, MSAA_DEPTH_AMBIGUOUS_DIAGNOSTIC, "MSAA Depth=2 layout facts should reject as ambiguous")

    msaa_unknown_depth_policy_layout_facts_c = out_dir / "msaa_unknown_depth_policy_layout_facts.c"
    result = run([
        shadercross,
        str(msaa_unknown_depth_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(msaa_unknown_depth_policy_layout_facts_c),
        "--resource-layout-symbol-prefix", "msaa_unknown_depth_policy_shader",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=multisampled_unfilterable_float,sampler=none",
    ])
    generated = msaa_unknown_depth_policy_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "note: sampled texture slot 0 uses explicit sampled-slot policy", "explicit MSAA sampled policy should be documented")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT", "SDL_GPU_SHADERSAMPLERTYPE_NONE"),
        ],
        "explicit MSAA sampled policy should resolve ambiguous Depth=2 evidence",
    )
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 uses explicit sampled-slot policy", "explicit MSAA sampled policy should log its policy note")
    compile_layout_facts_source(msaa_unknown_depth_policy_layout_facts_c, "msaa_unknown_depth_policy_layout_facts")

    result = run([
        shadercross,
        str(msaa_sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "msaa_contradictory_policy_layout_facts.c"),
        "--resource-layout-symbol-prefix", "msaa_contradictory_policy_shader",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=multisampled_depth,sampler=none",
    ], expect_success=False)
    require_contains(result.stdout, "explicit sampled-slot policy does not match the reflected sampled texture evidence", "explicit sampled slot policy should reject contradictory concrete MSAA evidence")

    result = run([
        shadercross,
        str(msaa_array_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "msaa_array_layout_facts.c"),
        "--resource-layout-symbol-prefix", "msaa_array_shader",
    ], expect_success=False)
    require_contains(result.stdout, "sampled texture dimension is outside the current SDL_GPU resource layout fact matrix", "arrayed MSAA sampled layout facts should reject clearly")

    result = run([
        shadercross,
        str(msaa_uint_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "msaa_uint_layout_facts.c"),
        "--resource-layout-symbol-prefix", "msaa_uint_shader",
    ], expect_success=False)
    require_contains(result.stdout, "sampled texture sample kind and sampler binding kind", "integer MSAA sampled layout facts should reject until a carrier shape exists")

    result = run([
        shadercross,
        str(msaa_wrong_set_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "msaa_wrong_set_layout_facts.c"),
        "--resource-layout-symbol-prefix", "msaa_wrong_set_shader",
    ], expect_success=False)
    require_contains(result.stdout, "sampled texture binding does not match SDL's slot convention", "MSAA sampled textures outside SDL's sampled set should reject")

    mixed_paired_and_samplerless_layout_facts_c = out_dir / "mixed_paired_and_samplerless_layout_facts.c"
    result = run([
        shadercross,
        str(mixed_paired_and_samplerless_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(mixed_paired_and_samplerless_layout_facts_c),
        "--resource-layout-symbol-prefix", "mixed_paired_and_samplerless_shader",
    ])
    generated = mixed_paired_and_samplerless_layout_facts_c.read_text(encoding="utf-8")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_FILTERABLE_FLOAT", "SDL_GPU_SHADERSAMPLERTYPE_FILTERING"),
            ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_UINT", "SDL_GPU_SHADERSAMPLERTYPE_NONE"),
        ],
        "mixed paired and samplerless sampled images should emit both paired and exact-load layout facts",
    )
    require_contains(result.stdout, "resource layout note: sampled texture slot 0 resolved ambiguous float sampler policy", "mixed paired and samplerless sampled images should log paired policy resolution")
    compile_layout_facts_source(mixed_paired_and_samplerless_layout_facts_c, "mixed_paired_and_samplerless_layout_facts")

    normal_depth_layout_facts_c = out_dir / "normal_depth_layout_facts.c"
    run([
        shadercross,
        str(normal_depth_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(normal_depth_layout_facts_c),
        "--resource-layout-symbol-prefix", "normal_depth_shader",
    ])
    generated = normal_depth_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription normal_depth_shader_sampled_texture_slots[]", "normal depth resource layout C output should emit sampled texture layout_facts")
    require_contains(generated, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH", "normal depth layout facts should preserve depth sample type")
    require_contains(generated, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING", "normal depth layout facts should use non-filtering sampler layout_facts")
    require_not_contains(generated, "SDL_GPU_SHADERSAMPLERTYPE_COMPARISON", "normal depth sampling should not be upgraded to comparison layout_facts")
    compile_layout_facts_source(normal_depth_layout_facts_c, "normal_depth_layout_facts")

    depth_dref_gather_layout_facts_c = out_dir / "depth_dref_gather_layout_facts.c"
    run([
        shadercross,
        str(depth_dref_gather_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(depth_dref_gather_layout_facts_c),
        "--resource-layout-symbol-prefix", "depth_gather_shader",
    ])
    generated = depth_dref_gather_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, ".sample_type = SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH", "depth gather layout facts should preserve depth sample type")
    require_contains(generated, ".sampler_type = SDL_GPU_SHADERSAMPLERTYPE_COMPARISON", "Dref gather layout facts should require a comparison sampler")
    require_not_contains(generated, "SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING", "Dref gather sampling should not be downgraded to non-filtering layout_facts")
    compile_layout_facts_source(depth_dref_gather_layout_facts_c, "depth_dref_gather_layout_facts")

    depth_cube_dref_layout_facts_c = out_dir / "depth_cube_dref_layout_facts.c"
    run([
        shadercross,
        str(depth_cube_dref_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(depth_cube_dref_layout_facts_c),
        "--resource-layout-symbol-prefix", "depth_cube_dref_shader",
    ])
    generated = depth_cube_dref_layout_facts_c.read_text(encoding="utf-8")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_CUBE", "SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH", "SDL_GPU_SHADERSAMPLERTYPE_COMPARISON"),
        ],
        "depth cube Dref layout facts should preserve cube depth comparison facts",
    )
    compile_layout_facts_source(depth_cube_dref_layout_facts_c, "depth_cube_dref_layout_facts")

    depth_cube_dref_layout_c = out_dir / "depth_cube_dref_layout.c"
    run([
        shadercross,
        str(depth_cube_dref_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(depth_cube_dref_layout_c),
        "--resource-layout-symbol-prefix", "depth_cube_dref_layout",
    ])
    generated = depth_cube_dref_layout_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUShaderResourceLayout depth_cube_dref_layout_resource_layout", "depth cube layout C output should emit shader layout facts")
    require_contains(generated, ".sampled_texture_slots = depth_cube_dref_layout_sampled_texture_slots", "depth cube layout C output should reference generated sampled texture layout facts")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_CUBE", "SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH", "SDL_GPU_SHADERSAMPLERTYPE_COMPARISON"),
        ],
        "depth cube layout layout facts should preserve cube depth comparison facts",
    )
    compile_layout_facts_source(depth_cube_dref_layout_c, "depth_cube_dref_layout")

    depth_cube_array_layout_facts_c = out_dir / "depth_cube_array_layout_facts.c"
    run([
        shadercross,
        str(depth_cube_array_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(depth_cube_array_layout_facts_c),
        "--resource-layout-symbol-prefix", "depth_cube_array_shader",
    ])
    generated = depth_cube_array_layout_facts_c.read_text(encoding="utf-8")
    require_sampled_texture_initializers(
        generated,
        [
            ("SDL_GPU_TEXTURETYPE_CUBE_ARRAY", "SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH", "SDL_GPU_SHADERSAMPLERTYPE_NONFILTERING"),
        ],
        "depth cube-array layout facts should preserve non-filtering depth facts",
    )
    compile_layout_facts_source(depth_cube_array_layout_facts_c, "depth_cube_array_layout_facts")

    result = run([
        shadercross,
        str(uint_sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "uint_sampled_layout_facts.c"),
        "--resource-layout-symbol-prefix", "uint_sampled_shader",
    ], expect_success=False)
    require_contains(result.stdout, "sampled texture sample kind and sampler binding kind", "integer sampled texture layout facts should be rejected clearly in the current matrix")

    result = run([
        shadercross,
        str(uint_sampled_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "uint_sampled_policy_mismatch.c"),
        "--resource-layout-symbol-prefix", "uint_sampled_policy_mismatch",
        "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ], expect_success=False)
    require_contains(result.stdout, "explicit sampled-slot policy does not match the reflected sampled texture evidence", "sampled slot policy should reject integer evidence when float layout facts is requested")

    result = run([
        shadercross,
        str(wrong_set_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "wrong_set_layout_facts.c"),
        "--resource-layout-symbol-prefix", "wrong_set_shader",
    ], expect_success=False)
    require_contains(result.stdout, "sampled texture binding does not match SDL's slot convention", "sampled texture/sampler pairs outside the SDL sampled-texture set should be rejected")

    wrong_set_json = out_dir / "wrong-set.json"
    run([
        shadercross,
        str(wrong_set_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(wrong_set_json),
    ])
    wrong_set_json_data = json.loads(wrong_set_json.read_text(encoding="utf-8"))
    wrong_set_layout_entries = wrong_set_json_data["sdl_resource_layout"]["sampled_textures"]
    wrong_set_unmapped = [
        entry for entry in wrong_set_layout_entries
        if entry["image_set"] == 3 and entry["sampler_set"] == 3
    ]
    wrong_set_mapped = [
        entry for entry in wrong_set_layout_entries
        if entry["image_set"] == 2 and entry["sampler_set"] == 2
    ]
    if len(wrong_set_mapped) != 1 or wrong_set_mapped[0]["stage"] != "fragment" or wrong_set_mapped[0]["slot"] != 1 or wrong_set_mapped[0]["derivation"] != "sdl_convention":
        raise AssertionError(f"JSON sampled layout should keep the valid descriptor set 2 pair mapped to fragment slot 1\n{wrong_set_json.read_text(encoding='utf-8')}")
    if len(wrong_set_unmapped) != 1 or wrong_set_unmapped[0]["stage"] is not None or wrong_set_unmapped[0]["slot"] is not None or wrong_set_unmapped[0]["derivation"] != "unmapped":
        raise AssertionError(f"JSON sampled layout should not mark descriptor set 3 as an SDL sampled slot\n{wrong_set_json.read_text(encoding='utf-8')}")

    multi_entrypoint_json = out_dir / "multi-entrypoint-alt.json"
    run([
        shadercross,
        str(multi_entrypoint_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-e", "alt",
        "-d", "JSON",
        "-o", str(multi_entrypoint_json),
    ])
    multi_entrypoint_json_text = multi_entrypoint_json.read_text(encoding="utf-8")
    multi_entrypoint_json_data = json.loads(multi_entrypoint_json_text)
    if multi_entrypoint_json_data["outputs"] != [{"name": "", "type": "float4", "location": 0}]:
        raise AssertionError(f"JSON selected-entrypoint graphics summary should preserve stage output layout_facts\n{multi_entrypoint_json_text}")
    multi_entrypoint_layout_entries = multi_entrypoint_json_data["sdl_resource_layout"]["sampled_textures"]
    if multi_entrypoint_json_data["samplers"] != 3:
        raise AssertionError(f"JSON summary sampler count should describe the requested entrypoint's SDL slots\n{multi_entrypoint_json_text}")
    if len(multi_entrypoint_layout_entries) != 2:
        raise AssertionError(f"JSON detailed reflection should use the requested entrypoint's active resources\n{multi_entrypoint_json_text}")
    multi_entrypoint_pairs = {
        (entry["image_name"], entry["sampler_name"], entry["slot"])
        for entry in multi_entrypoint_layout_entries
    }
    if multi_entrypoint_pairs != {
        ("AltTexture", "AltSampler", 1),
        ("AltExtraTexture", "AltExtraSampler", 2),
    }:
        raise AssertionError(f"JSON detailed reflection should describe the requested entrypoint's sampled pair\n{multi_entrypoint_json_text}")
    require_not_contains(multi_entrypoint_json_text, "MainTexture", "JSON detailed reflection should not include resources used only by another entrypoint")

    shared_depth_dref_json = out_dir / "multi-entrypoint-shared-depth-dref-main.json"
    run([
        shadercross,
        str(multi_entrypoint_shared_depth_dref_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-e", "main",
        "-d", "JSON",
        "-o", str(shared_depth_dref_json),
    ])
    shared_depth_dref_json_text = shared_depth_dref_json.read_text(encoding="utf-8")
    shared_depth_dref_json_data = json.loads(shared_depth_dref_json_text)
    shared_depth_dref_pairs = shared_depth_dref_json_data["resource_details"]["sampled_texture_sampler_pairs"]
    if len(shared_depth_dref_pairs) != 1:
        raise AssertionError(f"JSON sampled-pair details should describe the selected shared-resource entrypoint once\n{shared_depth_dref_json_text}")
    if shared_depth_dref_pairs[0]["uses_dref_sample_opcode"] is not False:
        raise AssertionError(f"Selected-entrypoint sampled-pair details should not inherit inactive-entrypoint Dref use\n{shared_depth_dref_json_text}")
    shared_depth_dref_layout = shared_depth_dref_json_data["sdl_resource_layout"]["sampled_textures"]
    if len(shared_depth_dref_layout) != 1 or shared_depth_dref_layout[0]["uses_dref_sample_opcode"] is not False:
        raise AssertionError(f"Selected-entrypoint SDL layout should not inherit inactive-entrypoint Dref use\n{shared_depth_dref_json_text}")

    helper_parameter_json = out_dir / "helper-parameter-sampled-pair.json"
    run([
        shadercross,
        str(helper_parameter_sampled_pair_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(helper_parameter_json),
    ])
    helper_parameter_json_text = helper_parameter_json.read_text(encoding="utf-8")
    helper_parameter_json_data = json.loads(helper_parameter_json_text)
    helper_parameter_pairs = helper_parameter_json_data["resource_details"]["sampled_texture_sampler_pairs"]
    helper_parameter_layout = helper_parameter_json_data["sdl_resource_layout"]["sampled_textures"]
    if len(helper_parameter_pairs) != 1 or helper_parameter_pairs[0]["pair_derivation"] != "spirv_cross_combined_image_sampler":
        raise AssertionError(f"JSON helper-parameter sampled-pair details should preserve SPIRV-Cross recovered pair evidence\n{helper_parameter_json_text}")
    if len(helper_parameter_layout) != 1 or helper_parameter_layout[0]["image_name"] != "HelperTexture" or helper_parameter_layout[0]["sampler_name"] != "HelperSampler":
        raise AssertionError(f"JSON helper-parameter sampled-pair layout should describe the global resource pair\n{helper_parameter_json_text}")
    require_not_contains(helper_parameter_json_text, "spvc_compiler_build_combined_image_samplers failed", "JSON helper-parameter sampled-pair diagnostics should not expose a raw SPIRV-Cross failure")

    helper_parameter_layout_facts_c = out_dir / "helper_parameter_sampled_pair_layout_facts.c"
    run([
        shadercross,
        str(helper_parameter_sampled_pair_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(helper_parameter_layout_facts_c),
        "--resource-layout-symbol-prefix", "helper_parameter_sampled_pair",
    ])
    helper_parameter_layout_facts_text = helper_parameter_layout_facts_c.read_text(encoding="utf-8")
    require_contains(helper_parameter_layout_facts_text, ".num_samplers = 1,", "resource layout C helper-parameter sampled-pair output should preserve the sampled pair count")
    compile_layout_facts_source(helper_parameter_layout_facts_c, "helper_parameter_sampled_pair_layout_facts")

    readwrite_r32_uint_storage_layout_facts_c = out_dir / "readwrite_r32_uint_storage_layout_facts.c"
    run([
        shadercross,
        str(readwrite_r32_uint_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readwrite_r32_uint_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readwrite_r32_uint_storage_pipeline",
    ])
    generated = readwrite_r32_uint_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readwrite_r32_uint_storage_pipeline_readwrite_storage_textures[]", "read-write R32_UINT storage resource layout C output should emit storage texture layout_facts")
    require_contains(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D", "read-write R32_UINT storage layout facts should preserve 2D texture type")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_UINT", "read-write R32_UINT storage layout facts should preserve R32_UINT format")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "read-write R32_UINT storage layout facts should preserve read-write access")
    compile_layout_facts_source(readwrite_r32_uint_storage_layout_facts_c, "readwrite_r32_uint_storage_layout_facts")

    readwrite_r32_uint_storage_3d_layout_facts_c = out_dir / "readwrite_r32_uint_storage_3d_layout_facts.c"
    run([
        shadercross,
        str(readwrite_r32_uint_storage_3d_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readwrite_r32_uint_storage_3d_layout_facts_c),
        "--resource-layout-symbol-prefix", "readwrite_r32_uint_storage_3d_pipeline",
    ])
    generated = readwrite_r32_uint_storage_3d_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readwrite_r32_uint_storage_3d_pipeline_readwrite_storage_textures[]", "read-write 3D R32_UINT storage resource layout C output should emit storage texture layout_facts")
    require_contains(generated, ".texture_type = SDL_GPU_TEXTURETYPE_3D", "read-write 3D R32_UINT storage layout facts should preserve 3D texture type")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_UINT", "read-write 3D R32_UINT storage layout facts should preserve R32_UINT format")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "read-write 3D R32_UINT storage layout facts should preserve read-write access")
    compile_layout_facts_source(readwrite_r32_uint_storage_3d_layout_facts_c, "readwrite_r32_uint_storage_3d_layout_facts")

    readwrite_r32_uint_storage_2d_array_layout_facts_c = out_dir / "readwrite_r32_uint_storage_2d_array_layout_facts.c"
    if readwrite_r32_uint_storage_2d_array_layout_facts_c.exists():
        readwrite_r32_uint_storage_2d_array_layout_facts_c.unlink()
    result = run([
        shadercross,
        str(readwrite_r32_uint_storage_2d_array_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readwrite_r32_uint_storage_2d_array_layout_facts_c),
        "--resource-layout-symbol-prefix", "readwrite_r32_uint_storage_2d_array_pipeline",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture dimension is outside the current SDL_GPU resource layout fact matrix", "2D-array compute read-write storage resource layout C output should reject clearly")
    if readwrite_r32_uint_storage_2d_array_layout_facts_c.exists():
        raise AssertionError("unsupported 2D-array compute read-write storage resource layout should not create partial C output")

    result = run([
        shadercross,
        str(writeonly_storage_texture_resource_array_compute_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(out_dir / "writeonly_storage_texture_resource_array_compute_layout_facts.c"),
        "--resource-layout-symbol-prefix", "writeonly_storage_texture_resource_array_compute",
    ], expect_success=False)
    require_contains(result.stdout, "shader-visible writable storage texture arrays are not supported", "compute resource layout C output should reject writable storage texture resource arrays clearly")

    writable_r32_scalar_storage_3d_layout_facts_c = out_dir / "writable_r32_scalar_storage_3d_layout_facts.c"
    run([
        shadercross,
        str(writable_r32_scalar_storage_3d_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(writable_r32_scalar_storage_3d_layout_facts_c),
        "--resource-layout-symbol-prefix", "writable_r32_scalar_storage_3d_pipeline",
    ])
    generated = writable_r32_scalar_storage_3d_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription writable_r32_scalar_storage_3d_pipeline_readwrite_storage_textures[]", "writable 3D R32 scalar storage resource layout C output should emit storage texture layout_facts")
    require_count(generated, ".texture_type = SDL_GPU_TEXTURETYPE_3D", 6, "writable 3D R32 scalar storage layout facts should preserve 3D texture type for every slot")
    require_count(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_UINT", 2, "writable 3D R32 scalar storage layout facts should preserve both R32_UINT access modes")
    require_count(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_INT", 2, "writable 3D R32 scalar storage layout facts should preserve both R32_INT access modes")
    require_count(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT", 2, "writable 3D R32 scalar storage layout facts should preserve both R32_FLOAT access modes")
    require_count(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", 3, "writable 3D R32 scalar storage layout facts should preserve write-only access for every format")
    require_count(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", 3, "writable 3D R32 scalar storage layout facts should preserve read-write access for every format")
    require_storage_texture_initializer(generated, "SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32_UINT", "SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", "writable 3D R32 scalar storage layout facts should preserve R32_UINT write-only as one slot")
    require_storage_texture_initializer(generated, "SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32_UINT", "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "writable 3D R32 scalar storage layout facts should preserve R32_UINT read-write as one slot")
    require_storage_texture_initializer(generated, "SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32_INT", "SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", "writable 3D R32 scalar storage layout facts should preserve R32_INT write-only as one slot")
    require_storage_texture_initializer(generated, "SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32_INT", "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "writable 3D R32 scalar storage layout facts should preserve R32_INT read-write as one slot")
    require_storage_texture_initializer(generated, "SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32_FLOAT", "SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", "writable 3D R32 scalar storage layout facts should preserve R32_FLOAT write-only as one slot")
    require_storage_texture_initializer(generated, "SDL_GPU_TEXTURETYPE_3D", "SDL_GPU_TEXTUREFORMAT_R32_FLOAT", "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "writable 3D R32 scalar storage layout facts should preserve R32_FLOAT read-write as one slot")
    compile_layout_facts_source(writable_r32_scalar_storage_3d_layout_facts_c, "writable_r32_scalar_storage_3d_layout_facts")

    writable_broad_storage_layout_facts_c = out_dir / "writable_broad_storage_layout_facts.c"
    run([
        shadercross,
        str(writable_broad_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(writable_broad_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "writable_broad_storage_pipeline",
    ])
    generated = writable_broad_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription writable_broad_storage_pipeline_readwrite_storage_textures[]", "broad write-only storage resource layout C output should emit storage texture layout_facts")
    require_count(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D", 8, "broad write-only storage layout facts should preserve 2D type for every slot")
    require_count(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", 8, "broad storage layout facts should preserve write-only access for every slot")
    require_not_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "broad storage layout facts fixture should not broaden read-write access")
    for storage_format in [
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT",
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT",
    ]:
        require_storage_texture_initializer(
            generated,
            "SDL_GPU_TEXTURETYPE_2D",
            storage_format,
            "SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY",
            f"broad write-only storage layout facts should preserve {storage_format}",
        )
    compile_layout_facts_source(writable_broad_storage_layout_facts_c, "writable_broad_storage_layout_facts")

    writable_broad_storage_json = out_dir / "writable_broad_storage.json"
    run([
        shadercross,
        str(writable_broad_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "-d", "JSON",
        "-o", str(writable_broad_storage_json),
    ])
    writable_broad_json_text = writable_broad_storage_json.read_text(encoding="utf-8")
    require_contains(writable_broad_json_text, '"storage_format": "rg32f"', "broad storage JSON output should preserve raw Rg32f storage format")
    require_contains(writable_broad_json_text, '"mapped_webgpu_format": "rg32float"', "broad storage JSON output should map Rg32f to WebGPU rg32float")
    require_contains(writable_broad_json_text, '"mapped_sdl_format": "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT"', "broad storage JSON output should map Rg32f to SDL R32G32_FLOAT")

    readwrite_broad_storage_layout_facts_c = out_dir / "readwrite_broad_storage_layout_facts.c"
    run([
        shadercross,
        str(readwrite_broad_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readwrite_broad_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readwrite_broad_storage_pipeline",
    ])
    generated = readwrite_broad_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readwrite_broad_storage_pipeline_readwrite_storage_textures[]", "broad read-write storage resource layout C output should emit storage texture layout_facts")
    require_count(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D", 6, "broad read-write storage layout facts should preserve 2D type for every slot")
    require_count(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", 6, "broad read-write storage layout facts should preserve read-write access for every slot")
    require_not_contains(generated, "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM", "broad read-write storage layout facts should not include SNORM format")
    require_not_contains(generated, "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT", "broad read-write storage layout facts should not include RG32 float format")
    for storage_format in [
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT",
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT",
    ]:
        require_storage_texture_initializer(
            generated,
            "SDL_GPU_TEXTURETYPE_2D",
            storage_format,
            "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE",
            f"broad read-write storage layout facts should preserve {storage_format}",
        )
    compile_layout_facts_source(readwrite_broad_storage_layout_facts_c, "readwrite_broad_storage_layout_facts")

    readwrite_rg32_float_storage_layout_facts_c = out_dir / "readwrite_rg32_float_storage_layout_facts.c"
    result = run([
        shadercross,
        str(readwrite_rg32_float_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readwrite_rg32_float_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readwrite_rg32_float_storage_pipeline",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture format is outside the current SDL_GPU resource layout fact matrix", "RG32 float read-write storage layout facts should reject clearly")
    if readwrite_rg32_float_storage_layout_facts_c.exists():
        raise AssertionError("RG32 float read-write storage layout facts should not create a partial resource layout C output")

    readonly_broad_storage_layout_facts_c = out_dir / "readonly_broad_storage_layout_facts.c"
    run([
        shadercross,
        str(readonly_broad_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readonly_broad_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readonly_broad_storage_pipeline",
    ])
    generated = readonly_broad_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readonly_broad_storage_pipeline_readonly_storage_textures[]", "broad read-only storage resource layout C output should emit read-only storage texture layout_facts")
    require_count(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D", 8, "broad read-only storage layout facts should preserve 2D type for every slot")
    require_count(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY", 8, "broad read-only storage layout facts should preserve read-only access for every slot")
    require_not_contains(generated, "readonly_broad_storage_pipeline_readwrite_storage_textures", "broad read-only storage layout facts fixture should not emit read-write texture layout_facts")
    for storage_format in [
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT",
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT",
        "SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT",
        "SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT",
    ]:
        require_storage_texture_initializer(
            generated,
            "SDL_GPU_TEXTURETYPE_2D",
            storage_format,
            "SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY",
            f"broad read-only storage layout facts should preserve {storage_format}",
        )
    compile_layout_facts_source(readonly_broad_storage_layout_facts_c, "readonly_broad_storage_layout_facts")

    readonly_broad_storage_json = out_dir / "readonly_broad_storage.json"
    run([
        shadercross,
        str(readonly_broad_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "-d", "JSON",
        "-o", str(readonly_broad_storage_json),
    ])
    readonly_broad_json_text = readonly_broad_storage_json.read_text(encoding="utf-8")
    require_contains(readonly_broad_json_text, '"resource_class": "readonly_storage_texture"', "broad read-only storage JSON output should classify set 0 as read-only storage textures")
    require_contains(readonly_broad_json_text, '"storage_format": "rg32f"', "broad read-only storage JSON output should preserve raw Rg32f storage format")
    require_contains(readonly_broad_json_text, '"mapped_sdl_format": "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT"', "broad read-only storage JSON output should map Rg32f to SDL R32G32_FLOAT")

    readonly_broad_storage_vertex_layout_facts_c = out_dir / "readonly_broad_storage_vertex_layout_facts.c"
    run([
        shadercross,
        str(readonly_broad_storage_vertex_spv),
        "-s", "SPIRV",
        "-t", "vertex",
        "--resource-layout-c", str(readonly_broad_storage_vertex_layout_facts_c),
        "--resource-layout-symbol-prefix", "readonly_broad_storage_vertex_shader",
    ])
    generated = readonly_broad_storage_vertex_layout_facts_c.read_text(encoding="utf-8")
    require_broad_readonly_graphics_storage_layout_facts(generated, "readonly_broad_storage_vertex_shader", "broad vertex read-only storage layout_facts")
    compile_layout_facts_source(readonly_broad_storage_vertex_layout_facts_c, "readonly_broad_storage_vertex_layout_facts")

    readonly_broad_storage_fragment_layout_facts_c = out_dir / "readonly_broad_storage_fragment_layout_facts.c"
    run([
        shadercross,
        str(readonly_broad_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(readonly_broad_storage_fragment_layout_facts_c),
        "--resource-layout-symbol-prefix", "readonly_broad_storage_fragment_shader",
    ])
    generated = readonly_broad_storage_fragment_layout_facts_c.read_text(encoding="utf-8")
    require_broad_readonly_graphics_storage_layout_facts(generated, "readonly_broad_storage_fragment_shader", "broad fragment read-only storage layout_facts")
    compile_layout_facts_source(readonly_broad_storage_fragment_layout_facts_c, "readonly_broad_storage_fragment_layout_facts")

    readonly_broad_storage_fragment_json = out_dir / "readonly_broad_storage_fragment.json"
    run([
        shadercross,
        str(readonly_broad_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(readonly_broad_storage_fragment_json),
    ])
    readonly_broad_fragment_json_text = readonly_broad_storage_fragment_json.read_text(encoding="utf-8")
    require_contains(readonly_broad_fragment_json_text, '"resource_class": "storage_texture"', "broad graphics read-only storage JSON output should classify set 2 as shader storage textures")
    require_contains(readonly_broad_fragment_json_text, '"storage_format": "rg32f"', "broad graphics read-only storage JSON output should preserve raw Rg32f storage format")
    require_contains(readonly_broad_fragment_json_text, '"mapped_sdl_format": "SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT"', "broad graphics read-only storage JSON output should map Rg32f to SDL R32G32_FLOAT")

    writeonly_graphics_storage_layout_facts_c = out_dir / "writeonly_graphics_storage_layout_facts.c"
    result = run([
        shadercross,
        str(writeonly_graphics_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(writeonly_graphics_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "writeonly_graphics_storage_shader",
    ], expect_success=False)
    require_contains(result.stdout, "fragment writable storage layout C output is not supported", "fragment writable storage layout should reject clearly without SDL layout support")
    if writeonly_graphics_storage_layout_facts_c.exists():
        raise AssertionError("unsupported fragment writable storage layout should not create partial resource layout C output")

    fragment_writable_texture_buffer_layout_facts_c = out_dir / "fragment_writable_texture_buffer_layout_facts.c"
    result = run([
        shadercross,
        str(fragment_writable_texture_writable_buffer_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(fragment_writable_texture_buffer_layout_facts_c),
        "--resource-layout-symbol-prefix", "fragment_writable_texture_buffer_shader",
    ], expect_success=False)
    require_contains(result.stdout, "fragment writable storage layout C output is not supported", "combined fragment writable texture/buffer layout should reject clearly without SDL layout support")
    if fragment_writable_texture_buffer_layout_facts_c.exists():
        raise AssertionError("unsupported combined fragment writable storage layout should not create partial resource layout C output")

    result = run([
        shadercross,
        str(fragment_writable_texture_writable_buffer_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--suggest-resource-layout-policy",
    ])
    require_contains(result.stdout, "# No resource layout policy suggestions.", "combined fragment writable texture/buffer policy mode should not fail or suggest unsupported policies")

    writeonly_graphics_storage_json = out_dir / "writeonly_graphics_storage.json"
    run([
        shadercross,
        str(writeonly_graphics_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(writeonly_graphics_storage_json),
    ])
    writeonly_graphics_storage_json_text = writeonly_graphics_storage_json.read_text(encoding="utf-8")
    writeonly_graphics_storage_layout_json = json.loads(writeonly_graphics_storage_json_text)
    if writeonly_graphics_storage_layout_json["storage_textures"] != 0:
        raise AssertionError(f"fragment writable storage texture JSON should not count unsupported writes as SDL layout storage textures: {writeonly_graphics_storage_layout_json}")
    storage_textures = writeonly_graphics_storage_layout_json["sdl_resource_layout"]["storage_textures"]
    if len(storage_textures) != 1:
        raise AssertionError(f"fragment writable storage texture JSON should still expose one diagnostic storage texture entry, got {len(storage_textures)}")
    storage_texture = storage_textures[0]
    if (storage_texture["resource_class"] != "storage_texture" or
            "fragment_storage_texture_writes" not in storage_texture["unsupported_features"] or
            storage_texture["derivation"] != "unmapped"):
        raise AssertionError(f"fragment writable storage texture JSON should expose unsupported regular storage texture evidence: {storage_texture}")

    fragment_writable_storage_buffer_json = out_dir / "fragment_writable_storage_buffer.json"
    run([
        shadercross,
        str(fragment_writable_storage_buffer_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(fragment_writable_storage_buffer_json),
    ])
    fragment_writable_storage_buffer_json_text = fragment_writable_storage_buffer_json.read_text(encoding="utf-8")
    fragment_writable_storage_buffer_layout_json = json.loads(fragment_writable_storage_buffer_json_text)
    if fragment_writable_storage_buffer_layout_json["storage_buffers"] != 0:
        raise AssertionError(f"fragment writable storage buffer JSON should not count unsupported writes as SDL layout storage buffers: {fragment_writable_storage_buffer_layout_json}")
    storage_buffers = fragment_writable_storage_buffer_layout_json["sdl_resource_layout"]["storage_buffers"]
    if len(storage_buffers) != 1:
        raise AssertionError(f"fragment writable storage buffer JSON should still expose one diagnostic storage buffer entry, got {len(storage_buffers)}")
    storage_buffer = storage_buffers[0]
    if (storage_buffer["resource_class"] != "storage_buffer" or
            "fragment_storage_buffer_writes" not in storage_buffer["unsupported_features"] or
            storage_buffer["derivation"] != "unmapped"):
        raise AssertionError(f"fragment writable storage buffer JSON should expose unsupported regular storage buffer evidence: {storage_buffer}")

    result = run([
        shadercross,
        str(writeonly_graphics_storage_vertex_spv),
        "-s", "SPIRV",
        "-t", "vertex",
        "--resource-layout-c", str(out_dir / "writeonly_graphics_storage_vertex_layout.c"),
        "--resource-layout-symbol-prefix", "writeonly_graphics_storage_vertex_layout",
    ], expect_success=False)
    require_contains(result.stdout, "vertex-stage writable storage textures are not supported", "layout C output should reject vertex-stage writable storage textures clearly")

    result = run([
        shadercross,
        str(writeonly_wrong_set_graphics_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "writeonly_wrong_set_graphics_storage_layout.c"),
        "--resource-layout-symbol-prefix", "writeonly_wrong_set_graphics_storage_layout",
    ], expect_success=False)
    require_contains(result.stdout, "fragment writable storage texture binding does not match SDL's resource layout slot convention", "layout C output should reject fragment writable storage textures outside SDL's resource layout convention")

    result = run([
        shadercross,
        str(writeonly_sparse_graphics_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "writeonly_sparse_graphics_storage_layout.c"),
        "--resource-layout-symbol-prefix", "writeonly_sparse_graphics_storage_layout",
    ], expect_success=False)
    require_contains(result.stdout, "fragment writable storage texture binding does not match SDL's resource layout slot convention", "layout C output should reject sparse fragment writable storage texture slots clearly")

    result = run([
        shadercross,
        str(vertex_writable_storage_buffer_spv),
        "-s", "SPIRV",
        "-t", "vertex",
        "--resource-layout-c", str(out_dir / "vertex_writable_storage_buffer_layout.c"),
        "--resource-layout-symbol-prefix", "vertex_writable_storage_buffer_layout",
    ], expect_success=False)
    require_contains(result.stdout, "vertex-stage writable storage buffers are not supported", "layout C output should reject vertex-stage writable storage buffers clearly")

    result = run([
        shadercross,
        str(fragment_writable_storage_buffer_array_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "fragment_writable_storage_buffer_array_layout.c"),
        "--resource-layout-symbol-prefix", "fragment_writable_storage_buffer_array_layout",
    ], expect_success=False)
    require_contains(result.stdout, "shader-visible writable storage buffer arrays are not supported", "layout C output should reject fragment writable storage buffer arrays clearly")

    result = run([
        shadercross,
        str(fragment_atomic_storage_buffer_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "fragment_atomic_storage_buffer_layout.c"),
        "--resource-layout-symbol-prefix", "fragment_atomic_storage_buffer_layout",
    ], expect_success=False)
    require_contains(result.stdout, "fragment storage buffer atomics are not supported", "layout C output should reject fragment storage buffer atomics clearly")

    fragment_atomic_storage_buffer_json = out_dir / "fragment_atomic_storage_buffer_layout.json"
    run([
        shadercross,
        str(fragment_atomic_storage_buffer_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "-d", "JSON",
        "-o", str(fragment_atomic_storage_buffer_json),
    ])
    layout_json_text = fragment_atomic_storage_buffer_json.read_text(encoding="utf-8")
    layout_json = json.loads(layout_json_text)
    if layout_json["storage_buffers"] != 1:
        raise AssertionError(f"fragment storage-buffer atomics JSON should keep atomics in the regular storage buffer count: {layout_json}")
    storage_buffers = layout_json["sdl_resource_layout"]["storage_buffers"]
    if len(storage_buffers) != 1:
        raise AssertionError(f"fragment storage-buffer atomics fixture should reflect one storage buffer, got {len(storage_buffers)}")
    storage_buffer = storage_buffers[0]
    if storage_buffer["resource_class"] != "storage_buffer" or "fragment_storage_buffer_atomics" not in storage_buffer["unsupported_features"]:
        raise AssertionError(f"fragment storage-buffer atomics JSON should keep the buffer regular and name the unsupported feature: {storage_buffer}")

    result = run([
        shadercross,
        str(writeonly_storage_texture_resource_array_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(out_dir / "writeonly_storage_texture_resource_array_layout.c"),
        "--resource-layout-symbol-prefix", "writeonly_storage_texture_resource_array_layout",
    ], expect_success=False)
    require_contains(result.stdout, "shader-visible writable storage texture arrays are not supported", "layout C output should reject fragment writable storage texture resource arrays clearly")

    unsupported_graphics_storage_layout_facts_c = out_dir / "unsupported_graphics_storage_layout_facts.c"
    result = run([
        shadercross,
        str(unsupported_graphics_storage_fragment_spv),
        "-s", "SPIRV",
        "-t", "fragment",
        "--resource-layout-c", str(unsupported_graphics_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "unsupported_graphics_storage_shader",
    ], expect_success=False)
    require_contains(result.stdout, "storage texture format is outside the current SDL_GPU resource layout fact matrix", "unsupported graphics storage format layout facts should reject clearly")
    if unsupported_graphics_storage_layout_facts_c.exists():
        raise AssertionError("unsupported graphics storage layout facts should not create a partial resource layout C output")

    readwrite_r32_scalar_storage_layout_facts_c = out_dir / "readwrite_r32_scalar_storage_layout_facts.c"
    run([
        shadercross,
        str(readwrite_r32_scalar_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readwrite_r32_scalar_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readwrite_r32_scalar_storage_pipeline",
    ])
    generated = readwrite_r32_scalar_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readwrite_r32_scalar_storage_pipeline_readwrite_storage_textures[]", "writable R32 scalar storage resource layout C output should emit storage texture layout_facts")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_INT", "write-only R32 scalar storage layout facts should preserve R32_INT format")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT", "read-write R32 scalar storage layout facts should preserve R32_FLOAT format")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY", "write-only R32 scalar storage layout facts should preserve write-only access")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE", "read-write R32 scalar storage layout facts should preserve read-write access")
    compile_layout_facts_source(readwrite_r32_scalar_storage_layout_facts_c, "readwrite_r32_scalar_storage_layout_facts")

    readonly_r32_uint_storage_layout_facts_c = out_dir / "readonly_r32_uint_storage_layout_facts.c"
    run([
        shadercross,
        str(readonly_r32_uint_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readonly_r32_uint_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readonly_r32_uint_storage_pipeline",
    ])
    generated = readonly_r32_uint_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readonly_r32_uint_storage_pipeline_readonly_storage_textures[]", "read-only R32_UINT storage resource layout C output should emit storage texture layout_facts")
    require_contains(generated, ".texture_type = SDL_GPU_TEXTURETYPE_3D", "read-only R32_UINT storage layout facts should preserve 3D texture type")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_UINT", "read-only R32_UINT storage layout facts should preserve R32_UINT format")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY", "read-only R32_UINT storage layout facts should preserve read-only access")
    compile_layout_facts_source(readonly_r32_uint_storage_layout_facts_c, "readonly_r32_uint_storage_layout_facts")

    readonly_r32_scalar_storage_layout_facts_c = out_dir / "readonly_r32_scalar_storage_layout_facts.c"
    run([
        shadercross,
        str(readonly_r32_scalar_storage_spv),
        "-s", "SPIRV",
        "-t", "compute",
        "--resource-layout-c", str(readonly_r32_scalar_storage_layout_facts_c),
        "--resource-layout-symbol-prefix", "readonly_r32_scalar_storage_pipeline",
    ])
    generated = readonly_r32_scalar_storage_layout_facts_c.read_text(encoding="utf-8")
    require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription readonly_r32_scalar_storage_pipeline_readonly_storage_textures[]", "read-only R32 scalar storage resource layout C output should emit storage texture layout_facts")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_INT", "read-only R32 scalar storage layout facts should preserve R32_INT format")
    require_contains(generated, ".format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT", "read-only R32 scalar storage layout facts should preserve R32_FLOAT format")
    require_contains(generated, ".access = SDL_GPU_STORAGETEXTUREACCESS_READ_ONLY", "read-only R32 scalar storage layout facts should preserve read-only access")
    compile_layout_facts_source(readonly_r32_scalar_storage_layout_facts_c, "readonly_r32_scalar_storage_layout_facts")

    result = run([
        shadercross,
        str(sampled_spv),
        "-s", "SPIRV",
        "-t", "vertex",
        "--resource-layout-c", str(out_dir / "wrong_stage_layout_facts.c"),
        "--resource-layout-symbol-prefix", "wrong_stage_shader",
    ], expect_success=False)
    require_contains(result.stdout, "SPIR-V entry point 'main' with requested shader stage is not present", "layout_facts generation should honor the requested shader stage")

    if hlsl_to_spirv_supported(shadercross, out_dir, hlsl_required):
        hlsl_fragment_input = out_dir / "layout_facts_input.frag.hlsl"
        hlsl_fragment_input.write_text(HLSL_FRAGMENT_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_fragment_layout_facts_c = out_dir / "hlsl_fragment_layout_facts.c"
        result = run([
            shadercross,
            str(hlsl_fragment_input),
            "-s", "HLSL",
            "-t", "fragment",
            "--resource-layout-c", str(hlsl_fragment_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_fragment_shader",
        ])
        generated = hlsl_fragment_layout_facts_c.read_text(encoding="utf-8")
        require_contains(generated, "static const SDL_GPUShaderResourceLayout hlsl_fragment_shader_resource_layout", "HLSL fragment resource layout C output should emit shader layout facts")
        require_contains(generated, "note: sampled texture slot 0 resolved ambiguous float sampler policy", "HLSL fragment resource layout C output should document float sampler policy resolution")
        require_not_contains(generated, "static const SDL_GPUSampledTextureSlotDescription hlsl_fragment_shader_sampled_texture_slots[]", "default HLSL sampled texture layout facts should not emit an unnecessary slot array")
        require_contains(result.stdout, "resource layout note: sampled texture slot 0 resolved ambiguous float sampler policy", "HLSL fragment layout facts should log float sampler policy resolution")
        compile_layout_facts_source(hlsl_fragment_layout_facts_c, "hlsl_fragment_layout_facts")

        hlsl_layout_input = out_dir / "layout_facts_buffer_layout.frag.hlsl"
        hlsl_layout_input.write_text(HLSL_BUFFER_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_layout_layout_facts_c = out_dir / "hlsl_buffer_layout_layout_facts.c"
        hlsl_layout_json = out_dir / "hlsl_buffer_layout.json"
        run([
            shadercross,
            str(hlsl_layout_input),
            "-s", "HLSL",
            "-t", "fragment",
            "-d", "JSON",
            "-o", str(hlsl_layout_json),
            "--resource-layout-c", str(hlsl_layout_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_buffer_layout_shader",
        ])
        generated = hlsl_layout_layout_facts_c.read_text(encoding="utf-8")
        layout_json = json.loads(hlsl_layout_json.read_text(encoding="utf-8"))
        uniform_buffers = layout_json["sdl_resource_layout"]["uniform_buffers"]
        if len(uniform_buffers) != 1:
            raise AssertionError(f"HLSL buffer layout fixture should reflect one uniform buffer, got {len(uniform_buffers)}")
        uniform_buffer = uniform_buffers[0]
        members = {member["name"]: member for member in uniform_buffer["members"]}
        if uniform_buffer["size"] <= 0:
            raise AssertionError(f"HLSL buffer layout fixture should reflect a positive uniform buffer size: {uniform_buffer}")
        if not uniform_buffer["fixed_size"] or uniform_buffer["runtime_or_dynamic_array"]:
            raise AssertionError(f"HLSL fixed cbuffer should report a fixed binding size without runtime array facts: {uniform_buffer}")
        require_contains(generated, f"enum {{ hlsl_buffer_layout_shader_uniform_buffer_0_size = {uniform_buffer['size']} }};", "resource layout C output should emit the reflected uniform buffer size constant")
        require_contains(generated, "// buffer layout: uniform_buffer slot 0", "resource layout C output should comment the reflected uniform buffer slot")
        require_contains(generated, "member 0 eye_pos: offset=0 size=12 type=float32 vec=3 columns=1", "resource layout C output should include float3 member offset and size facts")
        require_contains(generated, "the next member is packed at byte 12", "resource layout C output should distinguish scalar-after-float3 packing from tail padding")
        require_contains(generated, "member 2 values:", "resource layout C output should include array member facts")
        require_contains(generated, "array_stride=16", "resource layout C output should include reflected array stride")
        require_contains(generated, "matrix_stride=16", "resource layout C output should include reflected matrix stride")
        if members["eye_pos"]["offset"] != 0 or members["eye_pos"]["size"] != 12:
            raise AssertionError(f"float3 layout facts were not reflected as expected: {members['eye_pos']}")
        if "scalar_or_member_after_float3_is_packed_at_plus12" not in members["eye_pos"]["hazards"]:
            raise AssertionError(f"float3 packed-following-member diagnostic was not emitted: {members['eye_pos']}")
        if members["values"]["array_stride"] != 16:
            raise AssertionError(f"array stride was not reflected as expected: {members['values']}")
        if members["basis"]["matrix_stride"] != 16 or members["basis"]["matrix_major"] not in ("row_major", "column_major"):
            raise AssertionError(f"matrix layout facts were not reflected as expected: {members['basis']}")
        require_contains(generated, f"matrix_major={members['basis']['matrix_major']}", "resource layout C output should include the reflected matrix orientation")
        compile_layout_facts_source(hlsl_layout_layout_facts_c, "hlsl_buffer_layout_layout_facts")

        hlsl_fragment_storage_input = out_dir / "layout_facts_fragment_storage.frag.hlsl"
        hlsl_fragment_storage_input.write_text(HLSL_FRAGMENT_UNANNOTATED_WRITEONLY_STORAGE_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_fragment_storage_json = out_dir / "hlsl_fragment_storage.json"
        run([
            shadercross,
            str(hlsl_fragment_storage_input),
            "-s", "HLSL",
            "-t", "fragment",
            "-d", "JSON",
            "-o", str(hlsl_fragment_storage_json),
        ])
        fragment_storage_json_text = hlsl_fragment_storage_json.read_text(encoding="utf-8")
        fragment_storage_json = json.loads(fragment_storage_json_text)
        if fragment_storage_json["storage_textures"] != 1:
            raise AssertionError(f"HLSL fragment storage JSON should keep ambiguous storage in the regular storage texture count: {fragment_storage_json}")
        hlsl_storage_textures = fragment_storage_json["sdl_resource_layout"]["storage_textures"]
        if len(hlsl_storage_textures) != 1:
            raise AssertionError(f"HLSL fragment storage fixture should reflect one storage texture, got {len(hlsl_storage_textures)}")
        hlsl_storage_texture = hlsl_storage_textures[0]
        if hlsl_storage_texture["resource_class"] != "storage_texture":
            raise AssertionError(f"HLSL fragment storage JSON should keep ambiguous storage in the regular storage texture class: {hlsl_storage_texture}")
        if hlsl_storage_texture["observed_access"] != "write_only":
            raise AssertionError(f"HLSL fragment storage JSON should still report observed writes: {hlsl_storage_texture}")
        if hlsl_storage_texture["final_access"] is not None:
            raise AssertionError(f"HLSL fragment storage JSON should keep ambiguous final access unresolved: {hlsl_storage_texture}")

        hlsl_depth_cube_input = out_dir / "layout_facts_depth_cube.frag.hlsl"
        hlsl_depth_cube_input.write_text(HLSL_DEPTH_CUBE_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_depth_cube_layout_facts_c = out_dir / "hlsl_depth_cube_layout_facts.c"
        run([
            shadercross,
            str(hlsl_depth_cube_input),
            "-s", "HLSL",
            "-t", "fragment",
            "--resource-layout-c", str(hlsl_depth_cube_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_depth_cube_shader",
        ])
        generated = hlsl_depth_cube_layout_facts_c.read_text(encoding="utf-8")
        require_sampled_texture_initializers(
            generated,
            [
                ("SDL_GPU_TEXTURETYPE_CUBE", "SDL_GPU_SHADERTEXTURESAMPLETYPE_DEPTH", "SDL_GPU_SHADERSAMPLERTYPE_COMPARISON"),
            ],
            "HLSL TextureCube SampleCmp layout facts should preserve cube depth comparison facts",
        )
        compile_layout_facts_source(hlsl_depth_cube_layout_facts_c, "hlsl_depth_cube_layout_facts")

        hlsl_msaa_input = out_dir / "layout_facts_msaa.frag.hlsl"
        hlsl_msaa_input.write_text(HLSL_MSAA_SAMPLED_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_msaa_layout_facts_c = out_dir / "hlsl_msaa_layout_facts.c"
        result = run([
            shadercross,
            str(hlsl_msaa_input),
            "-s", "HLSL",
            "-t", "fragment",
            "--resource-layout-c", str(hlsl_msaa_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_msaa_shader",
        ], expect_success=False)
        require_contains(result.stdout, MSAA_DEPTH_AMBIGUOUS_DIAGNOSTIC, "current HLSL Texture2DMS layout facts should reject instead of guessing color or depth")

        hlsl_msaa_policy_layout_facts_c = out_dir / "hlsl_msaa_policy_layout_facts.c"
        result = run([
            shadercross,
            str(hlsl_msaa_input),
            "-s", "HLSL",
            "-t", "fragment",
            "--resource-layout-c", str(hlsl_msaa_policy_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_msaa_policy_shader",
            "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=multisampled_unfilterable_float,sampler=none",
        ])
        generated = hlsl_msaa_policy_layout_facts_c.read_text(encoding="utf-8")
        require_contains(generated, "note: sampled texture slot 0 uses explicit sampled-slot policy", "HLSL Texture2DMS policy layout facts should document the explicit policy")
        require_sampled_texture_initializers(
            generated,
            [
                ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_UNFILTERABLE_FLOAT", "SDL_GPU_SHADERSAMPLERTYPE_NONE"),
            ],
            "HLSL Texture2DMS policy layout facts should emit MSAA color layout_facts",
        )
        compile_layout_facts_source(hlsl_msaa_policy_layout_facts_c, "hlsl_msaa_policy_layout_facts")

        hlsl_msaa_scalar_input = out_dir / "layout_facts_msaa_scalar.frag.hlsl"
        hlsl_msaa_scalar_input.write_text(HLSL_MSAA_SCALAR_SAMPLED_LAYOUT_FACTS + "\n", encoding="utf-8")
        result = run([
            shadercross,
            str(hlsl_msaa_scalar_input),
            "-s", "HLSL",
            "-t", "fragment",
            "--resource-layout-c", str(out_dir / "hlsl_msaa_scalar_layout_facts.c"),
            "--resource-layout-symbol-prefix", "hlsl_msaa_scalar_shader",
        ], expect_success=False)
        require_contains(result.stdout, MSAA_DEPTH_AMBIGUOUS_DIAGNOSTIC, "current HLSL Texture2DMS<float> layout facts should reject instead of guessing depth")

        hlsl_msaa_scalar_policy_layout_facts_c = out_dir / "hlsl_msaa_scalar_policy_layout_facts.c"
        run([
            shadercross,
            str(hlsl_msaa_scalar_input),
            "-s", "HLSL",
            "-t", "fragment",
            "--resource-layout-c", str(hlsl_msaa_scalar_policy_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_msaa_scalar_policy_shader",
            "--resource-layout-sampled-slot", "slot=0,texture=2d,sample=multisampled_depth,sampler=none",
        ])
        generated = hlsl_msaa_scalar_policy_layout_facts_c.read_text(encoding="utf-8")
        require_contains(generated, "note: sampled texture slot 0 uses explicit sampled-slot policy", "HLSL Texture2DMS<float> policy layout facts should document the explicit policy")
        require_sampled_texture_initializers(
            generated,
            [
                ("SDL_GPU_TEXTURETYPE_2D", "SDL_GPU_SHADERTEXTURESAMPLETYPE_MULTISAMPLED_DEPTH", "SDL_GPU_SHADERSAMPLERTYPE_NONE"),
            ],
            "HLSL Texture2DMS<float> policy layout facts should emit MSAA depth layout_facts",
        )
        compile_layout_facts_source(hlsl_msaa_scalar_policy_layout_facts_c, "hlsl_msaa_scalar_policy_layout_facts")

        hlsl_compute_input = out_dir / "layout_facts_input.comp.hlsl"
        hlsl_compute_input.write_text(HLSL_COMPUTE_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_compute_layout_facts_c = out_dir / "hlsl_compute_layout_facts.c"
        hlsl_compute_json = out_dir / "hlsl_compute.json"
        run([
            shadercross,
            str(hlsl_compute_input),
            "-s", "HLSL",
            "-t", "compute",
            "-d", "JSON",
            "-o", str(hlsl_compute_json),
            "--resource-layout-c", str(hlsl_compute_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_compute_pipeline",
        ])
        generated = hlsl_compute_layout_facts_c.read_text(encoding="utf-8")
        hlsl_compute_json_text = hlsl_compute_json.read_text(encoding="utf-8")
        require_contains(hlsl_compute_json_text, '"sdl_resource_layout"', "HLSL output plus resource layout mode should still write the requested JSON output file")
        require_contains(hlsl_compute_json_text, '"texture_dimension": "2d_array"', "HLSL compute JSON output should preserve the sampled 2D-array texture layout")
        require_contains(hlsl_compute_json_text, '"resource_class": "readwrite_storage_buffer"', "HLSL compute JSON output should preserve the storage-buffer layout")
        require_contains(generated, "static const SDL_GPUSampledTextureSlotDescription hlsl_compute_pipeline_sampled_texture_slots[]", "HLSL compute resource layout C output should emit sampled texture layout_facts")
        require_contains(generated, ".texture_type = SDL_GPU_TEXTURETYPE_2D_ARRAY", "HLSL compute resource layout C output should preserve sampled 2D-array texture facts")
        require_contains(generated, ".sampled_texture_slots = hlsl_compute_pipeline_sampled_texture_slots", "HLSL compute resource layout C output should reference generated sampled texture layout facts")
        require_not_contains(generated, ".version = sizeof(SDL_GPU", "HLSL compute resource layout C output should not emit SDL_GPU version initializers")
        compile_layout_facts_source(hlsl_compute_layout_facts_c, "hlsl_compute_layout_facts")

        hlsl_writeonly_storage_input = out_dir / "layout_facts_writeonly_storage.comp.hlsl"
        hlsl_writeonly_storage_input.write_text(HLSL_COMPUTE_WRITEONLY_STORAGE_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_writeonly_storage_layout_facts_c = out_dir / "hlsl_writeonly_storage_layout_facts.c"
        run([
            shadercross,
            str(hlsl_writeonly_storage_input),
            "-s", "HLSL",
            "-t", "compute",
            "--resource-layout-c", str(hlsl_writeonly_storage_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_writeonly_storage_pipeline",
        ])
        generated = hlsl_writeonly_storage_layout_facts_c.read_text(encoding="utf-8")
        require_contains(generated, "static const SDL_GPUStorageTextureSlotDescription hlsl_writeonly_storage_pipeline_readwrite_storage_textures[]", "HLSL write-only storage layout facts should emit storage texture layout_facts")
        require_storage_texture_initializer(
            generated,
            "SDL_GPU_TEXTURETYPE_2D",
            "SDL_GPU_TEXTUREFORMAT_R32_FLOAT",
            "SDL_GPU_STORAGETEXTUREACCESS_WRITE_ONLY",
            "HLSL write-only storage layout facts should infer write-only access from observed use",
        )
        require_not_contains(generated, "--storage-texture-slot", "HLSL write-only storage layout facts should not require an explicit slot policy note")
        compile_layout_facts_source(hlsl_writeonly_storage_layout_facts_c, "hlsl_writeonly_storage_layout_facts")

        hlsl_unannotated_writeonly_storage_input = out_dir / "layout_facts_unannotated_writeonly_storage.comp.hlsl"
        hlsl_unannotated_writeonly_storage_input.write_text(HLSL_COMPUTE_UNANNOTATED_WRITEONLY_STORAGE_LAYOUT_FACTS + "\n", encoding="utf-8")

        result = run([
            shadercross,
            str(hlsl_unannotated_writeonly_storage_input),
            "-s", "HLSL",
            "-t", "compute",
            "--resource-layout-c", str(out_dir / "hlsl_unannotated_storage_bad_policy_layout_facts.c"),
            "--resource-layout-symbol-prefix", "hlsl_unannotated_storage_bad_policy_pipeline",
            "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write",
        ], expect_success=False)
        require_contains(result.stdout, "--storage-texture-slot does not match the reflected storage texture evidence", "storage texture policy should keep exact format matching by default")
        require_contains(result.stdout, "storage texture 'OutputTexture' stage=compute source_set=1 source_binding=0 class=readwrite slot=0", "storage texture policy diagnostics should name the reflected resource and SDL slot")
        require_contains(result.stdout, "requested_policy=--storage-texture-slot class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write", "storage texture policy diagnostics should echo the rejected policy")
        require_contains(result.stdout, "suggested_policy=--storage-texture-slot class=readwrite,slot=0,texture=2d,format=rgba32float,access=write", "storage texture policy diagnostics should include an actionable reflected policy")

        result = run([
            shadercross,
            str(hlsl_unannotated_writeonly_storage_input),
            "-s", "HLSL",
            "-t", "compute",
            "--resource-layout-c", str(out_dir / "hlsl_unannotated_storage_authority_layout_facts_only.c"),
            "--resource-layout-symbol-prefix", "hlsl_unannotated_storage_authority_layout_facts_only_pipeline",
            "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
        ], expect_success=False)
        require_contains(result.stdout, "format_authority=explicit requires WGSL output or HLSL-to-SPIRV output with resource layout C output", "storage texture format authority should reject resource layout C output")

        result = run([
            shadercross,
            str(hlsl_unannotated_writeonly_storage_input),
            "-s", "HLSL",
            "-t", "compute",
            "-d", "MSL",
            "-o", str(out_dir / "hlsl_unannotated_storage_authority_msl.msl"),
            "--resource-layout-c", str(out_dir / "hlsl_unannotated_storage_authority_msl.c"),
            "--resource-layout-symbol-prefix", "hlsl_unannotated_storage_authority_msl_pipeline",
            "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
        ], expect_success=False)
        require_contains(result.stdout, "format_authority=explicit requires WGSL output or HLSL-to-SPIRV output with resource layout C output", "storage texture format authority should reject non-WGSL shader output")

        result = run([
            shadercross,
            str(hlsl_unannotated_writeonly_storage_input),
            "-s", "HLSL",
            "-t", "compute",
            "--resource-layout-c", str(out_dir / "hlsl_unannotated_storage_authority_bad_token_layout_facts.c"),
            "--resource-layout-symbol-prefix", "hlsl_unannotated_storage_authority_bad_token_pipeline",
            "--storage-texture-slot", "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=maybe",
        ], expect_success=False)
        require_contains(result.stdout, "unknown format_authority token 'maybe'", "storage texture format authority should reject unknown authority tokens")

        hlsl_storage_3d_input = out_dir / "layout_facts_storage_3d.comp.hlsl"
        hlsl_storage_3d_input.write_text(HLSL_COMPUTE_READWRITE_3D_STORAGE_LAYOUT_FACTS + "\n", encoding="utf-8")
        hlsl_storage_3d_layout_facts_c = out_dir / "hlsl_storage_3d_layout_facts.c"
        hlsl_storage_3d_json = out_dir / "hlsl_storage_3d.json"
        run([
            shadercross,
            str(hlsl_storage_3d_input),
            "-s", "HLSL",
            "-t", "compute",
            "-d", "JSON",
            "-o", str(hlsl_storage_3d_json),
        ])
        hlsl_storage_3d_json_text = hlsl_storage_3d_json.read_text(encoding="utf-8")
        require_contains(hlsl_storage_3d_json_text, '"dimension": "3d"', "HLSL 3D storage JSON output should preserve the texture dimension")
        require_contains(hlsl_storage_3d_json_text, '"observed_access": "read_write"', "HLSL 3D storage JSON output should preserve observed read-write use")
        require_contains(hlsl_storage_3d_json_text, '"final_access": null', "HLSL 3D storage JSON output should keep ambiguous final access unresolved")

        result = run([
            shadercross,
            str(hlsl_storage_3d_input),
            "-s", "HLSL",
            "-t", "compute",
            "--resource-layout-c", str(hlsl_storage_3d_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_storage_3d_pipeline",
        ], expect_success=False)
        require_contains(result.stdout, "storage texture access could not be resolved to concrete layout facts", "HLSL 3D storage layout facts should keep ambiguous access rejected")

        hlsl_storage_3d_policy_layout_facts_c = out_dir / "hlsl_storage_3d_policy_layout_facts.c"
        result = run([
            shadercross,
            str(hlsl_storage_3d_input),
            "-s", "HLSL",
            "-t", "compute",
            "--resource-layout-c", str(hlsl_storage_3d_policy_layout_facts_c),
            "--resource-layout-symbol-prefix", "hlsl_storage_3d_policy_pipeline",
            "--storage-texture-slot", "class=readwrite,slot=0,texture=3d,format=r32uint,access=read_write",
        ])
        generated = hlsl_storage_3d_policy_layout_facts_c.read_text(encoding="utf-8")
        require_contains(generated, "note: storage texture class readwrite slot 0 uses explicit --storage-texture-slot policy", "HLSL 3D storage policy layout facts should document the explicit policy")
        require_storage_texture_initializer(
            generated,
            "SDL_GPU_TEXTURETYPE_3D",
            "SDL_GPU_TEXTUREFORMAT_R32_UINT",
            "SDL_GPU_STORAGETEXTUREACCESS_READ_WRITE",
            "HLSL 3D storage policy layout facts should emit the explicit read-write slot",
        )
        require_contains(result.stdout, "resource layout note: storage texture class readwrite slot 0 uses explicit --storage-texture-slot policy", "HLSL 3D storage policy layout facts should log its policy note")
        compile_layout_facts_source(hlsl_storage_3d_policy_layout_facts_c, "hlsl_storage_3d_policy_layout_facts")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
