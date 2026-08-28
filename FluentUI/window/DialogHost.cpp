#include "DialogHost.h"
#include <algorithm>

namespace fluent {

void DialogHost::CloseAll(DialogResult result) {
    for (const auto& dialog : modeless_)
        if (dialog) dialog->Close(result);
    modeless_.clear();
}

void DialogHost::PruneClosed() {
    modeless_.erase(
        std::remove_if(modeless_.begin(), modeless_.end(),
                       [](const auto& dialog) {
                           return !dialog || !dialog->IsDialogOpen();
                       }),
        modeless_.end());
}

size_t DialogHost::ModelessCount() const {
    return static_cast<size_t>(std::count_if(
        modeless_.begin(), modeless_.end(),
        [](const auto& dialog) { return dialog && dialog->IsDialogOpen(); }));
}

} // namespace fluent
