# Troubleshooting

## Symptom index

Find the symptom in the table; the second column leads to the entry that explains it. The first two sections hold the problems of the install and the build and those of the run in general, and the sections after the logs hold the problems of the plugin.

| Symptom | Section |
|---|---|
| Configuring fails with an error about a file name or a path that is too long | [A deep folder breaks the configure step](#a-deep-folder-breaks-the-configure-step) |
| PowerShell says that running scripts is disabled, or asks whether to run a script from the internet | [PowerShell refuses to run a script](#powershell-refuses-to-run-a-script) |
| A command fails with a syntax error or an unknown command in a window titled Windows PowerShell | [A command of this documentation fails in Windows PowerShell](#a-command-of-this-documentation-fails-in-windows-powershell) |
| pip, aqt, git, winget or CMake stops with a certificate error | [A download fails with a certificate error](#a-download-fails-with-a-certificate-error) |
| Configuring ends with a fatal error that says that GoogleTest is not available | [Configuring stops because GoogleTest is not available](#configuring-stops-because-googletest-is-not-available) |
| A script says that Visual Studio 2019 with the C++ x64 build tools was not found | [The scripts cannot find Visual Studio 2019](#the-scripts-cannot-find-visual-studio-2019) |
| rviz2.exe, ros2 or rclpy fails at start with a missing DLL (status 0xC0000135) | [A program of the ROS 2 distribution fails to load a DLL](#a-program-of-the-ros-2-distribution-fails-to-load-a-dll) |
| ROS 2 cannot load a middleware, or rviz2.exe ends with the exit code 0xC0000409 and shows no window | [The middleware cannot start, or RViz exits without a window](#the-middleware-cannot-start-or-rviz-exits-without-a-window) |
| Configuring says that the module ament_package or a typesupport package is not found | [CMake cannot find ROS 2 packages](#cmake-cannot-find-ros-2-packages) |
| find_package(rviz_common) fails, or a path under C:/pixi_ws is rejected | [Configuring fails in a ROS 2 vendor package or on a path of the build machine](#configuring-fails-in-a-ros-2-vendor-package-or-on-a-path-of-the-build-machine) |
| rviz2.exe exits at once with the message to instantiate the QApplication object first | [RViz exits at start and Qt complains that no application exists](#rviz-exits-at-start-and-qt-complains-that-no-application-exists) |
| The plugin build fails when it replaces qar_rviz_streaming.dll | [The plugin cannot be rebuilt because its DLL is in use](#the-plugin-cannot-be-rebuilt-because-its-dll-is-in-use) |
| The plugin build stops with Cannot build the plugin: install qar_ogre_adapter and qar_qt_streaming | [The plugin build stops because the upstream packages are not installed](#the-plugin-build-stops-because-the-upstream-packages-are-not-installed) |
| The plugin build stops at Qt5 CMake config or at ninja | [The plugin build cannot find Qt 5 or Ninja](#the-plugin-build-cannot-find-qt-5-or-ninja) |
| The compiler prints warning D9025 while it builds the plugin | [The compiler prints warning D9025 for one source file](#the-compiler-prints-warning-d9025-for-one-source-file) |
| A Qt program or RViz cannot initialise a Qt platform plugin | [A Qt program cannot find its platform plugin](#a-qt-program-cannot-find-its-platform-plugin) |
| ros2 fails with an error that a process could not be created | [The ros2 command does not start](#the-ros2-command-does-not-start) |
| Creating the session fails, and the launcher mentions MsgInternalError | [Creating a QarOS session fails because UDP port 7446 is held](#creating-a-qaros-session-fails-because-udp-port-7446-is-held) |
| A large topic stops on a domain after programs crashed | [Large ROS messages stall after a crash](#large-ros-messages-stall-after-a-crash) |
| One live ROS test fails or crashes once and passes again | [A live ROS test fails now and then](#a-live-ros-test-fails-now-and-then) |
| RViz ends when you press Connect to QarOS, or an access violation inside qar_library_init has MSVCP140.dll on the stack | [The QarOS library crashes while it starts](#the-qaros-library-crashes-while-it-starts) |
| The launcher warns or stops because the C++ runtime beside rviz2.exe is older than 14.40 | [The QarOS library crashes while it starts](#the-qaros-library-crashes-while-it-starts) |
| The AR picture stutters when another window covers RViz | [The stream stutters when the source window is covered](#the-stream-stutters-when-the-source-window-is-covered) |
| The frame rate is low on a laptop with an integrated and a discrete GPU | [The picture is rendered on the wrong graphics adapter](#the-picture-is-rendered-on-the-wrong-graphics-adapter) |
| Log files of a gigabyte or more, or all streams stop every 15 seconds | [Logs grow very large, or all streams stall every 15 seconds](#logs-grow-very-large-or-all-streams-stall-every-15-seconds) |
| The launcher warns that a drive is nearly full, that the GPU is in use or that the commit charge is low | [The launcher warns about the drive, the graphics processor or the memory](#the-launcher-warns-about-the-drive-the-graphics-processor-or-the-memory) |
| RViz does not repaint for a moment when you invite a peer, stream a panel or close one | [RViz stops repainting for a moment when you invite a peer or stream a panel](#rviz-stops-repainting-for-a-moment-when-you-invite-a-peer-or-stream-a-panel) |
| A publisher script prints that this Python has no ROS 2 packages, or a program ends with No module named rclpy | [A script stops with No module named rclpy](#a-script-stops-with-no-module-named-rclpy) |
| Where are the logs of QarOS? | [Where the QarOS library writes its logs](#where-the-qaros-library-writes-its-logs) |
| How loud are the logs of QarOS, and which setting wins? | [Which severity applies](#which-severity-applies) |
| How large is a trace log? | [How much a trace log writes](#how-much-a-trace-log-writes) |
| A session cannot be created and the log does not say why | [The log of the transport](#the-log-of-the-transport) |
| Which lines of the log of RViz do I read first? | [The first lines to read in the log of RViz](#the-first-lines-to-read-in-the-log-of-rviz) |
| The panel Qar Streaming is missing in RViz, or a launcher says that the overlay was not found | [The panel is missing in the list of panels](#the-panel-is-missing-in-the-list-of-panels) |
| The panel shows Start Qar and a simulated peer, and no Visualizer opens | [The panel offers Start Qar and no connection to QarOS](#the-panel-offers-start-qar-and-no-connection-to-qaros) |
| After Connect to QarOS the label stays on CONNECTING and Last error shows QarOS connect failed | [The connection fails and the label stays on CONNECTING](#the-connection-fails-and-the-label-stays-on-connecting) |
| RViz stops with 0xC0000409 after a robot description reached the Robot model display, for example in the test scene with the robot model | [RViz stops with 0xC0000409 when a robot description reaches the Robot model display](#rviz-stops-with-0xc0000409-when-a-robot-description-reaches-the-robot-model-display) |
| Windows reports that rviz2.exe stopped working when you close RViz | [RViz crashes when it is closed](#rviz-crashes-when-it-is-closed) |
| The Visualizer or a device is there and the heartbeat says streams=0 | [No stream opens for a device](#no-stream-opens-for-a-device) |
| A stream is listed and the room is empty or shows only the grid | [A stream opens and the scene is empty](#a-stream-opens-and-the-scene-is-empty) |
| The Visualizer shows a black picture | [The picture is black](#the-picture-is-black) |
| Every object has the same colour in the depth | [The depth shows no gradation](#the-depth-shows-no-gradation) |
| The scene appears turned by a quarter, with the floor like a wall | [The scene is tipped on its side](#the-scene-is-tipped-on-its-side) |
| The shading in the headset changes when the RViz camera moves | [The shading of the scene swings with the camera of RViz](#the-shading-of-the-scene-swings-with-the-camera-of-rviz) |
| No panel Displays (AR) appears after the connection | [The hidden Displays panel is not in AR](#the-hidden-displays-panel-is-not-in-ar) |
| The Visualizer disappears after the hidden Displays panel was started again following a stop, or at times when another panel is closed or created | [The Visualizer crashes when the hidden Displays panel is started again](#the-visualizer-crashes-when-the-hidden-displays-panel-is-started-again) |
| A closed-window notice stays in the headset, or a dock that you showed again is not streamed | [An AR panel stays after its window was closed](#an-ar-panel-stays-after-its-window-was-closed) |
| You click a panel in AR and the control does not react | [A click in AR does nothing](#a-click-in-ar-does-nothing) |
| The page of an AR tool does not load in the headset | [The headset cannot open the page of an AR tool](#the-headset-cannot-open-the-page-of-an-ar-tool) |
| A click on the scene publishes nothing on the pick topics | [A pick publishes nothing](#a-pick-publishes-nothing) |
| ros2 bag play runs and RViz shows an empty scene | [A recorded bag plays and RViz shows nothing](#a-recorded-bag-plays-and-rviz-shows-nothing) |
| Point clouds stay empty and the message filter of RViz drops messages | [RViz drops messages because transforms are missing](#rviz-drops-messages-because-transforms-are-missing) |
| The log of RViz says that a lookup would require extrapolation into the past | [A transform lookup fails with an extrapolation error](#a-transform-lookup-fails-with-an-extrapolation-error) |
| A ros2 bag command says that it could not find the metadata of the bag | [A bag has no metadata file](#a-bag-has-no-metadata-file) |
| A raw video topic stutters in the Image display and in the AR video panel | [Large images stutter in RViz and in AR](#large-images-stutter-in-rviz-and-in-ar) |
| An Image display stays on No Image | [An Image display shows No Image](#an-image-display-shows-no-image) |
| The configure step stops with a Python error about `ament_package`, or with a message that a typesupport package is not found | [CMake cannot find ROS 2 packages](#cmake-cannot-find-ros-2-packages) |
| The compiler stops in `type_description.hpp` with the errors C2236 and C2332 | [The compiler stops at a member named interface](#the-compiler-stops-at-a-member-named-interface) |
| A program fails at start with the status 0xC0000135, or a DLL is missing | [A program of the ROS 2 distribution fails to load a DLL](#a-program-of-the-ros-2-distribution-fails-to-load-a-dll) |
| A path is too long, a script is refused, a command fails in Windows PowerShell, or a download fails | [Installation and build problems](#installation-and-build-problems) |
| A Qt program cannot find its platform plugin | [A Qt program cannot find its platform plugin](#a-qt-program-cannot-find-its-platform-plugin) |
| Creating a QarOS session fails, or the QarOS library crashes while it starts | [Run-time problems](#run-time-problems) |
| A live ROS test fails now and then | [A live ROS test fails now and then](#a-live-ros-test-fails-now-and-then) |
| The address of a panel answers 404 | [The address of a panel answers 404](#the-address-of-a-panel-answers-404) |
| A page cannot open its socket: 403, 426, 503 or a closed socket | [A viewer cannot connect](#a-viewer-cannot-connect) |
| The headset cannot reach the server | [The headset cannot reach the server](#the-headset-cannot-reach-the-server) |
| The page says that its script did not run | [The page says that its script did not run](#the-page-says-that-its-script-did-not-run) |
| The page says that the browser is missing features | [The page says that the browser is missing features](#the-page-says-that-the-browser-is-missing-features) |
| The panel is squeezed, too small, or shows only the top of the page | [The panel is squeezed, too small or shows only the top of the page](#the-panel-is-squeezed-too-small-or-shows-only-the-top-of-the-page) |
| The connection indicator shows Offline or Stale | [The connection indicator shows Offline or Stale](#the-connection-indicator-shows-offline-or-stale) |
| A control does nothing, or its value jumps back | [A control does nothing, or its value jumps back](#a-control-does-nothing-or-its-value-jumps-back) |
| `add_panel` refuses a model | [add_panel refuses a model](#add_panel-refuses-a-model) |
| A type cannot be found: `TypeSupportUnavailable` or `InvalidArgument` | [A message type cannot be found](#a-message-type-cannot-be-found) |
| A topic is on the graph and nothing arrives | [A topic is on the graph and nothing arrives](#a-topic-is-on-the-graph-and-nothing-arrives) |
| A service call or an action goal does not complete | [A service call or an action goal does not complete](#a-service-call-or-an-action-goal-does-not-complete) |
| Where do the messages of the libraries go, and what do they mean | [Reading the logs](#reading-the-logs) |

## Installation and build problems

### A deep folder breaks the configure step

**Symptom.** Configuring fails while it downloads or unpacks GoogleTest, with an error about a file name or path that is too long (the Windows limit is 260 characters). **Cause.** The repository was cloned into a deeply nested folder, so the paths below its build folder exceed the limit. **Fix.** Use the short layout of this documentation, `C:\qar\src\<repository>`. Enabling long paths in Windows also removes the limit, but it is a machine-wide policy that this stack neither needs nor changes.

### PowerShell refuses to run a script

**Symptom.** PowerShell reports that running scripts is disabled, or asks whether you want to run a script that came from the internet. **Cause.** The execution policy of your machine blocks unsigned scripts, or the files carry the mark that Windows puts on downloads. **Fix.** Start the script through `pwsh` with its `-ExecutionPolicy` option set to `Bypass`, or remove the mark with `Unblock-File`; [Toolchain](INSTALL.md#toolchain) explains both.

### A command of this documentation fails in Windows PowerShell

**Symptom.** A command fails with a syntax error, or with an unknown command or parameter, in a window titled Windows PowerShell. **Cause.** Every command of this documentation was run in PowerShell 7 (`pwsh`); Windows PowerShell 5.1 is not what it was written and recorded for. **Fix.** Start `pwsh` and run the command there. The helper file `scripts\qar_locations.ps1` is the one script that states that it works in both editions.

### A download fails with a certificate error

**Symptom.** `pip`, `aqt`, `git` or `winget` stops with a certificate verification error, or CMake cannot download GoogleTest. **Cause.** Your network inspects TLS traffic and re-signs it with a company certificate that these tools do not trust by themselves. On the reference machine behind such a network, the downloads of `scripts\fetch_dependencies.ps1` worked, because they use the Windows certificate store, while the tools below needed the following settings.

- `pip`: point it at a bundle that contains your company root certificate with its `--cert` option.
- `aqt` and other Python programs: set the environment variables `SSL_CERT_FILE` and `REQUESTS_CA_BUNDLE` to the same bundle.
- `git`: tell Git to use the Windows certificate store by setting its option http.sslBackend to schannel. This changes your global Git configuration; undo it by unsetting the option again.
- `winget`: install with `--source winget`, as the commands of [Toolchain](INSTALL.md#toolchain) do. The default search also queries the Microsoft Store source, which can fail with a certificate error on such networks.
- CMake: download the GoogleTest archive in another way and use the offline option of the next entry.

The command below writes the root certificates of the Windows machine store into a bundle in PEM format and prints how many certificates it holds.

```powershell
$pem = foreach ($cert in Get-ChildItem -Path Cert:\LocalMachine\Root) {
    "-----BEGIN CERTIFICATE-----`n" + [Convert]::ToBase64String($cert.RawData, 'InsertLineBreaks') + "`n-----END CERTIFICATE-----"
}
$bundle = Join-Path $env:QAR_ROOT 'ca-bundle.pem'
Set-Content -Path $bundle -Value $pem -Encoding ascii
(Select-String -Path $bundle -Pattern 'BEGIN CERTIFICATE' | Measure-Object).Count
```
```text
76
```

### Configuring stops because GoogleTest is not available

**Symptom.** `cmake --preset vs2019-x64` ends with a fatal error that says GoogleTest 1.14.0 is not available and names network access to GitHub. **Cause.** Every repository downloads GoogleTest 1.14.0 as an archive at configure time and checks its SHA256, `8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7`; without access to `github.com` the download fails. **Fix.** Download `https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz` on a machine that has access, check the SHA256, unpack it, and configure with `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST` set to the unpacked folder `googletest-1.14.0`.

### The scripts cannot find Visual Studio 2019

**Symptom.** A script stops because `vswhere` finds no Visual Studio 2019 with the C++ x64 build tools. **Cause.** Only a newer Visual Studio is installed, or the Build Tools were installed without the C++ workload and the MSVC component; the scripts accept only a 16.x installation. **Fix.** Install the components listed in [Toolchain](INSTALL.md#toolchain) and run its verify command; a newer Visual Studio next to it does no harm.

### A program of the ROS 2 distribution fails to load a DLL

**Symptom.** Programs that use the ROS 2 client libraries, `ros2` commands, `rviz2.exe` and the URDF tools such as `check_urdf.exe` fail at start with the Windows status 0xC0000135 (DLL not found), or a Python import of `rclpy` reports that a DLL load failed. **Cause.** The official release lacks `spdlog.dll`, which `rcl.dll` needs, five libraries that RViz, the transform library and the middleware need, and `tinyxml.dll`, which the URDF libraries need. **Fix.** Run the repairs of [Repairs to the ROS 2 Windows distribution](INSTALL.md#repairs-to-the-ros-2-windows-distribution), in the order given there.

### The middleware cannot start, or RViz exits without a window

**Symptom.** Initialising ROS 2 fails because no middleware can be loaded, and `rviz2.exe` ends with the exit code 0xC0000409 and shows no window. **Cause.** Fast-DDS imports the OpenSSL 1.1.1 libraries, which the release lacks; OpenSSL 3 does not help, because it has other file names. **Fix.** Run `scripts\fix_missing_openssl_dll.ps1`; it downloads a package from `conda.anaconda.org`, checks its SHA-256 and installs the pair from it. On a machine without network access, pass a local copy of the package with `-SourceArchive`, or a folder that holds both files with `-SourceDir`.

### CMake cannot find ROS 2 packages

**Symptom.** Configuring stops with a Python error that the module `ament_package` is missing, or with a message that a typesupport package of ROS 2 is not found. **Cause.** The CMake files of the distribution run the distribution's Python, which imports `ament_package`, and they look the packages up through `AMENT_PREFIX_PATH`. The CMake files of this stack set both for the configure process from `ROS2_INSTALL_ROOT`, so a wrong or unset location breaks them. **Fix.** Check `ROS2_INSTALL_ROOT` with the verify command of [ROS 2 Humble for Windows](INSTALL.md#ros-2-humble-for-windows).

### Configuring fails in a ROS 2 vendor package or on a path of the build machine

**Symptom.** `find_package(rviz_common)` fails because Eigen3, assimp, curl, yaml-cpp, TinyXML2, spdlog, orocos_kdl or console_bridge is not found, or the generate step rejects an imported target whose include path is under `C:/pixi_ws`. **Cause.** The release lacks the CMake files of these vendor packages, and its exported targets name folders of the CI machine. **Fix.** Run `scripts\fix_missing_vendor_cmake_configs.ps1`, `scripts\fix_missing_eigen.ps1` and `scripts\fix_stale_pixi_paths.ps1`; `scripts\build_plugin.ps1` runs them itself before it builds.

### RViz exits at start and Qt complains that no application exists

**Symptom.** `rviz2.exe` exits at once and a message says that the QApplication object must be instantiated first. **Cause.** RViz imports Qt DLLs whose names carry the suffix _conda; copies under those names load a second Qt core into the process. **Fix.** Run `scripts\fix_qt_conda_imports.ps1` once, and again after any new extraction of the distribution; never create copies of the Qt DLLs under the suffixed names.

### The plugin cannot be rebuilt because its DLL is in use

**Symptom.** The plugin build fails when it replaces `qar_rviz_streaming.dll`. **Cause.** A running `rviz2.exe` has loaded the DLL, and Windows does not let a loaded DLL be overwritten. **Fix.** Close RViz and build again.


### The plugin build stops because the upstream packages are not installed

**Symptom.** `scripts\build_plugin.ps1` stops with a message that begins `Cannot build the plugin: install qar_ogre_adapter and qar_qt_streaming into`, names the prefix and lists the packages for which it found no CMake package. **Cause.** The plugin is built against the installed CMake packages of qar_ogre_adapter and qar_qt_streaming, and before it builds anything the script looks for the file `share\<package>\cmake\<package>Config.cmake` below the prefix for each of them. **Fix.** Install the two repositories into the same prefix, as [Upstream repositories](INSTALL.md#upstream-repositories) shows, and run the script again. A developer who builds them from source checkouts passes `-DQAR_OGRE_ADAPTER_SOURCE_DIR` and `-DQAR_QT_STREAMING_SOURCE_DIR` in `-CMakeArgs`; the script then skips the check, and the build reads the SDK folder from `QAR_SDK_ROOT`.

### The plugin build cannot find Qt 5 or Ninja

**Symptom.** The script stops with `Qt5 CMake config not found at` and a folder, or with `ninja not found on PATH after the Visual Studio environment import.` **Cause.** The first message means that the Qt folder has no `lib\cmake\Qt5\Qt5Config.cmake`, which is the case when `QAR_QT_ROOT` or `-QtRoot` names another folder than the `msvc2019_64` folder of Qt 5.15.2. The second means that `ninja` is not on `PATH`: the build environment of Visual Studio 2019 that the script imports does not provide it, and it comes from the separate Ninja install. **Fix.** Point `QAR_QT_ROOT` at the `msvc2019_64` folder that [Qt 5.15.2](INSTALL.md#qt-5152) installs, and install Ninja as [Ninja](INSTALL.md#ninja) shows.

### The compiler prints warning D9025 for one source file

**Symptom.** While `colcon` builds the plugin, the compiler prints the warning D9025 for `hidden_rviz_panel.cpp`; it says that one option overrides another. **Cause.** The whole target is compiled with `QT_NO_KEYWORDS`, and that one source removes the definition again, because an installed header of RViz that it needs declares `public slots:` with the plain Qt keyword. The removal comes after the definition on the command line, the compiler takes the last one, and it notes the override. **Fix.** None; the note is harmless. [Qt keyword and header pitfalls](DEVELOPING.md#qt-keyword-and-header-pitfalls) explains why that file is the exception.

### The compiler stops at a member named interface

**Symptom.** A source file fails to compile at the declaration of a member named `interface` in `type_description.hpp`, with the errors C2236 and C2332. **Cause.** `<windows.h>` defines `interface` as a macro that stands for `struct`, so the member becomes `std::string struct;`, which is not valid C++. The first command shows the definitions in the headers of the Windows SDK that the toolchain installs: for C++ the header defines `__STRUCT__` as `struct` and `interface` as `__STRUCT__`, and for C it defines `interface` as `struct`. The second command shows the member of `TypeNameParts` that the macro breaks. The QarOS wrapper `qar_ar_core/qar_streaming_c.hpp` includes `<windows.h>`, so a file that includes the wrapper before a header of `qar_ui_model` fails the same way.

```powershell
Select-String -Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.19041.0\um\combaseapi.h" -Pattern '^\s*#define (interface|__STRUCT__)\s' | ForEach-Object { $_.Line.Trim() }
```

```text
#define __STRUCT__ struct
#define interface __STRUCT__
#define interface               struct
```

The second command reads a file of this repository by a path relative to the clone, so go to the folder of the clone first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

```powershell
Select-String -LiteralPath 'qar_ui_model\include\qar_ui_model\type_description.hpp' -Pattern 'std::string interface;' | ForEach-Object { '{0}:{1}: {2}' -f 'type_description.hpp', $_.LineNumber, $_.Line.Trim() }
```

```text
type_description.hpp:250: std::string interface;  ///< "msg", "srv" or "action"
```

**Fix.** Include the headers of `qar_ui_model` before every header that includes `<windows.h>`, as the examples do, or include `url_panel_sizing.hpp` first, which includes the headers of `qar_ui_model` before the wrapper. [Coding conventions](DEVELOPING.md#coding-conventions) states the rule.

## Run-time problems

### A Qt program cannot find its platform plugin

**Symptom.** A Qt program, or RViz, ends at start with a message that no Qt platform plugin could be initialised, or it does not start at all. **Cause.** Qt looks for its plugins relative to the program. The Qt folder of this layout is not next to the program, and RViz imports renamed Qt libraries that cannot find the plugin folder this way.
**Fix.** Put `$env:QAR_QT_ROOT\bin` on `PATH`, set `QT_QPA_PLATFORM_PLUGIN_PATH` to `$env:QAR_QT_ROOT\plugins\platforms` and `QT_PLUGIN_PATH` to `$env:QAR_QT_ROOT\plugins`.
In a shell that you prepared with `scripts\env_setup.ps1` both are already set.

### The ros2 command does not start

**Symptom.** `ros2` fails with an error that a process could not be created. **Cause.** The launcher of the distribution names the CI build machine. **Fix.** Run the script behind it with Python: `python $env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py` followed by the arguments of the `ros2` command, in a shell prepared with `scripts\env_setup.ps1`.

### Creating a QarOS session fails because UDP port 7446 is held

**Symptom.** Creating the session fails with a QarOS error that names neither the port nor the program that holds it; the live launcher of the plugin reports it as a message that mentions MsgInternalError.
**Cause.** QarOS uses UDP port 7446 for its discovery (zenoh scouting) and needs it free. ROS 2 participants on domain 0, the default domain, take UDP ports in steps of two upward from 7410, so after about nineteen participants, for example test publishers that were never stopped, one of them holds 7446. On the domain with the number N the steps start at 7410 + 250 x N, so only domain 0 reaches 7446. **Fix.** Find the holder and stop that program yourself. The command prints the processes that hold the port, or says that it is free.

```powershell
$holders = Get-NetUDPEndpoint -LocalPort 7446 -ErrorAction SilentlyContinue
if ($holders) {
    $holders | ForEach-Object { 'UDP 7446 is held by process {0} ({1})' -f $_.OwningProcess, (Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue).ProcessName }
} else {
    'UDP 7446 is free'
}
```
```text
UDP 7446 is free
```

### Large ROS messages stall after a crash

**Symptom.** A large topic, such as a video, stops being delivered on a domain after programs crashed or were killed, while another domain works. **Cause.** Fast-DDS keeps shared-memory segments between local ROS 2 processes, and segments of dead processes stay behind. The live tests of this stack turn the shared-memory transport off for that reason. **Fix.** Remove the segments of dead processes with the Fast-DDS command line tool, which only touches segments whose owner process has ended.

```powershell
python "$env:ROS2_INSTALL_ROOT\tools\fastdds\fastdds.py" shm clean
```
```text
...
shm.clean:
1 ports in use
1 segments in use
4 zombie ports cleaned
111 zombie segments cleaned
```

### A live ROS test fails now and then

**Symptom.** One test of an executable with live ROS 2 tests fails or crashes once, and passes when you run it again. **Cause.** Those tests start real ROS 2 participants, and their teardown is not always clean. **Fix.** Re-run only the failed tests, at most three times; a failure in any other executable is real. [Live ROS tests and the re-run rule](TESTING.md#live-ros-tests-and-the-re-run-rule) in the testing document has the command and the reasoning.

### The QarOS library crashes while it starts

**Symptom.** The process ends with an access violation inside `qar_library_init`, and the call stack is in a `MSVCP140.dll` that lies next to the program. In RViz, `"Connect to QarOS"` ends `rviz2.exe`, and the log of RViz holds a line about a `SEH FAULT` in `qar_library_init` that names the `MSVCP140.dll` of the distribution. **Cause.** QarOS needs a Microsoft C++ runtime of version 14.40 or newer, and the process was using an older copy that lies next to the host program (the ROS 2 distribution ships one beside `rviz2.exe`).
**What the loader of this stack does.** The adapter loads `qar-streaming-c.dll` by its full path, with the flags `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS` and `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` that the loader of the vendored header sets. Windows then searches for the dependencies of the library in the folder of the library first, then in the folder of the host program, the folders that the program added itself and the system folder, and never in the current folder or in `PATH`. The runtime files in the SDK `bin` folder therefore come before an older copy in the folder of the host program, and that is why you deploy the SDK folder whole: a runtime file that is missing from it is taken from the folder of the host program. This is the order of the search and no guarantee: Windows uses a module of the same name that the process already holds, whichever folder it came from, so a runtime that the host program has already loaded stays in use.
**Fix.** Close RViz, run `scripts\fix_old_vc_runtime.ps1` as [Repairs to the ROS 2 Windows distribution](INSTALL.md#repairs-to-the-ros-2-windows-distribution) says, and start RViz again, because a running RViz keeps the old runtime. The script does what the authors of qar_rviz_plugin did by hand: it replaces the runtime files next to `rviz2.exe` with the same files of version 14.40 or newer from the system folder, or from the `bin` folder of the SDK when you pass `-SdkRoot`, and it keeps the originals as `.vcrt.orig`. The live launcher `scripts\run_live_rviz_stream_real.ps1` reads the version of that `MSVCP140.dll` before it changes or starts anything: with `-Confirm` it stops when the version is older than 14.40 and prints the file, the version and the command of the repair, the dry run prints the same text as a warning, and a file whose version it cannot read only gives a warning. If the crash remains, report it together with the versions of the `MSVCP140.dll` files on your machine.

### The stream stutters when the source window is covered

**Symptom.** The AR picture is smooth while the source window is in front and stutters, with a steady frame rate that is a fraction of the target, when another window, such as a full-screen Visualizer, covers it. **Cause.** From Windows 11 on, a process whose windows are fully covered may no longer get the timer resolution it requested.
The plugin opts its process out of power throttling and requests a 1 ms timer itself when it starts.
**Fix.** Keep the source window visible, for example side by side with the Visualizer.

### The picture is rendered on the wrong graphics adapter

**Symptom.** On a laptop with an integrated and a discrete GPU, the frame rate is low or each frame is copied between adapters. **Cause.** Windows ran the program on the integrated GPU, while QarOS encodes on the NVIDIA GPU.
**Fix.** Set the graphics preference of the program to high performance in Windows Settings, under System, Display, Graphics.
The live launcher does this for `rviz2.exe` when you run it with `-Confirm`.

### Logs grow very large, or all streams stall every 15 seconds

**Symptom.** The QarOS log folder holds files of a gigabyte or more, or every stream stops for about a second and a half at a steady 15-second rhythm. **Cause.** The library log severity is trace, which writes hundreds of thousands of lines in a short run, and a real-time virus scanner or a nearly full system drive turns that into stalls.
**Fix.** Run with the severity warn or higher, and use trace only for a log that QuaternAR asks for; [Reading the logs](#reading-the-logs) explains how the severity is decided.

### The launcher warns about the drive, the graphics processor or the memory

**Symptom.** The dry run or the start of `scripts\run_live_rviz_stream_real.ps1` prints a warning that a drive is nearly full, that other processes use the NVIDIA GPU for compute, or that only a few GB of commit charge are left. **Cause.** Each of the three can stall every QarOS stream at once, so the launcher checks them before it starts anything, and it only warns. The drive warning appears when a drive that holds `TEMP`, the QarOS log folder or the log folder of the launcher has less than 10 percent or less than 15 GB free. The graphics warning lists the programs that `nvidia-smi` reports, and the launcher waits 10 seconds for that program. The memory warning appears below 4 GB of free commit charge, which is the free part of the RAM and the page file together. **Fix.** Free space, or point `QAR_PANEL_LOG_DIR` at a drive with room; stop the compute jobs; close memory-hungry programs or enlarge the page file; judge stalls only after that. [Logs grow very large, or all streams stall every 15 seconds](#logs-grow-very-large-or-all-streams-stall-every-15-seconds) explains the stalls.

### RViz stops repainting for a moment when you invite a peer or stream a panel

**Symptom.** The window of RViz neither repaints nor reacts for a short time after you press `"Invite peer"`, when a panel that you stream is created, and when a streamed panel is closed, and the heartbeat shows a `max_gap=` of hundreds of milliseconds or more. **Cause.** These calls into QarOS are synchronous and run on the Qt GUI thread of RViz. The invitation runs inside the slot of the button, the QarOS panel of a streamed window is created on a tick of the GUI thread, which calls `qar_video_sender_create` and `qar_gui_panels_add_panel` there, and the close of a panel calls `qar_gui_panels_close_panel` and destroys the video sender there too. The URL panel of an AR tool is created on a tick with `qar_gui_panels_add_panel`, changed in the handler of the viewport report of its page with `qar_gui_panels_update_pose` and `qar_gui_panels_change_size`, and closed with `qar_gui_panels_close_panel` in the slot of `"Remove tool"` or `"Re-open in AR"`, all on the GUI thread. While a call runs, the thread handles no event of the window. The connect does not belong to them, because the session is built on a worker thread, and neither does the creation of the 3D streams, which is asynchronous so that the render thread never waits for it. **Fix.** None in the plugin: press the button once and wait. The line that the adapter writes for a close says how long the call held the thread, and `max_gap=` of the heartbeat shows the longest stall of the GUI thread since the previous beat; [The first lines to read in the log of RViz](#the-first-lines-to-read-in-the-log-of-rviz) says how to use it.

### A script stops with No module named rclpy

**Symptom.** One of the publisher scripts of this repository, `scripts\publish_test_markers.py`, `scripts\publish_test_tf.py`, `scripts\publish_test_pointcloud.py`, `scripts\publish_test_robot.py`, `scripts\publish_template_topics.py` or `scripts\publish_video_file.py`, ends at once and prints `This Python has no ROS 2 packages (a plain virtual environment does not contain rclpy).` and, on the next line, `Run this script in a shell that was prepared with scripts\env_setup.ps1.` A program of your own that imports `rclpy` in the same Python ends with the traceback "ModuleNotFoundError: No module named 'rclpy'". **Cause.** The Python that runs the script has no ROS 2 packages, as a plain virtual environment has none. The scripts turn the missing module `rclpy` into that message and leave every other missing module to its traceback. **Fix.** Open a PowerShell window, dot-source `scripts\env_setup.ps1` in it, with a leading dot and a space, and run the publisher script in that window: `env_setup.ps1` imports the environment of the ROS 2 distribution and puts the virtual environment in front of `PATH`, so that `python` finds the ROS 2 packages.

## Reading the logs

The three libraries write no log of their own, to no file and to no console. They report through return values and error strings, through counters that a host reads, and through two optional hooks: the `RosUiDiagnosticsSink` of `qar_ros_ui` and the signals of a `PanelSession`. The host decides where the messages go. The examples print one line that begins with the word example and a colon for each event that they show ([Getting started](GETTING_STARTED.md#serve-a-ui-model-panel) has them).

| Where | What it holds |
|---|---|
| `WebPanelServer::diagnostics()` | the counters of the HTTP server (connections accepted and refused, requests served and rejected, upgrades, timeouts and open connections), the number of panels and viewers, the requests that named no session, and the upgrades that were refused |
| `PanelSession::diagnostics()` | the viewers, the frames in and out, the frames that were ignored, the control events, the viewport reports, the patches that were refused, the sockets that were closed by policy and the viewers that did not keep up |
| `PanelViewportReport::describe()` | one line for a report of the viewport of a page: its size in CSS pixels, the pixel ratio, the content, the root box and the reason |
| `UrlPanelSizeResult::note` | one line for the size of a panel: the page or the reported viewport, the panel in metres, and the bounds, the pin and the fit that applied |
| `TopicSubscriptionDiagnostics`, `TopicPublisherDiagnostics`, `ServiceBridgeDiagnostics`, `ActionBridgeDiagnostics`, `ActionGoalDiagnostics` | the counters of an entity, the QoS that is active, and the last error |
| `RosUiDiagnosticsSink::on_message` | the lines of the next table |


A `RosUiDiagnosticsSink` gets a severity, a category and a text. The bridges use the categories topic, service and action, and the texts are these:

| The text | Severity | What it means |
|---|---|---|
| begins with subscribed to, then the topic, its type and the QoS profile | Info | a subscription was created |
| begins with advertising, with the same three parts | Info | a publisher was created |
| begins with re-subscribed to or re-advertised, and ends with the profile that it was before | Info | `reconcile` created the entity again because the profile that suits the endpoints changed |
| the topic, then a colon and incompatible QoS | Warning | a publisher cannot be matched with the subscription, and the text says how many |
| the topic, then a colon and the reason of a conversion | Warning | a message could not be converted to a value; the reason names the field |
| the topic, then carries, the number of types and using | Warning | one topic has several types and the first one in sorted order is used |
| begins with re-subscribe failed or re-advertise failed | Warning | `reconcile` could not create the entity again |
| begins with subscribed without QoS event callbacks | Info | the middleware has no QoS events, and the subscription works without them |
| begins with no wall timer for call timeouts or for goal timeouts | Warning | the bridge could not create its timer; call `poll()` periodically |


A program that opens the page as a URL panel through qar_ogre_adapter, as the examples do with `--sdk-bin`, also has the logs of QarOS.

QarOS writes its own logs, separate from the logs of this stack. When something fails between your program and QarOS, the cause is often only in the QarOS logs, so you need to know where they are and how loud they are.

### Where the QarOS library writes its logs

The library writes its log files to the folder `quaternar` inside your `TEMP` folder, `%TEMP%\quaternar`, unless the program that initialises the library names another folder; an empty folder name selects the default. Every QarOS process that the library starts writes its own files, and the file names carry the role of the process and a GUID, for example `mixer_<guid>_stdout.log`. QuaternAR asks for full trace-level library logs with every bug report.

### Which severity applies

The library reads its severity from the environment variable `QAR_LOG_SEVERITY`, through the environment table of the C runtime, and that variable wins over the severity field of the library initialisation, in both directions: the field cannot make a run quieter than the variable, and the variable cannot be overridden by the field. A change made with the Windows function that sets an environment variable is not seen by the library; the adapter of this stack therefore sets the variable with the C runtime function before it initialises the library, and also passes the severity in the initialisation structure. The library accepts the spellings trace, debug, info, warn, warning, error and fatal, in any letter case, and no others; this stack accepts the same spellings.

In the plugin, the variable `QAR_PANEL_LOG_SEVERITY` comes first, then `QAR_LOG_SEVERITY`, then the default of the plugin. A value that is not usable is reported in the log instead of being ignored silently.

### How much a trace log writes

A run at trace severity writes hundreds of thousands of lines in a short time. Use trace only for the log that QuaternAR asks for, delete it afterwards, and run at warn or higher otherwise. [Installation and build problems](#installation-and-build-problems) and [Run-time problems](#run-time-problems) describe what a large log does to a machine.

### The log of the transport

QarOS uses zenoh as its transport. When a session cannot be created, the C API reports only a generic transport error; the actual reason, such as a port that cannot be bound, is in zenoh's own log, which stays silent unless it is switched on. The adapter of this stack switches it on when the environment variable `QAR_ZENOH_LOG` holds a tracing filter, such as debug, and only while the library severity is trace or debug; at a quieter severity the request is ignored and the log says so. Setting `RUST_LOG` alone does nothing in these programs.

### The first lines to read in the log of RViz

The log of RViz is the file `rviz_live_<stamp>.err.log` that the launcher writes, and the line to read first is the heartbeat that the panel writes every 2 seconds. The table maps what you see in it to the entry that explains it. [The heartbeat line](LOG_REFERENCE.md#the-heartbeat-line) describes every field, and [Where the logs go](LOG_REFERENCE.md#where-the-logs-go) the other files.

| In the heartbeat | It means | Entry |
|---|---|---|
| `tick_fps=` far below the frame rate of the streams, or `max_gap=` of hundreds of milliseconds | the Qt GUI thread of RViz did not run for that long | [RViz stops repainting for a moment when you invite a peer or stream a panel](#rviz-stops-repainting-for-a-moment-when-you-invite-a-peer-or-stream-a-panel) |
| `streams=0` after you invited a peer | no stream of the 3D scene is open | [No stream opens for a device](#no-stream-opens-for-a-device) |
| `skip=` climbs while `sub=` stands still, with `why=` after it | frames are not submitted, and the reason is named | [The heartbeat line](LOG_REFERENCE.md#the-heartbeat-line) |
| `rx=0` after `"AR gestures:"` on every beat | the runtime sends no gestures to the plugin | [A pick publishes nothing](#a-pick-publishes-nothing) |
| `"UNSERVED"` after the key of a panel | QarOS asks for the stream of a panel that the plugin does not serve | [A click in AR does nothing](#a-click-in-ar-does-nothing) |
| `fg=0` in a line `"AR VIDEO:"` | another window covers RViz | [The stream stutters when the source window is covered](#the-stream-stutters-when-the-source-window-is-covered) |
| `NOPUB` or `WAITING` after `vdisp` | an Image display of RViz gets nothing | [An Image display shows No Image](#an-image-display-shows-no-image) |


## What to send us

When you report a problem to the maintainers of this repository, send the facts below. They let someone reproduce your setup without a long exchange. The first command writes the facts of your machine into one text file and prints it: the commit of this repository, the Windows and tool versions, the variables of the layout, and the SHA256 of the SDK header, which identifies the SDK drop. The commit is the abbreviated hash, with the suffix "-dirty" when you changed files, or the name of a tag when the commit has one. It prints only the variables of this stack, no others.

Git reads the commit from the clone, so go to its folder first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

```powershell
$out = Join-Path $env:TEMP 'qar_support_info.txt'
$info = @(
    'repository : ' + (git describe --tags --always --dirty)
    'windows    : ' + (Get-CimInstance Win32_OperatingSystem).Version
    'powershell : ' + $PSVersionTable.PSVersion.ToString()
    'cmake      : ' + ((cmake --version) | Select-Object -First 1)
)
$info += Get-ChildItem Env:QAR_*, Env:ROS2_INSTALL_ROOT | ForEach-Object { '{0} = {1}' -f $_.Name, $_.Value }
$info += 'sdk header  : ' + (Get-FileHash -LiteralPath (Join-Path $env:QAR_SDK_ROOT 'include\qar_streaming.h') -Algorithm SHA256).Hash
$info | Set-Content -Path $out
Get-Content -Path $out
```
```text
repository : d6dd6af
windows    : 10.0.26200
powershell : 7.4.4
cmake      : cmake version 3.29.5-msvc4
QAR_ROOT = C:\qar
QAR_DEPS_ROOT = C:\qar\deps
QAR_SDK_ROOT = C:\QarOS\install
QAR_PREFIX = C:\qar\install
QAR_VENV = C:\qar\venv
QAR_QT_ROOT = C:\qar\deps\Qt\5.15.2\msvc2019_64
ROS2_INSTALL_ROOT = C:\qar\deps\ros2_humble\ros2-windows
sdk header  : 5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA
```

The line `cmake` shows the CMake of your machine. The output above is the one of the recorded runs, which used the CMake that Visual Studio 2022 version 17.11 bundles; [CMake](INSTALL.md#cmake) says what that means for the CMake that you install.

Then send the logs of the failing run. The QarOS log folder can hold gigabytes, so send only the newest files, those whose time matches the failure. The command lists the five newest files of the folder with their sizes; it prints nothing when QarOS has not written there yet.

```powershell
Get-ChildItem -LiteralPath (Join-Path $env:TEMP 'quaternar') -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object { '{0}  {1:N0} bytes' -f $_.Name, $_.Length }
```
```text
qar_streaming.log  818 bytes
qar_launcher_stdout.log  204,402 bytes
mixer_51cca380-fcfb-4c84-907e-8b668a9c9ee2_stdout.log  10,026 bytes
streaming_viz_b829d79d-b8a8-478e-bf23-c0d28ca52ad0_stdout.log  866 bytes
mixer_51cca380-fcfb-4c84-907e-8b668a9c9ee2_stderr.log  0 bytes
```

Send, in addition:

1. The file `$env:TEMP\qar_support_info.txt` that the first command wrote.
2. The newest files of the QarOS log folder, and a log made at trace severity if the problem is between your program and QarOS; [Reading the logs](#reading-the-logs) explains how to switch it on.
3. The RViz logs of the run: the live launcher writes `rviz_live_<stamp>.out.log` and `rviz_live_<stamp>.err.log` into its log folder, which defaults to `qar_rviz_logs` inside your `TEMP` folder.
4. The steps that lead to the problem, in the order you did them, and what you expected to see. A screenshot helps when the problem is visible.
5. Anything you changed from this documentation: other folders, other versions, a proxy.

## RViz and plugin problems

Three problems of RViz and of the plugin build have their entries in [Installation and build problems](#installation-and-build-problems): [the middleware cannot start, or RViz exits without a window](#the-middleware-cannot-start-or-rviz-exits-without-a-window), [RViz exits at start and Qt complains that no application exists](#rviz-exits-at-start-and-qt-complains-that-no-application-exists) and [the plugin cannot be rebuilt because its DLL is in use](#the-plugin-cannot-be-rebuilt-because-its-dll-is-in-use). The entries below are the others.

### The panel is missing in the list of panels

**Symptom.** RViz starts, but the panel named Qar Streaming is not among its panels, or a launcher stops with a message that begins `Plugin overlay not found at` or `Overlay not found at`. **Cause.** RViz finds the panel through pluginlib: `plugin_description.xml` is registered for the base package `rviz_common` in the resource index of ament, and ROS 2 reads that index through the folders of `AMENT_PREFIX_PATH`. The chain breaks when the shell that started RViz has not sourced the overlay of the prefix, so that `AMENT_PREFIX_PATH` does not hold the prefix, or when the prefix has no entry `share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming` because the plugin was not installed into it. The launchers source `local_setup.ps1` of the prefix themselves, and the live launcher stops with the first message and the preview script with the second when that file is missing; the dry run of the live launcher only warns. **Fix.** Run the commands of [Install smoke test](INSTALL.md#install-smoke-test) in the shell that starts RViz: the first prints the prefix that ROS 2 finds for the package and the second checks the three files. Run `scripts\build_plugin.ps1` when a file is missing, dot-source `$env:QAR_PREFIX\local_setup.ps1` before you start `rviz2.exe` yourself, or use a launcher.

### The panel offers Start Qar and no connection to QarOS

**Symptom.** The button that should read `"Connect to QarOS"` reads `"Start Qar"`, the button `"Add simulated peer"` takes the place of `"Invite peer"`, and after the click the state label says `"Qar runtime: RUNNING (offline preview)"` and the list holds a simulated peer, while no Visualizer opens. **Cause.** `QAR_PANEL_LIVE_STREAM` is not set in the environment of RViz, so the panel runs as the offline preview, which has no session. The launcher `scripts\run_live_rviz_stream_real.ps1` sets the variable, and `scripts\run_rviz_with_plugin.ps1` starts RViz in the preview. **Fix.** Start RViz with the live launcher, or set `QAR_PANEL_LIVE_STREAM` and the other variables that [Running RViz without the launcher](CONFIGURATION.md#running-rviz-without-the-launcher) lists. Any value that is not empty switches the mode on, `0` and `off` included.

### The connection fails and the label stays on CONNECTING

**Symptom.** After you press `"Connect to QarOS"` the state label says `"Qar runtime: CONNECTING..."` and stays there, and `"Last error:"` turns red with a text that begins `"QarOS connect failed:"` or `"QarOS connect threw:"`. **Cause.** The session is built on a worker thread, and a connect that fails ends with the text of the failure. The panel shows that text and does not refresh the state label, which keeps the words of the connect in progress. The text names the call that failed. The failure that comes first on a new machine is `"qar_library_load failed for"` and a path: the library `qar-streaming-c.dll` was not found in the folder that `QAR_RUNTIME_BINARIES_DIR` names, or one of its dependencies did not resolve. The loader searches the folder of the library first and never `PATH`. A session that cannot be created because UDP port 7446 is held fails in `qar_runtime_create_session`, and [Creating a QarOS session fails because UDP port 7446 is held](#creating-a-qaros-session-fails-because-udp-port-7446-is-held) has that case. **Fix.** Read the text, correct the cause, and press `"Connect to QarOS"` again: a failed connect leaves no session behind, so that the next click builds one anew. The launcher sets `QAR_RUNTIME_BINARIES_DIR` from `-SdkRoot` or `QAR_SDK_ROOT` and stops when that folder holds no `bin\qar-streaming-c.dll`.

### RViz stops with 0xC0000409 when a robot description reaches the Robot model display

**Symptom.** RViz ends with the exit code 0xC0000409, and its crash report names an unhandled C++ exception, shortly after a robot description reaches the Robot model display: when you open the test scene `scene4_grid_robot.rviz` while `scripts\publish_test_robot.py` publishes the robot, and with any other robot. The other four test scenes have no Robot model display. It differs from [the entry in which RViz never shows a window](#the-middleware-cannot-start-or-rviz-exits-without-a-window). **Cause.** The display passes the robot description to the URDF parser of the distribution. The parser is the plugin `urdf_xml_parser.dll`, which needs `urdfdom_model.dll`, and that library imports `tinyxml.dll`. The release of the distribution does not contain that file, so the load of the plugin fails with the Windows error 126, and the display does not catch the exception. **Fix.** Run `scripts\fix_missing_tinyxml_dll.ps1` as [Repairs to the ROS 2 Windows distribution](INSTALL.md#repairs-to-the-ros-2-windows-distribution) says, and start RViz again. The program `check_urdf.exe` of the distribution tells whether the file works: it parses a robot description and prints `Successfully Parsed XML`, and before the repair it ends with the Windows status 0xC0000135. The launcher `scripts\run_live_rviz_stream_real.ps1` looks for the file before it changes or starts anything and stops with `-Confirm` when it is missing.

### RViz crashes when it is closed

**Symptom.** When you close RViz, Windows reports that `rviz2.exe` stopped working, or the log of the launcher ends without a clean shutdown. **Cause.** The authors of qar_rviz_plugin know a crash of RViz while it exits and tolerate it; most of the runs that they recorded ended with a crash report. **Fix.** None known. Judge a run by its heartbeat lines and not by a clean exit. Closing RViz while a connect still runs can take a few seconds longer, because the panel waits for the worker thread of the connect instead of unloading its library under it.

## Nothing or the wrong picture in QarOS

Three causes of an empty or frozen headset have their entries in [Run-time problems](#run-time-problems): [UDP port 7446](#creating-a-qaros-session-fails-because-udp-port-7446-is-held), [large logs and stalls every 15 seconds](#logs-grow-very-large-or-all-streams-stall-every-15-seconds) and [a covered source window](#the-stream-stutters-when-the-source-window-is-covered). The entries below are the others, in the order in which a session goes wrong: no stream, no content, a wrong picture.

### No stream opens for a device

**Symptom.** The Visualizer or the device is there, but nothing is rendered for it: the heartbeat says `streams=0` after the invitation, and the line `LIVE: opened stream for peer` never appears. After `"Invite peer"` the list may hold a simulated peer instead. **Cause.** The plugin streams only to peers that it admitted, and a peer that is not admitted leaves no line in the log. The default policy admits only the peers that the plugin invited itself, by their exact identifier, so a headset that joins QarOS by itself or a Visualizer that you started by hand is not one of them; a peer above `QAR_PANEL_MAX_PEERS` is refused with a warning. `"Invite peer"` invites a device only while the label shows `"Qar runtime: RUNNING (live)"`: before the connection and after `"Disconnect (pause)"` it adds a simulated peer. **Fix.** Connect first, and press `"Invite peer"` for the kind of device that you want. For devices that join by themselves start the launcher with `-RealPeers`, which selects the policy `new`: it also admits a peer that first appears after the settle time, 5 seconds by default. An invitation is more reliable than that timing rule, and [Inviting peers](OPERATOR_GUIDE.md#inviting-peers) lists the policies.

### A stream opens and the scene is empty

**Symptom.** A Visualizer window is open and the heartbeat lists its stream, but the room is empty or shows only the grid. **Cause.** There are two common ones. RViz has nothing to show: the scene `scene3_grid_marker.rviz` draws markers, and without the publisher `scripts\publish_test_markers.py` it draws the grid only; the live launcher starts that publisher with `-Confirm` only. Or the stream has no app volume: QarOS composites the streams by app volume, and a stream without one is not placed in the scene. `QAR_PANEL_APP_VOLUME=off` creates none, and when a volume cannot be created the adapter writes a line with the words `NO render stream can be created without an app volume` and the reason. **Fix.** Start a publisher or play a bag, and leave `QAR_PANEL_APP_VOLUME` unset; only the word `off` switches the volume off.

### The picture is black

**Symptom.** The Visualizer shows a black picture, and the log has the line `LIVE: GL submit mode = supply-own (expected BLACK against the real SDK)`. **Cause.** `QAR_PANEL_GL_SUBMIT` is `supply-own`. In that mode the plugin hands QarOS texture identifiers of its own, and the runtime overwrites them, so that the picture stays black; the default, `render-into`, copies the picture of RViz into the textures that QarOS owns. **Fix.** Unset the variable or set it to `render-into`. A value that is neither is reported in `"Last error:"`.

### The depth shows no gradation

**Symptom.** The picture is right, but in the depth every object has the same colour. **Cause.** The Visualizer reads the window-space depth value, which is reciprocal in the distance, so a far plane much wider than the scene squeezes the scene into the first per cent of the range. The plugin renders with the planes 0.01 m and 150 m, and `QAR_PANEL_NEAR_M` and `QAR_PANEL_FAR_M` change them; the authors of qar_rviz_plugin recorded the symptom for content between 0.14 and 4.99 m under a range of 0.1 to 500 m. The depth is also wrong when `QAR_DEPTH_ENCODING` is not `ndc`, because the Visualizer does not use `depth_scale` or the advertised range to correct it. **Fix.** Look for the line `LIVE: near/far =` in the log, set `QAR_PANEL_FAR_M` to the size of the scene plus some margin, and keep `QAR_DEPTH_ENCODING` at `ndc`, which the launcher sets. Everything beyond the far plane is clipped away, so a far plane that is too close removes content.

### The scene is tipped on its side

**Symptom.** The scene appears turned by a quarter, and the floor stands like a wall. **Cause.** QarOS reports head poses in a world whose y axis is up, and RViz renders a world whose z axis is up. The conversion applies only for `QAR_POSE_WORLD` with the exact word `zup`; without it the pose conversion is the identity. **Fix.** Set `QAR_POSE_WORLD` to `zup`, as the launcher does; another spelling changes nothing.

### The shading of the scene swings with the camera of RViz

**Symptom.** The brightness of the faces of an object changes when the operator at RViz moves the camera, and the headset sees it. **Cause.** RViz re-aims its only light at the camera of its own view in every frame, a headlight, so that the shading swings with the RViz operator. The live mode suppresses that light and lights the scene with a rig of its own, unless `QAR_SCENE_LIGHT_HEADLIGHT` is `keep` or `on`, or `QAR_SCENE_LIGHT` is `off`. **Fix.** Remove those settings. The line `Scene lighting: mode=` in the log says the mode and whether the headlight is kept or suppressed.

## AR panel and tool problems

### The hidden Displays panel is not in AR

**Symptom.** After the connection no panel `"Displays (AR)"` appears in the headset or in the Visualizer. **Cause.** The log has a line that says why. `AR PANEL: the hidden Displays panel is not started on connect` means that `QAR_PANEL_HIDDEN_DISPLAYS` is `0`, `false`, `off` or `no`, which stops only the automatic start. `AR PANEL: the hidden Displays panel was not started:` and a reason mean that the session or the panel manager was not ready, or that RViz gave the panel no display context. `AR PANEL: could not create the hidden Displays panel` means that the copy of the Displays panel of RViz could not be made: it is made through the panel factory of the main window of RViz, which the plugin accepts only after the factory has identified itself by its run-time type information, and the line names the class identifier, the factory and the reason; the plugin then goes on without the feature. A stream that was stopped, or that the operator closed in AR, is gone until it is started again. **Fix.** Read the reason in the line. Start the copy by hand with the entry of the list in the group `"AR GUI panels"` that starts with `"Displays (AR copy "` and ends with `"hidden in RViz)"`, and press `"Stream panel to AR"`. When the check of the factory rejected the object, rebuild the plugin against the distribution that runs RViz, because the access assumes that the installed headers match the installed binaries, and report the line. [The hidden Displays panel](ARCHITECTURE.md#the-hidden-displays-panel) explains the mechanism.

### The Visualizer crashes when the hidden Displays panel is started again

**Symptom.** The window of the QarOS Visualizer disappears, and so do the windows of all other Visualizers that run, within a second of the start of the hidden Displays panel after it was stopped: press `"Stop streaming"` on its entry, wait about three seconds and press `"Stream panel to AR"`. At times the same happens within a second of another close or creation of a panel, such as the close of the panel of a dialog, the close and the creation again of a video panel, or the opening of the page of an AR tool. QarOS starts a Visualizer again after several seconds, and until then the 3D streams stand still. A Visualizer that is started again can show an empty or black scene. What you see for those seconds is the crash and the restart of the Visualizer, and not a slow start.

**Cause.** With a Visualizer attached, the Visualizer crashed in every one of the 25 cases that the authors of qar_rviz_plugin observed in which the hidden Displays panel was created again after it had been closed, within a second of the creation. It happened with the title unchanged, with a numbered title, with a shifted position of the panel and with a pause of 8 seconds before the start. In one longer session it crashed five more times, each within a second of the close or the creation of a panel: twice at the close of the panel of a dialog, once at the close and the creation again of a video panel, and twice at the opening of the page of an AR tool. It did not crash at the other closes and creations of panels in that session. It is a defect of the Visualizer, and no title that the plugin chooses prevents it. The crash reports are those of the process of the Visualizer and not those of RViz: all of them name the same access violation, which reads address 0x4 at the same place inside `WebCore.dll`, the browser engine library of QarOS. The SDK documents no rule for display names.

**What the plugin does.** The plugin gives every QarOS panel that it starts for a window of RViz or for a video a title that no earlier QarOS panel of the same RViz process used. The first start under a title keeps the title, and each later start under it gets a number after it. The hidden Displays panel is streamed under the title `"Displays (AR)"`, so its restarts are titled Displays (AR) (2), then Displays (AR) (3), then Displays (AR) (4), as long as nothing else was started under that title in between. This does not prevent the crash, and it has no effect on what the restarted Visualizer shows that the authors of qar_rviz_plugin could see: in their trials the Visualizer that QarOS started again showed an empty or black scene in some of the restarts, with a numbered title as well as with the unchanged one, and the difference between the two was within chance. The numbering is a precaution of unproven value. The count belongs to the title that was asked for and lasts until RViz is restarted, so that `"Disconnect (pause)"` and a new connect do not reset it. The docks, the panel itself and the video entries of the list in the group `"AR GUI panels"`, the hidden Displays panel and the video panel that an image tool opens are numbered in the same way. A start that only brings back a retired panel in the close mode `notice` creates no QarOS panel, so it is not counted and the panel keeps its title. Only the title that the headset and the Visualizer show carries the number: the key of the panel, such as `hidden:Displays`, and the entries of the list do not change. Each start that gets a number writes one line to the log of the panel and of RViz, which has the words `gets the QarOS panel name`, and [AR panel lines](LOG_REFERENCE.md#ar-panel-lines) has its form.

**Fix.** No setting of the plugin removes the cause, which is in the Visualizer. The close mode `notice` avoids the crash at the price of a notice panel in AR for each closed window, which you close in AR: a start in that mode brings the notice panel back to life and creates no QarOS panel, and no crash occurred in the 4 restarts that the authors of qar_rviz_plugin tried in that mode. [An AR panel stays after its window was closed](#an-ar-panel-stays-after-its-window-was-closed) describes the mode. Do not close and start panels in quick succession, because a quick burst of closes and starts once gave a black scene in the trials of the authors. After a start, wait several seconds for the Visualizer to come back before you press the next button, and press `"Invite peer"` for a new Visualizer if none comes back. Send the crash reports of the Visualizer, which are the files of the QarOS log folder whose names end in crash.txt, with the logs that [What to send us](#what-to-send-us) lists, and say whether a line with `gets the QarOS panel name` stood in the log before the crash.

**Panels without a number.** The numbering covers the panels that the plugin starts for a window of RViz or for a video. It does not cover the panel that the library of Qt creates by itself for a dialog of a streamed window, such as the dialog of the `Add` button of the Displays panel, which takes the title of the dialog; the panel that the library creates again under the same title when the size of a streamed window has changed and settled; and the URL panels of the AR tools, which take the name of their topic, service or action. Open and close those no more often than you need.

### An AR panel stays after its window was closed

**Symptom.** A panel in the headset shows the text `This window has been closed in RViz, so its picture is no longer live.`, or you showed a closed dock again and its panel does not come back. **Cause.** In the close mode `notice` the AR panel stays as a notice and its video sender is kept until the session ends; the plugin offers that mode for a QarOS runtime that does not survive the destruction of a sender, and the line `AR PANEL: close mode` says which mode is in force. The default mode, `destroy`, closes the AR panel together with its window, and showing a closed dock again does not restart its stream. **Fix.** Unset `QAR_PANEL_CLOSE_MODE` or set it to `destroy`, and press `"Stream panel to AR"` to stream the window again. [Closing panels](OPERATOR_GUIDE.md#closing-panels) lists what closes a panel.

### A click in AR does nothing

**Symptom.** You point at a panel in AR and click, and the control does not react. **Cause.** The first line of the readout `"AR panels:"` has the form `"input: in %1, missed %2, dropped %3"` and counts the pointer samples: the first number is everything that reached the library, the second the samples that resolved to no panel, and the third the samples that the state machine refused, with the reason in brackets. With the first number at 0 the runtime sends nothing, and then no other number can be anything but zero; a growing second number says that the pointer is aimed at nothing, and the third that the sequence of events was wrong, for example a release without a press. A panel that holds a sender and has never submitted a frame is marked `"UNSERVED"` in the heartbeat. **Fix.** Look at the first number first. When it stays at 0 the cause is the session or the SDK drop and not the panel: check the drop as [Identifying an SDK drop](SDK_COMPATIBILITY.md#identifying-an-sdk-drop) shows. [AR GUI panels](OPERATOR_GUIDE.md#ar-gui-panels) describes the readout.

### The headset cannot open the page of an AR tool

**Symptom.** An AR tool opens in the headset but its page does not load, or `"Copy URL"` gives an address that another machine cannot open. **Cause.** The page is served by the web server of the plugin at an address of the form `http://<host>:<port>/p/<token>/`, and the headset has to reach that host and that port. By default the server listens on every network interface at a port that the operating system picks, so the port changes at every start, and the host in the address is the one that the machine uses towards the local network, which can be the wrong network on a machine that has several. A wrong or missing token is answered with a 404. **Fix.** Read the line `AR TOOL: panel server listening on` for the address in use. Pin the port with `QAR_PANEL_WEB_PORT` and open it in the firewall of the machine, and set `QAR_PANEL_WEB_HOST` to the address of the machine in the network of the headset. [What this stack adds](GETTING_STARTED.md#what-this-stack-adds) lists what the network has to allow.

### A pick publishes nothing

**Symptom.** You aim at the scene and click in AR, and no message appears on `/clicked_point`, `/goal_pose` or `/initialpose`. **Cause.** Several gates stand between the click and the topic, and the line `"Spatial:"` and the heartbeat show which one is closed. The mode starts at `Off`. A pick waits for `"Confirm pick"` unless `"Publish without confirming"` is ticked. The default target refuses a click that the runtime resolved to nothing, which counts as `clicks-on-nothing=`, and a hover only previews. `rx=0` after `"AR gestures:"` on every beat says that the runtime sends no gestures, and `NOT SUBSCRIBED` names the reason why the plugin never subscribed; a session without an app volume subscribes to nothing. **Fix.** Arm a mode, aim at the floor or at a surface of the scene, and confirm the pick. [Spatial picking](OPERATOR_GUIDE.md#spatial-picking) describes the modes, the targets and the counters.

## ROS data problems

### A recorded bag plays and RViz shows nothing

**Symptom.** `ros2 bag play` runs and RViz shows an empty scene without an error. The player prints one warning that says that a new subscription on a topic is "requesting incompatible QoS" and that no messages will be sent to it, followed by the policy that did not match. **Cause.** A bag replays each topic with the quality of service that it was recorded with. A topic recorded with the sensor-data profile is BEST_EFFORT, a display of RViz asks for RELIABLE, and a BEST_EFFORT publisher cannot serve a RELIABLE subscriber, so DDS delivers nothing. **Fix.** Replay with the option `--qos-profile-overrides-path` and a file that offers the topics as RELIABLE, which also serves BEST_EFFORT subscribers. `scripts\rosbag_qos_override.yaml` is such a file for the topics of the recordings of the authors of qar_rviz_plugin, a point cloud, an IMU and `/tf_static`; copy it and put the names of your own topics in it. Read the standard error of the player whenever a topic draws nothing.

### RViz drops messages because transforms are missing

**Symptom.** Displays of point clouds stay empty, and RViz reports that its message filter drops messages because the queue is full, or the Fixed Frame cannot be reached from the frame of the data. **Cause.** A bag publishes its static transforms at its start, and a subscriber that joins later receives them only from a publisher with the durability TRANSIENT_LOCAL that keeps enough samples. A bag that sends its static transforms as several messages, six in the recordings for which the override file was written, needs a history depth that covers all of them: with the depth 1 a late subscriber receives only one of them, the frames of the others have no parent, and everything that is stamped in them is dropped. A `/tf_static` that is declared VOLATILE fails for another reason, because RViz subscribes TRANSIENT_LOCAL and the two are incompatible. A bag can also hold two trees of transforms that are not connected to each other, and then the frames of the second tree are not reached from a Fixed Frame of the first. **Fix.** Replay with the override file, whose entry for `/tf_static` is TRANSIENT_LOCAL with the depth 100. For two trees, publish one static transform between a frame of each with `static_transform_publisher` of the package `tf2_ros`, and keep it running for the whole replay; then choose a Fixed Frame that every display reaches.

### A transform lookup fails with an extrapolation error

**Symptom.** A display of a recorded cloud stays empty, and the log of RViz has the error "Lookup would require extrapolation into the past" with the requested time and the earliest time of the data. **Cause.** The stamps of the data and the stamps of the dynamic transforms are on different clocks, for example a LiDAR driver that stamps its clouds with the clock of the sensor, which counts the seconds since the sensor started, while the transforms are stamped in Unix time. The two cannot be reconciled, so every lookup that crosses a dynamic transform fails, while a static transform does not depend on time. **Fix.** Choose a Fixed Frame from which the frame of the data is reached through static transforms only. The price is that a robot that moves in the bag no longer moves through the world but stays at the origin, which is enough to try the streaming path.

### A bag has no metadata file

**Symptom.** Every `ros2 bag` command fails with the message "Could not find metadata in bag directory". **Cause.** A bag is a folder with one or more `.db3` files and an index, `metadata.yaml`, and a bag that was copied out of a recording or unpacked from an archive can lack the index. **Fix.** Make the index again with `ros2 bag reindex` and the storage `sqlite3`, calling `ros2` through `python` as [The ros2 command does not start](#the-ros2-command-does-not-start) shows. When that exits without writing anything, which can happen on a large bag, `scripts\rosbag_write_metadata.py` writes the index from the tables of the `.db3` file, which hold every topic and the count and the time range of its messages. It offers every topic as RELIABLE with the history depth 10 and the durability VOLATILE, except `/tf_static` and `/robot_description`, which it declares TRANSIENT_LOCAL with the depth 100.

### Large images stutter in RViz and in AR

**Symptom.** A raw video topic of 1080p stutters in the Image display of RViz and in the AR video panel at the same time, with frames that arrive 10 to 25 ms late at every reader. **Cause.** Fast-DDS 2.6 sends between processes of one machine through a shared-memory segment of 512 KB for each sending participant, and a raw frame is 6.2 MB, about 96 fragments of 64 KB, so that 5 to 8 percent of the frames lose fragments and the reliable protocol repairs them late. **Fix.** Use the profile `scripts\fastdds_large_data.xml` at both ends. The launcher exports it as `FASTRTPS_DEFAULT_PROFILES_FILE` unless you pass `-NoDdsProfile` or set the variable yourself, `scripts\publish_video_file.py` applies it to its own process, and for an `rviz2.exe` that you start by hand you set the variable yourself. Large messages that stop altogether after a crash are a different problem, which [Large ROS messages stall after a crash](#large-ros-messages-stall-after-a-crash) describes. [DDS profile](CONFIGURATION.md#dds-profile) has the details. The plugin receives the video of an AR panel in a private ROS context with a thread of its own, so that the Image display of RViz cannot starve it, and it subscribes with the reliability that the publishers offer.

### An Image display shows No Image

**Symptom.** An Image display of RViz stays on the picture `No Image`, and the heartbeat has a field `vdisp` for it. **Cause.** RViz shows that picture until its own subscription delivers a frame, and nothing else says why, so the field gives a verdict after the topic: `NOPUB` means that RViz sees no publisher on the topic, `WAITING` that a publisher is visible but nothing arrived, `ERR` that subscribing failed, `RX` that frames reach the display, `OFF` that the display is disabled, `"NOTOPIC"` that it has no topic, and `UNKNOWN` that its status cannot be read. For a `WAITING` that stayed the authors of qar_rviz_plugin have a hypothesis and no proof: the host had almost no commit charge left, and Ogre reported that it was out of memory for a buffer. **Fix.** With `NOPUB` start the publisher or correct the topic, and compare the domain identifier of both programs. With `WAITING` check the free memory; the launcher warns when the free commit charge is below 4 GB.

## Web panel problems

### The address of a panel answers 404

**Symptom.** A browser or a client gets 404 with the body `not found` for the address of a panel. **Cause.** The server answers 404, without a hint, for a token that names no panel and for any path that does not exist. The token is made when `add_panel` runs, so a host that was started again has new tokens and the old addresses are dead, and `remove_panel` forgets a token at once. A token that is not 32 characters of `[0-9a-f]`, and a path that is not `/p/<token>/` or `/p/<token>/<asset>`, are also 404.
**Fix.** Take the address from the host again, with `page_url`, or from the line that the example prints. A request that is answered 200 for the root of the server and 404 for the address shows that the server runs and the token is wrong.

### A viewer cannot connect

**Symptom.** The page loads and stays Offline, or a client that opens the socket gets an HTTP status instead of the upgrade, or the socket closes at once with a code. **Cause and fix.** The answers of the server to the handshake and the closes after it are these:

| What you see | Why | What to do |
|---|---|---|
| 404 | the token in `/ws/<token>` names no panel | use the address of the page that was served; the page derives nothing, it dials the address that the server wrote into it |
| 403 with `"cross-origin websocket refused"` | the `Origin` header of the handshake is not the `Host` of the request: the page was loaded through another name or port than the socket, for example through a proxy or a port forward | open the page and its socket through the same address, or, to debug a saved copy of a page, set `allow_any_origin` |
| 426 with `"this route speaks WebSocket"` | a plain `GET` on `/ws/...` | the route is for a WebSocket client only |
| 503 with `"too many viewers"` | the panel already has `max_clients` viewers, 16 by default | close a viewer, or raise `PanelSessionLimits::max_clients` |
| 503 with `"too many connections"` | the server already serves `max_connections` connections, 64 by default | the reply has the header `Retry-After: 1` |
| a close with 1008 and the reason `unknown panel` | the hello names another panel than the one of the token | the page and the server were made from different models |
| a close with 1002 | the first frame was not a hello, or had another protocol version | [Wire protocol](WIRE_PROTOCOL.md#transport) lists the cases |
| a close with 1008 and the reason `viewer is not keeping up` | more than 4 MiB of data were waiting to be sent to the viewer | the network or the viewer is too slow for the rate of publishing; publish less often |

### The headset cannot reach the server

**Symptom.** The page loads on the machine of the server and not in the headset, or the address of the panel names a host that the headset cannot reach. **Cause.** The server binds every interface by default, and the host in the address is the one that the machine routes through to reach a network, which is the interface of its default route: with a VPN, several network cards or a virtual switch it can be the wrong one. A firewall that blocks the port, and a bind address of loopback, also keep other machines out.
**Fix.** Set `advertised_host` to the name or the address that the headset can reach, list the candidates with `candidate_hosts()`, pin `port` and open that port in the firewall, and leave `bind_address` at its default. [Getting started](GETTING_STARTED.md#running-a-panel-server-safely) lists what the network has to allow.

### The page says that its script did not run

**Symptom.** The page shows the message that its script did not run and that nothing below it will update or respond. **Cause.** The page writes the message with the markup and the script hides it as the last step of its start, so a browser that never parsed or never ran the script leaves it standing: scripting is switched off in that browser view, the script has a syntax that the engine cannot parse, or the page was written without the script.
**Fix.** Open the same address in a desktop browser. If the page works there, the cause is the embedded browser: [The QarOS in-AR browser](SDK_COMPATIBILITY.md#the-qaros-in-ar-browser) says what is known about it. The script uses ES5 only for this reason, and a page rendered with `include_runtime` false never has the message.

### The page says that the browser is missing features

**Symptom.** The page shows a message that begins with this browser is missing and a number of features, and then lists them after not supported here and ends with the engine. **Cause.** At its start the script probes what it needs and names every probe that failed: `"JSON"`, `"querySelector"`, `"addEventListener"`, `"setTimeout/setInterval"`, `"WebSocket (live data and controls)"`, `"CSS custom properties"` and, for a page with a chart, `"inline SVG (chart widgets)"`. The message also gives the user agent of the engine, which is the quickest way to find out which engine rendered the page.
**Fix.** A browser without a WebSocket cannot show live data or send controls, and the page cannot work there. Compare the user agent with the one in [The QarOS in-AR browser](SDK_COMPATIBILITY.md#the-qaros-in-ar-browser).

### The panel is squeezed, too small or shows only the top of the page

**Symptom.** The text of the page is squeezed horizontally, or lies on top of other text, or the panel is smaller than you expect, or only the top of a long page is visible. **Cause.** The browser of QarOS gives the page a viewport that does not follow the size of the panel, 500 by 500 CSS pixels in the measurements of the authors of qar_rviz_plugin. A panel that is sized from a guess, or from a viewport that the host did not use, is stretched without keeping the aspect, and a page taller than the viewport is not drawn below its fold. A size that was pinned with `QAR_PANEL_WEB_SIZE` overrules the measurement, the bounds of the room limit the result, and `QAR_PANEL_WEB_SCALE` multiplies it.
**Fix.** Follow the signal `viewport_reported` as the example does: copy the report into `UrlPanelSizing` with `use_reported_viewport`, size the panel with `size_url_panel_for`, and place it again when the size changed. The line `UrlPanelSizeResult::note` says what applied. Unset the two variables to test, and reach the part of a long page below the fold with the click pager at the right edge of the panel. [Panel sizing](ARCHITECTURE.md#panel-sizing) explains the rules.

### The connection indicator shows Offline or Stale

**Symptom.** The indicator of the page says Offline or Stale and the values do not change. **Cause.** Offline means that the socket is closed: the server stopped or removed the panel, the server closed the socket, or the page cannot reach the address. The page dials again after `reconnect_ms`, 2 s by default, for as long as it lives, and a reconnect starts from the document that the hello of the server carries. Stale means that the socket is open and no frame has arrived for `stale_after_ms`, 5 s by default; the page sends a ping after the same silence, and a server that answers it changes the state back at once.
**Fix.** Check that the host runs, that the panel was not removed, and the table of [A viewer cannot connect](#a-viewer-cannot-connect). A host that publishes nothing keeps the page Live with the pings alone; a Stale page with a running host points at the network between them.

### A control does nothing, or its value jumps back

**Symptom.** A press of a button has no effect, or a slider that was moved returns to its old value. **Cause.** A control event reaches the host through the sink of the session and the signal `control_received`; if the host set no sink and connected to no signal, nobody hears it. The page changes its own value at once, and only the host's next publish confirms or corrects it: the session does not apply the value of a control event to its document, so a host that does not publish the value that it accepted makes the page return to what the server holds. An event that the host does not know should be answered with `ack` and `false`, as the examples do, which the page shows next to the control. A button that is marked `confirm` needs a second press within 4 seconds.
**Fix.** Set the sink with `set_control_sink`, call `set_value` or `publish` with the value that the host accepted, and answer every event with `ack`. [Data and control flow](ARCHITECTURE.md#data-and-control-flow) shows the order.

### add_panel refuses a model

**Symptom.** `add_panel` returns a null pointer, and the error string begins with model does not render. **Cause.** `add_panel` renders the page once, and the model or the theme has an error: the string names the first one, with the id of the widget, for example a duplicate id, a button without an action, a slider without a range or a binding that does not parse. **Fix.** Call `validate(model)` and read the problems of `Error` severity; with a schema, `validate(model, &schema)` also checks every binding against it.

## ROS problems

### A message type cannot be found

**Symptom.** A call of `qar_ros_ui` returns a `Result` with the status `InvalidArgument` or `TypeSupportUnavailable`. The text says that a name is not a ROS type name, or that a typesupport library cannot be loaded and how many locations were tried, or that a symbol is not exported by a library. **Cause.** A type is named `pkg/msg/Name`, `pkg/srv/Name` or `pkg/action/Name`, and the two-part form `pkg/Name` means a message. The registry loads the library `<pkg>__rosidl_typesupport_introspection_cpp` and the library `<pkg>__rosidl_typesupport_cpp` of the package, from the prefixes that the host added, then from the folders of `AMENT_PREFIX_PATH`, and last through the search of the operating system. A package that is not installed, a folder that is on none of the three, and a name with a typing error fail in that way.
**Fix.** Check the spelling of the name, put the folder `bin` of the ROS 2 distribution on `PATH`, as [Configuration](CONFIGURATION.md#environment-variables) says, or call `TypeSupportRegistry::global().add_search_prefix` with the root of the distribution before the first call. A type that a graph lookup cannot find answers `NotFound`, with the text that the topic or the service is not on the graph.

### A topic is on the graph and nothing arrives

**Symptom.** `ros2 topic list` shows the topic, the subscription is created, and `take_latest` returns false. **Cause.** DDS matching goes one way: a reliable subscription never hears a best-effort publisher, and a volatile one never gets what a transient-local publisher latched before it started. A fixed preset can be wrong for the topic, and a publisher that starts later with another profile is not matched until the entity is created again. Another cause is a different `ROS_DOMAIN_ID`, which puts the host on another graph.
**Fix.** Use the preset `Adaptive`, which is the default, and call `TopicBridge::reconcile` about once a second. The diagnostics of the subscription show `publisher_count`, the active QoS and the counter of incompatible-QoS events, which tell whether a publisher was seen and why it could not be matched.

### A service call or an action goal does not complete

**Symptom.** The callback of a service call receives a `Timeout`, or an action goal ends with the event `Rejected` or `Error`. **Cause.** A call waits for a server that is not up yet, by default, and ends with `Timeout` after 5 s: the text says that there is no server for the service, or no response from it. A goal that the server does not answer within `response_timeout` ends with an `Error` event, and a server that refuses it ends it with `Rejected`; once a goal is accepted, nothing bounds its execution. A type name that the graph does not know, or a request that does not convert, fails with a `Result` before anything is sent.
**Fix.** Check with `server_available` that a server for the service or the action is matched, give the call a longer timeout when the server starts slowly, and read the `Result` of the call and the text of the outcome, which name the field of a request that did not convert. A host that spins no timers has to call `poll()`.
