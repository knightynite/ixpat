#pragma once
#include <filesystem>
#include <string>

namespace ixpat::win {

/// The publisher every self-installed update must be signed by: the subject of the release signing certificate
/// (Azure Artifact Signing, public trust). The certificate rotates; the name on it does not.
constexpr const wchar_t* kUpdatePublisher = L"AL Najafi";

/// What WinVerifyTrust and the signer's certificate say about a file.
struct SignatureCheck {
    bool trusted = false;        // a valid embedded Authenticode signature chaining to a trusted root, revocation checked
    std::wstring commonName;     // the signer's CN, when a signature could be read at all
    std::wstring organization;   // the signer's O
    std::string error;           // when !trusted: why, in a sentence for the user
};

/// Verifies the file's embedded Authenticode signature (no UI, no catalog lookup) and reads the signer's subject.
SignatureCheck checkAuthenticode(const std::filesystem::path& file);

/// True when the file is trusted and the signer's CN and O both equal kUpdatePublisher.
bool isPinnedPublisher(const SignatureCheck& c);

}  // namespace ixpat::win
