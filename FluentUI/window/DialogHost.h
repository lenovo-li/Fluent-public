// DialogHost.h — lifetime and convenience manager for dialogs.
#pragma once

#include "DialogWindow.h"
#include <memory>
#include <utility>
#include <vector>

namespace fluent {

class DialogHost {
public:
    DialogHost() = default;
    DialogHost(const DialogHost&) = delete;
    DialogHost& operator=(const DialogHost&) = delete;

    template <typename T, typename... Args>
    std::shared_ptr<T> ShowModeless(NativeWindowHost& owner, Args&&... args) {
        PruneClosed();
        auto dialog = std::make_shared<T>(std::forward<Args>(args)...);
        if (FAILED(dialog->Show(owner)))
            return {};
        modeless_.push_back(dialog);
        return dialog;
    }

    void CloseAll(DialogResult result = DialogResult::Cancel);
    void PruneClosed();
    size_t ModelessCount() const;

private:
    std::vector<std::shared_ptr<DialogWindow>> modeless_;
};

} // namespace fluent
