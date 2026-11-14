# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Tests scripts/register-qaros-plugin.ps1 against a temporary APPDATA, a fake ROS 2 root and a
    fake install prefix. Needs no ROS 2, no build and no QAROS; touches nothing outside a new
    temporary folder.

.DESCRIPTION
    Runs the register script in child processes of the same PowerShell (Windows PowerShell 5.1
    or PowerShell 7) with APPDATA pointing into the temporary folder, and checks the file it
    writes. Prints one line per check and ends with exit code 0 when all pass, 1 otherwise.

.EXAMPLE
    pwsh -NoProfile -File scripts\test_register_qaros_plugin.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test_register_qaros_plugin.ps1
#>
[CmdletBinding()]
param([switch]$Keep)

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'register-qaros-plugin.ps1'
$hostExe = (Get-Process -Id $PID).Path
$hash = '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef'

# A folder name outside ASCII checks that paths survive as UTF-8.
$root = Join-Path ([IO.Path]::GetTempPath()) ("qar-register-test-{0}" -f [Guid]::NewGuid().ToString('N').Substring(0, 8))
$appData = Join-Path $root 'AppData Roaming'
$rosRoot = Join-Path $root 'ros2-windows'
$prefix = Join-Path $root "pr$([char]0x00E9)fix $([char]0x0160)t"
$registry = Join-Path $appData 'Quaternar\ros-plugins.json'

$failures = 0
function Check([string]$Name, [bool]$Condition) {
    if ($Condition) { Write-Host "PASS $Name" -ForegroundColor Green }
    else { Write-Host "FAIL $Name" -ForegroundColor Red; $script:failures++ }
}

function New-File([string]$Path, [string]$Text = '') {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-Register([string[]]$Arguments) {
    $saved = @{ APPDATA = $env:APPDATA; ROS2_INSTALL_ROOT = $env:ROS2_INSTALL_ROOT; QAR_PREFIX = $env:QAR_PREFIX;
        ROS_DISTRO = $env:ROS_DISTRO; AMENT_PREFIX_PATH = $env:AMENT_PREFIX_PATH }
    try {
        $env:APPDATA = $appData
        $env:ROS2_INSTALL_ROOT = $null; $env:QAR_PREFIX = $null; $env:ROS_DISTRO = $null; $env:AMENT_PREFIX_PATH = $null
        # Windows PowerShell 5.1 turns stderr of a child into error records; they must not stop the test.
        $ErrorActionPreference = 'Continue'
        $out = & $hostExe -NoProfile -ExecutionPolicy Bypass -File $script @Arguments 2>&1
        return [pscustomobject]@{ Code = $LASTEXITCODE; Output = ($out | Out-String) }
    } finally {
        foreach ($k in $saved.Keys) { Set-Item -Path "Env:$k" -Value $saved[$k] -ErrorAction SilentlyContinue
            if ($null -eq $saved[$k]) { Remove-Item -Path "Env:$k" -ErrorAction SilentlyContinue } }
    }
}

function Read-Doc { return ([IO.File]::ReadAllText($registry, [Text.Encoding]::UTF8) | ConvertFrom-Json) }

try {
    # Fake ROS 2 Humble root and plugin prefix.
    New-File (Join-Path $rosRoot 'local_setup.bat') '@echo off'
    New-File (Join-Path $rosRoot 'bin\rviz2.exe')
    New-File (Join-Path $rosRoot 'share\ros_environment\environment\1.ros_distro.dsv') "set;ROS_DISTRO;humble`n"
    New-File (Join-Path $prefix 'share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming') 'share/qar_rviz_streaming/plugin_description.xml'
    New-File (Join-Path $prefix 'bin\qar_rviz_streaming.dll')
    New-File (Join-Path $prefix 'share\qar_rviz_streaming\plugin_description.xml') '<library/>'
    New-File (Join-Path $prefix 'share\qar_rviz_streaming\config\qar_streaming_dev.rviz')
    New-File (Join-Path $prefix 'share\qar_rviz_streaming\package.xml') '<?xml version="1.0"?><package format="3"><name>qar_rviz_streaming</name><version>9.8.7</version></package>'
    New-File (Join-Path $prefix 'share\qar_rviz_streaming\qar_streaming_h.sha256') "$hash`n"

    # 1. A first registration creates the file.
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'register exits 0' ($r.Code -eq 0)
    Check 'registry file created' (Test-Path -LiteralPath $registry)
    $bytes = [IO.File]::ReadAllBytes($registry)
    Check 'no byte order mark' (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF))
    $doc = Read-Doc
    Check 'version is 1' ($doc.version -eq 1)
    Check 'one record' (@($doc.plugins).Count -eq 1)
    $rec = @($doc.plugins)[0]
    Check 'distro from ros_environment' ($rec.distro -eq 'humble')
    Check 'platform windows-x64' ($rec.platform -eq 'windows-x64')
    Check 'source local' ($rec.source -eq 'local')
    Check 'prefix round-trips (non-ASCII)' ($rec.prefix -eq $prefix)
    Check 'ros_root' ($rec.ros_root -eq $rosRoot)
    Check 'plugin_version from package.xml' ($rec.plugin_version -eq '9.8.7')
    Check 'c_api_header_sha256 from the stamp' ($rec.c_api_header_sha256 -eq $hash)
    Check 'registered_at is ISO 8601 UTC' ([string]$rec.registered_at -match '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$' -or $rec.registered_at -is [datetime])
    Check 'no temporary file left' (@(Get-ChildItem -LiteralPath (Split-Path $registry) -Filter '*.tmp').Count -eq 0)

    # 2. Other records and unknown fields are kept; re-running replaces the local humble record.
    $seed = @"
{
  "version": 1,
  "comment": "kept",
  "plugins": [
    { "distro": "humble", "platform": "windows-x64", "source": "prebuilt", "ros_root": "C:\\opt\\ros\\humble", "prefix": "C:\\x", "plugin_version": "0.1.0", "c_api_header_sha256": "$hash", "registered_at": "2026-01-02T03:04:05Z", "qaros_version": "0.9.0" },
    { "distro": "jazzy", "platform": "windows-x64", "source": "local", "ros_root": "C:\\j", "prefix": "C:\\jp", "plugin_version": "0.2.0", "c_api_header_sha256": "$hash", "registered_at": "2026-01-02T03:04:05Z" },
    { "distro": "humble", "platform": "windows-x64", "source": "local", "ros_root": "C:\\old", "prefix": "C:\\old", "plugin_version": "0.0.1", "c_api_header_sha256": "$hash", "registered_at": "2026-01-02T03:04:05Z" }
  ]
}
"@
    New-File $registry $seed
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 're-register exits 0' ($r.Code -eq 0)
    $text = [IO.File]::ReadAllText($registry, [Text.Encoding]::UTF8)
    $doc = $text | ConvertFrom-Json
    Check 'three records after replace' (@($doc.plugins).Count -eq 3)
    Check 'unknown top-level field kept' ($doc.comment -eq 'kept')
    $local = @($doc.plugins | Where-Object { $_.distro -eq 'humble' -and $_.source -eq 'local' })
    Check 'one local humble record' ($local.Count -eq 1)
    Check 'local humble record replaced' ($local.Count -eq 1 -and $local[0].prefix -eq $prefix)
    $pre = @($doc.plugins | Where-Object { $_.source -eq 'prebuilt' })
    Check 'prebuilt record kept with its extra field' ($pre.Count -eq 1 -and $pre[0].qaros_version -eq '0.9.0')
    Check 'timestamp of other record unchanged' ($text -match '"registered_at":\s*"2026-01-02T03:04:05Z"')
    Check 'jazzy record kept' (@($doc.plugins | Where-Object { $_.distro -eq 'jazzy' }).Count -eq 1)

    # 3. Validation failures leave the file untouched.
    $before = [IO.File]::ReadAllText($registry)
    Remove-Item -LiteralPath (Join-Path $prefix 'bin\qar_rviz_streaming.dll')
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'missing plugin library fails' ($r.Code -ne 0 -and $r.Output -match 'qar_rviz_streaming\.dll')
    Check 'file unchanged after failure' ([IO.File]::ReadAllText($registry) -eq $before)
    New-File (Join-Path $prefix 'bin\qar_rviz_streaming.dll')

    Remove-Item -LiteralPath (Join-Path $prefix 'share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming')
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'missing ament index resource fails' ($r.Code -ne 0 -and $r.Output -match 'rviz_common__pluginlib__plugin')
    New-File (Join-Path $prefix 'share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming') 'x'

    Remove-Item -LiteralPath (Join-Path $rosRoot 'bin\rviz2.exe')
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'ROS root without rviz2 fails' ($r.Code -ne 0 -and $r.Output -match 'rviz2')
    Check 'file unchanged after failure (rviz2)' ([IO.File]::ReadAllText($registry) -eq $before)
    New-File (Join-Path $rosRoot 'lib\rviz2\rviz2.exe')
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'rviz2 in lib\rviz2 accepted' ($r.Code -eq 0)

    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot, '-Distro', 'jazzy')
    Check 'distro mismatch with the root fails' ($r.Code -ne 0)

    # 4. Unregister removes only the local record of that distribution.
    $r = Invoke-Register @('-Unregister', '-Distro', 'humble')
    Check 'unregister exits 0' ($r.Code -eq 0)
    $doc = Read-Doc
    Check 'two records after unregister' (@($doc.plugins).Count -eq 2)
    Check 'local humble gone' (@($doc.plugins | Where-Object { $_.distro -eq 'humble' -and $_.source -eq 'local' }).Count -eq 0)
    Check 'prebuilt humble kept' (@($doc.plugins | Where-Object { $_.distro -eq 'humble' -and $_.source -eq 'prebuilt' }).Count -eq 1)
    $r = Invoke-Register @('-Unregister', '-Distro', 'humble')
    Check 'second unregister is a no-op' ($r.Code -eq 0 -and $r.Output -match 'Nothing to remove')

    # 5. Files the script does not understand are left alone.
    New-File $registry '{ "version": 1, "plugins": [ '
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'invalid JSON fails' ($r.Code -ne 0)
    Check 'invalid JSON left unchanged' ([IO.File]::ReadAllText($registry) -eq '{ "version": 1, "plugins": [ ')
    New-File $registry '{ "version": 2, "plugins": [] }'
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'newer version refused' ($r.Code -ne 0 -and $r.Output -match 'version 2')
    Check 'newer version left unchanged' ([IO.File]::ReadAllText($registry) -eq '{ "version": 2, "plugins": [] }')

    # 6. Empty plugins list and a fresh unregister of a missing file.
    New-File $registry '{ "version": 1, "plugins": [] }'
    $r = Invoke-Register @('-Prefix', $prefix, '-RosRoot', $rosRoot)
    Check 'register into an empty list' ($r.Code -eq 0 -and @((Read-Doc).plugins).Count -eq 1)
    $r = Invoke-Register @('-Unregister', '-Distro', 'humble')
    $text = [IO.File]::ReadAllText($registry)
    Check 'unregister of the last record keeps an empty array' ($r.Code -eq 0 -and $text -match '"plugins":\s*\[\s*\]')
    Remove-Item -LiteralPath $registry
    $r = Invoke-Register @('-Unregister', '-Distro', 'humble')
    Check 'unregister without a file is a no-op' ($r.Code -eq 0 -and -not (Test-Path -LiteralPath $registry))
} finally {
    if (-not $Keep) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
    else { Write-Host "Kept $root" }
}

if ($failures -gt 0) {
    Write-Host "$failures check(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host 'All checks passed.' -ForegroundColor Green
exit 0
