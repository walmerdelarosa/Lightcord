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
  <img src="https://img.shields.io/badge/version-0.2.0--alpha-orange" alt="Version 0.2.0 alpha">
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT license">
</p>

<p align="center">
  <a href="CODE_SIGNING_POLICY.md">Code signing policy</a>
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

**Available in 0.2 (alpha)**

- 💬 **Text chat** in servers and direct messages, with live updates (new, edited and deleted messages).
- 🧭 **Compact navigator**: servers, categories and channels in one tree, with instant search.
- 🔐 **Permission-aware**: channels you cannot see are hidden, and the composer is disabled where you cannot write.
- ✍️ **Discord Markdown**: bold, italics, underline, strikethrough, spoilers, code blocks, quotes, mentions,
  custom emoji names and `<t:…>` timestamps.
- 🎙️ **Voice chat** in server voice channels, with Discord's mandatory **DAVE end-to-end encryption**
  (via [libdave](https://github.com/discord/libdave)), Opus audio, mute/deafen, a live list of who is in each
  voice channel and speaking indicators.
- 🔇 **Noise suppression** powered by [RNNoise](https://github.com/xiph/rnnoise), applied to everything you send
  in a call, with adjustable strength, a voice gate that mutes the mic between sentences, and a
  **live microphone test** to hear the difference.
- 🖥️ **Simple screen sharing**: pick a whole screen or a single window, choose 5/15/30 fps and 720p/1080p/native,
  and see a live preview with the measured frame rate. *(Streaming to other people, "Go Live", is planned for 0.3.)*
- 🎨 **New visual identity**: "Night" and "Day" themes built around a warm amber accent, with original icons and logo.
- 🔑 **Secure token storage** in the Windows Credential Manager.
- 🔄 **Resilient connection**: heartbeats, zombie-connection detection, automatic reconnect and session resume.

**Not available yet**

| Feature | Status |
| --- | --- |
| Go Live / streaming to others | Planned for **0.3**, on top of the voice engine. |
| Calls in DMs and group DMs | Planned. Voice works in server voice channels for now. |
| Uploading files, reactions, threads, notifications, tabs | Planned. See the [roadmap](docs/ROADMAP.md). |

## Download

Get the latest build from the [Releases page](https://github.com/walmerdelarosa/Lightcord/releases):
download `Lightcord-<version>-windows-x64.zip`, unzip it anywhere and run `Lightcord.exe`. No installer or
administrator rights are needed. Every release also includes a `.sha256` file to verify the download.

Releases are being prepared for code signing through SignPath Foundation (see the
[code signing policy](CODE_SIGNING_POLICY.md)). Until then the executable is unsigned, so Windows SmartScreen may show
"Windows protected your PC" the first time: choose **More info → Run anyway**. Even signed releases can show this
warning for a while, until SmartScreen has seen enough downloads.

Development builds of every commit are available as the **Lightcord-windows-x64** artifact of the
[Build workflow](https://github.com/walmerdelarosa/Lightcord/actions/workflows/build.yml).

## Using voice

Double-click a voice channel to join it, and double-click it again (or use the red button in the bottom-left panel)
to leave. The panel shows the connection state and confirms when the call is end-to-end encrypted. The microphone,
headphones and noise-suppression buttons work during a call, and *Settings → Voice & Audio* lets you pick devices
and tune noise suppression.

How it works, in short: Lightcord joins the channel through the main gateway, connects to the voice server
(voice gateway v8), discovers its public address over UDP, and then sends 20 ms Opus frames that are
end-to-end encrypted with DAVE (an MLS group shared only by the people in the call) and
transport-encrypted with AES-256-GCM or XChaCha20-Poly1305. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

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
- [vcpkg](https://github.com/microsoft/vcpkg) for the voice dependencies (OpenSSL, Opus, libsodium, mlspp,
  nlohmann-json). They are declared in `vcpkg.json` and installed automatically when you pass vcpkg's
  toolchain file to CMake.

RNNoise and libdave are vendored in `third_party/`.

### Windows

```powershell
# Qt 6.8 for MSVC 2022, with the Qt Multimedia and Qt WebSockets add-ons; vcpkg cloned in C:/vcpkg
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build
ctest --test-dir build --output-on-failure
windeployqt --release --no-translations build/Lightcord.exe
```

Run the commands from a *Developer PowerShell for VS 2022* so that MSVC is on the `PATH`.

### Linux (Ubuntu 24.04)

```bash
sudo apt install cmake ninja-build qt6-base-dev qt6-websockets-dev qt6-multimedia-dev libqt6svg6-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
./build/Lightcord
```

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `LIGHTCORD_BUILD_APP` | `ON` | Build the Qt application. Set to `OFF` to build only the audio core and tests (no Qt needed). |
| `LIGHTCORD_BUILD_TESTS` | `ON` | Build the unit tests (`ctest`). |
| `LIGHTCORD_ENABLE_VOICE` | `ON` | Build voice chat. Set to `OFF` to build without OpenSSL, Opus, libsodium and mlspp (text chat only). |

## Project layout

```
src/
  core/         Discord REST + gateway clients, session state, models, settings, token storage
  audio/        NoiseSuppressor (RNNoise wrapper, Qt-free) and the microphone pipeline
  screenshare/  Screen and window capture
  voice/        Voice gateway, UDP transport and the VoiceEngine thread
  voice/core/   Qt-free voice core: RTP, transport encryption, IP discovery, Opus, DAVE session
  ui/           Main window, dialogs, theme engine
resources/      Icons, logo, stylesheet, Windows resources
third_party/    Vendored RNNoise (BSD-3-Clause) and libdave (MIT)
vcpkg/          vcpkg overlay port for mlspp
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
- [libdave](https://github.com/discord/libdave) (MIT), vendored. See
  [`third_party/libdave/README.lightcord.md`](third_party/libdave/README.lightcord.md).
- [mlspp](https://github.com/cisco/mlspp) (BSD-2-Clause), [OpenSSL](https://www.openssl.org/) (Apache-2.0),
  [Opus](https://opus-codec.org/) (BSD-3-Clause), [libsodium](https://libsodium.org/) (ISC) and
  [nlohmann/json](https://github.com/nlohmann/json) (MIT), linked statically through vcpkg. The Windows package
  includes their license texts in `licenses/`.
- [Qt 6](https://www.qt.io/) (LGPLv3), linked dynamically.

"Discord" is a trademark of Discord Inc. Lightcord is not affiliated with Discord Inc. or with the authors of Ripcord.
