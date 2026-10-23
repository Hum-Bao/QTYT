# QTYT — Ultra-Lightweight Native YouTube Client

**QTYT** is a fast, resource-efficient desktop YouTube client built with modern C++20 and Qt 6 for Linux and Windows.

Unlike standard web browsers or Electron-based desktop clients that easily consume **1–2 GB+ of RAM** and spawn dozens of background helper processes, QTYT runs as a lean, single native application. It uses approximately **40–60 MB of RAM** at launch and maintains **0.0% CPU usage** when idle or paused.

---

## Why QTYT?

* **Zero WebEngine / Zero Electron**: No Chromium, CEF, QtWebEngine, or embedded JavaScript UI runtimes.
* **Direct InnerTube API**: Communicates directly with YouTube's private InnerTube JSON API (`/browse`, `/search`, `/player`) via native HTTP requests.
* **Hardware-Accelerated Playback**: Built-in in-process `libmpv` video surface using OpenGL framebuffer rendering (`mpv_render_context`), with automatic fallback to standalone `mpv` IPC.
* **Instant Start (Speculative Prefetching)**: Hovering over any video card for 200 ms initiates background stream extraction, allowing videos to start playing immediately upon clicking.
* **Anti-Throttling & PO Token (POT) Acceleration**: Integrates yt-dlp's Proof-of-Origin Token (POT) plugin framework (`bgutil-ytdlp-pot-provider` / `bgutil-pot`) alongside a 150 MiB forward demuxer buffer, unlocking unthrottled line speeds (120–280+ Mbps) on modern AV1 and VP9 streams.
* **Battery & Codec Friendly**: Automatically optimizes codec selection (AV1 > VP9 > AVC when JS runtime/POT is available, or instant unthrottled H.264 fallback if no runtime exists) to prevent high CPU usage on machines without hardware AV1 decoding.
* **Strict Subprocess Safety**: Child processes (`yt-dlp`, `mpv`) are bound to Win32 Job Objects on Windows and isolated process groups with `PR_SET_PDEATHSIG` on Linux, ensuring zero orphaned processes remain on exit or crash.
* **Privacy Focused**: Cookies and preferences are stored exclusively on your local machine in your standard operating system AppData directory.

---

## Features

* **Personalized Feeds & Subscriptions**: View your genuine YouTube Home recommendations and Subscriptions feed when logged in.
* **Instant Search**: Fast YouTube search results displaying channel information, view counts, publish dates, video durations, and high-DPI thumbnails.
* **Full Account & Brand Channel Support**: Automatically detects and switches between multiple connected Google accounts and YouTube Brand Accounts without re-entering credentials.
* **YouTube-Authentic Watch Page**:
  * Expandable video description, view counts, and upload timestamps.
  * Channel avatar display and interactive Like, Dislike, and Subscribe buttons.
  * Syncs playback with your YouTube account's Watch History.
* **Custom Responsive Player Overlay**:
  * Scrubber bar with hover time preview and buffered position indicator.
  * Smooth-expanding volume slider with one-click mute.
  * Dynamic quality selector (`1080p`, `720p`, `480p`, `360p`, `Auto`, `Audio Only`).
  * Playback speed control (`1.0x` to `2.0x`).
  * Fullscreen toggle (via button, hotkey, or double-clicking the video).
* **Shorts Filter**: One-click toggle in the top bar to hide YouTube Shorts from feeds.
* **Infinite Scroll**: Seamless continuation loading as you scroll down the feed.
* **Lightweight Diagnostic CLI (`lightyt-cli`)**: A companion command-line tool to test authentication, query feeds, search, and resolve stream URLs headlessly.

---

## Runtime Dependencies

QTYT relies on a few external tools for media extraction and playback:

| Dependency | Purpose | Recommended Version |
| :--- | :--- | :--- |
| **`yt-dlp`** | Extracts direct adaptive audio/video stream URLs | Latest release |
| **`mpv` / `libmpv`** | Hardware-accelerated media rendering | `libmpv` (v2) or standalone `mpv` |
| **`libcurl`** | Network communication with YouTube's InnerTube API | Latest release |
| **PO Token (POT) Provider** | Bypasses YouTube CDN throttling on AV1/VP9 streams | `bgutil-pot` & `bgutil-ytdlp-pot-provider` plugin |
| **JavaScript Runtime** | Helper for `yt-dlp` to solve YouTube bot-detection / `n-sig` challenges | `node` (v20+), `deno`, or `bun` |

### Proof-of-Origin (POT) Plugin & Anti-Throttling Architecture

YouTube enforces strict server-side bandwidth rate-limiting (~10–15 KB/s or ~120 Kbps) on automated web client range requests unless validated with a **Google Proof-of-Origin Token (`po_token`)** and JavaScript `n-sig` challenge solving.

QTYT handles this adaptively:
1. **Unthrottled AV1 / VP9 via POT Plugin**:
   * QTYT integrates the native [`bgutil-pot`](https://github.com/jim60105/bgutil-ytdlp-pot-provider-rs) token generator alongside the [`bgutil-ytdlp-pot-provider`](https://github.com/Brainicism/bgutil-ytdlp-pot-provider) plugin in `lib/yt-dlp-plugins/`.
   * When a JavaScript runtime (`node`, `deno`, or `bun`) is detected on the host system (such as system PATH or NVM installations), QTYT automatically supplies it to `yt-dlp` via `--js-runtimes`.
   * `yt-dlp` solves `n-sig` challenges off-thread and attaches PO tokens to video stream requests, unlocking full line speeds of **120–280+ Mbps** on high-resolution **AV1** (`itag 399`) and **VP9** (`itag 303`) streams.
2. **Zero-Dependency Fallback to H.264**:
   * If no JavaScript runtime or POT provider is installed on your machine, QTYT automatically and safely defaults to unthrottled **H.264 (AVC)** streams via Apple VisionOS (`FAST_DEFAULT_ARGS`).
   * This guarantees instantaneous buffering and consistent **~70,000 Kbps** line speed with 0% CPU decoding overhead without requiring any external runtimes or plugins.

### Dependency Resolution Order

QTYT automatically locates `yt-dlp`, POT plugins, and `mpv` in this order:
1. **Local `lib/` directory**: Place executables, plugins (`lib/yt-dlp-plugins/`), or shared libraries directly in the `lib/` folder next to the app for a fully self-contained, portable setup.
2. **System `PATH`**: Standard system installations (e.g. installed via your package manager, WinGet, or Chocolatey).

---

## Installation & Building from Source

### 1. Linux

#### Prerequisites
Install the required build tools, Qt 6 libraries, and media packages:

* **Debian / Ubuntu**:
  ```bash
  sudo apt update
  sudo apt install build-essential cmake qt6-base-dev libcurl4-openssl-dev libmpv-dev yt-dlp
  ```

* **Arch Linux**:
  ```bash
  sudo pacman -S base-devel cmake qt6-base libmpv yt-dlp curl
  ```

* **Fedora**:
  ```bash
  sudo dnf install gcc-c++ cmake qt6-qtbase-devel mpv-libs-devel libcurl-devel yt-dlp
  ```

#### Building with the Automated Script (Recommended)
```bash
# Build and launch QTYT
./build.sh

# Additional build script options:
./build.sh --no-run         # Compile without launching
./build.sh --clean --test   # Clean rebuild and execute unit tests
./build.sh --build-type Debug
```

#### Building Manually with CMake
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Launch the application
./build/QTYT
```

---

### 2. Windows

#### Prerequisites
* **C++ Compiler**: MinGW GCC 13+ (bundled with Qt) or MSVC 2022.
* **CMake**: 3.16 or higher.
* **Qt 6**: Qt 6.8+ (Widgets, Core, Gui, Concurrent, OpenGLWidgets).
* **Media Dependencies**: Place `yt-dlp.exe` and `libmpv-2.dll` (or `mpv.exe`) inside the `lib\` directory, or add them to your Windows `PATH`.

#### Building with PowerShell (Recommended)
The automated script auto-discovers your local Qt MinGW installation, compiles the binary, runs `windeployqt`, and starts the app:

```powershell
# Build, deploy Qt dependencies, and launch:
.\build.ps1

# Options:
.\build.ps1 -NoRun          # Compile and deploy without launching
.\build.ps1 -Clean -Test    # Clean rebuild and run unit tests
```

#### Building Manually with CMake
```cmd
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\mingw_64"
cmake --build . --parallel
windeployqt QTYT.exe
QTYT.exe
```

---

## Configuration & Setup

### Signing In (Session Cookies)

Because QTYT contains no embedded browser or web engine, login is handled via standard Netscape session cookies. Authenticated requests use RFC 3174 `SAPISIDHASH` authorization, allowing you to access your personal recommendations, subscriptions, and synchronized watch history.

#### How to Import Cookies:

1. In your regular web browser, make sure you are logged into [YouTube](https://www.youtube.com).
2. Export your cookies in Netscape format using any cookie export extension (such as *Get cookies.txt LOCALLY* for Chromium/Firefox).
3. Open **QTYT** and click the **Account** button (profile icon) in the top-right masthead.
4. Select the **Cookies (cookies.txt)** tab.
5. Click **Import cookies.txt File...** to select your exported file, or paste the cookie text directly into the editor.
6. Click **Save & Apply Cookies**. Your Home and Subscriptions feeds will refresh immediately.

> **Privacy Guarantee**: Cookies are stored exclusively in your local operating system AppData directory and are never committed to git, copied into build folders, or transmitted to any third party:
> * **Linux**: `~/.local/share/LightYT/cookies.txt`
> * **Windows**: `%LOCALAPPDATA%\LightYT\cookies.txt`

---

### Channel & Profile Switching

If your Google account has multiple connected channels or YouTube Brand Accounts:

1. Open the **Account** dialog from the top-right masthead.
2. Under the **Channel / Profile** tab, click **Detect Channels**.
3. Choose your desired channel or Google account slot from the dropdown list.
4. Click **Apply & Reload Feed**.

---

### Playback & App Preferences

In the **Preferences** tab of the settings dialog, you can configure:

* **Default Quality**: Choose your preferred playback resolution (`1080p`, `720p`, `480p`, `360p`, `Auto`, `Audio Only`).
* **Allow AV1 Video Codec**: By default, AV1 is disabled to save CPU and battery life on hardware without hardware AV1 decoding (falling back to VP9/AVC). Check this box if your GPU supports native AV1 hardware decoding.
* **Application Logging**: Adjust console verbosity (`Warning`, `Info`, `Debug`, `Trace`, `Error`, or `Off`).

Preferences are saved to:
* **Linux**: `~/.local/share/LightYT/settings.ini`
* **Windows**: `%LOCALAPPDATA%\LightYT\settings.ini`

---

## Keyboard Shortcuts

During video playback, the following shortcuts are active:

| Key | Action |
| :--- | :--- |
| **`Space`** or **`K`** | Toggle Play / Pause |
| **`F`** | Toggle Fullscreen (or double-click video) |
| **`Esc`** | Exit Fullscreen / Return back to feed |
| **`M`** | Toggle Mute |
| **`Left` / `Right`** | Seek backward / forward 5 seconds |
| **`J` / `L`** | Seek backward / forward 10 seconds |
| **`Up` / `Down`** | Volume up / down by 5% |
| **`>`** (Shift + `.`) | Increase playback speed (+0.25x up to 2.0x) |
| **`<`** (Shift + `,`) | Decrease playback speed (-0.25x down to 1.0x) |

---

## Headless Diagnostic CLI (`lightyt-cli`)

A companion diagnostic CLI binary is automatically compiled alongside QTYT. It allows you to test network connectivity, verify cookie status, and inspect InnerTube responses headlessly:

```bash
# Check cookie status and authentication headers:
./build/lightyt-cli auth-check

# Inspect your personalized Home feed:
./build/lightyt-cli feed

# Search YouTube from the terminal:
./build/lightyt-cli search "lofi hip hop"

# Test stream extraction for a specific video:
./build/lightyt-cli resolve "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# List supported resolution and format flags:
./build/lightyt-cli formats
```

---

## Running Unit Tests

QTYT includes an automated unit test suite covering InnerTube JSON parsing, RFC 3174 SHA-1 authentication, cookie storage, subprocess safety, and player controller states:

```bash
# Run tests through CTest:
ctest --test-dir build --output-on-failure

# Or run the test executable directly:
./build/test_core
```

---

## Disclaimer

QTYT is an independent open-source project and is not affiliated with, endorsed by, or sponsored by YouTube, Google, or Alphabet Inc. All YouTube trademarks, logos, and brand names are property of their respective owners.
