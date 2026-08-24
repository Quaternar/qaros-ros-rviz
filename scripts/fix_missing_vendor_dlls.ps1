<#
.SYNOPSIS
    Builds console_bridge, tinyxml2, yaml-cpp, assimp, and libcurl from source and installs them
    into the ROS 2 Humble Windows binary distro's bin directory, working around packaging gaps
    in the official release archive (companion to fix_missing_spdlog_dll.ps1).

.DESCRIPTION
    Diagnosed at M0 time via a recursive dumpbin /dependents walk of rviz2.exe's full DLL
    dependency closure (after the spdlog.dll and Qt "_conda"-alias fixes were already applied -
    see fix_missing_spdlog_dll.ps1 and fix_qt_conda_imports.ps1): the official
    release-humble-20260220 archive is ALSO missing these 5 third-party DLLs entirely (confirmed
    absent anywhere in the extracted tree via `find`, same pattern as the spdlog gap):

      - tinyxml2.dll        (required by rviz_common.dll - XML/URDF parsing)
      - console_bridge.dll  (required by rviz_common.dll, tf2.dll, class_loader.dll - logging shim)
      - yaml-cpp.dll        (required by rviz_common.dll - YAML; distinct from libyaml/yaml.dll)
      - assimp-vc141-mt.dll (required by rviz_rendering.dll - 3D mesh/asset import)
      - libcurl.dll         (required by resource_retriever.dll - HTTP(S)/package:// fetching)

    Versions were traced to each ROS2 vendor package's pinned upstream source (via package.xml
    dependency names + the vendor package's CMakeLists.txt on GitHub at its pinned tag, the same
    technique used for the spdlog fix):

      - console_bridge_vendor (1.4.1) -> ros/console_bridge tag 1.0.2
      - tinyxml2_vendor (0.7.7) -> no vendor-built pin (find_package only); picked tinyxml2 9.0.0
        (leethomason/tinyxml2) - low ABI risk, the only symbols rviz_common.dll imports are a
        small, long-stable subset (XMLDocument/XMLElement/XMLNode basics).
      - yaml_cpp_vendor (8.0.2) -> jbeder/yaml-cpp tag yaml-cpp-0.7.0
      - rviz_assimp_vendor (part of the ros2/rviz monorepo, external/rviz submodule) -> assimp
        tag v4.1.0. NOTE: assimp's own CMake naming logic (as of 4.1.0) emits
        "assimp-vc140-mt.dll" even when built with the VS2019 (v142) generator - this script
        copies+renames it to the "assimp-vc141-mt.dll" filename rviz_rendering.dll actually
        expects. This is safe: Windows resolves DLL dependencies by filename only, and the
        MSVC C/C++ runtime ABI (VCRUNTIME140/MSVCP140) is compatible across the v140-v143
        toolset family (VS2015 through VS2022), so a VS2019-built DLL satisfies a "vc141"-named
        import regardless of the cosmetic toolset tag in the filename.
      - libcurl_vendor (found in github.com/ros/resource_retriever, branch humble, subdirectory
        libcurl_vendor/ - NOT a standalone ros2/libcurl_vendor repo) -> curl tag curl-7_81_0
        (7.81.0), built with CURL_USE_SCHANNEL=ON (Windows' native TLS) so no separate OpenSSL
        dependency is required.

    All builds use the VS2019 (v142) generator and RelWithDebInfo config to match the rest of
    the distro's toolchain (same choice validated by the spdlog fix).

.PARAMETER Ros2DistroRoot
    Path to the extracted ROS 2 Windows distro root. Default: D:\development\ros2_humble\ros2-windows

.PARAMETER BuildRoot
    Scratch directory for downloading/building. Default: D:\development\build_vendor_dlls

.EXAMPLE
    ./scripts/fix_missing_vendor_dlls.ps1
#>
param(
    [string]$Ros2DistroRoot = "D:\development\ros2_humble\ros2-windows",
    [string]$BuildRoot = "D:\development\build_vendor_dlls"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$destDir = Join-Path $Ros2DistroRoot "bin"
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$downloads = Join-Path (Split-Path $BuildRoot) "downloads"
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

function Get-AndExtract($url, $archiveName, $extractedDirName) {
    $archivePath = Join-Path $downloads $archiveName
    if (-not (Test-Path $archivePath)) {
        Write-Host "Downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $archivePath -UseBasicParsing
    }
    $destPath = Join-Path $BuildRoot $extractedDirName
    if (-not (Test-Path $destPath)) {
        Write-Host "Extracting $archiveName"
        tar -xzf $archivePath -C $BuildRoot
    }
    return $destPath
}

function Install-BuiltDll($srcPath, $destName) {
    $dst = Join-Path $destDir $destName
    Copy-Item $srcPath $dst -Force
    Write-Host "Installed: $dst  (from $srcPath)"
}

# --- console_bridge 1.0.2 ---
if (-not (Test-Path (Join-Path $destDir "console_bridge.dll"))) {
    $src = Get-AndExtract "https://github.com/ros/console_bridge/archive/1.0.2.tar.gz" "console_bridge-1.0.2.tar.gz" "console_bridge-1.0.2"
    $b = Join-Path $src "_build"
    cmake -S $src -B $b -G "Visual Studio 16 2019" -A x64 -DCMAKE_INSTALL_PREFIX="$src\_install"
    if ($LASTEXITCODE -ne 0) { throw "console_bridge configure failed" }
    cmake --build $b --config RelWithDebInfo --target console_bridge
    if ($LASTEXITCODE -ne 0) { throw "console_bridge build failed" }
    Install-BuiltDll "$b\bin\RelWithDebInfo\console_bridge.dll" "console_bridge.dll"
} else { Write-Host "console_bridge.dll already installed - skipping" }

# --- tinyxml2 9.0.0 ---
if (-not (Test-Path (Join-Path $destDir "tinyxml2.dll"))) {
    $src = Get-AndExtract "https://github.com/leethomason/tinyxml2/archive/refs/tags/9.0.0.tar.gz" "tinyxml2-9.0.0.tar.gz" "tinyxml2-9.0.0"
    $b = Join-Path $src "_build"
    cmake -S $src -B $b -G "Visual Studio 16 2019" -A x64 -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX="$src\_install"
    if ($LASTEXITCODE -ne 0) { throw "tinyxml2 configure failed" }
    cmake --build $b --config RelWithDebInfo --target tinyxml2
    if ($LASTEXITCODE -ne 0) { throw "tinyxml2 build failed" }
    Install-BuiltDll "$b\RelWithDebInfo\tinyxml2.dll" "tinyxml2.dll"
} else { Write-Host "tinyxml2.dll already installed - skipping" }

# --- yaml-cpp 0.7.0 ---
if (-not (Test-Path (Join-Path $destDir "yaml-cpp.dll"))) {
    $src = Get-AndExtract "https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-0.7.0.tar.gz" "yaml-cpp-0.7.0.tar.gz" "yaml-cpp-yaml-cpp-0.7.0"
    $b = Join-Path $src "_build"
    cmake -S $src -B $b -G "Visual Studio 16 2019" -A x64 -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF -DYAML_BUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX="$src\_install"
    if ($LASTEXITCODE -ne 0) { throw "yaml-cpp configure failed" }
    cmake --build $b --config RelWithDebInfo --target yaml-cpp
    if ($LASTEXITCODE -ne 0) { throw "yaml-cpp build failed" }
    Install-BuiltDll "$b\RelWithDebInfo\yaml-cpp.dll" "yaml-cpp.dll"
} else { Write-Host "yaml-cpp.dll already installed - skipping" }

# --- assimp 4.1.0 ---
if (-not (Test-Path (Join-Path $destDir "assimp-vc141-mt.dll"))) {
    $src = Get-AndExtract "https://github.com/assimp/assimp/archive/v4.1.0.tar.gz" "assimp-4.1.0.tar.gz" "assimp-4.1.0"
    $b = Join-Path $src "_build"
    cmake -S $src -B $b -G "Visual Studio 16 2019" -A x64 -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_SAMPLES=OFF -DCMAKE_CXX_FLAGS="/w /EHsc" -DCMAKE_C_FLAGS="/w" -DCMAKE_INSTALL_PREFIX="$src\_install"
    if ($LASTEXITCODE -ne 0) { throw "assimp configure failed" }
    cmake --build $b --config RelWithDebInfo --target assimp
    if ($LASTEXITCODE -ne 0) { throw "assimp build failed" }
    # assimp's own CMake naming emits "assimp-vc140-mt.dll" even under the VS2019 generator -
    # rename to the "vc141" filename rviz_rendering.dll's import table actually expects.
    Install-BuiltDll "$b\code\RelWithDebInfo\assimp-vc140-mt.dll" "assimp-vc141-mt.dll"
} else { Write-Host "assimp-vc141-mt.dll already installed - skipping" }

# --- curl 7.81.0 (schannel TLS backend, no OpenSSL dependency) ---
if (-not (Test-Path (Join-Path $destDir "libcurl.dll"))) {
    $src = Get-AndExtract "https://github.com/curl/curl/releases/download/curl-7_81_0/curl-7.81.0.tar.gz" "curl-7.81.0.tar.gz" "curl-7.81.0"
    $b = Join-Path $src "_build"
    cmake -S $src -B $b -G "Visual Studio 16 2019" -A x64 -DENABLE_MANUAL=OFF -DBUILD_TESTING=OFF -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DBUILD_SHARED_LIBS=ON -DCURL_STATICLIB=OFF -DCMAKE_INSTALL_PREFIX="$src\_install"
    if ($LASTEXITCODE -ne 0) { throw "curl configure failed" }
    cmake --build $b --config RelWithDebInfo --target libcurl
    if ($LASTEXITCODE -ne 0) { throw "curl build failed" }
    $dll = Get-ChildItem $b -Recurse -Filter "libcurl*.dll" | Where-Object { $_.FullName -match "RelWithDebInfo" } | Select-Object -First 1
    if (-not $dll) { throw "Could not locate built libcurl DLL" }
    Install-BuiltDll $dll.FullName "libcurl.dll"
} else { Write-Host "libcurl.dll already installed - skipping" }

Write-Host ""
Write-Host "Done. Re-run the M0 smoke test (ros2 --help / rviz2.exe) to confirm no DLL load errors remain."
