#!/usr/bin/env bash
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Registers a hand-built qar_rviz_streaming install prefix for QAROS, so that the Visualizer offers
# "ROS 2 Rviz" for that ROS 2 distribution and starts rviz2 with this plugin.
#
# Writes or replaces one record in the per-user file
#     ${XDG_CONFIG_HOME:-~/.config}/quaternar/ros-plugins.json
# keyed by (distro, platform, source); this script always writes source "local", so running it
# again for the same distribution replaces the earlier record. Other records are kept as they are.
# The format is documented in docs/CONFIGURATION.md ("QAROS plugin registration") and in
# docs/ros-plugins.schema.json.
#
# Before it writes, it checks that the prefix holds the plugin (the ament index resource
# share/ament_index/resource_index/rviz_common__pluginlib__plugin/qar_rviz_streaming,
# lib/libqar_rviz_streaming.so, share/qar_rviz_streaming/plugin_description.xml and
# share/qar_rviz_streaming/config/qar_streaming_dev.rviz) and that the ROS 2 root holds a setup
# script (setup.bash or local_setup.bash) and rviz2 (lib/rviz2/rviz2 or bin/rviz2). It never
# changes the distribution or the prefix. The file is written atomically (a temporary file in the
# same folder, then a rename) as UTF-8 without a byte order mark; no root rights are needed.
# The JSON is edited with python3, which every ROS 2 installation has (PYTHON overrides it).
#
# Usage:
#   scripts/register-qaros-plugin.sh [--prefix DIR] [--ros-root DIR] [--distro NAME]
#                                    [--c-api-header FILE]
#   scripts/register-qaros-plugin.sh --unregister [--distro NAME | --ros-root DIR]
#
#   --prefix        colcon install prefix of the plugin (default: $QAR_PREFIX, else <repo>/install)
#   --ros-root      ROS 2 root (default: /opt/ros/$ROS_DISTRO, else the first ROS 2 root on
#                   AMENT_PREFIX_PATH)
#   --distro        ROS 2 distribution (default: from the root's share/ros_environment, else
#                   $ROS_DISTRO)
#   --c-api-header  qar_streaming.h the plugin was built against (default: the hash the build
#                   installed, share/qar_rviz_streaming/qar_streaming_h.sha256, else the header of
#                   the submodule external/qaros)
#   --unregister    remove the local record of this distribution and platform
set -euo pipefail

PACKAGE=qar_rviz_streaming
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"

die() { echo "ERROR: $*" >&2; exit 1; }
usage() { sed -n '/^# Usage:/,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d; s/^# \{0,1\}//'; }

prefix="" ros_root="" distro="" header="" unregister=0
while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) prefix="${2:?--prefix needs a folder}"; shift 2 ;;
    --ros-root) ros_root="${2:?--ros-root needs a folder}"; shift 2 ;;
    --distro) distro="${2:?--distro needs a name}"; shift 2 ;;
    --c-api-header) header="${2:?--c-api-header needs a file}"; shift 2 ;;
    --unregister) unregister=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1 (see --help)" ;;
  esac
done

command -v "$PYTHON" >/dev/null 2>&1 || die "$PYTHON not found; it is needed to edit the JSON file."

case "$(uname -m)" in
  x86_64|amd64) platform=linux-x64 ;;
  aarch64|arm64) platform=linux-arm64 ;;
  *) die "unsupported machine $(uname -m)" ;;
esac

abspath() { (cd "$1" && pwd -P); }

is_ros_root() {
  { [ -f "$1/setup.bash" ] || [ -f "$1/local_setup.bash" ]; } &&
    { [ -x "$1/lib/rviz2/rviz2" ] || [ -x "$1/bin/rviz2" ]; }
}

root_distro() {
  local env_dir="$1/share/ros_environment/environment" line
  if [ -f "$env_dir/1.ros_distro.dsv" ]; then
    line="$(grep -m1 '^set;ROS_DISTRO;' "$env_dir/1.ros_distro.dsv" || true)"
    [ -n "$line" ] && { echo "${line#set;ROS_DISTRO;}" | tr -d '[:space:]'; return; }
  fi
  if [ -f "$env_dir/1.ros_distro.sh" ]; then
    line="$(grep -m1 'ROS_DISTRO=' "$env_dir/1.ros_distro.sh" || true)"
    [ -n "$line" ] && { echo "${line#*ROS_DISTRO=}" | tr -d '"[:space:]'; return; }
  fi
}

# --- Prefix, ROS 2 root, distribution -------------------------------------------------
[ -z "$prefix" ] && prefix="${QAR_PREFIX:-$REPO_ROOT/install}"
prefix_abs=""
[ -d "$prefix" ] && prefix_abs="$(abspath "$prefix")"

root_from="--ros-root"
if [ -z "$ros_root" ] && [ -n "${ROS_DISTRO:-}" ] && [ -d "/opt/ros/$ROS_DISTRO" ]; then
  ros_root="/opt/ros/$ROS_DISTRO"; root_from="/opt/ros/\$ROS_DISTRO"
fi
if [ -z "$ros_root" ] && [ -n "${AMENT_PREFIX_PATH:-}" ]; then
  IFS=':' read -r -a entries <<< "$AMENT_PREFIX_PATH"
  for entry in "${entries[@]}"; do
    [ -d "$entry" ] || continue
    [ -n "$prefix_abs" ] && [ "$(abspath "$entry")" = "$prefix_abs" ] && continue
    if is_ros_root "$entry"; then ros_root="$entry"; root_from="AMENT_PREFIX_PATH"; break; fi
  done
fi
root_abs=""
[ -n "$ros_root" ] && [ -d "$ros_root" ] && root_abs="$(abspath "$ros_root")"

rdistro=""
[ -n "$root_abs" ] && rdistro="$(root_distro "$root_abs")"
if [ -z "$distro" ]; then
  distro="${rdistro:-${ROS_DISTRO:-}}"
elif [ -n "$rdistro" ] && [ "$rdistro" != "$distro" ]; then
  die "--distro $distro does not match the ROS 2 root $root_abs, which is $rdistro."
fi
[ -n "$distro" ] || die "cannot tell the ROS 2 distribution: pass --distro NAME or --ros-root DIR, or source ROS 2."
distro="$(echo "$distro" | tr '[:upper:]' '[:lower:]')"
[[ "$distro" =~ ^[a-z][a-z0-9_]*$ ]] || die "invalid distribution name '$distro'."

config_dir="${XDG_CONFIG_HOME:-}"
if [ -z "$config_dir" ]; then
  [ -n "${HOME:-}" ] || die "neither XDG_CONFIG_HOME nor HOME is set."
  config_dir="$HOME/.config"
fi
registry="$config_dir/quaternar/ros-plugins.json"

# The JSON edit: reads the file, removes the record of (distro, platform, local), appends the new
# one unless unregistering, and replaces the file atomically. Values travel in the environment.
edit_registry() {
  QAR_MODE="$1" "$PYTHON" - "$registry" <<'PY'
import json, os, sys, tempfile
path = sys.argv[1]
mode = os.environ["QAR_MODE"]
distro, platform = os.environ["QAR_DISTRO"], os.environ["QAR_PLATFORM"]
doc = {"version": 1, "plugins": []}
if os.path.exists(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        text = f.read()
    if text.strip():
        try:
            doc = json.loads(text)
        except ValueError as e:
            sys.exit(f"ERROR: {path} is not valid JSON; fix or delete it, then run this script again. ({e})")
        if not isinstance(doc, dict):
            sys.exit(f"ERROR: {path} does not hold a JSON object; fix or delete it.")
        if "version" not in doc:
            sys.exit(f'ERROR: {path} has no "version" field; fix or delete it.')
        if doc["version"] != 1:
            sys.exit(f"ERROR: {path} has version {doc['version']}; this script writes version 1 "
                     "and leaves the file unchanged. Update qaros-ros-rviz.")
elif mode == "unregister":
    print(f"Nothing to remove: {path} does not exist.")
    sys.exit(0)
plugins = doc.get("plugins") or []
def same(r):
    return (isinstance(r, dict) and r.get("distro") == distro and r.get("platform") == platform
            and r.get("source") == "local")
others = [r for r in plugins if not same(r)]
replaced = len(others) != len(plugins)
if mode == "unregister":
    if not replaced:
        print(f"Nothing to remove: no local record for {distro} / {platform} in {path}.")
        sys.exit(0)
    doc["plugins"] = others
else:
    others.append({
        "distro": distro,
        "platform": platform,
        "source": "local",
        "ros_root": os.environ["QAR_ROS_ROOT"],
        "prefix": os.environ["QAR_PREFIX_ABS"],
        "plugin_version": os.environ["QAR_PLUGIN_VERSION"],
        "c_api_header_sha256": os.environ["QAR_HASH"],
        "registered_at": os.environ["QAR_TIME"],
    })
    doc["plugins"] = others
folder = os.path.dirname(path)
os.makedirs(folder, exist_ok=True)
fd, tmp = tempfile.mkstemp(prefix="ros-plugins.json.", suffix=".tmp", dir=folder)
try:
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
except BaseException:
    if os.path.exists(tmp):
        os.unlink(tmp)
    raise
if mode == "unregister":
    print(f"Removed the local record for {distro} / {platform} from {path}.")
else:
    print(f"{'Replaced' if replaced else 'Added'} the local record for {distro} / {platform} in {path}")
PY
}

export QAR_DISTRO="$distro" QAR_PLATFORM="$platform"

if [ "$unregister" = 1 ]; then
  edit_registry unregister
  exit 0
fi

# --- Validate ------------------------------------------------------------------------------
[ -n "$prefix_abs" ] || die "the install prefix does not exist: $prefix (pass --prefix DIR or set QAR_PREFIX)."
missing=()
for rel in "share/ament_index/resource_index/rviz_common__pluginlib__plugin/$PACKAGE" \
           "lib/lib$PACKAGE.so" \
           "share/$PACKAGE/plugin_description.xml" \
           "share/$PACKAGE/config/qar_streaming_dev.rviz"; do
  [ -f "$prefix_abs/$rel" ] || missing+=("$rel")
done
[ ${#missing[@]} -eq 0 ] || die "the prefix $prefix_abs does not hold an installed $PACKAGE plugin; missing: ${missing[*]}. Build and install it first (scripts/build_plugin.sh)."

[ -n "$ros_root" ] || die "ROS 2 root not found: pass --ros-root DIR or source the ROS 2 setup script."
[ -n "$root_abs" ] || die "the ROS 2 root does not exist: $ros_root (from $root_from)."
is_ros_root "$root_abs" || die "$root_abs (from $root_from) is not a ROS 2 root with RViz: it needs setup.bash or local_setup.bash, and lib/rviz2/rviz2 or bin/rviz2."

# --- Plugin version and C API header hash -----------------------------------------------------
# The full version the build installs (share/<package>/plugin_version, for example
# 0.2.1-dev.4+1a2b3c4d), else the X.Y.Z of package.xml (a build made before the stamp existed).
plugin_version=""
[ -f "$prefix_abs/share/$PACKAGE/plugin_version" ] &&
  plugin_version="$(head -n1 "$prefix_abs/share/$PACKAGE/plugin_version" | tr -d '[:space:]')"
if [ -z "$plugin_version" ]; then
  package_xml="$prefix_abs/share/$PACKAGE/package.xml"
  [ -f "$package_xml" ] || package_xml="$REPO_ROOT/$PACKAGE/package.xml"
  plugin_version="$(sed -n 's:.*<version>\([^<]*\)</version>.*:\1:p' "$package_xml" | head -n1 | tr -d '[:space:]')"
  [ -n "$plugin_version" ] || die "no <version> in $package_xml."
fi

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1 | tr -d '\\'
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
if [ -n "$header" ]; then
  [ -f "$header" ] || die "--c-api-header: $header does not exist."
  hash="$(sha256_of "$header")"; hash_from="$header"
elif [ -f "$prefix_abs/share/$PACKAGE/qar_streaming_h.sha256" ]; then
  hash="$(tr -d '[:space:]' < "$prefix_abs/share/$PACKAGE/qar_streaming_h.sha256")"
  hash_from="$prefix_abs/share/$PACKAGE/qar_streaming_h.sha256"
else
  h="$REPO_ROOT/external/qaros/qar-streaming-c/include/qar_streaming.h"
  [ -f "$h" ] || die "cannot find the QarOS C header: $h is missing (run 'git submodule update --init external/qaros') and the prefix has no qar_streaming_h.sha256; or pass --c-api-header."
  hash="$(sha256_of "$h")"
  hash_from="$h (the prefix has no qar_streaming_h.sha256; rebuild to record the header of the build)"
fi
hash="$(echo "$hash" | tr '[:upper:]' '[:lower:]')"
[[ "$hash" =~ ^[0-9a-f]{64}$ ]] || die "invalid SHA-256 '$hash' from $hash_from."

export QAR_ROS_ROOT="$root_abs" QAR_PREFIX_ABS="$prefix_abs" QAR_PLUGIN_VERSION="$plugin_version" \
  QAR_HASH="$hash" QAR_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
edit_registry register
echo "  ROS 2 root     : $root_abs"
echo "  prefix         : $prefix_abs"
echo "  plugin version : $plugin_version"
echo "  C API SHA-256  : $hash"
echo "                   (from $hash_from)"
