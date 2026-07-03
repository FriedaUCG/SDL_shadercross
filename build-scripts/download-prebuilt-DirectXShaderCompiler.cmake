set(DXC_LINUX_X64_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.10.2605.24/linux_dxc_preview_2026_05_22.x86_64.tar.gz")
set(DXC_LINUX_X64_HASH "SHA256=6119f59c4f758973cadc170607ba83e657dd2c6fb7ca3dfb7362717a4ece8d9e")
set(DXC_WINDOWS_X86_X64_ARM64_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.10.2605.24/dxc_preview_2026_05_22.zip")
set(DXC_WINDOWS_X86_X64_ARM64_HASH "SHA256=045e2cfd900135f640954553038febbc98692599c5606376726d00541dae69b6")

get_filename_component(EXTERNAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../external" ABSOLUTE)
if(NOT DEFINED DXC_ROOT)
    set(DXC_ROOT "${EXTERNAL_PATH}/DirectXShaderCompiler-binaries")
endif()
file(TO_CMAKE_PATH "${DXC_ROOT}" DXC_ROOT_CMAKE)

set(DOWNLOAD_LINUX ON)
set(DOWNLOAD_WINDOWS ON)
if(DEFINED CMAKE_SYSTEM_NAME)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(DOWNLOAD_LINUX OFF)
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(DOWNLOAD_WINDOWS OFF)
    endif()
endif()

set(DXC_LINUX_PROCESSOR "")
if(DEFINED CMAKE_SYSTEM_NAME AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DXC_LINUX_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(DXC_LINUX_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
if(DOWNLOAD_LINUX AND DXC_LINUX_PROCESSOR)
    string(TOLOWER "${DXC_LINUX_PROCESSOR}" DXC_LINUX_PROCESSOR_LOWER)
    if(NOT DXC_LINUX_PROCESSOR_LOWER MATCHES "^(x86_64|amd64)$")
        message(FATAL_ERROR "The prebuilt DirectXShaderCompiler helper only supports Linux x86_64 packages. Use the DXC source provider or provide a platform package with DirectXShaderCompiler_ROOT.")
    endif()
endif()

if(DOWNLOAD_LINUX)
    include(FetchContent)
    FetchContent_Populate(
        dxc_linux
        URL  "${DXC_LINUX_X64_URL}"
        URL_HASH  "${DXC_LINUX_X64_HASH}"
        SOURCE_DIR "${DXC_ROOT_CMAKE}/linux"
    )
endif()

if(DOWNLOAD_WINDOWS)
    include(FetchContent)
    FetchContent_Populate(
        dxc_windows
        URL  "${DXC_WINDOWS_X86_X64_ARM64_URL}"
        URL_HASH  "${DXC_WINDOWS_X86_X64_ARM64_HASH}"
        SOURCE_DIR "${DXC_ROOT_CMAKE}/windows"
    )
endif()

message("To make use of the prebuilt DirectXShaderCompiler libraries, configure with:")
message("")
message("  -DSDLSHADERCROSS_DXC_PROVIDER=package")
message("")
message("and")
message("")
message("  -DDirectXShaderCompiler_ROOT=\"${DXC_ROOT_CMAKE}\"")
message("")
