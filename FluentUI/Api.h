// Api.h — public build and compatibility macros.
#pragma once

// FluentUI currently ships as a static library. Keep the annotation on public
// classes now so a future DLL build does not require rewriting the API surface.
#if defined(FLUENTUI_SHARED)
#  if defined(FLUENTUI_BUILDING_LIBRARY)
#    define FLUENTUI_API __declspec(dllexport)
#  else
#    define FLUENTUI_API __declspec(dllimport)
#  endif
#else
#  define FLUENTUI_API
#endif

#if defined(_MSC_VER)
#  define FLUENTUI_DEPRECATED(message) __declspec(deprecated(message))
#else
#  define FLUENTUI_DEPRECATED(message) [[deprecated(message)]]
#endif

// FLUENTUI_INTERNAL marks headers that are part of the implementation but must
// be installed because the static-library build exposes inline methods that
// reference them. These headers are NOT part of the supported public API —
// types, functions, and conventions in them may change between minor versions.
// The macro itself does nothing (no deprecation warning, no link error); it
// exists as a searchable marker for documentation and as a hook for a future
// "internals are off-limits" static-analysis pass.
//
// Why they exist at all: a static library with heavy use of inline and template
// code in headers must install those headers so the consumer's compiler can see
// the definitions. NativeWindowHost.h is the primary example — it is the internal
// host layer, not the application-facing authoring model, but Window.h derives
// from it and exposes its protected virtuals, so the full definition must be
// visible. The supported public types are Window and DialogWindow; users who
// directly subclass NativeWindowHost are opting into implementation details.
#define FLUENTUI_INTERNAL
