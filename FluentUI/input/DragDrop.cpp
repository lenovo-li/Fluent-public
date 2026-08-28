#include "DragDrop.h"
#include <shellapi.h>

namespace fluent {

namespace {

class FileDropTarget final : public IFluentDropTarget {
public:
    explicit FileDropTarget(FilesDroppedHandler handler) : handler_(std::move(handler)) {}

    DragDropEffect OnDragEnter(const DragEventArgs& e) override {
        accepts_ = !GetDroppedFiles(e.data).empty();
        return accepts_ ? DragDropEffect::Copy : DragDropEffect::None;
    }
    DragDropEffect OnDragOver(const DragEventArgs&) override {
        return accepts_ ? DragDropEffect::Copy : DragDropEffect::None;
    }
    void OnDrop(const DragEventArgs& e) override {
        auto files = GetDroppedFiles(e.data);
        accepts_ = false;
        if (!files.empty() && handler_) handler_(std::move(files));
    }
    void OnDragLeave() override { accepts_ = false; }

private:
    FilesDroppedHandler handler_;
    bool accepts_ = false;
};

} // namespace

std::vector<std::wstring> GetDroppedFiles(IDataObject* data) {
    std::vector<std::wstring> files;
    if (!data) return files;

    FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = {};
    if (FAILED(data->GetData(&fmt, &stg))) return files;

    HDROP hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
    if (hDrop) {
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        files.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            DragQueryFileW(hDrop, i, path.data(), len + 1);
            files.push_back(std::move(path));
        }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return files;
}

std::wstring GetDroppedText(IDataObject* data) {
    if (!data) return {};

    FORMATETC fmt = { CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = {};
    if (FAILED(data->GetData(&fmt, &stg))) return {};

    wchar_t* text = static_cast<wchar_t*>(GlobalLock(stg.hGlobal));
    std::wstring result;
    if (text) {
        result = text;
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return result;
}

std::shared_ptr<IFluentDropTarget> MakeFileDropTarget(FilesDroppedHandler handler) {
    return std::make_shared<FileDropTarget>(std::move(handler));
}

} // namespace fluent
