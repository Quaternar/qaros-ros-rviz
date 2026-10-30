# Installation

## Overview

This document installs everything that you need to build, test and run qar_rviz_plugin on a Windows 11 machine. It goes in this order: the machine and its toolchain; the folders and the environment variables that every command uses; the QarOS SDK; a clone of this repository with the Python environment; the ROS 2 Humble distribution and its repairs; Qt; the two repositories that this one builds on, `qar_ogre_adapter` and `qar_qt_streaming`; and finally this repository, whose libraries, examples and units you build and test, and whose plugin you build and install into the same prefix, and then check with a few commands. Every command runs in PowerShell 7 and uses only the environment variables of the layout, so a different layout needs only different values for the variables.

You need QAROS installed to open a page as a URL panel in QAROS, which the examples do when they join the QAROS Hub; they load the installed QAROS library when they run.

qar_rviz_plugin is the one repository of the stack that needs the whole chain. The plugin is a panel that RViz loads, so it is built against the RViz2, `rclcpp` and Ogre of the ROS 2 Humble distribution, and the distribution needs all nine repairs of [Repairs to the ROS 2 Windows distribution](#repairs-to-the-ros-2-windows-distribution). It links the libraries of the two upstream repositories, which it finds as installed CMake packages below `QAR_PREFIX`, and the three web libraries of this repository, which it builds itself, and it uses Qt 5.15.2 with the modules Core, Gui and Widgets, and Network and WebSockets through the web panel library. The plugin is an ament package, which ROS 2 builds with `colcon`, so the build also needs the Python environment of this document and `ninja`.

QAROS is not needed to build or test. You need it to run: the plugin loads the installed QAROS library by its full path when you press `"Connect to QarOS"`, and the launcher `scripts\run_live_rviz_stream_real.ps1` puts nothing on `PATH`. The installation that the registry names is used; `QAR_SDK_ROOT` names the same folder for the configure checks and for the launcher, which reads the registry first.

When you are done, the plugin is installed below `QAR_PREFIX` in the layout that `colcon` writes with its merged install, next to the packages of the two upstream repositories, and the libraries, the examples and the tests of this repository are built in its build folder. The command below shows how much disk space each folder uses; run it after the last step.

```powershell
foreach ($folder in $env:QAR_DEPS_ROOT, $env:QAR_VENV, "$env:QAR_ROOT\src", $env:QAR_PREFIX, $env:QAR_SDK_ROOT) {
    $bytes = (Get-ChildItem -LiteralPath $folder -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
    '{0}  {1:N2} GiB' -f $folder, ($bytes / 1GB)
}
```

```text
C:\qar\deps  1.82 GiB
C:\qar\venv  0.24 GiB
C:\qar\src  0.90 GiB
C:\qar\install  0.03 GiB
C:\Program Files\Quaternar\QAROS  0.64 GiB
```

## Machine prerequisites

The installation was developed and run on Windows 11 Pro, 64-bit. Check the machine before you install anything: the commands below only read the system.

```powershell
$os = Get-CimInstance Win32_OperatingSystem
'{0}, version {1}, 64-bit: {2}' -f $os.Caption, $os.Version, [Environment]::Is64BitOperatingSystem
```
```text
Microsoft Windows 11 Pro, version 10.0.26200, 64-bit: True
```

| Requirement | What this stack needs | Why |
|---|---|---|
| Operating system | Windows 11, 64-bit. Other Windows versions and Linux are not built or tested. | The toolchain, the ROS 2 distribution and the Qt kit of this documentation are the Windows 64-bit builds. |
| Desktop session | An interactive desktop on the local console of the machine. Remote Desktop and services are not covered. | RViz and the QarOS Visualizer open OpenGL windows, and the documented runs were made from the local console. |
| Graphics | A GPU with a current OpenGL driver; the reference machine has an NVIDIA GPU. This stack states no minimum GPU or driver version. | Ogre renders with OpenGL, and QarOS encodes the frames on the GPU. |
| Administrator rights | Only for the installers of the toolchain. The scripts of this stack and the commands of this documentation run in a normal, non-elevated PowerShell. | The scripts write below the folders you pass them, to the ROS 2 distribution and, for the repairs of the distribution, to `C:\pixi_ws`. |
| Paths | A short root folder: this documentation uses `C:\qar`. | Windows limits a path to 260 characters unless long paths are enabled, and a deep clone path overflows that limit. |

Check the two Windows settings that matter for the layout. The first value is the long-path policy: `0` means long paths are off, which is the Windows default. This stack does not change it and does not need it, because the layout above is short.

```powershell
(Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled).LongPathsEnabled
```
```text
0
```

The second command lists the graphics adapters Windows sees. On a laptop with an integrated and a discrete adapter, Windows may run an application on the integrated one, while QarOS encodes on the NVIDIA GPU; frames then cross between the adapters.

```powershell
Get-CimInstance Win32_VideoController | ForEach-Object { '{0}, driver {1}' -f $_.Name, $_.DriverVersion }
```
```text
Intel(R) UHD Graphics, driver 31.0.101.5187
NVIDIA GeForce RTX 4080 Laptop GPU, driver 32.0.15.7657
```

The live launcher `scripts\run_live_rviz_stream_real.ps1` handles this case for RViz: with `-Confirm` it writes the Windows graphics preference of `rviz2.exe` (high performance) for the current user. Without `-Confirm` it only reads the value. [Removing everything again](#removing-everything-again) lists the value as a persistent change.

Plan the disk space before you start. The layout of this documentation puts the dependencies, the virtual environment, the clones, the install prefix and the SDK on one drive; the [Overview](#overview) has a command that measures them, so run that command after the last step of the chain to see what your machine used.

## Toolchain

The stack builds with six tools. Install them in the order of the table; every tool has an install command and a verify command, and the verify commands are the ones this documentation was run with.

| Tool | Version | Used for |
|---|---|---|
| Visual Studio 2019 Build Tools | MSVC v142 (compiler 19.29.30151.0) with a Windows 10 SDK | compiling and linking every C++ module |
| CMake | 3.22 or newer | configuring and building; the presets of every repository need it |
| Git | any recent version | cloning the repositories |
| PowerShell 7 | 7.x (`pwsh`) | running every command and script of this documentation |
| Python | 3.10.11 (any 3.10.x) | the virtual environment, `aqt`, the layering guard and the test helpers |
| Ninja | any recent version | the `colcon` build of the plugin only |
| Microsoft Visual C++ Redistributable (x64) | 14.40 or newer | the C++ runtime that the repair of the ROS 2 distribution puts next to `rviz2.exe`, so that the QarOS library runs in RViz |

The last row is no build tool but a package of the run time, and it has an install command only: the repair that uses it prints the version of the runtime that it finds.

All install commands use `winget`, the Windows package manager that ships with Windows 11 as part of the App Installer. They change the machine (they install software system-wide), so run them in an elevated PowerShell 7 and open a new window afterwards, so that the new `PATH` is read.

### Why Visual Studio 2019 and the v142 toolset

The ROS 2 Humble distribution of this documentation and the Qt 5.15.2 kit `msvc2019_64` are compiled with the Visual Studio 2019 toolset (v142). C++ classes cross DLL boundaries in this stack, between our code and Ogre, RViz and Qt, and the compiled layout of those classes and the C++ runtime they use must match the toolset that built the binaries. The stack therefore pins the toolset that built them instead of trying a newer one: the presets select the generator `Visual Studio 16 2019`, and the scripts find only a Visual Studio 16.x installation that has the C++ x64 tools. Visual Studio 2022 or 2026 may be installed next to it; they are ignored. Always build `RelWithDebInfo` or `Release`: the ROS 2 and Qt binaries are release builds, and a debug C runtime in the same process crashes on load.

### Visual Studio 2019 Build Tools

Install the workload `Microsoft.VisualStudio.Workload.VCTools` (the C++ build tools) together with the components `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (the MSVC v142 compiler and linker) and `Microsoft.VisualStudio.Component.Windows10SDK.19041` (the Windows 10 SDK 10.0.19041). The full Visual Studio 2019 IDE with the same workload works as well.

```powershell
winget install --id Microsoft.VisualStudio.2019.BuildTools --source winget --accept-package-agreements --accept-source-agreements --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK.19041"
```

> Success: `winget` finishes with exit code 0 once the installer has added the C++ build tools workload, the MSVC compiler and linker and the Windows 10 SDK. The options of the command make the installer wait until it is done, work without a window and not restart the machine. The verify command below shows the installation.
>
> This command changes your machine, so no output is shown here.

The scripts of this stack locate the installation with `vswhere`, a tool that the Visual Studio installer puts in a fixed folder. The verify command below runs the same query as `scripts\qar_locations.ps1`, then reads the toolset folder and the compiler version from that installation. The toolset folder starts with `14.2` for v142.

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -version '[16.0,17.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest
$vs
$toolset = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Name | Select-Object -Last 1
$toolset
(Get-Item (Join-Path $vs "VC\Tools\MSVC\$toolset\bin\Hostx64\x64\cl.exe")).VersionInfo.FileVersion
```
```text
C:\Program Files (x86)\Microsoft Visual Studio\2019\Community
14.29.30133
19.29.30151.0 built by: cloudtest
```

The first line is the installation folder (its last part names the edition), the second the toolset folder, the third the compiler version followed by a build annotation. The reference build used the compiler 19.29.30151.0; a later 16.11 servicing release of the same toolset is the same toolset family. If the first command prints nothing, the C++ x64 tools are not installed, and the scripts stop with an error that points here.

### CMake

The presets require CMake 3.22 or newer.

The runs that this documentation shows used CMake 3.29.5 as Visual Studio 2022 version 17.11 bundles it, which prints `cmake version 3.29.5-msvc4`; the compiler of those runs was still the toolset v142 of Visual Studio 2019. Every output of this documentation that shows a version of CMake comes from that build. No run used the build of Kitware that the install command below fetches, and none used a CMake 4 release, so nobody has checked whether a CMake 4 release configures this stack. The command therefore installs the Kitware build of the version number of the recorded runs, 3.29.5, which `winget` lists. A Kitware build and a Microsoft build of one version number are two builds, and this documentation does not claim that they behave alike.

```powershell
winget install --id Kitware.CMake --version 3.29.5 --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and CMake 3.29.5 is installed. In a new window the verify command below prints the version of the CMake that comes first on `PATH`.
>
> This command changes your machine, so no output is shown here.

```powershell
cmake --version
```
```text
cmake version 3.29.5-msvc4
...
```

### Git

Git clones the repositories and is the only tool of this chain that talks to a Git host. The CMake configure step of every repository downloads GoogleTest as an archive and does not need Git for it.

```powershell
winget install --id Git.Git --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and Git is installed. In a new window the verify command below prints the version of Git.
>
> This command changes your machine, so no output is shown here.

```powershell
git --version
```
```text
git version 2.39.2.windows.1
```

### PowerShell 7

Every command of this documentation was run in PowerShell 7 (`pwsh`), started without a profile, and that is the shell the commands are written for. Windows PowerShell 5.1 (`powershell.exe`) is not used by the documented commands. The helper file `scripts\qar_locations.ps1` states that it works in both editions.

```powershell
winget install --id Microsoft.PowerShell --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and PowerShell 7 is installed. In a new PowerShell 7 window (`pwsh`) the verify command below prints a version that starts with 7.
>
> This command changes your machine, so no output is shown here.

```powershell
$PSVersionTable.PSVersion.ToString()
```
```text
7.4.4
```

### Python 3.10

The compiled Python modules of the ROS 2 Humble Windows distribution carry the `cp310` ABI tag, so a Python 3.10 interpreter must be the one that runs them; the distribution ships no interpreter of its own. Install the machine-wide interpreter; it lands in the folder Python310 below Program Files.

```powershell
winget install Python.Python.3.10 --scope machine --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and the machine-wide Python 3.10 is installed in the folder Python310 below Program Files. The verify command below calls that interpreter by its full path and prints its version, which starts with 3.10.
>
> This command changes your machine, so no output is shown here.

The verify command calls the interpreter by its full path, because other Python versions may come first on `PATH`. The next section creates the virtual environment from this interpreter.

```powershell
& "$env:ProgramFiles\Python310\python.exe" --version
```
```text
Python 3.10.11
```

### Ninja

The plugin is built with `colcon`, which uses the Ninja generator. `scripts\build_plugin.ps1` imports the Visual Studio 2019 environment and then needs `ninja` on `PATH`; it stops with a message that says so when it is missing.

```powershell
winget install --id Ninja-build.Ninja --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` finishes with exit code 0 and Ninja is installed. In a new window the verify command below prints the version of Ninja.
>
> This command changes your machine, so no output is shown here.

```powershell
ninja --version
```
```text
1.11.0
```
### Microsoft Visual C++ Redistributable

The QarOS library needs a Microsoft C++ runtime of version 14.40 or newer, and RViz would run it on the older runtime that the ROS 2 distribution ships next to `rviz2.exe`. The script `scripts\fix_old_vc_runtime.ps1` of [Repairs to the ROS 2 Windows distribution](#repairs-to-the-ros-2-windows-distribution) replaces that runtime with the one of the system folder of Windows, so the machine needs the current redistributable for x64, and it needs it even when some other program has installed an older one: the repair wants 14.40 or newer in the system folder.

```powershell
winget install --id Microsoft.VCRedist.2015+.x64 --source winget --accept-package-agreements --accept-source-agreements
```

> Success: `winget` installs the current Microsoft Visual C++ Redistributable for x64, or it reports that the package is installed already and that no newer version is available; either way the machine is ready. The repair of the ROS 2 distribution prints the version of the runtime that it takes from the system folder, which must be 14.40 or newer.
>
> This command changes your machine, so no output is shown here.

### Execution policy and downloaded files

The scripts of this stack are not signed. If the execution policy of your machine blocks local scripts, start the script through `pwsh` and set its `-ExecutionPolicy` option to `Bypass`, which applies to that one process only. Windows marks files that were downloaded from the internet (for example a repository or the SDK saved as a zip file by a browser); the policy `RemoteSigned` refuses an unsigned script that carries the mark, and `Unblock-File` removes it.

The command prints the policy stored for your user and for the machine; `Undefined` means that scope sets nothing.

```powershell
foreach ($scope in 'CurrentUser', 'LocalMachine') { '{0,-13} {1}' -f $scope, (Get-ExecutionPolicy -Scope $scope) }
```
```text
CurrentUser   Undefined
LocalMachine  RemoteSigned
```

## Directory layout and environment variables

This documentation uses one short folder layout and refers to every folder through an environment variable. A different layout needs only different values for the variables, never different commands.

```text
C:\qar                                  QAR_ROOT, the root of the layout
C:\qar\deps                             QAR_DEPS_ROOT: downloads and dependencies
C:\qar\deps\downloads                   archives that the scripts download
C:\qar\deps\ros2_humble\ros2-windows    ROS2_INSTALL_ROOT: the ROS 2 Humble distribution
C:\qar\deps\Qt\5.15.2\msvc2019_64       QAR_QT_ROOT: Qt 5.15.2
C:\qar\venv                             QAR_VENV: the Python virtual environment
C:\qar\src\<repository>                 the clones of the repositories
C:\qar\install                          QAR_PREFIX: the install prefix of the repositories
C:\Program Files\Quaternar\QAROS   QAR_SDK_ROOT: the install folder of QAROS (made by its setup program, outside the layout root)
```

| Variable | Meaning | Value in this documentation |
|---|---|---|
| `QAR_ROOT` | Root of the layout. Documented commands use it to name the clone and working folders; no script and no CMake file reads it. | `C:\qar` |
| `QAR_DEPS_ROOT` | Dependency root: the downloaded archives and the build folders of the distribution repairs. | `C:\qar\deps` |
| `ROS2_INSTALL_ROOT` | The ROS 2 Humble Windows distribution, the folder that holds `local_setup.bat`. | `C:\qar\deps\ros2_humble\ros2-windows` |
| `QAR_QT_ROOT` | The Qt 5.15.2 kit `msvc2019_64`, the folder that holds `bin\Qt5Core.dll`. | `C:\qar\deps\Qt\5.15.2\msvc2019_64` |
| `QAR_VENV` | The Python 3.10 virtual environment, the folder that holds `Scripts\python.exe`. | `C:\qar\venv` |
| `QAR_PREFIX` | The install prefix: the repositories of this stack install into it, and the others find them there. | `C:\qar\install` |
| `QAR_SDK_ROOT` | The install folder of QAROS, the folder that holds `bin` and `include`; set it to the `InstallFolder` that the registry names, because CMake uses it only to check that the library exists. | `C:\Program Files\Quaternar\QAROS` |

The outputs shown in this documentation were recorded in another folder layout on a machine of the same kind; the folder names in them were replaced by the names of the layout above.

Every script of this stack that needs one of these folders takes it from its parameter first and from the environment variable second. When both are missing, the script stops with an error that names the parameter, the variable and the section of this document that explains them, in the form `not found: pass -<Parameter> <folder> or set the environment variable <EnvVar>`. When the folder does not exist, or lacks the file that identifies it, the error says so. CMake follows the same order for the locations it needs: a `-D` option wins over the environment variable.

### Create the folders and set the variables

The block below creates the folders and sets the seven variables for your Windows user, so that every new shell has them, and for the current shell, so that you can continue at once. It changes your user environment; [Removing everything again](#removing-everything-again) shows how to undo it. If you chose other folders, change the values at the top.

```powershell
$layout = [ordered]@{
    QAR_ROOT          = 'C:\qar'
    QAR_DEPS_ROOT     = 'C:\qar\deps'
    ROS2_INSTALL_ROOT = 'C:\qar\deps\ros2_humble\ros2-windows'
    QAR_QT_ROOT       = 'C:\qar\deps\Qt\5.15.2\msvc2019_64'
    QAR_VENV          = 'C:\qar\venv'
    QAR_PREFIX        = 'C:\qar\install'
    QAR_SDK_ROOT      = 'C:\Program Files\Quaternar\QAROS'
}
foreach ($name in $layout.Keys) {
    [Environment]::SetEnvironmentVariable($name, $layout[$name], 'User')
    Set-Item -Path "Env:$name" -Value $layout[$name]
}
New-Item -ItemType Directory -Force -Path $layout.QAR_ROOT, $layout.QAR_DEPS_ROOT, $layout.QAR_PREFIX | Out-Null
```

> Success: the block prints nothing and ends without an error. Afterwards the seven variables are set for your Windows user and in the current window, and these folders exist: `C:\qar`, `C:\qar\deps` and `C:\qar\install`. The folder of QAROS is made by its setup program, not by this block. The check below lists the variables.
>
> This command changes your machine, so no output is shown here.

A variable that is set for your user is visible to programs you start afterwards, not to windows that were already open: open a new PowerShell 7 window, or set the variable in the current window as the block does.

Check the result. Every variable must be set, and the folders you created must exist; the folders of the dependencies appear later in the chain.

```powershell
foreach ($name in 'QAR_ROOT', 'QAR_DEPS_ROOT', 'ROS2_INSTALL_ROOT', 'QAR_QT_ROOT', 'QAR_VENV', 'QAR_PREFIX', 'QAR_SDK_ROOT') {
    '{0,-18} {1}' -f $name, [Environment]::GetEnvironmentVariable($name)
}
```
```text
QAR_ROOT           C:\qar
QAR_DEPS_ROOT      C:\qar\deps
ROS2_INSTALL_ROOT  C:\qar\deps\ros2_humble\ros2-windows
QAR_QT_ROOT        C:\qar\deps\Qt\5.15.2\msvc2019_64
QAR_VENV           C:\qar\venv
QAR_PREFIX         C:\qar\install
QAR_SDK_ROOT       C:\Program Files\Quaternar\QAROS
```

Every command of this documentation that clones, builds or runs something uses only these variables, and a shell that has them needs nothing else.

## The QarOS SDK

QAROS is QuaternAR's proprietary runtime and is not part of this stack. You obtain its setup program from QuaternAR and install it once on the PC, for all users. The setup program records the installed version and the install folder in the registry value `Version` and the registry value `InstallFolder` of the key `HKLM\SOFTWARE\Quaternar\QAROS`. The QAROS version that this stack was verified against is in [SDK compatibility](SDK_COMPATIBILITY.md).

You do not copy any part of QAROS into a layout folder or into a repository. A program that uses QAROS loads the installed library `bin\qar-streaming-c.dll` from the install folder, and the runtime finds its launcher and its other programs next to that library.

The first command reads the version and the install folder from the registry, checks that the folder holds the C library, the runtime launcher and the Visualizer, and prints the SHA256 of the C header `include\qar_streaming.h` of the installation. [Identifying the QAROS version](SDK_COMPATIBILITY.md#identifying-the-qaros-version) explains what to compare the hash with. The command changes nothing.

```powershell
$qaros = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Quaternar\QAROS'
'Version        {0}' -f $qaros.Version
'InstallFolder  {0}' -f $qaros.InstallFolder
foreach ($file in 'bin\qar-streaming-c.dll', 'bin\qar-runtime-launcher.exe', 'bin\qar-streaming-viz.exe', 'include\qar_streaming.h') {
    '{0,-30} {1}' -f $file, (Test-Path -LiteralPath (Join-Path $qaros.InstallFolder $file))
}
(Get-FileHash -LiteralPath (Join-Path $qaros.InstallFolder 'include\qar_streaming.h') -Algorithm SHA256).Hash
```
```text
Version        0.9.0-dev.3008+65dc9aeb
InstallFolder  C:\Program Files\Quaternar\QAROS
bin\qar-streaming-c.dll        True
bin\qar-runtime-launcher.exe   True
bin\qar-streaming-viz.exe      True
include\qar_streaming.h        True
55E6E3F48D940CA29331F0528FE475C1B7230FB19A59C31D5525C6E90B097F6C
```

### How the stack finds QAROS

A program calls `qar_library_load` with no path. The C header documents the order in which that call looks for the library and takes the first hit: a library that is already loaded in the process; `qar-streaming-c.dll` beside the module that calls it; the folder that the environment variable `QAROS_RUNTIME_DIR` names; the install folder of the installed QAROS; a folder on `PATH`. A library found beside the program is loaded before the installed one, which is why a program must not ship its own copy. Each candidate is loaded with its dependencies taken from its own folder and from `System32` only.

The runtime initialisation leaves the field `runtime_binaries_folder_path` empty: the runtime then uses the folder that the loaded library came from, which is the `bin` folder of the installation and holds the launcher.

Nothing in this stack links against QAROS. Configuring this repository does not read `QAR_SDK_ROOT`: only the configure step of `qar_ogre_adapter` checks that the DLL exists below the folder that the variable names, and it prints a warning and continues when the variable is not set or the DLL is missing. A build and the whole test suite therefore work without QAROS; only the programs that connect to QAROS need the installation at run time.

How a program joins the QAROS Hub is described by QuaternAR: [Onboarding and sessions](https://docs.quaternar.com/docs/next/developer-guide/onboarding-and-sessions/) and [Source applications](https://docs.quaternar.com/docs/next/operator-guide/source-applications/). A program that runs on the PC of the Hub joins as a local source app, and the Hub asks its operator once to allow it; a Hub on another PC needs a pairing code.

## Python environment

### Clone this repository

The next steps use the scripts and the `requirements.txt` of this repository, so clone it first. The command takes the default branch of the repository. This documentation describes version 0.1.0 of the three repositories of this stack; the version of a clone is the number after the word VERSION in the project command of the file `CMakeLists.txt` in its root.

```powershell
git clone https://github.com/Quaternar/qaros-ros-rviz "$env:QAR_ROOT\src\qar_rviz_plugin"
```
```text
Cloning into 'C:\qar\src\qar_rviz_plugin'...
done.
```

Go to the folder of the clone; the next steps run there:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

### Create the virtual environment

The virtual environment holds the Python tools of the build in one folder that you can delete. It is created from the Python 3.10 interpreter of the toolchain section and filled from the pinned list `requirements.txt` of the repository. The list contains, among other packages, `aqtinstall`, which installs Qt, and `colcon-common-extensions`, which builds ROS 2 packages; every repository ships the same list.
The plugin build needs the environment in addition: the Python helpers of the ROS 2 build system need `catkin_pkg`, `lark` and `empy`, which the system Python lacks, so `scripts\build_plugin.ps1` takes the interpreter from `QAR_VENV`.

```powershell
& "$env:ProgramFiles\Python310\python.exe" -m venv $env:QAR_VENV
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
```
```text
...
Successfully installed PyYAML-6.0.3 Pygments-2.20.0 aqtinstall-3.3.0 backports.zstd-1.6.0 beautifulsoup4-4.15.0 brotli-1.2.0 bs4-0.0.2 catkin-pkg-1.1.0 certifi-2026.6.17 charset-normalizer-3.4.9 colcon-cmake-0.2.30 colcon-common-extensions-0.3.0 colcon-core-0.21.0 colcon-defaults-0.2.9 colcon-devtools-0.3.0 colcon-library-path-0.2.1 colcon-metadata-0.2.5 colcon-notification-0.3.1 colcon-output-0.2.14 colcon-package-information-0.4.1 colcon-package-selection-0.2.10 colcon-parallel-executor-0.4.0 colcon-pkg-config-0.1.0 colcon-powershell-0.5.0 colcon-python-setup-py-0.2.9 colcon-recursive-crawl-0.2.3 colcon-ros-0.5.0 colcon-test-result-0.3.8 colorama-0.4.6 coloredlogs-15.0.1 coverage-7.15.0 defusedxml-0.7.1 distlib-0.4.3 docutils-0.23 empy-4.2.1 exceptiongroup-1.3.1 humanfriendly-10.0 humanize-4.16.0 idna-3.18 inflate64-1.0.4 iniconfig-2.3.0 lark-1.3.1 multivolumefile-0.2.3 netifaces-0.11.0 numpy-2.2.6 opencv-python-5.0.0.93 packaging-26.2 patch-ng-1.19.1 pluggy-1.6.0 psutil-7.2.2 py7zr-1.1.3 pybcj-1.0.7 pycryptodomex-3.23.0 pyparsing-3.3.2 pyppmd-1.3.1 pyreadline3-3.5.6 pytest-9.1.1 pytest-cov-7.1.0 pytest-repeat-0.9.4 pytest-rerunfailures-16.4 python-dateutil-2.9.0.post0 pywin32-312 requests-2.34.2 semantic-version-2.10.0 six-1.17.0 soupsieve-2.8.4 texttable-1.7.0 tomli-2.4.1 typing_extensions-4.16.0 urllib3-2.7.0 vcstool-0.3.0
```

Check that the two tools you will use are installed at the pinned versions.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip list --format=freeze | Select-String -Pattern '^(aqtinstall|colcon-common-extensions)=='
```
```text
aqtinstall==3.3.0
colcon-common-extensions==0.3.0
...
```

## ROS 2 Humble for Windows

This repository is a plugin of RViz2, so it needs the RViz2 of this distribution, its `rclcpp` and the Ogre 1.12.1 that RViz links. The plugin is built against exactly these binaries and loaded by the `rviz2.exe` of the distribution, and the library `qar_ros_ui` links the same `rclcpp` to read topics and to publish from AR panels.

The stack uses the official ROS 2 Humble binary release for Windows, tag `release-humble-20260220`, as it is: the file `ros2-humble-20260220-windows-release-amd64.zip` (395014297 bytes, SHA256 `256ABCFA37F8D97D2CB3FF2A85F53465B016A25632399C99F67DC2BEE18E89C3`) from the ROS 2 releases on GitHub. The script `scripts\fetch_dependencies.ps1` downloads it into `$env:QAR_DEPS_ROOT\downloads`, checks the SHA256, and unpacks it into `$env:QAR_DEPS_ROOT\ros2_humble`; the distribution then lives in the folder `ros2-windows` below that, which is the value of `ROS2_INSTALL_ROOT`. The script is safe to run again: it skips a download whose hash matches and an extraction into a populated folder, unless you pass `-Force`. It also downloads the reference source manifest of the release (`ros2-release-humble-20260220-src.zip`), which nothing in this stack uses.

The download needs access to `github.com`. If your network inspects TLS traffic and the download fails with a certificate error, see the troubleshooting document before you continue.

```powershell
.\scripts\fetch_dependencies.ps1
```
```text
=== ROS 2 Humble Windows binary distro ===
Downloading https://github.com/ros2/ros2/releases/download/release-humble-20260220/ros2-humble-20260220-windows-release-amd64.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-humble-20260220-windows-release-amd64.zip
Extracting to C:\qar\deps\ros2_humble

=== ROS 2 source manifest repo (reference only) ===
Downloading https://github.com/ros2/ros2/archive/refs/tags/release-humble-20260220.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-release-humble-20260220-src.zip
Extracting to C:\qar\deps\ros2_src_release-humble-20260220

Done. Binary distro: C:\qar\deps\ros2_humble\ros2-windows
Reminder: the binary distro has packaging gaps - run the repair scripts next:
fix_missing_spdlog_dll.ps1, fix_missing_vendor_dlls.ps1, fix_missing_openssl_dll.ps1, fix_qt_conda_imports.ps1.
```

Check that the distribution is where `ROS2_INSTALL_ROOT` points and that it contains the Ogre 1.12.1 of the upstream release.

```powershell
Test-Path -LiteralPath (Join-Path $env:ROS2_INSTALL_ROOT 'local_setup.bat')
(Select-String -LiteralPath (Join-Path $env:ROS2_INSTALL_ROOT 'opt\rviz_ogre_vendor\CMake\OGREConfigVersion.cmake') -Pattern 'set\(PACKAGE_VERSION ').Line.Trim()
```
```text
True
set(PACKAGE_VERSION "1.12.1")
set(PACKAGE_VERSION "${PACKAGE_VERSION} (${installedBits}bit)")
```

The distribution has gaps that matter for what you build; the next section repairs them.

## Repairs to the ROS 2 Windows distribution

The official ROS 2 Humble binary release for Windows is built on a CI machine and has gaps that show up as soon as you run its programs or build against it: some runtime DLLs are missing, some CMake configuration files are missing, the Qt DLL names differ from a normal Qt, the exported CMake targets name paths of the CI machine, and the C++ runtime that it ships next to `rviz2.exe` is older than the QarOS library accepts. This stack repairs the distribution in place with scripts that ship in every repository.

All repair scripts are safe to run again: each one checks what is already there and skips it. They edit the distribution folder `ROS2_INSTALL_ROOT`, and `scripts\fix_stale_pixi_paths.ps1` also creates one folder on `C:`; none needs administrator rights. Run them in the order of the table, once, after you have fetched the distribution.

qar_rviz_plugin needs all nine repairs: RViz must start, be able to run the QarOS library and be able to load a robot description, and the plugin is built against the distribution.

| Script | What it fixes | Symptom without it | Needs |
|---|---|---|---|
| `scripts\fix_missing_spdlog_dll.ps1` | Builds `spdlog.dll` from source, in the version that the parameter `-SpdlogVersion` of the script names by default, and installs it with its import library. | The library `rcl.dll` does not load: `rclpy`, `rclcpp` programs and `rviz2.exe` fail with the Windows status DLL not found. | Visual Studio 2019 tools, CMake, network access to GitHub |
| `scripts\fix_missing_vendor_dlls.ps1` | Builds and installs five missing libraries: `console_bridge.dll`, `tinyxml2.dll`, `yaml-cpp.dll`, `assimp-vc141-mt.dll` and `libcurl.dll`. | The RViz, `tf2`, `class_loader` and `resource_retriever` libraries do not load, and neither does the middleware, because Fast-DDS imports `tinyxml2.dll`. | the same, and `tar` |
| `scripts\fix_missing_openssl_dll.ps1` | Installs the OpenSSL pair `libssl-1_1-x64.dll` and `libcrypto-1_1-x64.dll` that Fast-DDS imports, from a package whose SHA-256 it checks; OpenSSL 3 does not work, because its files have other names. | Initialising ROS 2 fails because the middleware cannot load; `rviz2.exe` exits without a window. | Network access to `conda.anaconda.org` and the virtual environment of [Python environment](#python-environment); on a machine without network access, `-SourceArchive` with a local copy of the package, or `-SourceDir` with a folder that holds both files |
| `scripts\fix_qt_conda_imports.ps1` | Patches four binaries (`rviz2.exe`, `rviz_common.dll`, `rviz_rendering.dll`, `rviz_default_plugins.dll`) so that they import the plain Qt DLL names. Keeps a `.conda.orig` copy of each. | `rviz2.exe` exits at start because two copies of Qt are loaded. | Nothing; Qt must be on `PATH` when RViz runs. Run it again after any new extraction of the distribution. |
| `scripts\fix_missing_vendor_cmake_configs.ps1` | Writes CMake configuration shims for assimp, curl, yaml-cpp, console_bridge, TinyXML2, spdlog and orocos_kdl. | `find_package(rclcpp)` and `find_package(rviz_common)` fail at configure time. | The output of the vendor DLL script |
| `scripts\fix_missing_eigen.ps1` | Installs the Eigen 3.4.0 headers, from a source archive whose SHA-256 it checks, and an `Eigen3Config.cmake`. | Configure fails because Eigen3 is not found. | `tar` and network access to Debian, Ubuntu or GitLab; on a machine without network access, `-SourceArchive` with a local copy of the archive, or `-EigenSourceInclude` with the `include/eigen3` folder of an Eigen 3.4.0 installation |
| `scripts\fix_stale_pixi_paths.ps1` | Creates the folder tree `C:\pixi_ws\.pixi\envs\default` that the exported targets name, filled with the Eigen headers of the Eigen repair, a Python import library and, when the script has a source for them, numpy headers. | The generate step of CMake rejects imported targets, `rclcpp::rclcpp` among them, with an include path that does not exist, and the linker cannot open `python310.lib` in that tree. | The Visual Studio environment and the Python 3.10 import library; the numpy headers are optional, and `-NumpySourceInclude` or the numpy package of the virtual environment (`QAR_VENV`) provides them |
| `scripts\fix_old_vc_runtime.ps1` | Replaces the C++ runtime files next to `rviz2.exe` (`msvcp140.dll`, `vcruntime140.dll` and their companions, version 14.29) with the same files of a runtime of version 14.40 or newer, and keeps a `.vcrt.orig` copy of each. | `"Connect to QarOS"` ends RViz, and the log of RViz holds a line about a `SEH FAULT` in `qar_library_init` that names the `MSVCP140.dll` of the distribution. | The current Microsoft Visual C++ Redistributable for x64 on the machine, or `-SdkRoot` with a QarOS SDK folder whose `bin` holds a newer runtime; RViz closed |
| `scripts\fix_missing_tinyxml_dll.ps1` | Builds `tinyxml.dll` (TinyXML 2.6.2) from its source archive, after it has checked the SHA-256 of the archive, and installs the file next to `rviz2.exe`; `urdfdom_model.dll` and three more libraries of the distribution import it, and the release does not ship it. | RViz ends with the exit code 0xC0000409 as soon as a robot description reaches the Robot model display, and `check_urdf.exe` of the distribution fails with the Windows status 0xC0000135. | Visual Studio 2019 tools, CMake, `tar`, the output of the vendor DLL script, and network access to Debian, Ubuntu or SourceForge, or `-SourceArchive` with a local copy of the archive |

The repairs of OpenSSL and Eigen need no conda installation: each one downloads a pinned file. `scripts\fix_missing_openssl_dll.ps1` downloads the package `openssl-1.1.1w-hcfcfb64_0.conda` of the conda-forge channel from `conda.anaconda.org` and unpacks the two DLLs from it with the Python of the virtual environment (`QAR_VENV`) and its module `backports.zstd`, which `requirements.txt` installs; if that Python is not there or cannot read the package, the script tries the `tar` of Windows, which must support Zstandard. `scripts\fix_missing_eigen.ps1` takes the archive `eigen-3.4.0.tar.bz2` from the first of three download locations (Debian, Ubuntu and GitLab) that answers with a file of the pinned SHA-256, unpacks the folders `Eigen` and `unsupported` and the file `signature_of_eigen3_matrix_library` with `tar`, and copies them into the distribution. Both scripts keep the download in the folder `downloads` of `QAR_DEPS_ROOT`, compare its SHA-256 with the value that they pin, delete a download with another hash and never unpack it. On a machine that cannot reach the download locations, pass a local copy of the file with `-SourceArchive`; it must have the same SHA-256. The parameter `-SourceDir` of the OpenSSL script (a folder that holds both DLLs) and the parameter `-EigenSourceInclude` of the Eigen script (the `include/eigen3` folder of an Eigen 3.4.0 installation) name files that you already have, and they win over the download.

The numpy headers of `scripts\fix_stale_pixi_paths.ps1` are optional. The script creates their folder in any case and fills it from `-NumpySourceInclude`, or else from the numpy package of the virtual environment (`QAR_VENV`), which `requirements.txt` pins; if it finds neither, it prints a warning and goes on. No C++ or CMake file of this stack mentions numpy, so the folder only has to exist.

The scripts `scripts\fix_missing_vendor_cmake_configs.ps1`, `scripts\fix_missing_eigen.ps1` and `scripts\fix_stale_pixi_paths.ps1` are also run by `scripts\build_plugin.ps1` before the plugin build; running them here shows their errors early.

```powershell
.\scripts\fix_missing_spdlog_dll.ps1
.\scripts\fix_missing_vendor_dlls.ps1
.\scripts\fix_missing_openssl_dll.ps1
.\scripts\fix_qt_conda_imports.ps1
.\scripts\fix_missing_vendor_cmake_configs.ps1
.\scripts\fix_missing_eigen.ps1
.\scripts\fix_stale_pixi_paths.ps1
```
```text
...
Done. spdlog.dll SHA256: BC272567EDB344A4EACDD42902271879B890C1DFB05C45618957064A4EE46D11
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\console_bridge.dll  (from C:\qar\deps\build_vendor_dlls\console_bridge-1.0.2\_build\bin\RelWithDebInfo\console_bridge.dll)
Installed: console_bridge headers and import library under C:\qar\deps\ros2_humble\ros2-windows\opt\console_bridge_vendor
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\tinyxml2.dll  (from C:\qar\deps\build_vendor_dlls\tinyxml2-9.0.0\_build\RelWithDebInfo\tinyxml2.dll)
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\yaml-cpp.dll  (from C:\qar\deps\build_vendor_dlls\yaml-cpp-yaml-cpp-0.7.0\_build\RelWithDebInfo\yaml-cpp.dll)
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\assimp-vc141-mt.dll  (from C:\qar\deps\build_vendor_dlls\assimp-4.1.0\_build\code\RelWithDebInfo\assimp-vc140-mt.dll)
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\libcurl.dll  (from C:\qar\deps\build_vendor_dlls\curl-7.81.0\_build\lib\RelWithDebInfo\libcurl.dll)

Done. Verify with ros2 --help (through ros2-script.py) or rviz2.exe: no DLL load errors should remain.
Downloading https://conda.anaconda.org/conda-forge/win-64/openssl-1.1.1w-hcfcfb64_0.conda
Downloaded and verified: C:\qar\deps\downloads\openssl-1.1.1w-hcfcfb64_0.conda
Extracting openssl-1.1.1w-hcfcfb64_0.conda
Installed libssl-1_1-x64.dll (OpenSSL 1.1.1w) from C:\qar\deps\build_openssl\unpacked
Installed libcrypto-1_1-x64.dll (OpenSSL 1.1.1w) from C:\qar\deps\build_openssl\unpacked
Done. Fast-DDS (rmw_fastrtps_cpp) can now load; rcl/rclpy init and rviz2 will start.
Patched rviz2.exe : 1 '_conda' import name(s) -> plain Qt5 names.
Patched rviz_common.dll : 3 '_conda' import name(s) -> plain Qt5 names.
Patched rviz_rendering.dll : 2 '_conda' import name(s) -> plain Qt5 names.
Patched rviz_default_plugins.dll : 3 '_conda' import name(s) -> plain Qt5 names.

Done. rviz2 binaries now import plain Qt5Core/Qt5Gui/Qt5Widgets DLLs (single Qt copy).
Ensure the Qt bin folder is on PATH (scripts/env_setup.ps1 handles this).
assimp:
  wrote C:\qar\deps\ros2_humble\ros2-windows\opt\rviz_assimp_vendor\lib\cmake\assimp-4.1\assimpConfig.cmake
curl:
  wrote C:\qar\deps\ros2_humble\ros2-windows\opt\libcurl_vendor\lib\CMake\curlConfig.cmake
yaml-cpp:
  wrote C:\qar\deps\ros2_humble\ros2-windows\opt\yaml_cpp_vendor\CMake\yaml-cpp-config.cmake
console_bridge:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\console_bridge\cmake\console_bridgeConfig.cmake
TinyXML2:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\tinyxml2\cmake\TinyXML2Config.cmake
spdlog:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\spdlog\cmake\spdlogConfig.cmake
orocos_kdl:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\orocos_kdl\cmake\orocos_kdlConfig.cmake

All vendor CMake config shims written. find_package(rviz_common) will now configure.
(Eigen3 headers+config are handled separately by fix_missing_eigen.ps1.)
Downloading https://deb.debian.org/debian/pool/main/e/eigen3/eigen3_3.4.0.orig.tar.bz2
Downloaded and verified: C:\qar\deps\downloads\eigen-3.4.0.tar.bz2
Extracting eigen-3.4.0.tar.bz2
Eigen source: C:\qar\deps\build_eigen\eigen-3.4.0
Copied Eigen headers -> C:\qar\deps\ros2_humble\ros2-windows\opt\eigen3_vendor\include\eigen3
Wrote Eigen3Config.cmake -> C:\qar\deps\ros2_humble\ros2-windows\share\eigen3\cmake
find_package(Eigen3) will now succeed for downstream ament plugin builds.
Stale pixi_ws paths materialized under C:\pixi_ws\.pixi\envs\default.
```

The first run of `scripts\fix_stale_pixi_paths.ps1` on a machine that has no `C:\pixi_ws` prints a `mkdir` line for each folder that it creates and a line for each set of files that it fills in. In a shell without the Visual Studio environment, such as the shell in which you run these commands, it then prints the warning `WARNING: cl/lib not on PATH; cannot create stub tinyxml.lib (run from build_plugin.ps1)`. The warning is expected: the stub `tinyxml.lib` is a file that the linker needs and that the script builds with `cl` and `lib`, and `scripts\build_plugin.ps1` loads the Visual Studio environment before it runs the script again, which completes the stub.

The eighth repair is a command of its own, because it takes its files from your machine. RViz loads the C++ runtime files that lie in the folder of `rviz2.exe`, and the QarOS library, which the plugin loads when you press `"Connect to QarOS"`, then runs on that copy, because Windows keeps using a runtime that a program has already loaded. The release ships the version 14.29, and the QarOS library needs 14.40 or newer. The script replaces every runtime file of the distribution that is older than 14.40 with the file of the same name from a newer runtime, keeps each original as `<name>.vcrt.orig` next to it, and prints every file that it replaced.

The newer runtime comes from the system folder of Windows, so the machine needs the current Microsoft Visual C++ Redistributable for x64, which [Toolchain](#toolchain) installs. If the system folder holds no runtime of version 14.40 or newer, the script says so and changes nothing; pass `-SdkRoot` with the folder of the QarOS SDK, and it takes the files from the `bin` folder of the SDK when the runtime there is newer. A folder in `QAR_SDK_ROOT` that does not exist yet is no error, so you can run the command before you unpack the SDK and repeat it with `-SdkRoot` afterwards. Close RViz first, because the script refuses while `rviz2.exe` runs or while a file is in use, and it never stops a process. A second run changes nothing. Start RViz again after the repair: a running RViz keeps the runtime that it loaded.

```powershell
.\scripts\fix_old_vc_runtime.ps1
```
```text
8 Visual C++ runtime file(s) older than 14.40 below C:\qar\deps\ros2_humble\ros2-windows; source C:\WINDOWS\System32 (msvcp140.dll 14.50.35719.0).
  replaced bin\concrt140.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_1.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_2.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_atomic_wait.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_codecvt_ids.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\vcruntime140.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\vcruntime140_1.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)

Done. Replaced 8 of 8 file(s); each original is kept as <name>.vcrt.orig next to it.
Start rviz2 again: a running process keeps the runtime it loaded.
```

The ninth repair is a command of its own, because it builds a library from a source archive that it downloads. RViz loads the URDF parser of the distribution, `urdf_xml_parser.dll`, when the Robot model display receives its first robot description, and the parser needs `urdfdom_model.dll`, which imports `tinyxml.dll`. The release ships no `tinyxml.dll`, and `urdfdom_world.dll`, `urdfdom_model_state.dll` and `urdfdom_sensor.dll` import it as well. Without the file the load of the parser fails with the Windows error 126, nothing in the Robot model display catches the exception, and RViz ends with the exit code 0xC0000409.

The script takes the source archive `tinyxml_2.6.2.orig.tar.gz` from the first of three download locations (Debian, Ubuntu and SourceForge) that answers with a file of the SHA-256 that the script pins, or from the file that you pass with `-SourceArchive` when the machine cannot reach them, and it never unpacks a file with another hash. It applies the two fixes for the published defects CVE-2021-42260 and CVE-2023-34194 of TinyXML 2.6.2 to the source, stops when a fix does not match the source text exactly once, and builds the library with the generator `Visual Studio 16 2019` in the `RelWithDebInfo` configuration, as a shared library that exports every symbol. It copies only `tinyxml.dll` into the `bin` folder of the distribution and runs `check_urdf.exe` of the distribution on a robot with one link; the repair counts only when that program prints `Successfully Parsed XML`. A second run finds the file and the passing check, says that there is nothing to do and changes nothing; a `tinyxml.dll` that fails the check is refused until you delete it or pass `-Force`. Run the script after the vendor DLL script, because the check needs `console_bridge.dll` and the C++ runtime in the `bin` folder. The built file differs from build to build, because a build records its time; the SHA-256 of the archive is the pinned input.

```powershell
.\scripts\fix_missing_tinyxml_dll.ps1
```
```text
Visual Studio 2019 tools: C:\Program Files (x86)\Microsoft Visual Studio\2019\Community
Downloading https://deb.debian.org/debian/pool/main/t/tinyxml/tinyxml_2.6.2.orig.tar.gz
Downloaded and verified: C:\qar\deps\downloads\tinyxml_2.6.2.orig.tar.gz
Extracting tinyxml_2.6.2.orig.tar.gz
Configuring (VS2019 x64, matching the distro's v142 toolset)
...
Building tinyxml (RelWithDebInfo, matching the distro's build config)
...
  tinyxml.vcxproj -> C:\qar\deps\build_tinyxml\tinyxml\_build\RelWithDebInfo\tinyxml.dll
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\tinyxml.dll  (from C:\qar\deps\build_tinyxml\tinyxml\_build\RelWithDebInfo\tinyxml.dll)
Check: check_urdf.exe parses a robot with the new tinyxml.dll.

Done. tinyxml.dll SHA256: 9F1EAF765712B58FCAF6924D96E59554D91834C46D68B99F5DAEC68B8FAF9365
Start RViz again; its Robot model display can load a robot description now.
```

The check of the repaired distribution comes after the installation of Qt, in the next section, because it runs `scripts\env_setup.ps1`, and that script stops with an error while the Qt folder that `QAR_QT_ROOT` names is missing: [Check the environment without a window](#check-the-environment-without-a-window).

## Qt 5.15.2

Qt is not part of the ROS 2 distribution, so you install it separately. The stack uses the Qt 5.15.2 kit for MSVC 2019 (`win64_msvc2019_64`), the Qt build that matches the v142 toolset. The distribution ships no Qt at all.

The plugin uses the Qt modules Core, Gui and Widgets for its RViz panel, the web panel server uses Core, Network and WebSockets, and the tests of the web panel library also use the Qt Test module.

The installer is `aqt` (aqtinstall), a command-line installer for Qt that needs no Qt account; it is in the virtual environment you created. The command installs the kit below `$env:QAR_DEPS_ROOT\Qt`, which makes the Qt folder `$env:QAR_DEPS_ROOT\Qt\5.15.2\msvc2019_64`, the value of `QAR_QT_ROOT`.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir "$env:QAR_DEPS_ROOT\Qt"
```
```text
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.10.11 [CPython MSC v.1929 64 bit (AMD64)]
...
INFO    : Patching C:\qar\deps\Qt\5.15.2\msvc2019_64\bin\qmake.exe
...
INFO    : Finished installation
INFO    : Time elapsed: 15.21180160 second
```

Check the version and that the CMake packages of the modules this stack uses are present.

```powershell
& "$env:QAR_QT_ROOT\bin\qmake.exe" -v
foreach ($module in 'Core', 'Gui', 'Widgets', 'Network', 'WebSockets', 'Test') {
    '{0,-11} {1}' -f $module, (Test-Path -LiteralPath (Join-Path $env:QAR_QT_ROOT "lib\cmake\Qt5$module"))
}
```
```text
QMake version 3.1
Using Qt version 5.15.2 in C:/qar/deps/Qt/5.15.2/msvc2019_64/lib
Core        True
Gui         True
Widgets     True
Network     True
WebSockets  True
Test        True
```

### Check the environment without a window

With the distribution repaired and Qt installed, check the environment without a window. The script `scripts\env_setup.ps1` must be dot-sourced, with a leading dot and a space, because a script that is run normally cannot change the environment of the shell that started it. It imports the environment of the distribution (the `local_setup.bat` of the release), puts the virtual environment and Qt in front of `PATH`, and sets `QT_QPA_PLATFORM_PLUGIN_PATH` and `QT_PLUGIN_PATH`. Run it in every new shell in which you start ROS 2 tools. The `ros2` launcher of the distribution does not start on another machine, because it points at the CI machine; run the script behind it with Python instead.

```powershell
. .\scripts\env_setup.ps1
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" --help | Select-Object -First 3
```
```text
...
=== Qar RViz2 environment set up in this PowerShell session ===
PATH prepended with: venv Scripts, Qt bin, ROS 2 distribution bin folders (via local_setup.bat)
AMENT_PREFIX_PATH: C:\qar\deps\ros2_humble\ros2-windows
QT_QPA_PLATFORM_PLUGIN_PATH: C:\qar\deps\Qt\5.15.2\msvc2019_64\plugins\platforms

python  -> C:\qar\venv\Scripts\python.exe

REMINDER: the 'ros2' command-line wrapper exe has a broken shebang (a build-machine path).
  Use: python "C:\qar\deps\ros2_humble\ros2-windows\Scripts\ros2-script.py" <args>   (NOT ros2.exe / ros2 directly)
...
usage: ros2 [-h] [--use-python-default-buffering]
            Call `ros2 <command> -h` for more detailed usage. ...
```

`local_setup.bat` of the ROS 2 distribution prints a warning about RTI Connext every time `scripts\env_setup.ps1` runs, before the banner of the script. RTI Connext is a DDS product that this stack does not use, because it uses Fast-DDS, so you can ignore the warning.

## Upstream repositories

### qar_ogre_adapter 0.1.0

qar_ogre_adapter is the QarOS integration layer. It renders an Ogre scene for each eye into OpenGL textures and streams them to QarOS, and it holds the adapter for the QarOS C API (session, peers, panels, URL panels and gestures) that the other repositories build on. It also carries the C header of the QarOS SDK that every repository of this stack compiles against.

Clone it next to the other repositories.

```powershell
git clone https://github.com/Quaternar/qaros-ogre "$env:QAR_ROOT\src\qar_ogre_adapter"
```
```text
Cloning into 'C:\qar\src\qar_ogre_adapter'...
done.
```

Go to the folder of the clone; the commands below run there:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_ogre_adapter"
```

Configure with the preset of the repository. The preset selects the generator `Visual Studio 16 2019` for x64, the build folder `build\vs2019-x64` inside the clone and the configuration `RelWithDebInfo`, and it reads the location variables of the layout. Configuring downloads GoogleTest 1.14.0 as an archive from `github.com` and checks its SHA256, so this step needs network access; without it, pass `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST` with the path of an extracted GoogleTest 1.14.0 folder.

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_ROS2_INSTALL_ROOT="C:\qar\deps\ros2_humble\ros2-windows"
  QAR_SDK_ROOT="C:\Program Files\Quaternar\QAROS"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
-- Found rviz_ogre_vendor: 11.2.25 (C:/qar/deps/ros2_humble/ros2-windows/share/rviz_ogre_vendor/cmake)
...
-- qar_qar_adapter: QarOS SDK backend, DYNAMIC loading of C:\Program Files\Quaternar\QAROS/bin/qar-streaming-c.dll (no import lib linked)
...
-- Configuring done (10.4s)
-- Generating done (0.2s)
-- Build files have been written to: C:/qar/src/qar_ogre_adapter/build/vs2019-x64
```

Build it. The build writes into the build folder of the repository only.

```powershell
cmake --build --preset vs2019-x64
```
```text
  qar_ar_core.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ar_core\RelWithDebInfo\qar_ar_core.lib
...
  qar_qar_adapter_session.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_qar_adapter\RelWithDebInfo\qar_qar_adapter_session.lib
...
  qar_ogre_streaming.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ogre_streaming\RelWithDebInfo\qar_ogre_streaming.lib
...
  qar_qar_adapter_render.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_qar_adapter\RelWithDebInfo\qar_qar_adapter_render.lib
...
  qar_ogre_standalone_streaming_sample.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\ogre_standalone_streaming_sample\RelWithDebInfo\qar_ogre_standalone_streaming_sample.exe
...
  qar_ogre_live_stream_example.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\qaros_live_stream\RelWithDebInfo\qar_ogre_live_stream_example.exe
```

Install it into the prefix. The install puts the libraries of the three modules `qar_ar_core`, `qar_ogre_streaming` and `qar_qar_adapter` and the vendored QarOS C header below `$env:QAR_PREFIX`, together with the CMake package that the other repositories of this stack find there.

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_ar_core.lib
-- Installing: C:\qar\install/lib/qar_ogre_streaming.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_capi.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_session.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_render.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_panels.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_url.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_gestures.lib
-- Installing: C:\qar\install/include
...
-- Installing: C:\qar\install/include/qar_streaming.h
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE.qar_streaming_header
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfig.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfigVersion.cmake
```

### qar_qt_streaming 0.1.0

qar_qt_streaming turns Qt widgets into QarOS AR GUI panels: it captures a widget off screen, streams it as a panel and feeds AR touch input back to the widget.

Clone it next to the other repositories.

```powershell
git clone https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
```
```text
Cloning into 'C:\qar\src\qar_qt_streaming'...
done.
```

Go to the folder of the clone; the commands below run there:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

Configure with the preset of the repository. The preset selects the generator `Visual Studio 16 2019` for x64, the build folder `build\vs2019-x64` inside the clone and the configuration `RelWithDebInfo`, and it reads the location variables of the layout. Configuring downloads GoogleTest 1.14.0 as an archive from `github.com` and checks its SHA256, so this step needs network access; without it, pass `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST` with the path of an extracted GoogleTest 1.14.0 folder.

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_PREFIX="C:\qar\install"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
...
-- qar_ogre_adapter 0.1.0: C:/qar/install/share/qar_ogre_adapter/cmake
...
-- Configuring done (10.9s)
-- Generating done (0.2s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

Build it. The build writes into the build folder of the repository only.

```powershell
cmake --build --preset vs2019-x64
```
```text
  qar_qt_streaming.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\RelWithDebInfo\qar_qt_streaming.lib
...
  qar_qt_panel_example.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe
...
  qar_qt_streaming_doc_snippets.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_qt_streaming_doc_snippets.exe
...
  qar_qt_streaming_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\test\RelWithDebInfo\qar_qt_streaming_tests.exe
```

Install it into the prefix. The install puts the library `qar_qt_streaming` below `$env:QAR_PREFIX`, together with the CMake package that the other repositories of this stack find there.

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_qt_streaming.lib
-- Up-to-date: C:\qar\install/include
-- Installing: C:\qar\install/include/qar_qt_streaming
...
-- Installing: C:\qar\install/share/qar_qt_streaming/LICENSE
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfig.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfigVersion.cmake
```

## Build and install qar_rviz_plugin

This repository holds two builds that use the same sources. A CMake project at its root compiles the three web libraries, the two examples and the parts of the plugin that need neither RViz nor QarOS, the pure units, together with their tests; it is the project of the preset `vs2019-x64`, like the upstream repositories. The plugin itself, the shared library that RViz loads, is built by `colcon` through the script `scripts\build_plugin.ps1`, because it needs the ament build system of ROS 2. The first build is quick and tells you that the upstream packages and the units work; the second is the one that produces the plugin.

### Configure, build and test the units

Run the commands from the clone of this repository; the last commands of [Upstream repositories](#upstream-repositories) left the window in the clone of `qar_qt_streaming`, so go back to this one first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

The preset selects the generator `Visual Studio 16 2019` for x64, the build folder `build\vs2019-x64` inside the clone and the configuration `RelWithDebInfo`, and it reads the location variables of the layout. Configuring downloads GoogleTest 1.14.0 as an archive from `github.com` and checks its SHA256, so this step needs network access. The project finds the two upstream packages in `QAR_PREFIX`, so [Upstream repositories](#upstream-repositories) has to be done first.

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_PREFIX="C:\qar\install"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"
  QAR_ROS2_INSTALL_ROOT="C:\qar\deps\ros2_humble\ros2-windows"
...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
...
-- qar_ogre_adapter 0.1.0: C:/qar/install/share/qar_ogre_adapter/cmake
-- qar_qt_streaming 0.1.0: C:/qar/install/share/qar_qt_streaming/cmake
...
-- Configuring done (12.8s)
-- Generating done (0.5s)
-- Build files have been written to: C:/qar/src/qar_rviz_plugin/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
...
  qar_rviz_streaming_widget_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_widget_tests.exe
...
  qar_rviz_streaming_ros_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_ros_tests.exe
...
  qar_rviz_streaming_video_source_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_video_source_tests.exe
...
  qar_rviz_plugin_doc_snippets.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_rviz_plugin_doc_snippets.exe
...
  qar_rviz_streaming_image_source_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_image_source_tests.exe
...
  qar_rviz_streaming_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_tests.exe
...
```

The build produces eight test executables, and the tests need no QAROS and open no window. [Testing](TESTING.md) explains what they cover. Run them now:

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_rviz_plugin/build/vs2019-x64
        Start   1: check_layering_qar_ui_model
  1/889 Test   #1: check_layering_qar_ui_model ..................................................................   Passed    0.15 sec
        Start   2: check_layering_qar_web_panels
  2/889 Test   #2: check_layering_qar_web_panels ................................................................   Passed    0.07 sec
        Start   3: check_layering_qar_ros_ui
  3/889 Test   #3: check_layering_qar_ros_ui ....................................................................   Passed    0.10 sec
        Start   4: check_layering_qar_rviz_streaming
  4/889 Test   #4: check_layering_qar_rviz_streaming ............................................................   Passed    0.39 sec
...

100% tests passed, 0 tests failed out of 889

Total Test time (real) =  82.42 sec
```

The test project never compiles the file that holds the panel, `qar_streaming_panel.cpp`, because that file needs RViz. Only the `colcon` build below compiles it, so a green `ctest` run is no proof that the plugin builds.

### Build the plugin and install it into the prefix

Close every RViz window before you build: a running `rviz2.exe` has `qar_rviz_streaming.dll` loaded, and the install step has to replace that file. The command below runs from the clone, takes the prefix, the distribution, Qt and the virtual environment from the variables of the layout, and needs no parameter.

```powershell
.\scripts\build_plugin.ps1
```
```text
=== Importing the Visual Studio 2019 x64 build environment ===
  vcvars64: C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat
...
=== Dot-sourcing env_setup.ps1 ===
...
=== Ensuring downstream ament CMake config gaps are filled ===
...
=== colcon build (Ninja, RelWithDebInfo) ===
...
  Qt5_DIR: C:\qar\deps\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5
  Python3: C:\qar\venv\Scripts\python.exe
  prefix : C:\qar\install
  colcon : C:\qar\src\qar_rviz_plugin\build\colcon
Starting >>> qar_rviz_streaming
...
Finished <<< qar_rviz_streaming [27.7s]
...
Summary: 1 package finished [28.3s]
...
=== BUILD OK ===
Plugin DLL: C:\qar\install\bin\qar_rviz_streaming.dll
plugin_description.xml installed: C:\qar\install\share\qar_rviz_streaming\plugin_description.xml
Overlay (dot-source it): C:\qar\install\local_setup.ps1
```

The script is not interactive. It does these things in this order:

1. It resolves the four locations from the parameters `-Prefix`, `-Ros2Root`, `-QtRoot` and `-Venv` or, when a parameter is omitted, from `QAR_PREFIX`, `ROS2_INSTALL_ROOT`, `QAR_QT_ROOT` and `QAR_VENV`, and it stops with a message that names the parameter, the variable and the section of this document when one is missing. It checks that the two upstream packages are installed below the prefix, and stops with a message that tells you to install them first when one is not.
2. It imports the build environment of Visual Studio 2019 for x64 by running `vcvars64.bat` of the installation that `vswhere` reports, which puts the compiler on `PATH`. The generator `ninja` is not part of that environment: it comes from the Ninja install of the toolchain section, and the script stops with a message when it is not on `PATH`.
3. It dot-sources `scripts\env_setup.ps1`, which imports the environment of the distribution and puts Qt and the virtual environment in front of `PATH`.
4. It runs the three repair scripts that `find_package(rviz_common)` needs, `fix_missing_vendor_cmake_configs.ps1`, `fix_missing_eigen.ps1` and `fix_stale_pixi_paths.ps1`; they do nothing when their work is done.
5. It runs `colcon build` for the one package of this repository with the Ninja generator and the configuration `RelWithDebInfo`, with the install base set to the prefix and the option `--merge-install`. It passes the Qt folder, the prefix and the interpreter of the virtual environment to CMake, because the Python helpers of the ROS 2 build system need packages that only the virtual environment has.

The configuration is `RelWithDebInfo` and never `Debug`: a plugin in the debug configuration would mix its C++ runtime and Qt libraries with those of the release build of `rviz2.exe`, and it would crash when RViz loads it.

When the build succeeds, the script prints `=== BUILD OK ===` and then one line for each of the three results below that exists. A result that is missing gets a warning. The build tree and the logs of `colcon` are kept in the folder `build\colcon` of the clone. The option `-Clean` deletes that folder before the build and nothing else, and the script never deletes or rewrites the prefix as a whole.

| Result | Where it is | What it is for |
|---|---|---|
| `qar_rviz_streaming.dll` | `bin` below the prefix | the plugin, a shared library that RViz loads through its plugin loader |
| `plugin_description.xml` | `share\qar_rviz_streaming` below the prefix | the file that names the panel class `qar_rviz_streaming/QarStreamingPanel`, so that RViz lists it among its panels |
| `local_setup.ps1` | the root of the prefix | the overlay: dot-source it, and RViz finds the plugin |

An overlay is a folder of installed ROS 2 packages that you put on top of the environment of the distribution: the setup scripts that `colcon` writes into the install folder extend your path and library paths with the packages of that folder. ROS 2 finds installed packages, and the entries that they register, through the folders that are listed in `AMENT_PREFIX_PATH`. The launchers of this repository dot-source the overlay for you, so that RViz finds the plugin, and you need the command of the next section only when you start `rviz2.exe` yourself.

## Install smoke test

The smoke test checks the three results without starting RViz. The first command prepares a shell with `scripts\env_setup.ps1`, as in [Check the environment without a window](#check-the-environment-without-a-window), sources the overlay, and asks the command line of ROS 2 where it finds the package. It must print the prefix, and a message that the package was not found means that the overlay is not on `AMENT_PREFIX_PATH`.

```powershell
. .\scripts\env_setup.ps1
. $env:QAR_PREFIX\local_setup.ps1
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" pkg prefix qar_rviz_streaming
```
```text
"[rti_connext_dds_cmake_module][warning] RTI Connext DDS environment script not found (\resource\scripts\rtisetenv_x64Win64VS2017.bat). RTI Connext DDS will not be available at runtime, unless you already configured PATH manually."
=== Qar RViz2 environment set up in this PowerShell session ===
PATH prepended with: venv Scripts, Qt bin, ROS 2 distribution bin folders (via local_setup.bat)
AMENT_PREFIX_PATH: C:\qar\deps\ros2_humble\ros2-windows
QT_QPA_PLATFORM_PLUGIN_PATH: C:\qar\deps\Qt\5.15.2\msvc2019_64\plugins\platforms

python  -> C:\qar\venv\Scripts\python.exe

REMINDER: the 'ros2' command-line wrapper exe has a broken shebang (a build-machine path).
  Use: python "C:\qar\deps\ros2_humble\ros2-windows\Scripts\ros2-script.py" <args>   (NOT ros2.exe / ros2 directly)
rviz2.exe works: launch it directly (rviz2.exe).
  It needs the repaired distribution: run the fix_*.ps1 scripts of this folder once.
  docs/INSTALL.md, section 'ROS 2 Humble for Windows', leads to the repairs this repository needs.
"[rti_connext_dds_cmake_module][warning] RTI Connext DDS environment script not found (\resource\scripts\rtisetenv_x64Win64VS2017.bat). RTI Connext DDS will not be available at runtime, unless you already configured PATH manually."
=== Qar RViz2 environment set up in this PowerShell session ===
PATH prepended with: venv Scripts, Qt bin, ROS 2 distribution bin folders (via local_setup.bat)
AMENT_PREFIX_PATH: C:\qar\deps\ros2_humble\ros2-windows
QT_QPA_PLATFORM_PLUGIN_PATH: C:\qar\deps\Qt\5.15.2\msvc2019_64\plugins\platforms

python  -> C:\qar\venv\Scripts\python.exe

REMINDER: the 'ros2' command-line wrapper exe has a broken shebang (a build-machine path).
  Use: python "C:\qar\deps\ros2_humble\ros2-windows\Scripts\ros2-script.py" <args>   (NOT ros2.exe / ros2 directly)
rviz2.exe works: launch it directly (rviz2.exe).
  It needs the repaired distribution: run the fix_*.ps1 scripts of this folder once.
  docs/INSTALL.md, section 'ROS 2 Humble for Windows', leads to the repairs this repository needs.
C:\qar\install
```

The second command looks for the three files that RViz needs. The third path is the entry that registers the description of the plugin in the resource index of ament, which is how RViz2 discovers the panel; a missing entry is a reason for a panel that RViz does not list. Every line must start with `True`.

```powershell
foreach ($path in 'bin\qar_rviz_streaming.dll', 'share\qar_rviz_streaming\plugin_description.xml', 'share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming') {
    '{0,-6} {1}' -f (Test-Path -LiteralPath (Join-Path $env:QAR_PREFIX $path)), $path
}
```
```text
True   bin\qar_rviz_streaming.dll
True   share\qar_rviz_streaming\plugin_description.xml
True   share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming
```

These commands do not load the plugin. The first time that RViz loads it is [Launch RViz with the plugin](GETTING_STARTED.md#launch-rviz-with-the-plugin), and [Check the launch configuration](GETTING_STARTED.md#check-the-launch-configuration) shows without a window what the launcher will do. If the panel is missing in RViz although the three files exist, [RViz and plugin problems](TROUBLESHOOTING.md#rviz-and-plugin-problems) lists the causes.

## Removing everything again

The installation changes your machine in a few places. This section lists every one of them and how to undo it. The scripts and commands that this documentation tells you to run write only to the folders and registry values listed here, to the build folders inside the clones, and to the files they report. The programs that they call keep caches and logs of their own outside these folders, such as the download cache of pip; this section does not list them.

| Change | Made by | Where it lives | How to undo it |
|---|---|---|---|
| The toolchain: Visual Studio 2019 Build Tools, CMake, Git, PowerShell 7, Python 3.10, Ninja and the Microsoft Visual C++ Redistributable | the installers of [Toolchain](#toolchain) | the Windows installation and `C:\Program Files` | Uninstall each program in Windows Settings, or with `winget uninstall` and the package id used to install it; keep the Visual C++ Redistributable if another program may use it. |
| Seven environment variables of your user | the block of [Directory layout and environment variables](#directory-layout-and-environment-variables) | the user environment of Windows | The second block below removes them. |
| The layout folders: the dependencies, the virtual environment, the clones, the install prefix | the steps of this document | `C:\qar` | Delete the folder; the second block below does it. The repaired ROS 2 distribution, the Qt kit, the downloads and the build folders of the distribution repairs are all inside it. |
| The QarOS SDK | [The QarOS SDK](#the-qaros-sdk) | the installed QAROS, whose folder `QAR_SDK_ROOT` names when you set it | No block below removes it; the paragraph below the table says how to remove it. |
| The tree that the exported targets of the distribution name | `scripts\fix_stale_pixi_paths.ps1` | `C:\pixi_ws` | Delete the folder `C:\pixi_ws`. |
| The Windows graphics preference of `rviz2.exe` | `scripts\run_live_rviz_stream_real.ps1` with `-Confirm` | the registry value of `rviz2.exe` below HKCU:\Software\Microsoft\DirectX\UserGpuPreferences | Remove `rviz2.exe` in Windows Settings under System, Display, Graphics, or delete that registry value. |
| The logs of the live launcher | `scripts\run_live_rviz_stream_real.ps1` | the folder `qar_rviz_logs` in `%TEMP%`, or the folder that you gave as `-LogDir` | Delete the folder. |
| The demo bag | `scripts\play_demo_rosbag.ps1` | the folder `qar_demo_rosbag` in `%TEMP%`, or the folder that you gave as `-BagDir` | Delete the folder. |
| The results of the offline preview | `scripts\run_rviz_with_plugin.ps1` | the folder `qar_rviz_preview` in `%TEMP%`, or the folder that you gave as `-OutDir` | Delete the folder. |
| The logs of the QarOS library | QarOS, when an application connects to it | the folder `quaternar` in `%TEMP%` | Delete the folder. It can grow to gigabytes when trace logging is on. |

QAROS is a product of QuaternAR that you may use for other work, so no block of this document removes or uninstalls it. Uninstall it with its own uninstaller from the install folder when you have no use for it, and remove the variable `QAR_SDK_ROOT` with the blocks below.

Close every program that uses the layout first, and go back to your home folder, so that the window does not stand in a folder that the blocks remove:

```powershell
Set-Location "$env:USERPROFILE"
```

The first block lists what the second one deletes and changes nothing: it prints the folder and runs the removal with `-WhatIf`, which only reports what it would do. Read the folder before you go on, because the second block deletes without asking. Both blocks stop with an error when `QAR_ROOT` is empty, does not start with a drive letter, a colon and a backslash (so a network path and every path that starts with two backslashes are refused), is the root of a drive, or is your user profile folder or a folder above it.

```powershell
$folder = $env:QAR_ROOT
if (-not $folder -or $folder -notmatch '^[A-Za-z]:\\') {
    throw 'QAR_ROOT is empty or does not start with a drive letter and a backslash: nothing is removed.'
}
$full = [System.IO.Path]::GetFullPath($folder).TrimEnd('\')
$profileFolder = [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\')
if ($full -eq [System.IO.Path]::GetPathRoot($full).TrimEnd('\') -or
    $full -eq $profileFolder -or
    $profileFolder.StartsWith("$full\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove '$full': it is a drive, your user profile folder or a folder above it."
}
"Folder to remove: $full"
foreach ($name in 'QAR_DEPS_ROOT', 'ROS2_INSTALL_ROOT', 'QAR_QT_ROOT', 'QAR_VENV', 'QAR_PREFIX') {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ($value -and -not "$value\".StartsWith("$full\", [System.StringComparison]::OrdinalIgnoreCase)) {
        "Not removed, outside that folder: $name = $value"
    }
}
Remove-Item -LiteralPath $full -Recurse -Force -WhatIf
```

> Success: the block prints one line that starts with "Folder to remove:" and gives the full path of the folder that `QAR_ROOT` names, one line that starts with "Not removed, outside that folder:" for each of the other folder variables that points outside it, and a line from `-WhatIf` that starts with "What if: Performing the operation" and names the folder. It deletes nothing. When `QAR_ROOT` is not a safe folder, it ends with an error that says so.
>
> This block changes nothing and prints your own folders, so no output is shown here.

The second block deletes the folder that `QAR_ROOT` names and then the seven variables, and nothing else. It leaves the QarOS SDK, the other items of the table and the programs of the toolchain alone. If you gave `QAR_DEPS_ROOT`, `ROS2_INSTALL_ROOT`, `QAR_QT_ROOT`, `QAR_VENV` or `QAR_PREFIX` a value outside the folder of `QAR_ROOT`, delete those folders yourself: the first block prints them. Run the second block when the first one shows the folder that you want to remove:

```powershell
$folder = $env:QAR_ROOT
if (-not $folder -or $folder -notmatch '^[A-Za-z]:\\') {
    throw 'QAR_ROOT is empty or does not start with a drive letter and a backslash: nothing is removed.'
}
$full = [System.IO.Path]::GetFullPath($folder).TrimEnd('\')
$profileFolder = [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\')
if ($full -eq [System.IO.Path]::GetPathRoot($full).TrimEnd('\') -or
    $full -eq $profileFolder -or
    $profileFolder.StartsWith("$full\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove '$full': it is a drive, your user profile folder or a folder above it."
}
Remove-Item -LiteralPath $full -Recurse -Force
foreach ($name in 'QAR_ROOT', 'QAR_DEPS_ROOT', 'ROS2_INSTALL_ROOT', 'QAR_QT_ROOT', 'QAR_VENV', 'QAR_PREFIX', 'QAR_SDK_ROOT') {
    [Environment]::SetEnvironmentVariable($name, $null, 'User')
}
```

> Success: the command prints nothing and ends without an error. Afterwards the folder `C:\qar` is gone and the seven variables are deleted from the environment of your Windows user: a new PowerShell 7 window shows them as empty, while a window that was already open keeps the values it had. The QarOS SDK is still there.
>
> This command changes your machine, so no output is shown here.

## Next steps

Continue with [Getting started](GETTING_STARTED.md): it checks the launch configuration, starts RViz with the plugin, connects it to QarOS and walks through the panel, the AR tools, the video and the picking of points, and then runs the examples of the web libraries. [Operator guide](OPERATOR_GUIDE.md) describes every control of the panel. [Testing](TESTING.md) explains what the tests cover, [Configuration](CONFIGURATION.md) lists every build option, launcher parameter and environment variable, and [Troubleshooting](TROUBLESHOOTING.md) helps when a step fails.
