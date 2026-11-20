# Lightcord architecture

Lightcord is a single Qt Widgets application. Everything runs on the GUI thread except what Qt
itself runs in the background (network I/O and audio devices). Keeping one thread keeps the code
simple and the memory footprint small, and none of the current work is CPU-heavy. The noise
suppressor processes 10 ms frames in well under 1 ms.

```
                    ┌──────────────────────────── ui/ ────────────────────────────┐
                    │ MainWindow ── VoiceBar ── SettingsDialog ── ScreenShareDialog │
                    └───────┬───────────────┬──────────────────┬──────────────────┘
                            │ signals       │                  │
            ┌───────────────▼───┐   ┌───────▼────────┐  ┌──────▼──────────────┐
            │ core/Session      │   │ voice/         │  │ screenshare/        │
            │  state + events   │   │ VoiceSession   │  │ ScreenCapturer      │
            ├─────────┬─────────┤   └───────┬────────┘  └─────────────────────┘
            │DiscordRest│Gateway│◄──────────┘ (opcode 4)
            └─────────┴─────────┘
                                        ┌──────────── audio/ ─────────────┐
                                        │ AudioPipeline ──► NoiseSuppressor│──► RNNoise (C)
                                        └──────────────────────────────────┘
```

## Modules

### `core/`

| Class | Responsibility |
| --- | --- |
| `DiscordRest` | Async HTTP client for `https://discord.com/api/v10`. Adds the `Authorization` header (with the `Bot` prefix for bot tokens) and retries HTTP 429 responses after `retry_after`. Callbacks are guarded by a context `QObject`. |
| `Gateway` | WebSocket connection to the Discord gateway (v10, JSON). Implements HELLO → IDENTIFY/RESUME, heartbeats with jitter, zombie detection (missing HEARTBEAT_ACK), RECONNECT and INVALID_SESSION handling, and exponential back-off. Fatal close codes (4004, 4010–4014) are reported to the UI instead of retried. |
| `Session` | The single source of truth for the UI. Parses READY / GUILD_* / CHANNEL_* / MESSAGE_* events into the models, keeps the last 200 messages per channel and computes channel permissions. |
| `Models` | Plain structs (`User`, `Guild`, `Channel`, `Role`, `Message`) plus `computeChannelPermissions()`, which follows Discord's documented algorithm (base role permissions, then `@everyone`, role and member overwrites). |
| `MessageFormatter` | Converts Discord Markdown to the HTML subset supported by `QTextBrowser`. Code spans and links are swapped for placeholders first, so the emphasis rules never touch them. |
| `TokenStore` | Stores the token in the Windows Credential Manager (`CredWriteW`, protected by DPAPI). Other platforms fall back to `QSettings`, and the UI says so. |
| `Settings` | Typed wrapper around `QSettings` (INI format). |

### `audio/`

`NoiseSuppressor` is deliberately **Qt-free**, so it can be unit-tested (`tests/`) and reused by the
future voice engine. It accepts 48 kHz mono `int16` chunks of any size, buffers them into RNNoise's
480-sample frames, and always returns exactly as many samples as it receives (constant 10 ms latency).
On top of RNNoise it adds:

- **Strength:** a wet/dry mix between the denoised and the original signal.
- **Voice gate:** uses RNNoise's voice-activity probability, with a hold time and attack/release
  ramps (interpolated per sample) so that the gate never clicks.

`AudioPipeline` connects a `QAudioSource` to the suppressor. It accepts whatever format the device
prefers (8/16/32-bit integer or float, any channel count, any sample rate). It down-mixes to mono,
resamples to 48 kHz and emits the processed stream (`processedAudio`), which is exactly what the
Opus encoder will consume in 0.2. Today the stream feeds the microphone test in Settings, with an
optional monitor output.

### `screenshare/`

`ScreenCapturer` enumerates screens (`QScreen`) and, on Windows, top-level windows (`EnumWindows`,
skipping tool, owned, minimised and DWM-cloaked windows). Windows are captured with
`PrintWindow(PW_RENDERFULLCONTENT)`, so GPU-composited apps (browsers, games in windowed mode) are
not black. Frames are scaled to the chosen maximum height and delivered as `QImage` at a fixed rate;
the measured FPS is shown in the preview.

### `voice/`

`VoiceSession` implements the first half of joining a voice channel: it sends gateway opcode 4 and
collects `VOICE_STATE_UPDATE` (session id) and `VOICE_SERVER_UPDATE` (endpoint and token) into a
`TransportInfo`. The second half, the media transport, is described in the [roadmap](ROADMAP.md).

### `ui/`

- `Theme` owns the two palettes. It fills the `{{token}}` placeholders in `resources/themes/base.qss`
  and recolours the SVG icons at runtime by replacing `currentColor`, so one icon set serves every theme.
- `MainWindow` is a two-pane splitter: the navigator (tree plus voice bar) and the chat (header,
  message view, composer). Tree rebuilds are debounced, and the expanded and collapsed state survives rebuilds.

## Design decisions

- **Qt Widgets, not QML.** It is lighter at runtime, needs no JavaScript engine and gives a native,
  desktop-style feel.
- **`QTextBrowser` for messages.** Rendering 200 messages of rich text is cheap. Message-level
  widgets or delegates can come later, if reactions and inline images need them.
- **No avatars or image downloads by default.** This saves bandwidth and memory, in line with the
  project's goal.
- **Vendored RNNoise (classic model).** It is small, BSD-licensed and has no build-time download. A
  tiny patch replaces C99 variable-length arrays with `_alloca` so that MSVC can compile it.
