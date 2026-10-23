// The core library must stay free of Windows headers so it compiles and tests anywhere.
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "src/core must not include <windows.h>"
#endif

namespace ixpat {

const char* coreName() { return "ixpat-core"; }

}  // namespace ixpat
