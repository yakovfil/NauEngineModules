param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$HostSdk = $env:NAU_ENGINE_SDK_DIR,
    [string]$HostTools = $env:NAU_HOST_TOOLS_DIR,
    [string]$UsdSdk = $env:NAU_USD_SDK_DIR,
    [string]$PythonExecutable,
    [Parameter(Mandatory)][string]$ProjectDirectory,
    [Parameter(Mandatory)][string]$EngineDirectory,
    [Parameter(Mandatory)][string]$ModulesDirectory,
    [Parameter(Mandatory)][string]$ApplicationTarget,
    [string[]]$AcceptanceScenes = @(),
    [ValidateSet('Windows','Web')][string]$Platform = 'Web',
    [string]$PreparedArchive,
    [string]$Declaration,
    [switch]$Run,
    [switch]$Acceptance,
    [switch]$CleanOnly,
    [switch]$Serve,
    [ValidateRange(1, 65535)][int]$Port = 8000
)

$ErrorActionPreference = 'Stop'
$projectDirectory = (Resolve-Path -LiteralPath $ProjectDirectory).Path
$engineDirectory = (Resolve-Path -LiteralPath $EngineDirectory).Path
$modulesDirectory = (Resolve-Path -LiteralPath $ModulesDirectory).Path
$checkout = Split-Path $engineDirectory
if (-not $Declaration) { $Declaration = Join-Path $projectDirectory 'portable_project.json' }
if (-not [IO.Path]::IsPathRooted($Declaration)) { $Declaration = Join-Path $projectDirectory $Declaration }
$Declaration = (Resolve-Path -LiteralPath $Declaration).Path
$buildRoot = Join-Path $projectDirectory 'build'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
try { $buildLock = [IO.File]::Open((Join-Path $buildRoot 'portable-build.lock'), 'OpenOrCreate', 'ReadWrite', 'None') }
catch { throw 'Another portable build is using this project. Retry after it finishes.' }
try {
$build = Join-Path $buildRoot ($Platform.ToLowerInvariant() + '-' + $Configuration.ToLowerInvariant())
if ($Platform -eq 'Web') { $build = Join-Path $buildRoot ('web-' + $Configuration.ToLowerInvariant()) }
if (-not $PythonExecutable) { $PythonExecutable = (Get-Command python -ErrorAction Stop).Source }
$policyJson = & $PythonExecutable (Join-Path $modulesDirectory 'tools/asset_tools/packaged_project.py') --declaration $Declaration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$policy = $policyJson | ConvertFrom-Json
if ($policy.profile -eq 'basic-rendering-scene') {
    $build = Join-Path $buildRoot ($policy.profile + '/' + $Platform.ToLowerInvariant() + '-' + $Configuration.ToLowerInvariant())
}
Write-Host "Portable selection: $Declaration ($($policy.profile)), $Platform $Configuration"
Write-Host "Delivery: $build/package"
if ($CleanOnly) {
    $ownedRoot = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\', '/')
    $ownedTarget = [IO.Path]::GetFullPath($build)
    if (-not $ownedTarget.StartsWith($ownedRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe clean target: $ownedTarget"
    }
    # Reject junctions/symlinks along the path before recursive removal.
    $ancestor = $ownedTarget
    while ($ancestor -and $ancestor -ne $projectDirectory) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing clean through reparse point: $ancestor"
            }
        }
        $ancestor = Split-Path $ancestor
    }
    if (Test-Path -LiteralPath $ownedTarget) {
        $redirectedChild = Get-ChildItem -LiteralPath $ownedTarget -Recurse -Force |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
        if ($redirectedChild) { throw "Refusing clean with redirected child: $($redirectedChild.FullName)" }
        Remove-Item -LiteralPath $ownedTarget -Recurse -Force
    }
    exit 0
}
if (-not $PreparedArchive) {
function Find-LocalDirectory($explicitPath, $candidates, $marker, $description) {
    if ($explicitPath) { $candidates = @($explicitPath) }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate $marker) -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Cannot find $description. Checked: $($candidates -join ', '). See web/README.md for provisioning and overrides."
}
$HostSdk = Find-LocalDirectory $HostSdk @(
    (Join-Path $checkout 'NauEditorPublic/dist/win_vs2026_x64_dll')
) 'cmake/NauModule.cmake' 'native engine SDK (NAU_ENGINE_SDK_DIR)'
$HostTools = Find-LocalDirectory $HostTools @(
    (Join-Path $checkout 'NauEditorPublic/build/latest-release/bin/Release'),
    (Join-Path $HostSdk 'bin')
) 'AssetToolCmd.exe' 'native asset tools (NAU_HOST_TOOLS_DIR)'
$UsdSdk = Find-LocalDirectory $UsdSdk @(
    (Join-Path $engineDirectory 'engine/3rdparty_libs/build/openusd/install')
) 'lib/python/pxr/__init__.py' 'OpenUSD SDK (NAU_USD_SDK_DIR)'
foreach ($required in @($HostSdk, $HostTools, $UsdSdk)) {
    if (-not $required -or -not (Test-Path -LiteralPath $required -PathType Container)) {
        throw 'Provision HostSdk, HostTools and UsdSdk for native cooking. See packaged_project/README.md.'
    }
}
$HostSdk = (Resolve-Path -LiteralPath $HostSdk).Path
$HostTools = (Resolve-Path -LiteralPath $HostTools).Path
$UsdSdk = (Resolve-Path -LiteralPath $UsdSdk).Path
foreach ($required in @((Join-Path $HostTools 'AssetToolCmd.exe'), (Join-Path $HostTools 'AssetPackToolCmd.exe'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing prerequisite: $required" }
}
if (-not $PSBoundParameters.ContainsKey('PythonExecutable')) {
    $PythonExecutable = (& py -3.10 -c 'import sys; print(sys.executable)')
    if ($LASTEXITCODE -ne 0) { throw 'Provide PythonExecutable matching the native USD SDK.' }
}
$PythonExecutable = (Resolve-Path -LiteralPath $PythonExecutable).Path
$pythonDirectory = Split-Path $PythonExecutable
$env:PATH = "$HostTools;$HostTools/plugins;$pythonDirectory;$UsdSdk/bin;$UsdSdk/lib;$env:PATH"
$env:PYTHONPATH = "$UsdSdk/lib/python"
$env:PXR_PLUGINPATH_NAME = "$HostTools/plugins;$UsdSdk/plugin/usd"

# Windows PowerShell treats native stderr (including CMake NOTICE/warnings) as
# error records. Native commands below are checked by their actual exit codes.
$ErrorActionPreference = 'Continue'

$hostBuild = Join-Path $projectDirectory 'build/portable-host-components'
cmake -S $projectDirectory -B $hostBuild -G 'Visual Studio 18 2026' -A x64 `
    "-DNauEngineFolder=$HostSdk" '-DNAU_WEB_HOST_COMPONENTS=ON' '-DBUILD_SHARED_LIBS=ON'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $hostBuild --config Release --target $policy.projectModule --parallel 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$prepared = Join-Path $projectDirectory ('build/prepared-' + $Configuration.ToLowerInvariant())
if ($policy.profile -eq 'basic-rendering-scene') {
    $prepared = Join-Path $buildRoot ($policy.profile + '/prepared-' + $Configuration.ToLowerInvariant())
}
$prepareArguments = @(
    (Join-Path $modulesDirectory 'tools/asset_tools/prepare_packaged_project.py'),
    '--project', $projectDirectory, '--output', $prepared,
    '--asset-tool', (Join-Path $HostTools 'AssetToolCmd.exe'),
    '--pack-tool', (Join-Path $HostTools 'AssetPackToolCmd.exe'),
    '--module', (Join-Path $hostBuild ('bin/Release/' + $policy.projectModule + '.dll')),
    '--declaration', $Declaration
)
if ($Acceptance) { foreach ($scene in $AcceptanceScenes) { $prepareArguments += @('--scene', $scene) } }
& $PythonExecutable @prepareArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$PreparedArchive = Join-Path $prepared 'content.nau_pack'
}
$PreparedArchive = (Resolve-Path -LiteralPath $PreparedArchive).Path
& $PythonExecutable (Join-Path $modulesDirectory 'tools/asset_tools/packaged_project.py') --declaration $Declaration --archive $PreparedArchive
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$inputDirectory = Join-Path $build 'selected-input'
New-Item -ItemType Directory -Force -Path $inputDirectory | Out-Null
$selectedArchive = Join-Path $inputDirectory 'content.nau_pack'
Copy-Item -LiteralPath $PreparedArchive -Destination $selectedArchive -Force
$configure = @('-S', $engineDirectory, '-B', $build,
    '-U', 'NAU_BROWSER_PROJECT_SOURCE_DIR', '-U', 'NAU_BROWSER_PROJECT_ARCHIVE', '-U', 'NAU_BROWSER_PROJECT_MODULES',
    '-DNAU_RUNTIME_PROFILE=scene', '-DBUILD_SHARED_LIBS=OFF', '-DNAU_CORE_SAMPLES=OFF', '-DNAU_CORE_TOOLS=OFF', '-DNAU_CORE_TESTS=OFF',
    "-DCMAKE_PROJECT_NauEngine_INCLUDE=$modulesDirectory/cmake/attach_packaged_project.cmake",
    "-DNAU_PACKAGED_PROJECT_SOURCE_DIR=$projectDirectory", "-DNAU_PACKAGED_PROJECT_DECLARATION=$Declaration",
    "-DNAU_PACKAGED_PROJECT_ARCHIVE=$selectedArchive", "-DNAU_BROWSER_PROJECT_ACCEPTANCE=$($Acceptance.IsPresent)")
if ($Platform -eq 'Web') {
    if (-not $env:EMSDK) { throw 'Activate Emscripten SDK 6.0.9 before building web.' }
    $toolchain = Join-Path $env:EMSDK 'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'
    if (-not (Test-Path -LiteralPath $toolchain)) { throw "Missing Emscripten toolchain: $toolchain" }
    $configure += @('-G', 'Ninja', "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DCMAKE_BUILD_TYPE=$Configuration", '-DNAU_BUILD_BROWSER=ON')
    $suffix = 'WebPackage'
} else {
    if ($Serve) { throw '-Serve applies only to web delivery.' }
    $configure += @('-G', 'Visual Studio 18 2026', '-A', 'x64')
    $suffix = 'Package'
}
$ErrorActionPreference = 'Continue'
cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$targets = @($ApplicationTarget + $suffix)
if ($Acceptance) { $targets += $ApplicationTarget + 'AcceptancePackage' }
cmake --build $build --config $Configuration --target @targets --parallel 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Serve) {
    & $PythonExecutable (Join-Path $build 'package/serve.py') --port $Port --open-browser
} elseif ($Run) {
    if ($Platform -eq 'Web') { throw 'Use -Serve for web delivery.' }
    & (Join-Path $build ('package/' + $ApplicationTarget + '.exe'))
}
exit $LASTEXITCODE
} finally { $buildLock.Dispose() }
