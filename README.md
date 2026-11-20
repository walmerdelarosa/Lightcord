<p align="center">
  <img src="resources/icons/lightcord.svg" width="96" alt="Lightcord logo">
</p>

<h1 align="center">Lightcord</h1>

<p align="center">
  A lightweight, native desktop client for Discord, written in C++17 and Qt 6.<br>
  No Electron, no embedded browser. Just a fast, compact chat window.
</p>

<p align="center">
  <a href="https://github.com/walmerdelarosa/Lightcord/actions/workflows/build.yml"><img src="https://github.com/walmerdelarosa/Lightcord/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
  <img src="https://img.shields.io/badge/version-0.1.0--alpha-orange" alt="Version 0.1.0 alpha">
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT license">
</p>

---

> [!WARNING]
> **Lightcord is an unofficial, third-party client and is not affiliated with or endorsed by Discord Inc.**
> Discord's Terms of Service do not allow third-party clients for user accounts. Logging in with a
> **user token** may get your account restricted or suspended. Use it at your own risk, or log in with a
> **bot token**, which is fully supported by the Discord API.

## Why Lightcord?

The official Discord desktop app is built on Electron and often uses several hundred megabytes of RAM.
Lightcord follows the idea that made the (now discontinued, closed-source) [Ripcord](https://cancel.fm/ripcord/)
client popular: a **native, desktop-style** interface that starts instantly and stays out of your way.

Lightcord is a **new, independent, open-source project**. It does not contain or modify any Ripcord code or
binaries. It only borrows the philosophy.

## Features

**Available in 0.1 (alpha)**

- 💬 **Text chat** in servers and direct messages, with live updates (new, edited and deleted messages).
- 🧭 **Compact navigator**: servers, categories and channels in one tree, with instant search.
- 🔐 **Permission-aware**: channels you cannot see are hidden, and the composer is disabled where you cannot write.
- ✍️ **Discord Markdown**: bold, italics, underline, strikethrough, spoilers, code blocks, quotes, mentions,
  custom emoji names and `<t:…>` timestamps.
- 🔇 **Noise suppression** powered by [RNNoise](https://github.com/xiph/rnnoise), with adjustable strength,
  a voice gate that mutes the mic between sentences, and a **live microphone test** to hear the difference.
- 🖥️ **Simple screen sharing**: pick a whole screen or a single window, choose 5/15/30 fps and 720p/1080p/native,
  and see a live preview with the measured frame rate. *(Streaming to other people arrives with voice in 0.2. See below.)*
- 🎨 **New visual identity**: "Night" and "Day" themes built around a warm amber accent, with original icons and logo.
- 🔑 **Secure token storage** in the Windows Credential Manager.
- 🔄 **Resilient connection**: heartbeats, zombie-connection detection, automatic reconnect and session resume.

**Not available yet**

| Feature | Status |
| --- | --- |
| Voice chat (audio) | Planned for **0.2**. Discord requires its DAVE end-to-end encryption for every call since March 2026, which Lightcord will implement through [libdave](https://github.com/discord/libdave). Voice *signalling* can already be tested (Settings → Advanced). |
| Go Live / streaming to others | Planned for **0.3**, on top of the voice engine. |
| Uploading files, reactions, threads, notifications, tabs | Planned. See the [roadmap](docs/ROADMAP.md). |

## Download

Pre-release builds for Windows x64 are produced by GitHub Actions for every commit:
open the latest successful run of the [Build workflow](https://github.com/walmerdelarosa/Lightcord/actions/workflows/build.yml)
and download the **Lightcord-windows-x64** artifact. Unzip it and run `Lightcord.exe`. No installer is needed.

## Logging in

Lightcord never asks for your password. On first start you paste a token:

- **Bot token (recommended for testing):** create an application in the
  [Discord Developer Portal](https://discord.com/developers/applications), add a bot, copy its token and enable the
  **Message Content** privileged intent. Invite the bot to a server to see its channels.
- **User token:** possible, but read the warning at the top of this page first.

Tick **Remember me** to keep the token in the Windows Credential Manager (on Linux and macOS it is stored unencrypted
in the settings file, and the login dialog tells you so). *Settings → Advanced → Log out* removes it.

## Building from source

### Requirements

- CMake ≥ 3.21 and a C++17 compiler (MSVC 2022, GCC 11+ or Clang 14+)
- Qt ≥ 6.4 with the modules **Core, Gui, Widgets, Network, WebSockets, Multimedia, Svg**

RNNoise is vendored in `third_party/rnnoise`, so there is nothing else to download.

### Windows

```powershell
# Qt 6.8 for MSVC 2022, with the Qt Multimedia and Qt WebSockets add-ons
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
ctest --test-dir build --output-on-failure
windeployqt --release --no-translations build/Lightcord.exe
```

Run the commands from a *Developer PowerShell for VS 2022* so that MSVC is on the `PATH`.

### Linux (Ubuntu 24.04)

```bash
sudo apt install cmake ninja-build qt6-base-dev qt6-websockets-dev qt6-multimedia-dev libqt6svg6-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Lightcord
```

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `LIGHTCORD_BUILD_APP` | `ON` | Build the Qt application. Set to `OFF` to build only the audio core and tests (no Qt needed). |
| `LIGHTCORD_BUILD_TESTS` | `ON` | Build the unit tests (`ctest`). |

## Project layout

```
src/
  core/         Discord REST + gateway clients, session state, models, settings, token storage
  audio/        NoiseSuppressor (RNNoise wrapper, Qt-free) and the microphone pipeline
  screenshare/  Screen and window capture
  voice/        Voice-channel signalling (media transport comes in 0.2)
  ui/           Main window, dialogs, theme engine
resources/      Icons, logo, stylesheet, Windows resources
third_party/    Vendored RNNoise (BSD-3-Clause)
tests/          Unit tests
docs/           Architecture notes and roadmap
```

More details in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Contributing

Issues and pull requests are welcome. Please keep the spirit of the project:

1. **Lightweight first.** Prefer Qt and small, permissively licensed libraries; avoid web engines and heavy runtimes.
2. **Native look and keyboard-friendly.** Compact layouts, no unnecessary animations.
3. **Respect users.** Never send data anywhere except Discord's own API; never automate user accounts (no self-bots).

Before opening a PR, make sure `cmake --build` and `ctest` pass.

## License

Lightcord is released under the [MIT License](LICENSE).

Third-party components:

- [RNNoise](https://github.com/xiph/rnnoise) (BSD-3-Clause), vendored with a small portability patch. See
  [`third_party/rnnoise/README.lightcord.md`](third_party/rnnoise/README.lightcord.md).
- [Qt 6](https://www.qt.io/) (LGPLv3), linked dynamically.

"Discord" is a trademark of Discord Inc. Lightcord is not affiliated with Discord Inc. or with the authors of Ripcord.
