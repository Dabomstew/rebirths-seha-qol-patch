[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('normal', 'preparer', 'qualify')]
    [string]$Variant,
    [string]$OutputDirectory,
    [string]$VisualStudioDirectory
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$outputs = @{
    normal = 'build/native-build'
    preparer = 'build/prepare/proxy'
    qualify = 'build/qualify/proxy'
}
if (-not $OutputDirectory) { $OutputDirectory = $outputs[$Variant] }
$artifactDirectory = $OutputDirectory.Replace('/', '\').TrimEnd('\')
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repository $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if (-not $VisualStudioDirectory -and $env:PATCH_VISUAL_STUDIO) { $VisualStudioDirectory = $env:PATCH_VISUAL_STUDIO }
if (-not $VisualStudioDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio discovery requires vswhere.exe; supply -VisualStudioDirectory explicitly.'
    }
    $VisualStudioDirectory = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $VisualStudioDirectory) {
        throw 'No Visual Studio installation with the x86 C++ tools was found.'
    }
}
$vcvars = Join-Path $VisualStudioDirectory 'VC/Auxiliary/Build/vcvars32.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "Missing x86 toolchain setup: $vcvars"
}
# Import the x86 environment only into this build process, even from an x64 shell.
$environment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x86 C++ toolchain.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

# One inventory in the established compilation order. The three foundation
# objects have always linked last; preserve that separate ordering constraint.
$units = @(
    'asset_store', 'asset_entry', 'uncompressed_assets', 'tutorial_skip',
    'rebirths_core', 'rebirths_identity', 'call_patch', 'audio_tail', 'voice_tail',
    'adv_blackout', 'rebirth1_adv_fast_forward',
    'rebirth2_adv_fast_forward', 'rebirth3_adv_fast_forward', 'rebirth3_battle_delay',
    'fast_texture_conversion', 'rebirth1_adv_auto_skip', 'rebirth2_adv_auto_skip',
    'rebirth3_adv_auto_skip', 'sega_adv_fast_forward', 'sega_adv_auto_skip',
    'fast_face_texture_creation', 'sega_dungeon_movement_fix', 'x3daudio_forwarder'
)
$sources = @($units | ForEach-Object { "native\src\$_.cpp" })
$lastObjects = @('rebirths_identity', 'call_patch', 'x3daudio_forwarder')
$linkUnits = @($units | Where-Object { $_ -notin $lastObjects }) + $lastObjects
$objects = @($linkUnits | ForEach-Object { "$artifactDirectory\$_.obj" })
$compile = @('/nologo', '/std:c++17', '/O2', '/W4', '/EHsc', '/MT', '/FIbuild/generated/release-version.hpp', '/I', 'native\include', '/c')
if ($Variant -eq 'qualify') { $compile += '/DREBIRTHS_ASSET_LAA_QUALIFICATION' }
$compile += $sources
$objectDirectory = "$artifactDirectory\"
# Windows PowerShell 5 quotes arguments containing spaces without escaping a
# trailing backslash; double it so MSVC receives the directory terminator.
if ($objectDirectory -match '\s') { $objectDirectory += '\' }
$compile += "/Fo:$objectDirectory"
$link = @('/nologo', '/dll', 'build/generated/proxy-version.res') + $objects + @(
    'bcrypt.lib', '/DEF:native\x3daudio_proxy.def',
    "/OUT:$artifactDirectory\X3DAudio1_7.dll",
    "/IMPLIB:$artifactDirectory\X3DAudio1_7.lib",
    "/PDB:$artifactDirectory\X3DAudio1_7.pdb"
)
if ($Variant -ne 'normal') { $link += '/Brepro' }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
# Remove only the legacy proxy artifacts previously managed by these scripts.
foreach ($name in @('dinput8', 'rebirth3-steam-api')) {
    foreach ($extension in @('dll', 'lib', 'exp', 'pdb')) {
        $legacy = Join-Path $OutputDirectory "$name.$extension"
        if (Test-Path -LiteralPath $legacy -PathType Leaf) { Remove-Item -LiteralPath $legacy }
    }
}
Push-Location $repository
try {
    Write-Host "Proxy variant: $Variant; output: $OutputDirectory; toolchain: $VisualStudioDirectory (x86)"
    Write-Host ('cl ' + ($compile -join ' '))
    & cl.exe @compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host ('link ' + ($link -join ' '))
    & link.exe @link
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
