# Lightcord architecture

Lightcord is a single Qt Widgets application. The UI, the main gateway and text chat run on the GUI
thread. A call runs in a dedicated voice thread, so that a busy UI (for example, a large READY payload
or a long channel) never makes audio stutter. The noise suppressor processes 10 ms frames in well
under 1 ms.

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
480-sample frames, and always returns exactly as many samples as it receives (constant latency: 10 ms of
frame buffering plus 30 ms of look-ahead for the voice gate).
On top of RNNoise it adds:

- **Strength:** a wet/dry mix between the denoised and the original signal.
- **Voice gate:** uses RNNoise's voice-activity probability, with a hold time and attack/release
  ramps (interpolated per sample) so that the gate never clicks. The decision is taken on the newest
  frame and applied to audio delayed by three frames, so word onsets are not clipped.

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

Voice is split into a Qt-free core (unit-tested offline) and a thin Qt layer.

```
 main gateway (op 4) ──► VOICE_STATE_UPDATE + VOICE_SERVER_UPDATE ──► VoiceSession (GUI thread)
                                                                           │ queued calls
                                                         ┌─────────────────▼──────────────────┐
                                                         │ VoiceEngine (own QThread)           │
  mic ─► AudioPipeline (RNNoise, gate) ─► 20 ms frames ─►│ Opus ─► DAVE encrypt ─► RTP + AEAD ─┼─► UDP
  speakers ◄─ PlaybackMixer ◄─ jitter buffers ◄─ Opus ◄─ │ DAVE decrypt ◄─ AEAD ◄─ RTP parse ◄─┼── UDP
                                                         │ VoiceGateway (v8, JSON + binary)    │
                                                         └────────────────────────────────────┘
```

| Class | Responsibility |
| --- | --- |
| `VoiceSession` | GUI-side entry point. Sends gateway opcode 4 to join and leave, waits for both `VOICE_STATE_UPDATE` (session id) and `VOICE_SERVER_UPDATE` (endpoint, token), then starts the `VoiceEngine` in its own thread and relays its status to the UI. Handles being moved or disconnected by others. |
| `VoiceGateway` | Voice WebSocket, version 8: IDENTIFY (with `max_dave_protocol_version`), heartbeats with `seq_ack`, buffered RESUME, READY, SESSION_DESCRIPTION, SPEAKING, client connect/disconnect, and the DAVE opcodes, including binary frames (`[seq:u16][op:u8][payload]`). |
| `VoiceEngine` | Owns the UDP socket, IP discovery and keep-alives, transport crypto, the `DaveSession`, the Opus encoder and decoders, capture and playback. Decides when you are speaking (voice gate or energy detector with hold), sends opcode 5 before audio and five silence frames after it, and never sends media unencrypted while DAVE keys are pending. |
| `core/DaveSession` | DAVE state machine over libdave's C API, mirroring Discord's reference session manager: external sender, key packages, proposals → commit/welcome, commit and welcome processing, protocol transitions (opcodes 21–31), and installation of per-sender key ratchets on the encryptor and per-user decryptors. Does no I/O, which makes it testable. |
| `core/TransportCrypto` | `aead_aes256_gcm_rtpsize` (OpenSSL) and `aead_xchacha20_poly1305_rtpsize` (libsodium): RTP header as AAD, 32-bit big-endian nonce counter appended to the packet, RTP extension body stripped after decryption. |
| `core/Rtp`, `core/IpDiscovery`, `core/OpusCodec` | Packet formats and the Opus wrappers (stereo, 48 kHz, 20 ms, in-band FEC). |
| `audio/PlaybackMixer` | Pull-mode `QIODevice` feeding `QAudioSink`: one jitter buffer per SSRC (playback starts at 40 ms, capped at 200 ms), mixing, resampling to the device format. |

`tests/test_voice_core.cpp` runs an offline DAVE handshake between two `DaveSession`s, with libdave's test
"external sender" playing the voice gateway, and checks that each client decrypts the other's frames
while an outsider cannot.

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
