// DWriteContext.cpp

#include "DWriteContext.h"

#pragma comment(lib, "dwrite.lib")

namespace fluent {

namespace {
const char* kTag = "DWriteContext";

// True if the given font family is installed in the system font collection.
bool FamilyExists(IDWriteFactory* factory, const wchar_t* name) {
    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(factory->GetSystemFontCollection(coll.GetAddressOf(), FALSE)))
        return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(name, &index, &exists)))
        return false;
    return exists != FALSE;
}
} // namespace

HRESULT DWriteContext::Initialize() {
    FL_RETURN_IF_FAILED(
        kTag, DWriteCreateFactory(
                  DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                  reinterpret_cast<IUnknown**>(factory_.ReleaseAndGetAddressOf())));

    if (FamilyExists(factory_.Get(), L"Segoe UI Variable Text"))
        family_ = L"Segoe UI Variable Text";
    else if (FamilyExists(factory_.Get(), L"Segoe UI Variable"))
        family_ = L"Segoe UI Variable";
    else
        family_ = L"Segoe UI";

    TraceMsg(kTag, "initialized");
    return S_OK;
}

unsigned long long DWriteContext::PackKey(const FormatKey& k) {
    // Size quantized to 1/4 DIP (0..~16k) in the low 32 bits; the four enum
    // attributes packed into the high bits. Each enum fits comfortably in a byte.
    unsigned long long size = static_cast<unsigned long long>(k.sizeDip * 4.0f);
    return (size & 0xFFFFFFFFull) |
           ((static_cast<unsigned long long>(k.weight) & 0xFFFF) << 32) |
           ((static_cast<unsigned long long>(k.textAlign) & 0xF) << 48) |
           ((static_cast<unsigned long long>(k.paraAlign) & 0xF) << 52) |
           ((static_cast<unsigned long long>(k.wrapping) & 0xF) << 56);
}

IDWriteTextFormat* DWriteContext::Format(float sizeDip, DWRITE_FONT_WEIGHT weight,
                                         DWRITE_TEXT_ALIGNMENT textAlign,
                                         DWRITE_PARAGRAPH_ALIGNMENT paraAlign,
                                         DWRITE_WORD_WRAPPING wrapping) {
    FormatKey fk{sizeDip, weight, textAlign, paraAlign, wrapping};
    unsigned long long key = PackKey(fk);

    std::lock_guard<std::mutex> lock(formatCacheMutex_);

    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second.Get();

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = factory_->CreateTextFormat(
        family_.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"", fmt.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateTextFormat failed", hr);
        return nullptr;
    }
    // Bake the full layout key into the object; it is logically immutable after
    // this, so callers never mutate a shared format again.
    fmt->SetTextAlignment(textAlign);
    fmt->SetParagraphAlignment(paraAlign);
    fmt->SetWordWrapping(wrapping);

    auto* raw = fmt.Get();
    cache_.emplace(key, std::move(fmt));
    return raw;
}

} // namespace fluent
