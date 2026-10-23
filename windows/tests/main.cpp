#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <string>

/// The suite never touches the user's %LOCALAPPDATA%\iXpat: every log and settings file a test writes lands in a
/// per-run temp directory (IXPAT_DATA_DIR, honoured by win::appDataDir) that is removed afterwards.
int main(int argc, char** argv) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / L"ixpat-tests" / std::to_wstring(GetCurrentProcessId());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    SetEnvironmentVariableW(L"IXPAT_DATA_DIR", dir.c_str());
    doctest::Context ctx(argc, argv);
    const int rc = ctx.run();
    std::filesystem::remove_all(dir, ec);
    return rc;
}
