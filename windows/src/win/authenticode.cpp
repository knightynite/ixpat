#include "win/authenticode.h"

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <cstdio>
#include <vector>

namespace ixpat::win {
namespace {

std::string trustErrorText(LONG status) {
    switch (static_cast<DWORD>(status)) {
        case TRUST_E_NOSIGNATURE: return "The file is not signed.";
        case TRUST_E_BAD_DIGEST: return "The file does not match its signature.";
        case TRUST_E_EXPLICIT_DISTRUST: return "The signing certificate is explicitly distrusted on this PC.";
        case TRUST_E_SUBJECT_NOT_TRUSTED: return "The signature is not trusted on this PC.";
        case CERT_E_UNTRUSTEDROOT: return "The signing certificate does not chain to a trusted root.";
        case CERT_E_EXPIRED: return "The signing certificate has expired and the signature carries no timestamp.";
        case CERT_E_REVOKED: return "The signing certificate has been revoked.";
        case CERT_E_REVOCATION_FAILURE: return "The certificate's revocation status could not be checked (offline?).";
        case CERT_E_CHAINING: return "The signing certificate's chain could not be built.";
        case CRYPT_E_SECURITY_SETTINGS: return "The signature was rejected by this PC's security settings.";
        default: break;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "The signature check failed (0x%08lX).", static_cast<unsigned long>(status));
    return buf;
}

/// The subject attributes of the file's first signer, read from the embedded PKCS#7 message.
void readSigner(const std::filesystem::path& file, std::wstring& cn, std::wstring& org) {
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    DWORD encoding = 0, contentType = 0, formatType = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, file.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
                          &encoding, &contentType, &formatType, &store, &msg, nullptr)) {
        return;
    }
    DWORD size = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size) && size) {
        std::vector<BYTE> buf(size);
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &size)) {
            const auto* info = reinterpret_cast<const CMSG_SIGNER_INFO*>(buf.data());
            CERT_INFO want{};
            want.Issuer = info->Issuer;
            want.SerialNumber = info->SerialNumber;
            if (PCCERT_CONTEXT cert = CertFindCertificateInStore(store, encoding, 0, CERT_FIND_SUBJECT_CERT, &want, nullptr)) {
                auto attr = [&](const char* oid) {
                    wchar_t name[256]{};
                    const DWORD n = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(oid), name, 256);
                    return n > 1 ? std::wstring(name, n - 1) : std::wstring();
                };
                cn = attr(szOID_COMMON_NAME);
                org = attr(szOID_ORGANIZATION_NAME);
                CertFreeCertificateContext(cert);
            }
        }
    }
    if (msg) CryptMsgClose(msg);
    if (store) CertCloseStore(store, 0);
}

}  // namespace

SignatureCheck checkAuthenticode(const std::filesystem::path& file) {
    SignatureCheck c;
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof fi;
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof wd;
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    // No WTD_LIFETIME_SIGNING_FLAG: the release certificate is short-lived, the signature is timestamped and outlives it.
    wd.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    HWND noUi = static_cast<HWND>(INVALID_HANDLE_VALUE);
    const LONG status = WinVerifyTrust(noUi, &action, &wd);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(noUi, &action, &wd);
    c.trusted = status == ERROR_SUCCESS;
    if (!c.trusted) c.error = trustErrorText(status);
    if (static_cast<DWORD>(status) != TRUST_E_NOSIGNATURE) readSigner(file, c.commonName, c.organization);
    return c;
}

bool isPinnedPublisher(const SignatureCheck& c) { return c.trusted && c.commonName == kUpdatePublisher && c.organization == kUpdatePublisher; }

}  // namespace ixpat::win
