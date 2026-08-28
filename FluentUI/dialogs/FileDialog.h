// FileDialog.h — thin wrappers over IFileOpenDialog / IFileSaveDialog.
//
// These are synchronous modal COM dialogs: ShowDialog() blocks until the user
// picks a file or cancels. The returned path is empty on cancel. No layout,
// no element tree, no composition — just a COM pointer and a few property sets.
//
// Usage:
//   OpenFileDialog dlg;
//   dlg.SetTitle(L"Open document");
//   dlg.AddFilter(L"Text files", L"*.txt");
//   dlg.AddFilter(L"All files", L"*.*");
//   auto path = dlg.ShowDialog(hwnd);
//   if (!path.empty()) { /* load path */ }
//
// Both dialogs share IFileDialog methods (SetTitle / SetDefaultExtension /
// SetFileName), so the shared base FileDialogBase holds those. Open adds
// multiselect support; Save adds overwrite-prompt and create-prompt flags.
#pragma once

#include "../fl_common.h"
#include <ShObjIdl.h>  // IFileOpenDialog, IFileSaveDialog
#include <string>
#include <vector>
#include <memory>

namespace fluent {

// Shared base for file-dialog wrappers. Not instantiable on its own.
class FileDialogBase {
protected:
    FileDialogBase() = default;
    virtual ~FileDialogBase() = default;

    // The COM pointer to the underlying dialog (IFileOpenDialog or IFileSaveDialog).
    // Subclasses assign this in their constructor.
    Microsoft::WRL::ComPtr<IFileDialog> dialog_;

public:
    // Set the dialog title (the caption bar text).
    void SetTitle(const std::wstring& title);

    // Set the default extension appended when the user types a name with no extension.
    // Pass the extension with no leading dot: "txt", not ".txt".
    void SetDefaultExtension(const std::wstring& ext);

    // Set the initial filename shown in the textbox (useful for Save dialogs).
    void SetFileName(const std::wstring& name);

    // Add a file-type filter: `name` is shown in the dropdown (e.g. "Text files"),
    // `spec` is the wildcard pattern (e.g. "*.txt" or "*.txt;*.log").
    void AddFilter(const std::wstring& name, const std::wstring& spec);

    // Show the dialog modally, blocking until the user picks or cancels.
    // Returns the selected file path, or empty on cancel. `owner` is the parent
    // HWND; pass nullptr for a desktop-modal dialog (not recommended).
    std::wstring ShowDialog(HWND owner);

protected:
    std::vector<COMDLG_FILTERSPEC> filters_;
    std::vector<std::wstring> filterNames_;
    std::vector<std::wstring> filterSpecs_;
};

// Open-file dialog: select one or more existing files.
class OpenFileDialog : public FileDialogBase {
public:
    OpenFileDialog();

    // Allow selecting multiple files. When true, ShowMulti() returns all paths;
    // ShowDialog() returns only the first (for single-file callers).
    void SetMultiSelect(bool multi);

    // Show and return all selected paths (empty vector on cancel).
    std::vector<std::wstring> ShowMulti(HWND owner);
};

// Save-file dialog: choose a location and name for a new file.
class SaveFileDialog : public FileDialogBase {
public:
    SaveFileDialog();

    // Warn when overwriting an existing file (default: true).
    void SetOverwritePrompt(bool prompt);

    // Prompt to create a folder if the chosen path's parent doesn't exist (default: false).
    void SetCreatePrompt(bool prompt);
};

} // namespace fluent
