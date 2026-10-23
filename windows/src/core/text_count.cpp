#include "core/text_count.h"

namespace ixpat {

TextCount countWordsLines(std::wstring_view s) {
    TextCount c;
    bool inWord = false;
    for (const wchar_t ch : s) {
        const bool ws = ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r';
        if (ch == L'\n') ++c.lines;
        if (!ws && !inWord) ++c.words;
        inWord = !ws;
    }
    return c;
}

}  // namespace ixpat
