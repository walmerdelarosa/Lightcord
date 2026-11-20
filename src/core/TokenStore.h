// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace lightcord {

// Persists the account token.
//
// On Windows the token is stored in the Windows Credential Manager
// (encrypted with the user's login credentials by DPAPI). On other platforms
// it falls back to QSettings, which is plain text: a warning is shown in the
// login dialog in that case.
class TokenStore {
public:
    struct Credentials {
        QString token;
        bool isBot = false;
        bool valid() const { return !token.isEmpty(); }
    };

    static bool isSecure();
    static Credentials load();
    static bool save(const Credentials &credentials);
    static void clear();
};

} // namespace lightcord
