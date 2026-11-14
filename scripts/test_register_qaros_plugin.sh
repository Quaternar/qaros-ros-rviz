#!/usr/bin/env bash
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Tests scripts/register-qaros-plugin.sh against a temporary XDG_CONFIG_HOME, a fake ROS 2 root and
# a fake install prefix. Needs bash and python3 (PYTHON overrides it), no ROS 2 and no build;
# touches nothing outside a new temporary folder. Exit code 0 when all checks pass.
set -uo pipefail

SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/register-qaros-plugin.sh"
PYTHON="${PYTHON:-python3}"
export PYTHON
HASH=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
config="$tmp/config home"
ros="$tmp/opt/ros/humble"
prefix="$tmp/pr$(printf '\xc3\xa9')fix"
registry="$config/quaternar/ros-plugins.json"
failures=0

check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; failures=$((failures + 1)); fi; }
mkfile() { mkdir -p "$(dirname "$1")"; printf '%s' "${2:-}" > "$1"; }
run() { out="$(env -u ROS_DISTRO -u AMENT_PREFIX_PATH -u QAR_PREFIX XDG_CONFIG_HOME="$config" bash "$SCRIPT" "$@" 2>&1)"; code=$?; }
# Reads a value from the registry with python: q '<python expression over doc>'.
q() { "$PYTHON" -c "import json,sys; doc=json.load(open(sys.argv[1],encoding='utf-8')); print($1)" "$registry"; }

mkfile "$ros/setup.bash" '# setup'
mkfile "$ros/lib/rviz2/rviz2" "#!/bin/sh"; chmod +x "$ros/lib/rviz2/rviz2"
mkfile "$ros/share/ros_environment/environment/1.ros_distro.dsv" 'set;ROS_DISTRO;humble'
mkfile "$prefix/share/ament_index/resource_index/rviz_common__pluginlib__plugin/qar_rviz_streaming" 'x'
mkfile "$prefix/lib/libqar_rviz_streaming.so"
mkfile "$prefix/share/qar_rviz_streaming/plugin_description.xml" '<library/>'
mkfile "$prefix/share/qar_rviz_streaming/config/qar_streaming_dev.rviz"
mkfile "$prefix/share/qar_rviz_streaming/package.xml" '<package format="3"><name>qar_rviz_streaming</name><version>9.8.7</version></package>'
mkfile "$prefix/share/qar_rviz_streaming/qar_streaming_h.sha256" "$HASH"

run --prefix "$prefix" --ros-root "$ros"
check 'register exits 0' '[ $code -eq 0 ]'
check 'registry created' '[ -f "$registry" ]'
check 'no byte order mark' '[ "$(head -c3 "$registry" | od -An -tx1 | tr -d " ")" != "efbbbf" ]'
check 'version 1' '[ "$(q "doc[\"version\"]")" = 1 ]'
check 'one record' '[ "$(q "len(doc[\"plugins\"])")" = 1 ]'
check 'distro humble' '[ "$(q "doc[\"plugins\"][0][\"distro\"]")" = humble ]'
check 'source local' '[ "$(q "doc[\"plugins\"][0][\"source\"]")" = local ]'
check 'platform linux-*' '[[ "$(q "doc[\"plugins\"][0][\"platform\"]")" == linux-* ]]'
check 'plugin version' '[ "$(q "doc[\"plugins\"][0][\"plugin_version\"]")" = 9.8.7 ]'
check 'hash from the stamp' '[ "$(q "doc[\"plugins\"][0][\"c_api_header_sha256\"]")" = $HASH ]'
check 'timestamp' '[[ "$(q "doc[\"plugins\"][0][\"registered_at\"]")" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z$ ]]'

cat > "$registry" <<EOF
{ "version": 1, "comment": "kept", "plugins": [
  { "distro": "humble", "platform": "linux-x64", "source": "prebuilt", "prefix": "/x", "qaros_version": "0.9.0" },
  { "distro": "jazzy", "platform": "linux-x64", "source": "local", "prefix": "/j" },
  { "distro": "humble", "platform": "$(q 'doc["plugins"][0]["platform"]' 2>/dev/null || echo linux-x64)", "source": "local", "prefix": "/old" } ] }
EOF
run --prefix "$prefix" --ros-root "$ros"
check 're-register exits 0' '[ $code -eq 0 ]'
check 'three records' '[ "$(q "len(doc[\"plugins\"])")" = 3 ]'
check 'unknown field kept' '[ "$(q "doc[\"comment\"]")" = kept ]'
check 'old local record replaced' '[ "$(q "sum(1 for r in doc[\"plugins\"] if r[\"prefix\"]==\"/old\")")" = 0 ]'
check 'prebuilt kept' '[ "$(q "[r for r in doc[\"plugins\"] if r[\"source\"]==\"prebuilt\"][0][\"qaros_version\"]")" = 0.9.0 ]'

before="$(cat "$registry")"
rm "$prefix/lib/libqar_rviz_streaming.so"
run --prefix "$prefix" --ros-root "$ros"
check 'missing library fails' '[ $code -ne 0 ] && [[ "$out" == *libqar_rviz_streaming.so* ]]'
check 'file unchanged' '[ "$(cat "$registry")" = "$before" ]'
mkfile "$prefix/lib/libqar_rviz_streaming.so"
rm "$ros/lib/rviz2/rviz2"
run --prefix "$prefix" --ros-root "$ros"
check 'root without rviz2 fails' '[ $code -ne 0 ] && [[ "$out" == *rviz2* ]]'
mkfile "$ros/lib/rviz2/rviz2" "#!/bin/sh"; chmod +x "$ros/lib/rviz2/rviz2"
run --prefix "$prefix" --ros-root "$ros" --distro jazzy
check 'distro mismatch fails' '[ $code -ne 0 ]'

run --unregister --distro humble
check 'unregister exits 0' '[ $code -eq 0 ]'
check 'two records left' '[ "$(q "len(doc[\"plugins\"])")" = 2 ]'
check 'prebuilt humble kept' '[ "$(q "sum(1 for r in doc[\"plugins\"] if r[\"distro\"]==\"humble\")")" = 1 ]'
run --unregister --distro humble
check 'second unregister no-op' '[ $code -eq 0 ] && [[ "$out" == *"Nothing to remove"* ]]'

printf '{ "version": 2, "plugins": [] }' > "$registry"
run --prefix "$prefix" --ros-root "$ros"
check 'newer version refused' '[ $code -ne 0 ] && [ "$(cat "$registry")" = "{ \"version\": 2, \"plugins\": [] }" ]'
printf '{ broken' > "$registry"
run --prefix "$prefix" --ros-root "$ros"
check 'invalid JSON refused' '[ $code -ne 0 ] && [ "$(cat "$registry")" = "{ broken" ]'
rm "$registry"
run --unregister --distro humble
check 'unregister without file' '[ $code -eq 0 ] && [ ! -f "$registry" ]'
check 'no temporary files' '[ -z "$(ls "$config/quaternar" | grep tmp)" ]'

if [ $failures -gt 0 ]; then echo "$failures check(s) failed."; exit 1; fi
echo "All checks passed."
