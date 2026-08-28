// MessageDialog.h — the one-line message box.
//
// This is the shortest path in the framework, for the case that does not deserve a
// class of its own:
//
//     if (MessageDialog::Show(*this, L"Delete file?", L"This cannot be undone.",
//                             DialogButtons::YesNo) == DialogResult::Primary) { ... }
//
// It is a thin wrapper over ContentDialog, which is itself the canned
// title/content/buttons template. Nothing here is extensible on purpose — the
// moment you want a checkbox, an input field or any custom layout, stop using this
// and derive from DialogWindow. See ContentDialog.h for where that boundary sits.
//
// Button-to-result mapping is fixed so call sites can compare against a stable
// value: the affirmative button is always DialogResult::Primary and the dismissive
// one is always DialogResult::Cancel, regardless of the words on them. That is why
// the YesNo case tests Primary rather than a hypothetical DialogResult::Yes — there
// is one affirmative concept, not one per label.
#pragma once

#include "ContentDialog.h"
#include <string>

namespace fluent {

enum class DialogButtons {
    Ok,         // one button:  [OK]                    -> Primary
    OkCancel,   // two buttons: [OK] [Cancel]           -> Primary / Cancel
    YesNo,      // two buttons: [Yes] [No]              -> Primary / Cancel
};

class FLUENTUI_API MessageDialog final : public ContentDialog {
public:
    // Build, show modally, and return the result. `owner` is disabled for the
    // duration, as with any modal DialogWindow.
    static DialogResult Show(NativeWindowHost& owner, std::wstring title,
                             std::wstring message,
                             DialogButtons buttons = DialogButtons::Ok);

    // Configure without showing — for tests, and for callers that want to tweak
    // the size before showing. Prefer the static Show for ordinary use.
    MessageDialog(std::wstring title, std::wstring message,
                  DialogButtons buttons = DialogButtons::Ok);

private:
    // Populate the ContentDialog content slot and button row from `buttons`. Split
    // out from the constructor so it is exercised by headless tests without a host.
    void BuildContent(std::wstring message, DialogButtons buttons);
};

} // namespace fluent
