#!/usr/bin/env bash
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Packages an installed qar_rviz_streaming plugin as a release asset: the Linux counterpart of
# scripts/package-release.ps1, with the same zip layout and manifest format (docs/CONFIGURATION.md,
# section "Release assets and manifest.json"). Writes
#   <out-dir>/qar_rviz_streaming-<version>-<distro>-<platform>.zip
#   <out-dir>/qar_rviz_streaming-<version>-<distro>-<platform>.json   (the manifest entry)
#   <out-dir>/manifest.json                                            (all entries in out-dir)
# The prefix is only read. Needs python3 (PYTHON overrides it) for the zip and the JSON.
#
# Usage:
#   scripts/package-release.sh --out-dir DIR [--version X.Y.Z] [--prefix DIR] [--distro NAME]
#                              [--platform linux-x64] [--qt-major 5] [--c-api-header FILE]
#                              [--min-qaros-version X.Y.Z]
#
#   --prefix        colcon install prefix that holds the plugin (default: $QAR_PREFIX)
#   --version       release version, the plugin's own version: X.Y.Z, X.Y.Z-rc.N or X.Y.Z-pr.N
#                   (a leading v is removed; default: the file VERSION of the repository)
#   --distro        ROS 2 distribution (default: $ROS_DISTRO)
#   --platform      linux-x64 (default for x86_64), linux-arm64, windows-x64
#   --qt-major      Qt major version of the build (default 5)
#   --c-api-header  qar_streaming.h of the build (default: the prefix's qar_streaming_h.sha256,
#                   else the header of the submodule external/qaros)
#   --min-qaros-version  QAROS version of the C API header of the build, the manifest's
#                   min_qaros_version (default: the prefix's qaros_version, else the file VERSION
#                   of the submodule external/qaros)
set -euo pipefail

PACKAGE=qar_rviz_streaming
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
die() { echo "ERROR: $*" >&2; exit 1; }

prefix="${QAR_PREFIX:-}" version="" out_dir="" distro="${ROS_DISTRO:-}" platform="" qt_major=5 header="" min_qaros=""
while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) prefix="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    --out-dir) out_dir="$2"; shift 2 ;;
    --distro) distro="$2"; shift 2 ;;
    --platform) platform="$2"; shift 2 ;;
    --qt-major) qt_major="$2"; shift 2 ;;
    --c-api-header) header="$2"; shift 2 ;;
    --min-qaros-version) min_qaros="$2"; shift 2 ;;
    -h|--help) sed -n '/^# Usage:/,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done
if [ -z "$version" ]; then
  [ -f "$REPO_ROOT/VERSION" ] || die "pass --version (no $REPO_ROOT/VERSION)."
  version="$(head -n1 "$REPO_ROOT/VERSION" | tr -d '[:space:]')"
fi
[ -n "$out_dir" ] || die "--out-dir is required."
[ -n "$prefix" ] && [ -d "$prefix" ] || die "the prefix does not exist: '$prefix' (pass --prefix DIR)."
[ -n "$distro" ] || die "pass --distro NAME or source ROS 2."
if [ -z "$platform" ]; then
  case "$(uname -m)" in x86_64|amd64) platform=linux-x64 ;; aarch64|arm64) platform=linux-arm64 ;; *) die "pass --platform" ;; esac
fi
case "$qt_major" in 5|6) ;; *) die "--qt-major must be 5 or 6." ;; esac

repo_header=""
[ -f "$REPO_ROOT/external/qaros/qar-streaming-c/include/qar_streaming.h" ] &&
  repo_header="$REPO_ROOT/external/qaros/qar-streaming-c/include/qar_streaming.h"
repo_qaros_version=""
[ -f "$REPO_ROOT/external/qaros/VERSION" ] &&
  repo_qaros_version="$(head -n1 "$REPO_ROOT/external/qaros/VERSION" | tr -d '[:space:]')"
mkdir -p "$out_dir"

QAR_PACKAGE="$PACKAGE" QAR_VERSION="$version" QAR_DISTRO="$distro" QAR_PLATFORM="$platform" \
QAR_QT_MAJOR="$qt_major" QAR_HEADER="$header" QAR_REPO_HEADER="$repo_header" \
QAR_MIN_QAROS="$min_qaros" QAR_REPO_QAROS_VERSION="$repo_qaros_version" \
"$PYTHON" - "$prefix" "$out_dir" "$REPO_ROOT/LICENSE" <<'PY'
import glob, hashlib, json, os, re, sys, zipfile
prefix, out_dir, license_file = (os.path.abspath(p) for p in sys.argv[1:4])
env = os.environ
pkg, platform = env["QAR_PACKAGE"], env["QAR_PLATFORM"]
version = env["QAR_VERSION"].strip()
if version.startswith("v"):
    version = version[1:]
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.+-]+)?", version):
    sys.exit(f"ERROR: --version '{version}' is not a version like 0.9.0 or 0.9.0-rc.1.")
distro = env["QAR_DISTRO"].strip().lower()

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

library = f"bin/{pkg}.dll" if platform.startswith("windows") else f"lib/lib{pkg}.so"
required = [library,
            f"share/ament_index/resource_index/packages/{pkg}",
            f"share/ament_index/resource_index/rviz_common__pluginlib__plugin/{pkg}",
            f"share/{pkg}/plugin_description.xml",
            f"share/{pkg}/package.xml",
            f"share/{pkg}/config/qar_streaming_dev.rviz"]
missing = [r for r in required if not os.path.isfile(os.path.join(prefix, r))]
if missing:
    sys.exit(f"ERROR: the prefix {prefix} does not hold an installed {pkg} for {platform}; missing: {', '.join(missing)}.")

entries = {}
def add(rel):
    src = os.path.join(prefix, rel)
    if os.path.isfile(src):
        entries[rel] = src
add(library)
index = os.path.join(prefix, "share/ament_index/resource_index")
for type_dir in sorted(os.listdir(index)):
    if type_dir != "parent_prefix_path":   # names folders of the build machine
        add(f"share/ament_index/resource_index/{type_dir}/{pkg}")
add(f"share/colcon-core/packages/{pkg}")
share = os.path.join(prefix, "share", pkg)
for dirpath, _, files in os.walk(share):
    for name in files:
        full = os.path.join(dirpath, name)
        entries["share/" + pkg + "/" + os.path.relpath(full, share).replace(os.sep, "/")] = full
for name in os.listdir(prefix):
    if name.startswith("local_setup.") or (name.startswith("_local_setup_util_") and name.endswith(".py")):
        add(name)

stamp = f"share/{pkg}/qar_streaming_h.sha256"
if env.get("QAR_HEADER"):
    c_hash, hash_from = sha256(env["QAR_HEADER"]), env["QAR_HEADER"]
elif stamp in entries:
    c_hash, hash_from = open(entries[stamp], encoding="utf-8").read().strip().lower(), entries[stamp]
elif env.get("QAR_REPO_HEADER"):
    c_hash, hash_from = sha256(env["QAR_REPO_HEADER"]), env["QAR_REPO_HEADER"] + " (the prefix has no qar_streaming_h.sha256)"
else:
    sys.exit("ERROR: no qar_streaming_h.sha256 in the prefix and no external/qaros header; pass --c-api-header.")
if not re.fullmatch(r"[0-9a-f]{64}", c_hash):
    sys.exit(f"ERROR: invalid SHA-256 '{c_hash}' from {hash_from}.")
entries.pop(stamp, None)
generated = {stamp: c_hash + "\n"}
entries.setdefault(f"share/{pkg}/LICENSE", license_file)

qaros_stamp = f"share/{pkg}/qaros_version"
if env.get("QAR_MIN_QAROS"):
    min_qaros, qaros_from = env["QAR_MIN_QAROS"].strip(), "--min-qaros-version"
elif qaros_stamp in entries:
    min_qaros, qaros_from = open(entries[qaros_stamp], encoding="utf-8").read().strip(), entries[qaros_stamp]
elif env.get("QAR_REPO_QAROS_VERSION"):
    min_qaros, qaros_from = env["QAR_REPO_QAROS_VERSION"], "external/qaros/VERSION (the prefix has no qaros_version)"
else:
    sys.exit("ERROR: no qaros_version in the prefix and no external/qaros/VERSION; pass --min-qaros-version.")
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", min_qaros):
    sys.exit(f"ERROR: invalid QAROS version '{min_qaros}' from {qaros_from}.")
entries.pop(qaros_stamp, None)
generated[qaros_stamp] = min_qaros + "\n"

# The plugin version of the release is the release version; the build's package.xml should agree.
plugin_version = version
xml = open(os.path.join(prefix, "share", pkg, "package.xml"), encoding="utf-8").read()
m = re.search(r"<version>\s*([^<\s]+)\s*</version>", xml)
package_version = m.group(1) if m else ""
if package_version != re.split(r"[-+]", version)[0]:
    print(f"WARNING: the prefix holds {pkg} {package_version} (package.xml), the release version is {version}.",
          file=sys.stderr)
version_stamp = f"share/{pkg}/plugin_version"
entries.pop(version_stamp, None)
generated[version_stamp] = plugin_version + "\n"

base = f"{pkg}-{version}-{distro}-{platform}"
zip_path = os.path.join(out_dir, base + ".zip")
tmp = zip_path + ".tmp"
with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED) as z:
    for name in sorted(set(entries) | set(generated)):
        if name in generated:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, generated[name])
        else:
            z.write(entries[name], name)   # keeps the mode bits (the .so and the scripts)
os.replace(tmp, zip_path)

asset = {"name": base + ".zip", "sha256": sha256(zip_path), "size": os.path.getsize(zip_path),
         "distro": distro, "platform": platform, "qt_major": int(env["QAR_QT_MAJOR"]),
         "plugin_version": plugin_version, "c_api_header_sha256": c_hash, "min_qaros_version": min_qaros}
with open(os.path.join(out_dir, base + ".json"), "w", encoding="utf-8", newline="\n") as f:
    json.dump(asset, f, indent=2); f.write("\n")
assets = [json.load(open(p, encoding="utf-8-sig"))
          for p in sorted(glob.glob(os.path.join(out_dir, f"{pkg}-{version}-*.json")))]
manifest = {"version": 1, "release": version, "plugin_version": version, "package": pkg, "assets": assets}
with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
    json.dump(manifest, f, indent=2); f.write("\n")
print(f"Packaged {len(set(entries) | set(generated))} files from {prefix}")
print(f"  zip      : {zip_path}")
print(f"  sha256   : {asset['sha256']}")
print(f"  version  : {plugin_version}")
print(f"  C API    : {c_hash} (from {hash_from})")
print(f"  QAROS    : {min_qaros} or newer (from {qaros_from})")
print(f"  manifest : {os.path.join(out_dir, 'manifest.json')}")
PY
