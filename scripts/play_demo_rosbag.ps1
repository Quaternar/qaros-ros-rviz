# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Plays a small demo rosbag (markers, TF, point cloud and robot model) into ROS 2 so that RViz2
    (with the Qar streaming plugin) has live content to visualize and to stream. Records the bag
    first when none exists; falls back to running the live publishers when recording is
    unavailable.

.DESCRIPTION
    The ROS 2 Humble Windows distribution ships no bundled rosbag (no *.db3, *.mcap or
    metadata.yaml under <distribution>\share), but it has the rosbag2 tooling (ros2bag,
    rosbag2_py). This repository ships synthetic publishers
    (scripts\publish_test_{markers,tf,pointcloud,robot}.py). So the script:

      1. If a bag exists in -BagDir (metadata.yaml present): `ros2 bag play --loop`.
      2. Else, if -BagDir holds a *.db3 file without metadata.yaml (the leftover of a recording
         that ended before the index was written): writes the index as described below and plays
         the bag as in 1. It does not record into that folder, because rosbag2 refuses to record
         into a folder that exists.
      3. Else, with -Record (the default): starts the publishers, records the topics for
         -RecordSeconds into -BagDir with `ros2 bag record`, ends the recorder, stops the
         publishers, writes the index and plays the bag in a loop. -BagDir must not exist yet,
         for the reason given in 2.
      4. Else, or when recording or writing the index fails: runs the live publishers (no bag),
         so RViz still shows content.

    The recorder is ended with Kill(), and a recorder that is killed writes no metadata.yaml,
    without which no rosbag2 command opens the bag. So the script writes the file itself, once
    the recorder has exited: first with
        ros2 bag reindex "<BagDir>" -s sqlite3
    which takes the QoS profile of each topic from the database, and, when that leaves no
    metadata.yaml, with
        python "<script folder>\rosbag_write_metadata.py" "<the .db3 file>"
    which rebuilds the index from the tables of the database and offers every topic as RELIABLE,
    /tf_static and /robot_description as TRANSIENT_LOCAL with the history depth 100.

    The `ros2` wrapper exe has a broken build-machine shebang, so the script runs the
    command-line tool as
        python "<distribution>\Scripts\ros2-script.py" <args>
    and never as ros2.exe. It dot-sources env_setup.ps1 first for the ROS environment.

.PARAMETER BagDir        Bag folder. Default: %TEMP%\qar_demo_rosbag.
.PARAMETER Record        Record a bag if none exists (default $true).
.PARAMETER RecordSeconds Seconds to record when creating a bag (default 12).
.PARAMETER Loop          Loop playback (default $true).
.PARAMETER Topics        Topics to record and play (default: markers, points, tf, tf_static, robot_description).
.PARAMETER Ros2Root      ROS 2 distribution root; also passed to env_setup.ps1. Default: the
                         environment variable ROS2_INSTALL_ROOT. Required.
#>
param(
    [string]$BagDir = "",
    [bool]$Record = $true,
    [int]$RecordSeconds = 12,
    [bool]$Loop = $true,
    [string[]]$Topics = @("/qar_test_markers","/qar_test_points","/tf","/tf_static","/robot_description"),
    [Alias('Ros2DistroRoot', 'Ros2InstallRoot')]
    [string]$Ros2Root = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "qar_locations.ps1")
$Ros2Root = Resolve-QarLocation -Value $Ros2Root -Parameter 'Ros2Root' -EnvVar 'ROS2_INSTALL_ROOT' `
    -What 'ROS 2 distribution' -Marker 'local_setup.bat' -Section 'ROS 2 Humble for Windows'
if ([string]::IsNullOrEmpty($BagDir)) { $BagDir = Join-Path $env:TEMP "qar_demo_rosbag" }

# ROS environment (PATH, AMENT_PREFIX_PATH, PYTHONPATH, ...).
. (Join-Path $PSScriptRoot "env_setup.ps1") -Ros2Root $Ros2Root

$ros2Script = Join-Path $Ros2Root "Scripts\ros2-script.py"
if (-not (Test-Path $ros2Script)) { throw "ros2-script.py not found: $ros2Script" }

# The parameter is not named Args: the automatic variable $args would hide it, and ros2 would
# start without arguments.
function Invoke-Ros2 { param([string[]]$RosArgs) & python $ros2Script @RosArgs }

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
        if (Test-Path $s) {
            $procs += Start-Process -FilePath $pyExe -ArgumentList ('"' + $s + '"') `
                -PassThru -WindowStyle Hidden
        }
    }
    return $procs
}
function Stop-Publishers { param($procs) foreach ($p in $procs) { if ($p -and -not $p.HasExited) { try { $p.Kill() } catch {} } } }

$bagMeta = Join-Path $BagDir "metadata.yaml"

# The first database file of the bag folder; nothing when the folder or the file is missing.
function Get-BagDatabase {
    if (-not (Test-Path -LiteralPath $BagDir -PathType Container)) { return }
    Get-ChildItem -LiteralPath $BagDir -Filter "*.db3" -File |
        Sort-Object Name | Select-Object -First 1
}

# Writes the missing metadata.yaml of a bag folder that holds a database: rosbag2 opens no bag
# without it, and a recorder that is killed does not write it. `ros2 bag reindex` goes first (it
# takes the QoS profile of each topic from the database); rosbag_write_metadata.py follows when
# that leaves no file (it rebuilds the index from the tables of the database).
function Repair-BagMetadata {
    $db = Get-BagDatabase
    if (-not $db -or (Test-Path $bagMeta)) { return }
    Write-Host "No metadata.yaml in $BagDir. Running ros2 bag reindex..." -ForegroundColor Yellow
    Invoke-Ros2 -RosArgs @("bag","reindex",$BagDir,"-s","sqlite3")
    if (-not (Test-Path $bagMeta)) {
        Write-Host "ros2 bag reindex wrote no metadata.yaml. Running rosbag_write_metadata.py..." `
            -ForegroundColor Yellow
        & python (Join-Path $PSScriptRoot "rosbag_write_metadata.py") $db.FullName
    }
}

# A database without metadata.yaml is the leftover of a recording that ended early. The bag is
# repaired and played, never recorded into: rosbag2 refuses to record into a folder that exists.
$leftover = (-not (Test-Path $bagMeta)) -and ($null -ne (Get-BagDatabase))
if ($leftover) {
    Repair-BagMetadata
    if (-not (Test-Path $bagMeta)) {
        Write-Warning ("Could not write metadata.yaml for the bag in $BagDir. " +
            "Falling back to live publishers.")
    }
}

if (Test-Path $bagMeta) {
    Write-Host "Playing bag: $BagDir" -ForegroundColor Green
    $playArgs = @("bag","play",$BagDir)
    if ($Loop) { $playArgs += "--loop" }
    Invoke-Ros2 -RosArgs $playArgs
    exit $LASTEXITCODE
}

if ($Record -and -not $leftover) {
    $bagDirExisted = Test-Path -LiteralPath $BagDir
    Write-Host "No bag at $BagDir. Recording a $RecordSeconds s demo bag..." -ForegroundColor Yellow
    $pubs = Start-Publishers
    Start-Sleep -Seconds 2  # let publishers advertise
    try {
        $recArgs = @("bag","record","-o",$BagDir) + $Topics
        $recArgs = (@($ros2Script) + $recArgs) |
            ForEach-Object { '"' + ($_ -replace '(\\+)$', '$1$1') + '"' }
        $rec = Start-Process -FilePath $pyExe -ArgumentList $recArgs -PassThru -WindowStyle Hidden
        Start-Sleep -Seconds $RecordSeconds
        if (-not $rec.HasExited) { try { $rec.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500; $rec.Kill() } catch {} }
        # The recorder keeps the database file open until it has exited.
        $rec.WaitForExit(5000) | Out-Null
    } finally {
        # Ctrl+C in the recording window ends the script here, with the hidden recorder still running and the
        # database still open: end it as well. Nothing to do when the recorder has exited already.
        if ($rec -and -not $rec.HasExited) {
            try { $rec.Kill(); $rec.WaitForExit(5000) | Out-Null } catch {}
        }
        Stop-Publishers $pubs
    }
    Repair-BagMetadata
    if (Test-Path $bagMeta) {
        Write-Host "Recorded bag at $BagDir. Playing looped..." -ForegroundColor Green
        $playArgs = @("bag","play",$BagDir)
        if ($Loop) { $playArgs += "--loop" }
        Invoke-Ros2 -RosArgs $playArgs
        exit $LASTEXITCODE
    }
    if (Get-BagDatabase) {
        Write-Warning ("Recorded a bag in $BagDir, but could not write its metadata.yaml. " +
            "Falling back to live publishers.")
    } elseif ($bagDirExisted) {
        Write-Warning ("Recording did not produce a bag: $BagDir existed before the recording, " +
            "and rosbag2 does not record into a folder that exists. " +
            "Falling back to live publishers.")
    } else {
        Write-Warning "Recording did not produce a bag (rmw/DDS issue?). Falling back to live publishers."
    }
}

# Fallback: run the live publishers in the foreground (Ctrl+C to stop).
Write-Host "Running live publishers (no bag). Ctrl+C to stop." -ForegroundColor Yellow
$pubs = Start-Publishers
Write-Host "Publishing topics: $($Topics -join ', '). Add the matching displays in RViz2 (frame: map)."
try { while ($true) { Start-Sleep -Seconds 1 } } finally { Stop-Publishers $pubs }
