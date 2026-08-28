// InputHost.h — input routing, focus management, and click detection.
//
// Encapsulates the input subsystem that NativeWindowHost used to hold directly:
// InputManager (pointer routing, hover, capture), FocusManager (keyboard focus
// and blink timer), and ClickCounter (double/triple click detection).
#pragma once

#include "../fl_common.h"
#include "../input/InputManager.h"
#include "../input/FocusManager.h"
#include "../input/ClickCounter.h"
#include "../core/Subscription.h"
#include <objidl.h>
#include <wrl/client.h>
#include <vector>

namespace fluent {

class UIElement;
class UIContext;

// InputHost aggregates input-related state and provides setup/teardown helpers.
// The host itself is passive — the NativeWindow's WndProc calls the public
// methods below, and the host delegates to InputManager/FocusManager.
class InputHost {
public:
    InputHost() = default;
    ~InputHost() = default;

    // Non-copyable, non-movable (InputManager stores raw pointers to itself)
    InputHost(const InputHost&) = delete;
    InputHost& operator=(const InputHost&) = delete;
    InputHost(InputHost&&) = delete;
    InputHost& operator=(InputHost&&) = delete;

    // Initialize the input subsystem: wire InputManager ↔ FocusManager,
    // register context pointers and roots. Must be called before any input is routed.
    // The caller (NativeWindowHost) still owns the FocusChanged subscription because
    // it binds to UpdateBlinkTimer, a NativeWindowHost method.
    void Initialize(HWND hwnd, UIContext* context,
                    std::vector<UIElement*>* roots,
                    void (*invalidateThunk)(void*), void* owner);

    // Accessors for the three subsystems
    InputManager& Input() { return input_; }
    FocusManager& Focus() { return focus_; }
    ClickCounter& Clicks() { return clicks_; }

    const InputManager& Input() const { return input_; }
    const FocusManager& Focus() const { return focus_; }
    const ClickCounter& Clicks() const { return clicks_; }

    // IDropTarget COM state (used by NativeWindowHost's drag-drop implementation).
    // Stored here because it is input-related state (HWND-scoped drag session).
    Microsoft::WRL::ComPtr<IDataObject>& DragData() { return dragData_; }

private:
    InputManager input_;
    FocusManager focus_;
    ClickCounter clicks_;
    Microsoft::WRL::ComPtr<IDataObject> dragData_;
};

}  // namespace fluent
