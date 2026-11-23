# Changelog

All notable changes to Lightcord are listed here. Versions follow [Semantic Versioning](https://semver.org/).

## [0.2.1] - unreleased

### Fixed
- Your voice cutting out for the people listening:
  - The RTP timestamp now follows the microphone clock and keeps advancing during pauses. It used to freeze
    while you were silent, so the official client saw every pause as a large network delay and stretched or
    cut the next words.
  - Short pauses no longer stop the stream: transmission continues for 500 ms after the last voiced frame,
    and the five trailing silence frames are sent at the normal 20 ms pace instead of in one burst.
  - The voice gate looks 30 ms ahead, so it is already open when the first syllable arrives, and the default
    hold time is now 400 ms (settings still on the old 200 ms default are moved to 400 ms once).
- Choppy voice playback ("hiccups"): audio is now fed to the sound card in push mode every 10 ms with a
  high-precision timer, instead of Qt's pull mode, which on Windows polls with the coarse system timer and
  drained 30-45 ms at a time.

### Changed
- Adaptive jitter buffer per speaker: starts at 60 ms, grows by 20 ms after each real underrun (up to 160 ms),
  shrinks back after 20 s without problems, and drops excess audio to keep latency bounded.
- Playback statistics (target, buffered audio, underruns) in the voice diagnostics log.
- On Windows, the timer resolution is raised to 1 ms during calls, and the voice thread runs at a higher priority.
- The voice diagnostics log now reports captured audio against wall-clock time (a shortfall means lost
  microphone audio) and the process memory; memory is also logged at startup and after READY.

## [0.2.0] - 2026-11-20 (alpha)

### Added
- Voice chat in server voice channels: voice gateway v8, UDP transport with IP discovery, transport encryption
  (AES-256-GCM / XChaCha20-Poly1305), DAVE end-to-end encryption via libdave, Opus audio, jitter buffers and mixing.
- Noise suppression applied to the voice send path, with voice gate and speaking detection.
- Voice channel members and speaking indicators in the navigator; connection and encryption status panel.
- Log file with voice diagnostics (*Settings → Advanced → Open log folder*).

### Fixed
- Remote voices were not played on Windows: Qt 6.8's Windows audio sink only reads `bytesAvailable()` bytes from
  pull-mode sources.
- Audio arriving before the server announced the sender's SSRC was dropped.

## [0.1.0] - 2026-11-19 (alpha)

### Added
- First version: native Qt 6 client with text chat, gateway with resume, permission-aware navigator, Discord
  Markdown, RNNoise microphone test, screen and window capture preview, Night/Day themes, secure token storage.
