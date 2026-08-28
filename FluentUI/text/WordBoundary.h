// WordBoundary.h — where a "word" starts and ends, for double-click select and
// Ctrl+Arrow navigation.
//
// Pure string logic: no DWrite, no window, no allocation. That is a repo convention
// ("Pure functions get extracted for testability"), and here it is also the only way
// these get tested at all — the alternative home for this code is inside a pointer
// event handler, which needs a live input stack to reach.
//
// THE RULE: CHARACTER-CLASS RUNS. Every character falls into one of three classes
// (below); a word is a maximal run of one class. Double-clicking selects the run under
// the cursor, so "foo_bar" selects whole (underscore is Word), while "foo.bar" clicked
// on "foo" selects just "foo" (the '.' is Punct and therefore a boundary).
//
// WHAT THIS DELIBERATELY DOES NOT DO, stated up front because it is the one thing a
// CJK user will notice: it does not segment Chinese/Japanese text into words. Every
// non-ASCII character is classed as Word, so double-clicking 你好世界 selects all four
// characters rather than 你好 / 世界. Real segmentation needs IDWriteTextAnalyzer with
// a line-breaking pass per line, which costs per-line analysis work — directly against
// the virtualization this codebase just spent two phases building, where the whole
// point is that a line costs nothing until it is on screen. Notepad and VS Code make
// the same trade for the same reason. If segmentation is ever wanted, it belongs in a
// separate analyzer-backed implementation selected per-locale, not as a mutation of
// this one.
//
// NEWLINES ARE HARD BOUNDARIES. '\n' classes as Space, but WordRangeAt additionally
// refuses to extend a run ACROSS one: a double-click must never select text on two
// lines, which is what a naive "maximal run of Space" would do to the blank between
// two paragraphs. Ctrl+Arrow, by contrast, is allowed to cross a newline — that is how
// every editor behaves, and it is why the two questions are separate functions rather
// than one with a flag.
//
// OFFSETS are UTF-16 code-unit indices, matching TextEditBase's caret units. Surrogate
// pairs are therefore two positions; a boundary can in principle land between them.
// That is pre-existing across the whole edit model (caret_, selection, LineIndex all
// count code units), so this header does not invent a different convention — it would
// only disagree with everything that consumes it.
#pragma once

#include <cstddef>
#include <string_view>
#include <utility>

namespace fluent {

// The three classes a character can belong to. Ordering is not meaningful.
enum class CharClass {
    Word,    // letters, digits, '_', and ALL non-ASCII (see the CJK note above)
    Space,   // ' ', '\t', '\n', '\r', vertical tab, form feed
    Punct,   // everything else: . , ; ( ) { } + - = etc.
};

inline CharClass ClassifyChar(wchar_t c) {
    if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0x0B || c == 0x0C)
        return CharClass::Space;
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
        (c >= L'0' && c <= L'9') || c == L'_')
        return CharClass::Word;
    // Everything at or above 0x80 is Word. This is what makes CJK select as one run,
    // and it also gets accented Latin (é, ü) and Cyrillic right, which the ASCII test
    // above would otherwise class as Punct and break in the middle of a word.
    if (c >= 0x80) return CharClass::Word;
    return CharClass::Punct;
}

// The range [start, end) a double-click at `index` should select.
//
// `index` is a CARET position, so it can be 0..size() inclusive — including one past
// the last character. A click past the end of the text selects the run ending there,
// which is what clicking in the empty space after the last word does.
//
// Never crosses a '\n' (see the header note). Returns an empty range at the same index
// when there is nothing to select: an empty buffer, or a click that sits exactly on a
// newline (there is no word there, and selecting the line break itself would look like
// a stray highlight at the end of the line).
inline std::pair<size_t, size_t> WordRangeAt(std::wstring_view text, size_t index) {
    const size_t n = text.size();
    if (n == 0) return {0, 0};
    if (index > n) index = n;

    // A caret at the very end has no character AT index; use the one before it, which
    // is the run the user clicked into.
    size_t probe = index;
    if (probe == n) {
        if (probe == 0) return {0, 0};
        --probe;
    }
    // Clicking exactly on a line break: the caret is between two lines and belongs to
    // neither word. Selecting nothing is better than selecting the newline (which draws
    // as a highlight hanging off the end of the line) or the run on one arbitrary side.
    if (text[probe] == L'\n') return {index, index};
    // A caret sitting just AFTER a newline (start of a line) probes backwards onto that
    // newline only when index==n; handled above. Here probe is a real character.

    const CharClass cls = ClassifyChar(text[probe]);

    size_t start = probe;
    while (start > 0) {
        const wchar_t prev = text[start - 1];
        if (prev == L'\n') break;                  // do not cross a line boundary
        if (ClassifyChar(prev) != cls) break;
        --start;
    }
    size_t end = probe + 1;
    while (end < n) {
        const wchar_t next = text[end];
        if (next == L'\n') break;
        if (ClassifyChar(next) != cls) break;
        ++end;
    }
    return {start, end};
}

// The caret position one word to the RIGHT of `from` (Ctrl+Right).
//
// Semantics match Windows edit controls: skip the remainder of the current run, then
// skip any whitespace, landing on the START of the next word. That is why pressing
// Ctrl+Right repeatedly walks word-starts rather than alternating between the end of one
// word and the start of the next.
//
// Newlines are crossed freely here, unlike WordRangeAt — Ctrl+Right at the end of a line
// should reach the next line, which is what every editor does.
inline size_t NextWordBoundary(std::wstring_view text, size_t from) {
    const size_t n = text.size();
    if (from >= n) return n;

    size_t i = from;
    const CharClass startCls = ClassifyChar(text[i]);
    // Leave the current run.
    while (i < n && ClassifyChar(text[i]) == startCls) ++i;
    // Then skip whitespace so we land ON a word, not in the gap before it.
    while (i < n && ClassifyChar(text[i]) == CharClass::Space) ++i;
    return i;
}

// The caret position one word to the LEFT of `from` (Ctrl+Left).
//
// The mirror image, and asymmetric on purpose: it skips whitespace FIRST, then walks back
// over the run. Both directions therefore come to rest on a word start, which is what
// makes Ctrl+Left and Ctrl+Right land on the same set of positions instead of drifting.
inline size_t PrevWordBoundary(std::wstring_view text, size_t from) {
    if (from == 0) return 0;
    size_t i = from > text.size() ? text.size() : from;

    // Skip whitespace immediately to the left.
    while (i > 0 && ClassifyChar(text[i - 1]) == CharClass::Space) --i;
    if (i == 0) return 0;
    // Walk back over the run we just landed in.
    const CharClass cls = ClassifyChar(text[i - 1]);
    while (i > 0 && ClassifyChar(text[i - 1]) == cls) --i;
    return i;
}

// The logical line containing `index`, as [start, end) with end EXCLUDING the '\n'.
//
// Used by triple-click. Lives here rather than on LineIndex because it must work in Wrap
// mode too, where no line index is built (and where a "line" for selection purposes is
// still the logical one — a visual line would change what gets selected when the window
// is resized, which is indefensible).
inline std::pair<size_t, size_t> LogicalLineRangeAt(std::wstring_view text,
                                                    size_t index) {
    const size_t n = text.size();
    if (index > n) index = n;
    size_t start = index;
    while (start > 0 && text[start - 1] != L'\n') --start;
    size_t end = index;
    while (end < n && text[end] != L'\n') ++end;
    return {start, end};
}

} // namespace fluent
