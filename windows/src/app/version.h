#pragma once

// The product version. Windows releases are tagged win-v<version> on GitHub.
// Bump the three numbers only: the strings and the resource's fixed version block derive from them.
#define IXPAT_VERSION_MAJOR 0
#define IXPAT_VERSION_MINOR 2
#define IXPAT_VERSION_PATCH 12

#define IXPAT_STRINGIZE_(x) #x
#define IXPAT_STRINGIZE(x) IXPAT_STRINGIZE_(x)
#define IXPAT_VERSION_STRING IXPAT_STRINGIZE(IXPAT_VERSION_MAJOR) "." IXPAT_STRINGIZE(IXPAT_VERSION_MINOR) "." IXPAT_STRINGIZE(IXPAT_VERSION_PATCH)
#define IXPAT_VERSION_WSTRING L"" IXPAT_VERSION_STRING
