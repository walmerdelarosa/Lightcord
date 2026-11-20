// SPDX-License-Identifier: MIT
#include "TokenStore.h"

#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace lightcord {

namespace {
#ifdef Q_OS_WIN
const wchar_t *kCredentialTarget = L"Lightcord/account";
#endif

// Serialized as "bot:<token>" or "user:<token>".
QByteArray serialize(const TokenStore::Credentials &c)
{
    return (c.isBot ? QByteArrayLiteral("bot:") : QByteArrayLiteral("user:")) + c.token.toUtf8();
}

TokenStore::Credentials deserialize(const QByteArray &blob)
{
    TokenStore::Credentials c;
    if (blob.startsWith("bot:")) {
        c.isBot = true;
        c.token = QString::fromUtf8(blob.mid(4));
    } else if (blob.startsWith("user:")) {
        c.token = QString::fromUtf8(blob.mid(5));
    }
    return c;
}
} // namespace

bool TokenStore::isSecure()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

TokenStore::Credentials TokenStore::load()
{
#ifdef Q_OS_WIN
    PCREDENTIALW cred = nullptr;
    if (CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &cred) && cred) {
        const QByteArray blob(reinterpret_cast<const char *>(cred->CredentialBlob),
                              static_cast<int>(cred->CredentialBlobSize));
        CredFree(cred);
        return deserialize(blob);
    }
    return {};
#else
    QSettings s;
    return deserialize(s.value("account/credentials").toByteArray());
#endif
}

bool TokenStore::save(const Credentials &credentials)
{
    const QByteArray blob = serialize(credentials);
#ifdef Q_OS_WIN
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(kCredentialTarget);
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(L"Lightcord");
    return CredWriteW(&cred, 0) == TRUE;
#else
    QSettings s;
    s.setValue("account/credentials", blob);
    return true;
#endif
}

void TokenStore::clear()
{
#ifdef Q_OS_WIN
    CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0);
#endif
    QSettings s;
    s.remove("account/credentials");
}

} // namespace lightcord
