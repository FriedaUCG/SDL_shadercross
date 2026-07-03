<#
  .SYNOPSIS
  Initializes the pinned SDL_shadercross Git submodules.

  .DESCRIPTION
  Synchronizes submodule URLs and updates the bounded top-level dependency set
  plus the nested DirectXShaderCompiler dependencies to the gitlink-pinned
  commits. This intentionally avoids recursively initializing Dawn.

  .PARAMETER WithDawn
  Also initialize the pinned top-level Dawn checkout for the opt-in bundled
  Tint build. You can also set SDL_SHADERCROSS_DOWNLOAD_DAWN=1.

  .EXAMPLE
  PS> .\Get-GitModules.ps1
  < Initializes the required repositories to their pinned commits. >

  .EXAMPLE
  PS> .\Get-GitModules.ps1 -WithDawn
  < Also initializes the pinned top-level Dawn checkout. >
#>

param(
    [switch]$WithDawn
)

#------- Script ----------------------------------------------------------------
$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$GitLongPathArgs = @("-c", "core.longpaths=true")
$DownloadDawn = $WithDawn.IsPresent
$DawnEnv = $env:SDL_SHADERCROSS_DOWNLOAD_DAWN
if ((-not $DownloadDawn) -and ($null -ne $DawnEnv) -and ("" -ne $DawnEnv)) {
    switch ($DawnEnv.ToLowerInvariant()) {
        { $_ -in @("0", "false", "no", "off") } { $DownloadDawn = $false; break }
        { $_ -in @("1", "true", "yes", "on") } { $DownloadDawn = $true; break }
        default {
            Write-Error "SDL_SHADERCROSS_DOWNLOAD_DAWN must be 0 or 1."
            exit 2
        }
    }
}

Push-Location $RepoRoot
try {
    $TopLevelModules = @(
        "external/SPIRV-Cross",
        "external/SPIRV-Headers",
        "external/SPIRV-Tools",
        "external/DirectXShaderCompiler"
    )
    if ($DownloadDawn) {
        $TopLevelModules += "external/dawn"
    }

    Write-Host "git -c core.longpaths=true submodule sync -- $TopLevelModules" -ForegroundColor Blue
    git @GitLongPathArgs submodule sync -- @TopLevelModules
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "git -c core.longpaths=true submodule update --init --filter=blob:none -- $TopLevelModules" -ForegroundColor Blue
    git @GitLongPathArgs submodule update --init --filter=blob:none -- @TopLevelModules
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $DXCModules = @(
        "external/DirectX-Headers",
        "external/SPIRV-Headers",
        "external/SPIRV-Tools"
    )

    Write-Host "git -c core.longpaths=true -C external/DirectXShaderCompiler submodule sync -- $DXCModules" -ForegroundColor Blue
    git @GitLongPathArgs -C external/DirectXShaderCompiler submodule sync -- @DXCModules
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "git -c core.longpaths=true -C external/DirectXShaderCompiler submodule update --init --filter=blob:none -- $DXCModules" -ForegroundColor Blue
    git @GitLongPathArgs -C external/DirectXShaderCompiler submodule update --init --filter=blob:none -- @DXCModules
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "SDL_shadercross dependencies are initialized."
    if ($DownloadDawn) {
        Write-Host "The pinned top-level Dawn checkout was initialized for the opt-in bundled Tint build. Do not initialize external/dawn recursively."
    } else {
        Write-Host "The opt-in bundled Tint build also requires the pinned top-level Dawn checkout. Rerun this script with -WithDawn before configuring SDLSHADERCROSS_BUNDLED_TINT."
    }
}
finally {
    Pop-Location
}
