param(
    [ValidateSet('Windows','Web')][string]$Platform = 'Windows',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$EngineDirectory = $env:NAU_ENGINE_SOURCE_DIR,
    [string]$ModulesDirectory = $env:NAU_MODULES_SOURCE_DIR,
    [string]$HostSdk = $env:NAU_ENGINE_SDK_DIR,
    [string]$HostTools = $env:NAU_HOST_TOOLS_DIR,
    [string]$UsdSdk = $env:NAU_USD_SDK_DIR,
    [string]$PythonExecutable,
    [string]$Declaration,
    [string]$PreparedArchive,
    [switch]$CleanOnly,
    [switch]$Run,
    [switch]$Serve,
    [ValidateRange(1,65535)][int]$Port = 8000
)
$ErrorActionPreference = 'Stop'
foreach ($dependency in @('EngineDirectory','ModulesDirectory')) {
    $location = Get-Variable -Name $dependency -ValueOnly
    if (-not $location -or -not (Test-Path -LiteralPath $location -PathType Container)) {
        throw "Set $dependency explicitly or configure NAU_ENGINE_SOURCE_DIR and NAU_MODULES_SOURCE_DIR. See README.md."
    }
}
if ($Run -and $Platform -eq 'Web') { $Serve = $true; $Run = $false }
if ($Platform -eq 'Web' -and -not $CleanOnly) {
    if (-not $env:EMSDK) { $env:EMSDK = Join-Path $env:USERPROFILE 'emsdk' }
    $activate = Join-Path $env:EMSDK 'emsdk_env.ps1'
    if (-not (Test-Path -LiteralPath $activate)) { throw 'Provision Emscripten SDK 6.0.9 and set EMSDK.' }
    $env:EMSDK_QUIET = '1'
    . $activate > $null
}
$arguments = @{} + $PSBoundParameters
$arguments.Platform = $Platform
$arguments.EngineDirectory = $EngineDirectory
$arguments.ModulesDirectory = $ModulesDirectory
$arguments.ProjectDirectory = $PSScriptRoot
$arguments.ApplicationTarget = '%%(ProjectName:no_space)%%Main'
$arguments.Run = $Run
$arguments.Serve = $Serve
# Forward supplied settings; the shared driver owns default declaration resolution.
& (Join-Path $ModulesDirectory 'cmake/packaged_project/build.ps1') @arguments
exit $LASTEXITCODE
