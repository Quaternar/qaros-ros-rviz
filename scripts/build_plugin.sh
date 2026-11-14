#!/usr/bin/env bash
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Builds the qar_rviz_streaming RViz2 panel plugin with colcon on Linux and installs it into an
# install prefix: the Linux counterpart of scripts/build_plugin.ps1. NOT VERIFIED YET: the plugin
# has not been built on Linux by hand; the release workflow (.github/workflows/release.yml) is the
# first build. See docs/INSTALL.md, section "Linux (unverified)".
#
# Needs a ROS 2 distribution with RViz2 (/opt/ros/<distro>, for example from the apt packages
# ros-<distro>-rviz2 and ros-<distro>-rviz-default-plugins), the Qt 5 development packages
# (qtbase5-dev, libqt5websockets5-dev), colcon and a C++17 compiler. The upstream libraries
# qar_ogre_adapter and qar_qt_streaming come either from their installed CMake packages below
# --upstream-prefix, or from source checkouts (--ogre-source / --qt-source), which are then built
# into the plugin. The QarOS headers come from the git submodule external/qaros.
#
# Usage:
#   scripts/build_plugin.sh [--prefix DIR] [--ros-root DIR] [--upstream-prefix DIR]
#                           [--ogre-source DIR] [--qt-source DIR] [--clean] [-- <cmake args>]
#
#   --prefix           install prefix (default: $QAR_PREFIX, else <repo>/install)
#   --ros-root         ROS 2 root to source when ROS 2 is not sourced (default: /opt/ros/$ROS_DISTRO)
#   --upstream-prefix  prefix with the installed upstream packages (default: the install prefix)
#   --ogre-source      source checkout of qaros-ogre (qar_ogre_adapter)
#   --qt-source        source checkout of qaros-qt (qar_qt_streaming)
#   --clean            delete <repo>/build/colcon first
#   MAKEFLAGS=-jN limits the parallel compile jobs.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
die() { echo "ERROR: $*" >&2; exit 1; }

prefix="${QAR_PREFIX:-$REPO_ROOT/install}" ros_root="" upstream="" ogre_src="" qt_src="" clean=0
extra=()
while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) prefix="$2"; shift 2 ;;
    --ros-root) ros_root="$2"; shift 2 ;;
    --upstream-prefix) upstream="$2"; shift 2 ;;
    --ogre-source) ogre_src="$2"; shift 2 ;;
    --qt-source) qt_src="$2"; shift 2 ;;
    --clean) clean=1; shift ;;
    --) shift; extra=("$@"); break ;;
    -h|--help) sed -n '/^# Usage:/,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown argument: $1 (see --help)" ;;
  esac
done

# --- ROS 2 environment -------------------------------------------------------------------
if [ -z "${AMENT_PREFIX_PATH:-}" ] || [ -n "$ros_root" ]; then
  [ -n "$ros_root" ] || ros_root="/opt/ros/${ROS_DISTRO:-}"
  [ -f "$ros_root/setup.bash" ] || die "ROS 2 is not sourced and $ros_root/setup.bash does not exist; pass --ros-root DIR."
  set +u; # shellcheck disable=SC1091
  source "$ros_root/setup.bash"; set -u
fi
[ -n "${ROS_DISTRO:-}" ] || die "ROS_DISTRO is not set after sourcing ROS 2."
command -v colcon >/dev/null 2>&1 || die "colcon not found (apt install python3-colcon-common-extensions)."
[ -f "$REPO_ROOT/external/qaros/qar-streaming-c/include/qar_streaming.h" ] ||
  die "the submodule external/qaros is missing: git submodule update --init external/qaros"

mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd -P)"
[ -n "$upstream" ] || upstream="$prefix"

cmake_args=(-DCMAKE_BUILD_TYPE=RelWithDebInfo "-DQAR_PREFIX=$upstream")
if [ -n "$ogre_src" ]; then cmake_args+=("-DQAR_OGRE_ADAPTER_SOURCE_DIR=$(cd "$ogre_src" && pwd -P)")
else [ -f "$upstream/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfig.cmake" ] ||
  die "qar_ogre_adapter is not installed in $upstream; install it or pass --ogre-source DIR."; fi
if [ -n "$qt_src" ]; then cmake_args+=("-DQAR_QT_STREAMING_SOURCE_DIR=$(cd "$qt_src" && pwd -P)")
else [ -f "$upstream/share/qar_qt_streaming/cmake/qar_qt_streamingConfig.cmake" ] ||
  die "qar_qt_streaming is not installed in $upstream; install it or pass --qt-source DIR."; fi
command -v ninja >/dev/null 2>&1 && cmake_args+=(-G Ninja)
cmake_args+=("${extra[@]+"${extra[@]}"}")

colcon_root="$REPO_ROOT/build/colcon"
[ "$clean" = 1 ] && rm -rf "$colcon_root"

echo "=== colcon build (RelWithDebInfo) ==="
echo "  ROS 2  : $ROS_DISTRO"
echo "  prefix : $prefix"
echo "  colcon : $colcon_root"
colcon --log-base "$colcon_root/log" build \
  --paths "$REPO_ROOT/qar_rviz_streaming" \
  --build-base "$colcon_root/build" \
  --install-base "$prefix" \
  --merge-install \
  --event-handlers console_direct+ \
  --cmake-args "${cmake_args[@]}" --no-warn-unused-cli

lib="$prefix/lib/libqar_rviz_streaming.so"
[ -f "$lib" ] || die "colcon finished but $lib is missing."
echo
echo "=== BUILD OK ==="
echo "Plugin library: $lib"
echo "Overlay (source it): $prefix/local_setup.bash"
echo
echo "To make this build available to QAROS (\"ROS 2 Rviz\" in the Visualizer), register it:"
echo "  $REPO_ROOT/scripts/register-qaros-plugin.sh --prefix \"$prefix\""
