<#
.SYNOPSIS
    Capture the offscreen test scenes non-interactively.

.DESCRIPTION
    For each requested scene this script (a) starts the scene's data publisher (if
    any) as a background process using the sourced ROS 2 distro environment, then
    (b) launches rviz2 with the scene config in AUTO-CAPTURE mode via
    run_rviz_with_plugin.ps1 (the QarStreamingPanel renders the live scene into an
    offscreen color+depth RenderTexture and writes *_color.png / *_depth.png /
    *_depth.f32 / *_capture.txt, then RViz quits), then (c) stops the publisher.

    Scenes:
      1 Grid only
      2 Grid + TF axes            (publish_test_tf.py)
      3 Grid + MarkerArray        (publish_test_markers.py)
      4 Grid + RobotModel         (publish_test_robot.py)
      5 Grid + PointCloud2        (publish_test_pointcloud.py)

.PARAMETER Scene
    all | 1 | 2 | 3 | 4 | 5   (default all)
.PARAMETER OutDir
    Output directory for captures. Default: <repo>/build/m4_captures
.PARAMETER DelayMs
    Delay after RViz loads before capturing (lets data arrive). Default 8000.
#>
param(
    [ValidateSet("all", "1", "2", "3", "4", "5")]
    [string]$Scene = "all",
    [string]$OutDir = "",
    [int]$DelayMs = 8000,
    [string]$QtVersionDir = "D:\development\Qt\5.15.2\msvc2019_64"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "build\m4_captures" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Source the ROS 2 distro env so background publishers (rclpy) work.
. "$PSScriptRoot\env_setup.ps1" -QtVersionDir $QtVersionDir | Out-Null

$configDir = Join-Path $RepoRoot "qar_rviz_streaming\config\test_scenes"

# scene -> (config file, publisher script or $null, prefix stem)
$scenes = @{
    "1" = @{ cfg = "scene1_grid.rviz";            pub = $null;                        stem = "scene1_grid" }
    "2" = @{ cfg = "scene2_grid_tf.rviz";         pub = "publish_test_tf.py";         stem = "scene2_grid_tf" }
    "3" = @{ cfg = "scene3_grid_marker.rviz";     pub = "publish_test_markers.py";    stem = "scene3_grid_marker" }
    "4" = @{ cfg = "scene4_grid_robot.rviz";      pub = "publish_test_robot.py";      stem = "scene4_grid_robot" }
    "5" = @{ cfg = "scene5_grid_pointcloud.rviz"; pub = "publish_test_pointcloud.py"; stem = "scene5_grid_pointcloud" }
}

$order = if ($Scene -eq "all") { @("1", "2", "3", "4", "5") } else { @($Scene) }
$results = @()

foreach ($s in $order) {
    $info = $scenes[$s]
    $cfg = Join-Path $configDir $info.cfg
    $prefix = Join-Path $OutDir $info.stem
    Write-Host ""
    Write-Host "==================== SCENE $s : $($info.stem) ====================" -ForegroundColor Cyan

    $pubProc = $null
    if ($info.pub) {
        $pubPath = Join-Path $PSScriptRoot $info.pub
        Write-Host "Starting publisher: $($info.pub)" -ForegroundColor DarkCyan
        $pubProc = Start-Process -FilePath "python" -ArgumentList "`"$pubPath`"" -PassThru -WindowStyle Hidden
        Start-Sleep -Seconds 3   # let discovery + first messages settle
    }

    try {
        & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\run_rviz_with_plugin.ps1" `
            -Config $cfg -AutoCapture $prefix -DelayMs $DelayMs
        $rc = $LASTEXITCODE
    }
    finally {
        if ($pubProc -and -not $pubProc.HasExited) {
            Stop-Process -Id $pubProc.Id -Force -ErrorAction SilentlyContinue
        }
    }

    $statsFile = "$prefix" + "_capture.txt"
    $pass = $false
    if (Test-Path $statsFile) {
        $pass = (Get-Content $statsFile | Select-String -Pattern '^overall_capture_pass=true' -Quiet)
    }
    $results += [PSCustomObject]@{ Scene = $s; Stem = $info.stem; Pass = $pass; Rc = $rc }
}

Write-Host ""
Write-Host "==================== SUMMARY ====================" -ForegroundColor Cyan
foreach ($r in $results) {
    $color = if ($r.Pass) { "Green" } else { "Red" }
    Write-Host ("Scene {0} {1,-24} : {2}" -f $r.Scene, $r.Stem, ($(if ($r.Pass) { "PASS" } else { "FAIL/UNVERIFIED" }))) -ForegroundColor $color
}
$anyFail = ($results | Where-Object { -not $_.Pass } | Measure-Object).Count
exit $anyFail
