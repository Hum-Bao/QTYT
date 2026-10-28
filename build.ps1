param (
    [switch]$Clean,
    [switch]$Test,
    [switch]$NoRun,
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

if ($PSScriptRoot) {
    Set-Location $PSScriptRoot
} elseif ($MyInvocation.MyCommand.Path) {
    Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "         LightYT Windows Builder          " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 1. Locate Qt installation
$qtPrefix = "C:\Qt\6.8.0\mingw_64"
if (-not (Test-Path $qtPrefix)) {
    $found = Get-ChildItem "C:\Qt\*\mingw_64" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $qtPrefix = $found.FullName }
}

if (-not (Test-Path $qtPrefix)) {
    Write-Host "[ERROR] Could not find Qt MinGW installation at C:\Qt\6.8.0\mingw_64 or C:\Qt\*\mingw_64" -ForegroundColor Red
    exit 1
}
Write-Host "[Qt] Prefix: $qtPrefix" -ForegroundColor Green

# 2. Locate matching Qt MinGW toolchain
$qtGxx = Get-ChildItem "C:\Qt\Tools\mingw*_64\bin\g++.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
$compilerArgs = @()
$libDir = Join-Path $PSScriptRoot "lib"

if ($qtGxx) {
    $toolBin = $qtGxx.DirectoryName
    Write-Host "[Compiler] Using Qt MinGW toolchain: $toolBin" -ForegroundColor Green
    $env:PATH = "$libDir;$toolBin;$qtPrefix\bin;$env:PATH"
    $compilerArgs = @(
        "-DCMAKE_C_COMPILER=$toolBin\gcc.exe",
        "-DCMAKE_CXX_COMPILER=$($qtGxx.FullName)"
    )
} else {
    Write-Host "[Compiler] Qt Tools MinGW not found in C:\Qt\Tools. Using default MinGW in PATH." -ForegroundColor Yellow
    $env:PATH = "$libDir;$qtPrefix\bin;$env:PATH"
}

# 3. Clean build directory if requested
if ($Clean -and (Test-Path "build")) {
    Write-Host "[Clean] Removing build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# 4. Dependency checks (all binaries reside in lib\)
if (Test-Path "lib\yt-dlp.exe") {
    Write-Host "[Dependencies] yt-dlp backend ready in lib\." -ForegroundColor Green
} else {
    Write-Host "[Dependencies] Notice: yt-dlp.exe not found in lib\ folder." -ForegroundColor Yellow
}

if ((Test-Path "lib\mpv-2.dll") -or (Test-Path "lib\libmpv-2.dll")) {
    Write-Host "[Dependencies] In-process libmpv engine ready in lib\." -ForegroundColor Green
} elseif (Test-Path "lib\mpv.exe") {
    Write-Host "[Dependencies] Standalone mpv engine ready in lib\." -ForegroundColor Green
} else {
    Write-Host "[Dependencies] Notice: No mpv library or executable found in lib\ folder." -ForegroundColor Yellow
}

if (Test-Path "lib\libcurl-x64.dll") {
    Write-Host "[Dependencies] libcurl runtime DLL ready in lib\." -ForegroundColor Green
}

if (Test-Path "lib\bgutil-pot.exe") {
    Write-Host "[Dependencies] bgutil-pot POT provider ready in lib\." -ForegroundColor Green
} else {
    Write-Host "[Dependencies] Notice: bgutil-pot.exe not found in lib\ folder (needed for unthrottled AV1/VP9)." -ForegroundColor Yellow
}

if (Test-Path "lib\yt-dlp-plugins\bgutil-ytdlp-pot-provider\yt_dlp_plugins") {
    Write-Host "[Dependencies] bgutil-ytdlp-pot-provider plugin ready in lib\yt-dlp-plugins\." -ForegroundColor Green
} else {
    Write-Host "[Dependencies] Notice: bgutil-ytdlp-pot-provider plugin not found in lib\yt-dlp-plugins\ (see README.md)." -ForegroundColor Yellow
}

# 5. CMake Configure & Parallel Build
Write-Host "[CMake] Configuring project..." -ForegroundColor Cyan
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_PREFIX_PATH="$qtPrefix" @compilerArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] CMake configuration failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[Build] Compiling QTYT in parallel..." -ForegroundColor Cyan
cmake --build build --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

# 6. Run CTest unit tests if requested
if ($Test) {
    Write-Host "[Test] Running CTest unit tests..." -ForegroundColor Cyan
    ctest --test-dir build --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] CTest unit tests failed." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# 7. Deploy Qt dependencies
$exePath = "build\QTYT.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "[ERROR] $exePath was not produced by the build." -ForegroundColor Red
    exit 1
}

Write-Host "[Deploy] Running windeployqt..." -ForegroundColor Cyan
& "$qtPrefix\bin\windeployqt.exe" --compiler-runtime $exePath
if ($LASTEXITCODE -ne 0) {
    Write-Host "[WARN] windeployqt exited with code $LASTEXITCODE" -ForegroundColor Yellow
}

# 8. Launch application
if (-not $NoRun) {
    Write-Host "[Launch] Starting QTYT.exe..." -ForegroundColor Green
    & ".\$exePath"
}
