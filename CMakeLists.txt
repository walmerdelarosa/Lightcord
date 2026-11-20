cmake_minimum_required(VERSION 3.21)

project(Lightcord
    VERSION 0.2.0
    DESCRIPTION "A lightweight native Discord client"
    HOMEPAGE_URL "https://github.com/walmerdelarosa/Lightcord"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

include(GNUInstallDirs)

if(MSVC)
    # Sources are UTF-8 (UI strings contain symbols such as "·" and "…").
    add_compile_options(/utf-8)
endif()

option(LIGHTCORD_BUILD_APP   "Build the Qt desktop application" ON)
option(LIGHTCORD_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# RNNoise (BSD-3-Clause, vendored in third_party/rnnoise)
# ---------------------------------------------------------------------------
add_library(rnnoise STATIC
    third_party/rnnoise/src/celt_lpc.c
    third_party/rnnoise/src/denoise.c
    third_party/rnnoise/src/kiss_fft.c
    third_party/rnnoise/src/pitch.c
    third_party/rnnoise/src/rnn.c
    third_party/rnnoise/src/rnn_data.c
    third_party/rnnoise/src/rnn_reader.c
)
target_include_directories(rnnoise PUBLIC third_party/rnnoise/include)
target_compile_definitions(rnnoise PRIVATE _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0)
else()
    target_compile_options(rnnoise PRIVATE -w)
    target_link_libraries(rnnoise PRIVATE m)
endif()

# ---------------------------------------------------------------------------
# Qt-free audio core (shared by the app and the tests)
# ---------------------------------------------------------------------------
add_library(lightcord_audio_core STATIC
    src/audio/NoiseSuppressor.cpp
    src/audio/NoiseSuppressor.h
)
target_include_directories(lightcord_audio_core PUBLIC src)
target_link_libraries(lightcord_audio_core PUBLIC rnnoise)

# ---------------------------------------------------------------------------
# Voice: libdave (DAVE end-to-end encryption) + Qt-free voice core
# ---------------------------------------------------------------------------
option(LIGHTCORD_ENABLE_VOICE "Build voice chat (needs OpenSSL, Opus, libsodium and mlspp)" ON)

if(LIGHTCORD_ENABLE_VOICE)
    find_package(OpenSSL 1.1.1 REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(MLSPP CONFIG REQUIRED)

    find_package(Opus CONFIG QUIET)
    if(TARGET Opus::opus)
        set(LIGHTCORD_OPUS_TARGET Opus::opus)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
        set(LIGHTCORD_OPUS_TARGET PkgConfig::OPUS)
    endif()

    find_package(unofficial-sodium CONFIG QUIET)
    if(TARGET unofficial-sodium::sodium)
        set(LIGHTCORD_SODIUM_TARGET unofficial-sodium::sodium)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
        set(LIGHTCORD_SODIUM_TARGET PkgConfig::SODIUM)
    endif()

    # libdave, vendored from github.com/discord/libdave (MIT). Built with our own
    # target so that its -Werror flags do not leak into the project.
    add_library(dave STATIC
        third_party/libdave/src/bindings_capi.cpp
        third_party/libdave/src/codec_utils.cpp
        third_party/libdave/src/cryptor.cpp
        third_party/libdave/src/cryptor_manager.cpp
        third_party/libdave/src/decryptor.cpp
        third_party/libdave/src/encryptor.cpp
        third_party/libdave/src/frame_processors.cpp
        third_party/libdave/src/logger.cpp
        third_party/libdave/src/mls_key_ratchet.cpp
        third_party/libdave/src/openssl_cryptor.cpp
        third_party/libdave/src/version.cpp
        third_party/libdave/src/mls/key_serialization.cpp
        third_party/libdave/src/mls/parameters.cpp
        third_party/libdave/src/mls/persisted_key_pair_null.cpp
        third_party/libdave/src/mls/session.cpp
        third_party/libdave/src/mls/user_credential.cpp
        third_party/libdave/src/mls/util.cpp
        third_party/libdave/src/utils/leb128.cpp
    )
    target_include_directories(dave
        PUBLIC third_party/libdave/includes
        PRIVATE third_party/libdave/src)
    target_compile_definitions(dave PUBLIC DAVE_STATIC)
    if(OPENSSL_VERSION VERSION_GREATER_EQUAL 3)
        target_compile_definitions(dave PRIVATE WITH_OPENSSL3)
    endif()
    if(MSVC)
        target_compile_definitions(dave PRIVATE WINDOWS _CRT_SECURE_NO_WARNINGS)
        target_compile_options(dave PRIVATE /W0 /bigobj)
    else()
        target_compile_options(dave PRIVATE -w)
    endif()
    target_link_libraries(dave PUBLIC MLSPP::mlspp OpenSSL::Crypto nlohmann_json::nlohmann_json)

    add_library(lightcord_voice_core STATIC
        src/voice/core/Rtp.cpp             src/voice/core/Rtp.h
        src/voice/core/TransportCrypto.cpp src/voice/core/TransportCrypto.h
        src/voice/core/IpDiscovery.cpp     src/voice/core/IpDiscovery.h
        src/voice/core/OpusCodec.cpp       src/voice/core/OpusCodec.h
        src/voice/core/DaveSession.cpp     src/voice/core/DaveSession.h
    )
    target_include_directories(lightcord_voice_core PUBLIC src)
    target_link_libraries(lightcord_voice_core PUBLIC dave OpenSSL::Crypto
        ${LIGHTCORD_OPUS_TARGET} ${LIGHTCORD_SODIUM_TARGET})
    find_package(Threads REQUIRED)
    target_link_libraries(lightcord_voice_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Desktop application
# ---------------------------------------------------------------------------
if(LIGHTCORD_BUILD_APP)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Multimedia Svg)
    qt_standard_project_setup()

    qt_add_executable(Lightcord WIN32 MACOSX_BUNDLE
        src/main.cpp
        src/core/Models.cpp            src/core/Models.h
        src/core/DiscordRest.cpp       src/core/DiscordRest.h
        src/core/Gateway.cpp           src/core/Gateway.h
        src/core/Session.cpp           src/core/Session.h
        src/core/TokenStore.cpp        src/core/TokenStore.h
        src/core/Settings.cpp          src/core/Settings.h
        src/core/MessageFormatter.cpp  src/core/MessageFormatter.h
        src/audio/AudioPipeline.cpp    src/audio/AudioPipeline.h
        src/audio/PlaybackMixer.cpp    src/audio/PlaybackMixer.h
        src/screenshare/ScreenCapturer.cpp src/screenshare/ScreenCapturer.h
        src/voice/VoiceSession.cpp     src/voice/VoiceSession.h
        src/ui/Theme.cpp               src/ui/Theme.h
        src/ui/LoginDialog.cpp         src/ui/LoginDialog.h
        src/ui/MainWindow.cpp          src/ui/MainWindow.h
        src/ui/VoiceBar.cpp            src/ui/VoiceBar.h
        src/ui/SettingsDialog.cpp      src/ui/SettingsDialog.h
        src/ui/ScreenShareDialog.cpp   src/ui/ScreenShareDialog.h
    )

    qt_add_resources(Lightcord "lightcord_resources"
        PREFIX "/"
        BASE resources
        FILES
            resources/icons/lightcord.svg
            resources/icons/mic.svg
            resources/icons/mic-off.svg
            resources/icons/headphones.svg
            resources/icons/headphones-off.svg
            resources/icons/screen.svg
            resources/icons/settings.svg
            resources/icons/send.svg
            resources/icons/hash.svg
            resources/icons/speaker.svg
            resources/icons/waves.svg
            resources/icons/at.svg
            resources/icons/logout.svg
            resources/icons/eye.svg
            resources/icons/phone-off.svg
            resources/themes/base.qss
    )

    if(LIGHTCORD_ENABLE_VOICE)
        target_sources(Lightcord PRIVATE
            src/voice/VoiceGateway.cpp src/voice/VoiceGateway.h
            src/voice/VoiceEngine.cpp  src/voice/VoiceEngine.h)
        target_compile_definitions(Lightcord PRIVATE LIGHTCORD_HAS_VOICE)
        target_link_libraries(Lightcord PRIVATE lightcord_voice_core)
    endif()

    if(WIN32)
        target_sources(Lightcord PRIVATE resources/windows/lightcord.rc)
        target_link_libraries(Lightcord PRIVATE advapi32 user32 gdi32 dwmapi)
        target_compile_definitions(Lightcord PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

    target_compile_definitions(Lightcord PRIVATE
        LIGHTCORD_VERSION="${PROJECT_VERSION}")

    target_link_libraries(Lightcord PRIVATE
        lightcord_audio_core
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
        Qt6::WebSockets Qt6::Multimedia Qt6::Svg)

    set_target_properties(Lightcord PROPERTIES
        OUTPUT_NAME "Lightcord"
        MACOSX_BUNDLE_BUNDLE_NAME "Lightcord"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.walmerdelarosa.lightcord")

    install(TARGETS Lightcord
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(LIGHTCORD_BUILD_TESTS)
    enable_testing()
    add_executable(test_noise_suppressor tests/test_noise_suppressor.cpp)
    target_link_libraries(test_noise_suppressor PRIVATE lightcord_audio_core)
    add_test(NAME noise_suppressor COMMAND test_noise_suppressor)

    if(LIGHTCORD_ENABLE_VOICE)
        add_executable(test_voice_core
            tests/test_voice_core.cpp
            tests/support/external_sender.cpp
            tests/support/external_sender.h)
        target_include_directories(test_voice_core PRIVATE tests/support third_party/libdave/src)
        target_link_libraries(test_voice_core PRIVATE lightcord_voice_core)
        if(MSVC)
            target_compile_options(test_voice_core PRIVATE /bigobj)
            target_compile_definitions(test_voice_core PRIVATE WINDOWS)
        endif()
        add_test(NAME voice_core COMMAND test_voice_core)
    endif()
endif()
