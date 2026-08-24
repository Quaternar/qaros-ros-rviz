<#
.SYNOPSIS
    Patch the RViz2 binaries in the ROS 2 Humble Windows distro so they import the plain
    Qt5 DLL names (Qt5Core.dll / Qt5Gui.dll / Qt5Widgets.dll) instead of the "_conda"-suffixed
    names the official CI build baked in.

.DESCRIPTION
    ROOT CAUSE (found in M0b): the official release-humble-20260220 Windows distro was built
    against a conda/pixi Qt whose module DLLs are named Qt5Core_conda.dll / Qt5Gui_conda.dll /
    Qt5Widgets_conda.dll. Four binaries import those names:
        rviz2.exe, rviz_common.dll, rviz_rendering.dll, rviz_default_plugins.dll
    The distro ships NO Qt at all, so an external Qt (aqtinstall 5.15.2) must be supplied.

    An earlier approach made byte-identical COPIES of the
    aqt Qt DLLs under the _conda names. That is BROKEN: the copies' OWN import tables still
    reference the plain names (Qt5Widgets_conda.dll -> Qt5Gui.dll, Qt5Core.dll), so BOTH a plain
    Qt5Core.dll AND a Qt5Core_conda.dll end up loaded in the same process. QApplication (built via
    the Qt5Widgets_conda copy) sets QCoreApplication::self in the PLAIN Qt5Core.dll, while
    rviz_common.dll's VisualizerApp::init() reads QCoreApplication::arguments() from the
    Qt5Core_CONDA.dll copy (whose static is still null) -> Qt prints:
        "QCoreApplication::arguments: Please instantiate the QApplication object first"
        "QCommandLineParser: argument list cannot be empty ..."
    and rviz2.exe exits with code 1 before any window appears. (Two-Qt-copies-in-one-process bug.)

    FIX (this script): binary-patch the import-directory name strings in the 4 consumer binaries,
    replacing each "_conda" Qt name with its plain name. Removing the literal "_conda" (6 chars)
    frees exactly 6 bytes, which we null-pad AFTER the ".dll" so every string keeps its exact
    original byte length -> no RVAs shift, no other PE structure changes, no checksum concerns
    (Windows does not validate the optional-header checksum for ordinary user-mode DLLs/EXEs).
    After this, every RViz binary imports the single plain aqt Qt5Core.dll -> one Qt5Core module,
    one consistent QCoreApplication::self. This makes the distro self-consistent and makes the
    any byte-identical "_conda"-named copies of the Qt DLLs unnecessary (harmless if present).

    Idempotent: if a file no longer contains any "_conda" Qt string, it is skipped.

.PARAMETER Ros2DistroRoot
    Default: D:\development\ros2_humble\ros2-windows
#>
param(
    [string]$Ros2DistroRoot = "D:\development\ros2_humble\ros2-windows"
)
$ErrorActionPreference = "Stop"

$bin = Join-Path $Ros2DistroRoot "bin"
$targets = @("rviz2.exe", "rviz_common.dll", "rviz_rendering.dll", "rviz_default_plugins.dll")

# Each pair: the _conda name and its plain replacement. Both encoded ASCII, null-terminated.
# The replacement is padded with trailing NULs so total length == original (length-preserving).
$map = @(
    @{ Old = "Qt5Core_conda.dll";    New = "Qt5Core.dll" },
    @{ Old = "Qt5Gui_conda.dll";     New = "Qt5Gui.dll" },
    @{ Old = "Qt5Widgets_conda.dll"; New = "Qt5Widgets.dll" }
)

function Find-Bytes([byte[]]$hay, [byte[]]$needle, [int]$start) {
    for ($i = $start; $i -le $hay.Length - $needle.Length; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($hay[$i + $j] -ne $needle[$j]) { $ok = $false; break }
        }
        if ($ok) { return $i }
    }
    return -1
}

foreach ($name in $targets) {
    $path = Join-Path $bin $name
    if (-not (Test-Path $path)) { Write-Warning "Not found (skipping): $path"; continue }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $totalPatched = 0

    foreach ($m in $map) {
        $old = [System.Text.Encoding]::ASCII.GetBytes($m.Old)
        # replacement: plain name + NUL padding to match old length exactly
        $newStr = [System.Text.Encoding]::ASCII.GetBytes($m.New)
        $repl = New-Object byte[] ($old.Length)
        [Array]::Copy($newStr, $repl, $newStr.Length)   # remaining bytes are already 0x00

        $idx = 0
        while (($idx = Find-Bytes $bytes $old $idx) -ge 0) {
            [Array]::Copy($repl, 0, $bytes, $idx, $repl.Length)
            $totalPatched++
            $idx += $repl.Length
        }
    }

    if ($totalPatched -gt 0) {
        $backup = "$path.conda.orig"
        if (-not (Test-Path $backup)) {
            # one-time pristine backup (D:\ artifact, not committed)
            Copy-Item -LiteralPath $path -Destination $backup -ErrorAction SilentlyContinue
        }
        [System.IO.File]::WriteAllBytes($path, $bytes)
        Write-Host "Patched $name : $totalPatched '_conda' import name(s) -> plain Qt5 names."
    } else {
        Write-Host "Skipped $name : no '_conda' Qt import names found (already patched)."
    }
}

Write-Host ""
Write-Host "Done. rviz2 binaries now import plain Qt5Core/Qt5Gui/Qt5Widgets DLLs (single Qt copy)."
Write-Host "Ensure the aqt Qt bin dir is on PATH (scripts/env_setup.ps1 handles this)."
