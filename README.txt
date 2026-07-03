SDL_shadercross

This is a library for translating shaders to different formats, intended for use with SDL's GPU API.
It takes SPIRV or HLSL as the source and outputs DXBC, DXIL, SPIRV, MSL, or HLSL.
The command line interface can also emit WGSL as an offline output with an external Tint executable.

This library can perform runtime translation and conveniently returns compiled SDL GPU shader objects from HLSL or
SPIRV source.
This library also provides a command line interface for offline translation of shaders.
The command line interface can report reflection data as JSON and can emit SDL GPU resource-layout C data as
explicit pipeline-layout input.

This branch must be built against the paired FriedaUCG SDL webgpu branch:
https://github.com/FriedaUCG/SDL/tree/webgpu
CMake fails early if the selected SDL package does not provide the matching SDL GPU resource-layout API surface.
For WebGPU/WGSL builds, generate layout sidecars offline with --resource-layout-c and --resource-layout-symbol-prefix.
Use the generated <prefix>_resource_layout object as the resource_layout field for
SDL_CreateGPUShaderWithResourceLayout() or SDL_CreateGPUComputePipelineWithResourceLayout().

For SPIRV translation, this library depends on SPIRV-Cross: https://github.com/KhronosGroup/SPIRV-Cross
spirv-cross-c-shared.dll (or your platform's equivalent) can be obtained in the Vulkan SDK: https://vulkan.lunarg.com/
For compiling to DXIL, dxcompiler.dll and dxil.dll (or your platform's equivalent) are required.
DXIL dependencies can be obtained here: https://github.com/microsoft/DirectXShaderCompiler/releases
It is strongly recommended that you ship SPIRV-Cross and DXIL dependencies along with your application.
For compiling to DXBC, d3dcompiler_47 is shipped with Windows. Other platforms require vkd3d-utils.
For command line WGSL output, provide Tint with --tint, a bundled shadercross-tint helper, SDL_SHADERCROSS_TINT,
or tint on PATH. CMake can also discover Tint for optional WGSL CLI tests with SDLSHADERCROSS_TINT_EXECUTABLE.
The validated WGSL path requires the selected Tint to pass SDL_shadercross's matrix-order conformance canary.
--allow-unvalidated-tint skips this gate only for development/debugging and logs that the output is unvalidated.
Tint is an offline producer tool for the CLI path, not a runtime dependency of the SDL_shadercross library.
The opt-in SDLSHADERCROSS_BUNDLED_TINT build uses the pinned external/dawn submodule and may fetch Dawn build
dependencies while building Tint.
It builds a bundled shadercross-tint helper executable and installs its manifest and license notices without exposing
Dawn/Tint through SDL_shadercross package exports.
Initialize the normal dependencies with external/download.sh or external/Get-GitModules.ps1.
For bundled Tint, also initialize the pinned top-level Dawn checkout with SDL_SHADERCROSS_DOWNLOAD_DAWN=1
external/download.sh, external/download.sh --with-dawn, external/Get-GitModules.ps1 -WithDawn, or
git submodule update --init external/dawn.
Recursive initialization of external/dawn is unnecessary.

This library is under the zlib license, see LICENSE.txt for details.
