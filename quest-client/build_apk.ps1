# Build script for Project VXR Meta Quest Native Client (APK)
param(
    [string]$AndroidSdk = "D:\AndroidSDK",
    [string]$NdkVersion = "30.0.16248370",
    [string]$BuildToolsVersion = "36.0.0"
)

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
$ndkPath = Join-Path $AndroidSdk "ndk\$NdkVersion"
$toolchain = Join-Path $ndkPath "build\cmake\android.toolchain.cmake"
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$buildTools = Join-Path $AndroidSdk "build-tools\$BuildToolsVersion"
$aapt2 = Join-Path $buildTools "aapt2.exe"
$zipalign = Join-Path $buildTools "zipalign.exe"
$apksigner = Join-Path $buildTools "apksigner.bat"
$androidJar = Join-Path $AndroidSdk "platforms\android-34\android.jar"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   Building Project VXR Quest Native Client (ARM64 APK)   " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$buildDir = Join-Path $root "build-android"
$apkRoot = Join-Path $buildDir "apk-root"
$libDir = Join-Path $apkRoot "lib\arm64-v8a"
New-Item -ItemType Directory -Path $libDir -Force | Out-Null

# 1. Compile Native NDK arm64-v8a shared library
Write-Host ">>> [1/5] Compiling native C++ NDK library (libvxr_quest_client.so)..." -ForegroundColor Yellow
& $cmake -S $root -B $buildDir -G "Ninja" -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29
& $cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake NDK compilation failed." }

$soSource = Join-Path $buildDir "libvxr_quest_client.so"
Copy-Item $soSource -Destination $libDir -Force

# 2. Compile res/ (ic_launcher & ic_banner), AndroidManifest.xml, and SPIR-V Shader Assets into base APK
Write-Host ">>> [2/5] Compiling resources, AndroidManifest.xml & SPIR-V Shaders via aapt2..." -ForegroundColor Yellow
$assetsDir = Join-Path $apkRoot "assets\shaders"
New-Item -ItemType Directory -Path $assetsDir -Force | Out-Null
Copy-Item (Join-Path $root "shaders\*.spv") -Destination $assetsDir -Force

$resDir = Join-Path $root "res"
$resZip = Join-Path $buildDir "res.zip"
if (Test-Path $resZip) { Remove-Item $resZip -Force }
& $aapt2 compile --dir $resDir -o $resZip
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed." }

$manifest = Join-Path $root "AndroidManifest.xml"
$unalignedApk = Join-Path $buildDir "unaligned.apk"
& $aapt2 link -I $androidJar --manifest $manifest -R $resZip -A (Join-Path $apkRoot "assets") -o $unalignedApk --min-sdk-version 29 --target-sdk-version 32
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed." }

# 3. Add ARM64 shared library into APK archive
Write-Host ">>> [3/5] Packaging lib/arm64-v8a into APK..." -ForegroundColor Yellow
Push-Location $apkRoot
& jar -u -f $unalignedApk -0 "lib/arm64-v8a/libvxr_quest_client.so"
Pop-Location

# 4. 4-byte page-align APK for direct mmap execution
Write-Host ">>> [4/5] Aligning APK boundaries with zipalign..." -ForegroundColor Yellow
$alignedApk = Join-Path $buildDir "aligned.apk"
& $zipalign -f -p 4 $unalignedApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "zipalign failed." }

# 5. Sign APK with debug certificate using apksigner
Write-Host ">>> [5/5] Signing APK with apksigner (v2 & v3 schemes)..." -ForegroundColor Yellow
$keystore = Join-Path $buildDir "debug.keystore"
if (-not (Test-Path $keystore)) {
    & keytool -genkey -v -keystore $keystore -alias androiddebugkey -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
}
$finalApk = Join-Path $root "vxr-quest-client.apk"
& $apksigner sign --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true --ks $keystore --ks-pass pass:android --key-pass pass:android --out $finalApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "apksigner failed." }

# Verify and report
& $apksigner verify --verbose $finalApk
$apkInfo = Get-Item $finalApk

Write-Host "==========================================================" -ForegroundColor Green
Write-Host "   BUILD SUCCESSFUL: $finalApk ($([math]::Round($apkInfo.Length / 1MB, 2)) MB)" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
