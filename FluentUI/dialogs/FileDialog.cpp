#include "FileDialog.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace fluent {

//
// FileDialogBase — shared methods
//

void FileDialogBase::SetTitle(const std::wstring& title) {
    if (dialog_) dialog_->SetTitle(title.c_str());
}

void FileDialogBase::SetDefaultExtension(const std::wstring& ext) {
    if (dialog_) dialog_->SetDefaultExtension(ext.c_str());
}

void FileDialogBase::SetFileName(const std::wstring& name) {
    if (dialog_) dialog_->SetFileName(name.c_str());
}

void FileDialogBase::AddFilter(const std::wstring& name, const std::wstring& spec) {
    // Keep strings alive until Show (COMDLG_FILTERSPEC holds raw pointers).
    filterNames_.push_back(name);
    filterSpecs_.push_back(spec);
    filters_.push_back({ filterNames_.back().c_str(), filterSpecs_.back().c_str() });
}

std::wstring FileDialogBase::ShowDialog(HWND owner) {
    if (!dialog_) return {};

    // Apply filters if any were added.
    if (!filters_.empty()) {
        dialog_->SetFileTypes(static_cast<UINT>(filters_.size()), filters_.data());
    }

    // Show modal. HRESULT_FROM_WIN32(ERROR_CANCELLED) means user cancelled.
    HRESULT hr = dialog_->Show(owner);
    if (FAILED(hr)) return {};

    // Retrieve the selected file path.
    ComPtr<IShellItem> item;
    if (FAILED(dialog_->GetResult(&item))) return {};

    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};

    std::wstring result = path;
    CoTaskMemFree(path);
    return result;
}

//
// OpenFileDialog
//

OpenFileDialog::OpenFileDialog() {
    CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&dialog_));
}

void OpenFileDialog::SetMultiSelect(bool multi) {
    if (!dialog_) return;
    DWORD flags = 0;
    dialog_->GetOptions(&flags);
    if (multi)
        flags |= FOS_ALLOWMULTISELECT;
    else
        flags &= ~FOS_ALLOWMULTISELECT;
    dialog_->SetOptions(flags);
}

std::vector<std::wstring> OpenFileDialog::ShowMulti(HWND owner) {
    if (!dialog_) return {};

    if (!filters_.empty()) {
        dialog_->SetFileTypes(static_cast<UINT>(filters_.size()), filters_.data());
    }

    HRESULT hr = dialog_->Show(owner);
    if (FAILED(hr)) return {};

    // For multi-select, query IFileOpenDialog::GetResults instead of GetResult.
    ComPtr<IFileOpenDialog> openDlg;
    if (FAILED(dialog_.As(&openDlg))) return {};

    ComPtr<IShellItemArray> items;
    if (FAILED(openDlg->GetResults(&items))) return {};

    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return {};

    std::vector<std::wstring> paths;
    paths.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(items->GetItemAt(i, &item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                paths.push_back(path);
                CoTaskMemFree(path);
            }
        }
    }
    return paths;
}

//
// SaveFileDialog
//

SaveFileDialog::SaveFileDialog() {
    CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&dialog_));
}

void SaveFileDialog::SetOverwritePrompt(bool prompt) {
    if (!dialog_) return;
    DWORD flags = 0;
    dialog_->GetOptions(&flags);
    if (prompt)
        flags |= FOS_OVERWRITEPROMPT;
    else
        flags &= ~FOS_OVERWRITEPROMPT;
    dialog_->SetOptions(flags);
}

void SaveFileDialog::SetCreatePrompt(bool prompt) {
    if (!dialog_) return;
    DWORD flags = 0;
    dialog_->GetOptions(&flags);
    if (prompt)
        flags |= FOS_CREATEPROMPT;
    else
        flags &= ~FOS_CREATEPROMPT;
    dialog_->SetOptions(flags);
}

} // namespace fluent
