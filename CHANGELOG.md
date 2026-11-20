# Changelog

All notable changes to Lightcord are listed here. Versions follow [Semantic Versioning](https://semver.org/).

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
