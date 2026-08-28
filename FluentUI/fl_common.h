// fl_common.h — Shared includes, COM smart pointers, HRESULT trace helpers.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wrl/client.h>
#include <sal.h>       // _Printf_format_string_ (format-string checking on TraceFmt)
#include <cstdarg>     // va_list / va_start for TraceFmt
#include <cstdio>

namespace fluent {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// DIP rectangle (device independent pixels, 96-DPI basis). All control geometry
// is expressed in DIPs; physical pixels = DIP * dpi / 96.
struct RectDip {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
    bool intersects(const RectDip& o) const {
        return x < o.right() && right() > o.x && y < o.bottom() && bottom() > o.y;
    }
    bool isEmpty() const { return w <= 0.0f || h <= 0.0f; }
    // Grown by `pad` on all four sides (negative shrinks). Used by controls whose
    // painted visual exceeds their layout bounds to report an honest dirty rect.
    RectDip inflated(float pad) const {
        return {x - pad, y - pad, w + pad * 2.0f, h + pad * 2.0f};
    }
    static RectDip Union(const RectDip& a, const RectDip& b) {
        if (a.isEmpty()) return b;
        if (b.isEmpty()) return a;
        float x1 = (a.x < b.x) ? a.x : b.x;
        float y1 = (a.y < b.y) ? a.y : b.y;
        float x2 = (a.right() > b.right()) ? a.right() : b.right();
        float y2 = (a.bottom() > b.bottom()) ? a.bottom() : b.bottom();
        return {x1, y1, x2 - x1, y2 - y1};
    }
};

// --- Tracing ---------------------------------------------------------------
// FLUENTUI_ENABLE_TRACE gates every trace call. Default: on in Debug, off in
// Release. Both are overridable from the build, which is the point — a trace is
// only worth having if you can still get one out of the configuration the bug
// actually reproduces in:
//
//   /D FLUENTUI_ENABLE_TRACE=1   Release build WITH traces (field diagnosis)
//   /D FLUENTUI_ENABLE_TRACE=0   Debug build without them (clean timing runs)
//
// WHY THIS IS GATED AT ALL: OutputDebugStringA is not free. With no debugger
// attached it is a cheap kernel check, but WITH one attached it blocks the
// calling thread until the debugger drains the message. A trace on a
// per-input or per-frame path therefore adds nondeterministic latency to
// exactly the interaction you are trying to measure.
#ifndef FLUENTUI_ENABLE_TRACE
#  ifdef _DEBUG
#    define FLUENTUI_ENABLE_TRACE 1
#  else
#    define FLUENTUI_ENABLE_TRACE 0
#  endif
#endif

// Emit a trace line to the debugger output (VS Output window). Use a unified
// "[FluentUI.Module]" prefix so traces are easy to filter; print HRESULT on the
// critical DComp / surface paths so screenshots pinpoint failures.
//
// When tracing is disabled these are empty inline functions: the call sites stay
// valid and type-checked, and the compiler eliminates the call entirely. Callers
// that build a message string FIRST must use FL_TRACEF instead — an empty
// function body cannot remove the caller's own _snprintf_s.
inline void Trace(const char* tag, const char* msg, HRESULT hr) {
#if FLUENTUI_ENABLE_TRACE
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[FluentUI.%s] %s hr=0x%08X\n",
                tag, msg, static_cast<unsigned>(hr));
    OutputDebugStringA(buf);
#else
    (void)tag; (void)msg; (void)hr;
#endif
}

inline void TraceMsg(const char* tag, const char* msg) {
#if FLUENTUI_ENABLE_TRACE
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[FluentUI.%s] %s\n", tag, msg);
    OutputDebugStringA(buf);
#else
    (void)tag; (void)msg;
#endif
}

// printf-style trace. Declared unconditionally so FL_TRACEF still type-checks
// its arguments when tracing is compiled out.
inline void TraceFmt(const char* tag, _Printf_format_string_ const char* fmt, ...) {
#if FLUENTUI_ENABLE_TRACE
    char msg[240];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
    va_end(args);
    TraceMsg(tag, msg);
#else
    (void)tag; (void)fmt;
#endif
}

} // namespace fluent

// Formatted trace that vanishes completely when tracing is off — including the
// argument expressions, so a trace may read state that is expensive to compute.
// Use this instead of formatting into a local buffer and calling TraceMsg.
//
// `if constexpr (false)` rather than dropping the tokens: the discarded branch
// must still be well-formed, so a Release-only build cannot silently rot into
// referencing a variable that no longer exists.
#if FLUENTUI_ENABLE_TRACE
#  define FL_TRACEF(...) ::fluent::TraceFmt(__VA_ARGS__)
#else
#  define FL_TRACEF(...)                                                \
    do {                                                                \
        if constexpr (false) { ::fluent::TraceFmt(__VA_ARGS__); }        \
    } while (0)
#endif

// On failure: trace and return the HRESULT from the current function.
// `expr` is ALWAYS evaluated — it is the operation, not the diagnostic. Only the
// Trace call inside compiles away.
#define FL_RETURN_IF_FAILED(tag, expr)                                  \
    do {                                                                \
        HRESULT _hr = (expr);                                           \
        if (FAILED(_hr)) {                                              \
            ::fluent::Trace(tag, #expr " FAILED", _hr);                 \
            return _hr;                                                 \
        }                                                               \
    } while (0)
