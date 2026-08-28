// PasswordBox.h — Password input field with masked characters
#pragma once
#include "TextBox.h"

namespace fluent {

// PasswordBox is a TextBox with password masking always enabled.
// It's a thin wrapper that defaults to password mode and optionally
// adds a reveal button.
//
// Usage:
//   auto pwd = std::make_unique<PasswordBox>();
//   pwd->SetPlaceholder(L"Enter password");
//   pwd->SetMaxLength(128);

enum class PasswordRevealMode {
    Hidden,  // No reveal button
    Peek     // Show reveal button (eye icon) - feature pending
};

class PasswordBox : public TextBox {
public:
    PasswordBox();

    // Enable/disable the reveal button (placeholder feature)
    void SetPasswordRevealMode(PasswordRevealMode mode) { revealMode_ = mode; }
    PasswordRevealMode GetPasswordRevealMode() const { return revealMode_; }

private:
    PasswordRevealMode revealMode_ = PasswordRevealMode::Hidden;
};

} // namespace fluent
