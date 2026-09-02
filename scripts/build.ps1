[CmdletBinding()]
param(
    [ValidateSet('debug', 'releasedbg', 'release')]
    [string]$Configuration = 'releasedbg',
    [string]$CommonLibF4Path = '',
    [string]$WorkspaceRoot = '',
    [string]$WorkspaceDataDir = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Find-WorkspaceRoot {
    param([Parameter(Mandatory = $true)][string]$StartPath)

    $probe = $StartPath
    for ($i = 0; $i -lt 8; $i++) {
        if (Test-Path -LiteralPath (Join-Path $probe 'workspace.json') -PathType Leaf) {
            return (Resolve-Path -LiteralPath $probe).Path
        }

        $parent = Split-Path -Parent $probe
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $probe) {
            break
        }
        $probe = $parent
    }

    return $null
}

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Find-WorkspaceRoot -StartPath $projectRoot
} elseif (Test-Path -LiteralPath $WorkspaceRoot) {
    $WorkspaceRoot = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
}

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path)) {
    $CommonLibF4Path = [Environment]::GetEnvironmentVariable('COMMONLIBF4_PATH', 'Process')
}

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path) -and -not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $workspace = Get-Content -LiteralPath (Join-Path $WorkspaceRoot 'workspace.json') -Raw | ConvertFrom-Json
    $profileName = [string]$workspace.defaultProfiles.commonlibf4
    $profileProperty = $workspace.cppToolchains.PSObject.Properties[$profileName]
    if ($null -eq $profileProperty) {
        throw "Workspace CommonLibF4 profile is not defined: $profileName"
    }
    $CommonLibF4Path = Join-Path $WorkspaceRoot ([string]$profileProperty.Value.path)
}

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path)) {
    throw 'Set COMMONLIBF4_PATH, pass -CommonLibF4Path, or run this script inside the configured workspace.'
}

$CommonLibF4Path = [System.IO.Path]::GetFullPath($CommonLibF4Path)
if (-not (Test-Path -LiteralPath (Join-Path $CommonLibF4Path 'xmake.lua') -PathType Leaf)) {
    throw "CommonLibF4 checkout was not found at $CommonLibF4Path"
}

if ([string]::IsNullOrWhiteSpace($WorkspaceDataDir) -and -not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $workspaceProject = Split-Path -Parent $projectRoot
    if (Test-Path -LiteralPath (Join-Path $workspaceProject 'mod.json') -PathType Leaf) {
        $WorkspaceDataDir = Join-Path $workspaceProject 'data'
    }
}

if (-not [string]::IsNullOrWhiteSpace($WorkspaceDataDir)) {
    $WorkspaceDataDir = [System.IO.Path]::GetFullPath($WorkspaceDataDir)
}

$buildRoot = if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    Join-Path $projectRoot 'build'
} else {
    Join-Path $WorkspaceRoot 'build\ImmersiveArsenalDisplays'
}

$previousCommonLibF4Path = [Environment]::GetEnvironmentVariable('COMMONLIBF4_PATH', 'Process')
try {
    $env:COMMONLIBF4_PATH = $CommonLibF4Path
    Push-Location $projectRoot
    try {
        & xmake f -P . -o $buildRoot -y -m $Configuration -p windows -a x64
        if ($LASTEXITCODE -ne 0) { throw 'XMake configuration failed.' }

        & xmake b -P . -y ImmersiveArsenalDisplays
        if ($LASTEXITCODE -ne 0) { throw 'ImmersiveArsenalDisplays build failed.' }
    } finally {
        Pop-Location
    }
} finally {
    if ($null -eq $previousCommonLibF4Path) {
        Remove-Item Env:COMMONLIBF4_PATH -ErrorAction SilentlyContinue
    } else {
        $env:COMMONLIBF4_PATH = $previousCommonLibF4Path
    }
}

$builtDll = Join-Path $projectRoot 'data\F4SE\Plugins\ImmersiveArsenalDisplays.dll'
if (-not (Test-Path -LiteralPath $builtDll -PathType Leaf)) {
    throw "Build succeeded but the generated DLL was not found at $builtDll"
}

if (-not [string]::IsNullOrWhiteSpace($WorkspaceDataDir)) {
    $workspaceDll = Join-Path $WorkspaceDataDir 'F4SE\Plugins\ImmersiveArsenalDisplays.dll'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $workspaceDll) | Out-Null
    Copy-Item -LiteralPath $builtDll -Destination $workspaceDll -Force
    Write-Host "[OK] Workspace DLL: $workspaceDll"
}

$dllInfo = Get-Item -LiteralPath $builtDll
$dllHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
Write-Host "[OK] Built ImmersiveArsenalDisplays ($($dllInfo.Length) bytes)"
Write-Host "[SHA256] $dllHash"
