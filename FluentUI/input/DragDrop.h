// DragDrop.h — file/text drag-and-drop infrastructure.
//
// Provides two things:
//
// 1. Pure data helpers (GetDroppedFiles / GetDroppedText): extract files or text
//    from an IDataObject on a drop. These are free functions with no element
//    dependency and are the testable part of this module.
//
// 2. IFluentDropTarget: the interface a UIElement implements to receive drops.
//    NativeWindowHost registers a COM IDropTarget shim with the OS that resolves
//    the element under the cursor via hit-test and routes events through this
//    interface.
//
// NativeWindowHost wires everything up on Create and tears it down on Close.
// UIElement::SetDropTarget() installs the handler; UIElement::DropTarget()
// retrieves it (used by NativeWindowHost for routing).
#pragma once

#include "../fl_common.h"
#include "../input/InputTypes.h"  // Point
#include <oleidl.h>               // IDataObject
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace fluent {

// --- Data helpers (pure functions) ----------------------------------------

// Extract file paths from a CF_HDROP in `data`. Returns an empty vector when
// no CF_HDROP is present or the pointer is null.
std::vector<std::wstring> GetDroppedFiles(IDataObject* data);

// Extract CF_UNICODETEXT from `data`, or empty when absent / null.
std::wstring GetDroppedText(IDataObject* data);

// --- Drop-target interface -------------------------------------------------

enum class DragDropEffect {
    None = DROPEFFECT_NONE,
    Copy = DROPEFFECT_COPY,
    Move = DROPEFFECT_MOVE,
    Link = DROPEFFECT_LINK,
};

struct DragEventArgs {
    Point pos;                 // mouse position in window DIPs
    DragDropEffect effect;     // proposed effect
    IDataObject* data;         // COM data (non-owning)
};

// A UIElement implements this interface to receive drag events. Obtain one
// through an anonymous subclass or through the provided TextAreaDropTarget
// helper. Install it with UIElement::SetDropTarget(). The NativeWindowHost hit-
// tests the element tree on each event and calls the target on the deepest
// element that has one.
class IFluentDropTarget {
public:
    virtual ~IFluentDropTarget() = default;
    virtual DragDropEffect OnDragEnter(const DragEventArgs& e) = 0;
    virtual DragDropEffect OnDragOver(const DragEventArgs& e) = 0;
    virtual void OnDrop(const DragEventArgs& e) = 0;
    virtual void OnDragLeave() = 0;
};

// Common application case: accept file drops and invoke one callback. This keeps
// the full IFluentDropTarget contract available for custom hover/effect behavior
// without forcing every file-picking UI to implement four boilerplate methods.
using FilesDroppedHandler = std::function<void(std::vector<std::wstring>)>;
std::shared_ptr<IFluentDropTarget> MakeFileDropTarget(FilesDroppedHandler handler);

} // namespace fluent
