#include "MessageDialog.h"
#include "TextBlock.h"

namespace fluent {

MessageDialog::MessageDialog(std::wstring title, std::wstring message,
                             DialogButtons buttons) {
    SetTitle(std::move(title));

    // A tighter title bar than the generic 48 DIP a dialog inherits. A message
    // box's title is two or three words ("Confirm", "Error") while the MESSAGE is
    // what the user has to read, so the taller bar put the visual weight in the
    // wrong place and stopped reading as a message box at all.
    SetTitleBarHeight(32.0f);

    // The height is derived from ContentDialog's fixed layout rather than guessed,
    // because guessing is what clipped the button row: the old 190/220 DIP values
    // predated the title block and left the actions partly below the client area.
    // That layout is title(30) + content + spacing(12 x 2) + buttons(36), so the
    // client height is the content slot plus 90 DIP of chrome.
    //
    // The width stays fixed at 360: "you do not have to think about the size" is
    // the entire reason to reach for MessageDialog, and a caller who does want to
    // choose should be using ContentDialog directly.
    const bool multiline = message.find(L'\n') != std::wstring::npos || message.size() > 72;
    // 120 DIP fits a wrapped one-liner with breathing room; 180 DIP holds roughly
    // six lines at the body line height, which covers the long-path case that
    // motivated the multiline branch in the first place.
    const float contentSlotHeight = multiline ? 180.0f : 120.0f;
    SetClientSize(360.0f, 30.0f + contentSlotHeight + 24.0f + 36.0f);
    BuildContent(std::move(message), buttons);
}

void MessageDialog::BuildContent(std::wstring message, DialogButtons buttons) {
    auto text = std::make_unique<TextBlock>();
    text->SetText(std::move(message));
    text->SetWrap(true);
    SetContent(std::move(text));

    // The affirmative button is always Primary and the dismissive always Cancel —
    // see the header on why the label does not get its own DialogResult.
    switch (buttons) {
        case DialogButtons::Ok:
            AddButton(L"OK", DialogResult::Primary);
            break;
        case DialogButtons::OkCancel:
            AddButton(L"OK", DialogResult::Primary);
            AddButton(L"Cancel", DialogResult::Cancel);
            break;
        case DialogButtons::YesNo:
            AddButton(L"Yes", DialogResult::Primary);
            AddButton(L"No", DialogResult::Cancel);
            break;
    }
    SetDefaultResult(DialogResult::Primary);
}

DialogResult MessageDialog::Show(NativeWindowHost& owner, std::wstring title,
                                 std::wstring message, DialogButtons buttons) {
    MessageDialog dialog(std::move(title), std::move(message), buttons);
    return dialog.ShowDialog(owner);
}

} // namespace fluent
