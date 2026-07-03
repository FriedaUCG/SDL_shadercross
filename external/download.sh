#!/bin/sh

set -e

cd "$(dirname "$0")/.."

with_dawn="${SDL_SHADERCROSS_DOWNLOAD_DAWN:-0}"
if [ "$#" -gt 0 ] && [ "$1" = "--with-dawn" ]; then
    with_dawn=1
    shift
fi
case "$with_dawn" in
    1|true|TRUE|yes|YES|on|ON)
        with_dawn=1
        ;;
    0|false|FALSE|no|NO|off|OFF|"")
        with_dawn=0
        ;;
    *)
        echo "SDL_SHADERCROSS_DOWNLOAD_DAWN must be 0 or 1." >&2
        exit 2
        ;;
esac

git_with_platform_config() {
    case "$(uname -s 2>/dev/null || echo unknown)" in
        MINGW*|MSYS*|CYGWIN*)
            git -c core.longpaths=true "$@"
            ;;
        *)
            git "$@"
            ;;
    esac
}

for arg in "$@"; do
    case "$arg" in
        --recursive|--recurse-submodules)
            echo "external/download.sh does not support ${arg}; use bounded submodule initialization." >&2
            exit 2
            ;;
        --with-dawn)
            echo "external/download.sh --with-dawn must appear before submodule update options." >&2
            exit 2
            ;;
    esac
done

core_submodules="
    external/SPIRV-Cross
    external/SPIRV-Headers
    external/SPIRV-Tools
    external/DirectXShaderCompiler
"
dawn_submodules=
if [ "$with_dawn" = 1 ]; then
    dawn_submodules="external/dawn"
fi

# Keep the list explicit so the optional bundled Tint path does not initialize
# Dawn's unrelated recursive submodules. Some dependencies carry long
# test/reference filenames, so Windows uses per-command long-path support
# without requiring a global Git setting.
git_with_platform_config submodule sync -- \
    $core_submodules \
    $dawn_submodules

git_with_platform_config submodule update --init --filter=blob:none "$@" -- \
    $core_submodules \
    $dawn_submodules

git_with_platform_config -C external/DirectXShaderCompiler submodule sync -- \
    external/DirectX-Headers \
    external/SPIRV-Headers \
    external/SPIRV-Tools

git_with_platform_config -C external/DirectXShaderCompiler submodule update --init --filter=blob:none "$@" -- \
    external/DirectX-Headers \
    external/SPIRV-Headers \
    external/SPIRV-Tools

cat <<'EOF'
SDL_shadercross dependencies are initialized.
EOF

if [ "$with_dawn" = 1 ]; then
    cat <<'EOF'
The pinned top-level Dawn checkout was initialized for the opt-in bundled Tint
build. Do not initialize external/dawn recursively.
EOF
else
    cat <<'EOF'
The opt-in bundled Tint build also requires the pinned top-level Dawn checkout.
Run SDL_SHADERCROSS_DOWNLOAD_DAWN=1 external/download.sh, or rerun this script
as external/download.sh --with-dawn, before configuring SDLSHADERCROSS_BUNDLED_TINT.
EOF
fi
