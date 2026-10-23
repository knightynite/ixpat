#include <doctest/doctest.h>

#include "core/text_count.h"

using ixpat::countWordsLines;

TEST_CASE("text count: words and line breaks of a selection") {
    auto c = countWordsLines(L"");
    CHECK(c.words == 0);
    CHECK(c.lines == 0);
    c = countWordsLines(L"hello");
    CHECK(c.words == 1);
    CHECK(c.lines == 0);
    c = countWordsLines(L"  hello   world ");
    CHECK(c.words == 2);
    CHECK(c.lines == 0);
    c = countWordsLines(L"one\r\ntwo\nthree");
    CHECK(c.words == 3);
    CHECK(c.lines == 2);
    c = countWordsLines(L"tab\tsep");
    CHECK(c.words == 2);
}
