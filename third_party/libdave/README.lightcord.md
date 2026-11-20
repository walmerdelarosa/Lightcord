# libdave (vendored)

- Upstream: https://github.com/discord/libdave (C++ library in `cpp/`)
- Commit: `b5463c443df74db75a713c775b09a847621e6d62`
- License: MIT (see `LICENSE`)

libdave implements Discord's Audio & Video End-to-End Encryption (DAVE) protocol. Lightcord uses its C API
(`includes/dave/dave.h`) from `src/voice/core/DaveSession.cpp`.

## Local changes

None to the source files. Lightcord builds the library with its own CMake target (see the top-level
`CMakeLists.txt`) instead of upstream's, so that upstream's `-Werror` flags do not apply to Lightcord builds.

Removed from the upstream tree because Lightcord does not use them: the WebAssembly bindings, the BoringSSL
cryptor, the platform-specific persistent key storage (only the "null" implementation is kept), tests, the
fuzzing driver and the build scripts.

## Dependencies

- [mlspp](https://github.com/cisco/mlspp) (BSD-2-Clause), built by vcpkg from the overlay port in
  `vcpkg/ports/mlspp` (copied from libdave's `vcpkg-alts/overlay-ports`)
- OpenSSL 3 and nlohmann-json, from vcpkg
