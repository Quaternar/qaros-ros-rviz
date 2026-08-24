<#
.SYNOPSIS
    Play a small demo rosbag (markers + TF + pointcloud [+ robot]) into ROS 2 so
    RViz2 (with the Qar streaming plugin) has live content to visualize, and the
    M9.5 stereo viewer has a real scene to stream. Records the bag first if none
    is committed; falls back to running the live publishers if recording is
    unavailable.

.DESCRIPTION
    FINDING (M9.5): the ROS 2 Humble Windows distro ships NO bundled rosbag
    (no *.db3 / *.mcap / metadata.yaml under <distro>\share). The rosbag2 tooling
    IS present (ros2bag, rosbag2_py). This repo ships synthetic publishers
    (scripts\publish_test_{markers,tf,pointcloud,robot}.py). So this script:

      1. If a bag exists at -BagDir (metadata.yaml present) -> `ros2 bag play --loop`.
      2. Else if -Record (default): start the publishers, `ros2 bag record` the
         topics for -RecordSeconds into -BagDir, stop the publishers, then loop-play.
      3. Else / on record failure: just run the live publishers (no bag) so RViz
         still shows content.

    ROS 2 CLI caveat (docs/environment_lock.md): the `ros2` wrapper exe has a
    broken build-machine shebang, so this script invokes the CLI as
        python "<distro>\Scripts\ros2-script.py" <args>
    NOT ros2.exe. It dot-sources scripts\env_setup.ps1 first for the ROS env.

.PARAMETER BagDir        Bag directory (default: <repo>\demo_rosbag). NOT committed.
.PARAMETER Record        Record a bag if none exists (default $true).
.PARAMETER RecordSeconds Seconds to record when creating a bag (default 12).
.PARAMETER Loop          Loop playback (default $true).
.PARAMETER Topics        Topics to record/play (default: markers, points, tf, robot).
.PARAMETER Ros2Root      ROS 2 distro root. Default env ROS2_INSTALL_ROOT or known-good.
#>
param(
    [string]$BagDir = "",
    [bool]$Record = $true,
    [int]$RecordSeconds = 12,
    [bool]$Loop = $true,
    [string[]]$Topics = @("/qar_test_markers","/qar_test_points","/tf","/tf_static","/robot_description"),
    [string]$Ros2Root = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
if ([string]::IsNullOrEmpty($BagDir)) { $BagDir = Join-Path $RepoRoot "demo_rosbag" }

# ROS environment (PATH, AMENT_PREFIX_PATH, PYTHONPATH, ...).
. (Join-Path $PSScriptRoot "env_setup.ps1")

if ([string]::IsNullOrEmpty($Ros2Root)) {
    $Ros2Root = if ($env:ROS2_INSTALL_ROOT) { $env:ROS2_INSTALL_ROOT } else { "D:\development\ros2_humble\ros2-windows" }
}
$ros2Script = Join-Path $Ros2Root "Scripts\ros2-script.py"
if (-not (Test-Path $ros2Script)) { throw "ros2-script.py not found: $ros2Script" }

function Invoke-Ros2 { param([string[]]$Args) & python $ros2Script @Args }

$pyExe = (Get-Command python).Source
$pubScripts = @(
    (Join-Path $PSScriptRoot "publish_test_markers.py"),
    (Join-Path $PSScriptRoot "publish_test_tf.py"),
    (Join-Path $PSScriptRoot "publish_test_pointcloud.py"),
    (Join-Path $PSScriptRoot "publish_test_robot.py")
)

function Start-Publishers {
    Write-Host "Starting synthetic publishers ($([System.IO.Path]::GetFileName($pubScripts[0])) ...)" -ForegroundColor Cyan
    $procs = @()
    foreach ($s in $pubScripts) {
        if (Test-Path $s) { $procs += Start-Process -FilePath $pyExe -ArgumentList $s -PassThru -WindowStyle Hidden }
    }
    return $procs
}
function Stop-Publishers { param($procs) foreach ($p in $procs) { if ($p -and -not $p.HasExited) { try { $p.Kill() } catch {} } } }

$bagMeta = Join-Path $BagDir "metadata.yaml"

if (Test-Path $bagMeta) {
    Write-Host "Playing committed/recorded bag: $BagDir" -ForegroundColor Green
    $playArgs = @("bag","play",$BagDir)
    if ($Loop) { $playArgs += "--loop" }
    Invoke-Ros2 -Args $playArgs
    exit $LASTEXITCODE
}

if ($Record) {
    Write-Host "No bag at $BagDir. Recording a $RecordSeconds s demo bag..." -ForegroundColor Yellow
    $pubs = Start-Publishers
    Start-Sleep -Seconds 2  # let publishers advertise
    try {
        $recArgs = @("bag","record","-o",$BagDir) + $Topics
        $rec = Start-Process -FilePath $pyExe -ArgumentList (@($ros2Script) + $recArgs) -PassThru -WindowStyle Hidden
        Start-Sleep -Seconds $RecordSeconds
        if (-not $rec.HasExited) { try { $rec.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500; $rec.Kill() } catch {} }
    } finally {
        Stop-Publishers $pubs
    }
    if (Test-Path $bagMeta) {
        Write-Host "Recorded bag at $BagDir. Playing looped..." -ForegroundColor Green
        $playArgs = @("bag","play",$BagDir)
        if ($Loop) { $playArgs += "--loop" }
        Invoke-Ros2 -Args $playArgs
        exit $LASTEXITCODE
    }
    Write-Warning "Recording did not produce a bag (rmw/DDS issue?). Falling back to live publishers."
}

# Fallback: run the live publishers in the foreground (Ctrl+C to stop).
Write-Host "Running live publishers (no bag). Ctrl+C to stop." -ForegroundColor Yellow
$pubs = Start-Publishers
Write-Host "Publishing topics: $($Topics -join ', '). Add the matching displays in RViz2 (frame: map)."
try { while ($true) { Start-Sleep -Seconds 1 } } finally { Stop-Publishers $pubs }
