#pragma once
#include <string_view>

namespace ixpat {

/// Words and line breaks in a text selection (AXWatcher.swift counts them to pace textSelect / textLine ticks).
struct TextCount {
    int words = 0;
    int lines = 0;  // '\n' count; "\r\n" counts once
};

TextCount countWordsLines(std::wstring_view s);

}  // namespace ixpat
