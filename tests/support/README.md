`external_sender.{h,cpp}` come from libdave's test suite (MIT, https://github.com/discord/libdave, `cpp/test/`).
They play the role of Discord's voice gateway, which is the MLS "external sender", so that
`tests/test_voice_core.cpp` can run a complete DAVE handshake between two clients offline.
