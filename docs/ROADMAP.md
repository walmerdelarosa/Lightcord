# Roadmap

## 0.1: Foundations (current, alpha)

- [x] Native Qt 6 / C++17 application with the new Lightcord identity (logo, icons, Night and Day themes)
- [x] REST client with rate-limit retries
- [x] Gateway v10: identify, heartbeat, resume, reconnect with back-off
- [x] Servers, categories, channels and DMs in one navigator with search
- [x] Permission-aware channel list and composer
- [x] Read and send messages; live create/update/delete; Discord Markdown subset
- [x] RNNoise noise suppression with strength, voice gate and live microphone test
- [x] Screen and window capture with live preview (5/15/30 fps, 720p/1080p/native)
- [x] Token storage in the Windows Credential Manager
- [x] CI builds for Windows (MSVC) and Linux (GCC), with unit tests

## 0.2: Voice

Discord has required **DAVE** (Discord Audio & Video End-to-End Encryption) for every voice and video
call since March 2026, so a voice engine must implement it to join any call.

- [ ] Voice gateway (WebSocket): identify, heartbeat, `SELECT_PROTOCOL`, session description
- [ ] UDP socket with IP discovery
- [ ] Transport encryption (`aead_aes256_gcm_rtpsize` / `aead_xchacha20_poly1305_rtpsize`) via libsodium
- [ ] **DAVE** integration via [discord/libdave](https://github.com/discord/libdave): MLS group handling,
      per-sender media frame encryption, and protocol transitions signalled through the voice gateway
- [ ] Opus encode/decode, jitter buffer, per-user volume
- [ ] Send path: `AudioPipeline` (with noise suppression) → Opus → DAVE → transport encryption → RTP
- [ ] Speaking indicators, push-to-talk and voice-activity modes
- [ ] Voice channel member list in the navigator

## 0.3: Go Live (screen sharing to others)

- [ ] Faster capture back-ends: Windows.Graphics.Capture / DXGI Desktop Duplication
- [ ] Hardware H.264 encoding through Media Foundation (software fallback)
- [ ] RTP video packetization, DAVE for video frames
- [ ] Watching other people's streams in a separate window

## 0.4: Client comfort

- [ ] Ripcord-style tabs and split panes (several channels side by side)
- [ ] Unread and mention markers, desktop notifications
- [ ] File uploads (drag and drop), image previews (opt-in)
- [ ] Reactions, replies, threads and forum channels
- [ ] Typing indicators and member list
- [ ] `zlib-stream` gateway compression to cut bandwidth
- [ ] Translations (the UI already uses `tr()`)

## Non-goals

- Embedding a web engine or reusing the official client's JavaScript
- Self-bot or automation features for user accounts
