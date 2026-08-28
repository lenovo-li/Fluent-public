// DragDropTests.cpp — headless tests for drag-and-drop data helpers.
//
// GetDroppedFiles and GetDroppedText are pure functions over an IDataObject —
// no window, no element tree. The IDataObject is faked inline using a minimal
// implementation that serves exactly the format the function under test requests.

#include "../framework/Test.h"
#include "../../FluentUI/input/DragDrop.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include <ShlObj.h>
#include <cstring>

using namespace fluent;

namespace {

// Minimal IDataObject implementation. Supports CF_HDROP and CF_UNICODETEXT.
// Not thread-safe; only used on the test thread.
class FakeDataObject : public IDataObject {
public:
    std::vector<std::wstring> files;
    std::wstring text;
    ULONG ref_ = 1;

    // --- IUnknown ---
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)()  override { return ++ref_; }
    STDMETHOD_(ULONG, Release)() override {
        if (--ref_ == 0) { delete this; return 0; }
        return ref_;
    }

    // --- IDataObject ---
    STDMETHOD(GetData)(FORMATETC* fmt, STGMEDIUM* stg) override {
        if (!fmt || !stg) return E_POINTER;
        ZeroMemory(stg, sizeof(*stg));

        if (fmt->cfFormat == CF_HDROP && !files.empty()) {
            // Calculate the DROPFILES blob size.
            size_t totalChars = 0;
            for (auto& f : files) totalChars += f.size() + 1;
            totalChars += 1; // double-null terminator

            size_t blobSize = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, blobSize);
            if (!hg) return E_OUTOFMEMORY;

            auto* df = static_cast<DROPFILES*>(GlobalLock(hg));
            df->pFiles = sizeof(DROPFILES);
            df->fWide  = TRUE;
            wchar_t* wp = reinterpret_cast<wchar_t*>(df + 1);
            for (auto& f : files) {
                for (wchar_t c : f) *wp++ = c;
                *wp++ = L'\0';
            }
            *wp = L'\0'; // extra terminator
            GlobalUnlock(hg);

            stg->tymed    = TYMED_HGLOBAL;
            stg->hGlobal  = hg;
            return S_OK;
        }

        if (fmt->cfFormat == CF_UNICODETEXT && !text.empty()) {
            size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!hg) return E_OUTOFMEMORY;
            wchar_t* wp = static_cast<wchar_t*>(GlobalLock(hg));
            std::memcpy(wp, text.c_str(), bytes);
            GlobalUnlock(hg);
            stg->tymed   = TYMED_HGLOBAL;
            stg->hGlobal = hg;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    STDMETHOD(GetDataHere)(FORMATETC*, STGMEDIUM*)                      override { return E_NOTIMPL; }
    STDMETHOD(QueryGetData)(FORMATETC*)                                  override { return E_NOTIMPL; }
    STDMETHOD(GetCanonicalFormatEtc)(FORMATETC*, FORMATETC*)            override { return E_NOTIMPL; }
    STDMETHOD(SetData)(FORMATETC*, STGMEDIUM*, BOOL)                    override { return E_NOTIMPL; }
    STDMETHOD(EnumFormatEtc)(DWORD, IEnumFORMATETC**)                   override { return E_NOTIMPL; }
    STDMETHOD(DAdvise)(FORMATETC*, DWORD, IAdviseSink*, DWORD*)         override { return E_NOTIMPL; }
    STDMETHOD(DUnadvise)(DWORD)                                          override { return E_NOTIMPL; }
    STDMETHOD(EnumDAdvise)(IEnumSTATDATA**)                              override { return E_NOTIMPL; }
};

} // namespace

// --- GetDroppedFiles -------------------------------------------------------

TEST(DragDrop, GetDroppedFiles_NullData_ReturnsEmpty) {
    auto files = GetDroppedFiles(nullptr);
    EXPECT_EQ(static_cast<int>(files.size()), 0);
}

TEST(DragDrop, GetDroppedFiles_SingleFile_ReturnsOnePath) {
    auto* fake = new FakeDataObject;
    fake->files = { L"C:\\folder\\file.txt" };
    auto files = GetDroppedFiles(fake);
    fake->Release();
    EXPECT_EQ(static_cast<int>(files.size()), 1);
    EXPECT_TRUE(files[0] == L"C:\\folder\\file.txt");
}

TEST(DragDrop, GetDroppedFiles_ThreeFiles_ReturnsAllPaths) {
    auto* fake = new FakeDataObject;
    fake->files = { L"C:\\a.txt", L"D:\\b.png", L"E:\\c.log" };
    auto files = GetDroppedFiles(fake);
    fake->Release();
    EXPECT_EQ(static_cast<int>(files.size()), 3);
    EXPECT_TRUE(files[0] == L"C:\\a.txt");
    EXPECT_TRUE(files[1] == L"D:\\b.png");
    EXPECT_TRUE(files[2] == L"E:\\c.log");
}

TEST(DragDrop, GetDroppedFiles_NoHDrop_ReturnsEmpty) {
    // FakeDataObject with only text set, no files.
    auto* fake = new FakeDataObject;
    fake->text = L"some text";
    auto files = GetDroppedFiles(fake);
    fake->Release();
    EXPECT_EQ(static_cast<int>(files.size()), 0);
}

// --- GetDroppedText --------------------------------------------------------

TEST(DragDrop, GetDroppedText_NullData_ReturnsEmpty) {
    auto text = GetDroppedText(nullptr);
    EXPECT_TRUE(text.empty());
}

TEST(DragDrop, GetDroppedText_ValidText_ReturnsString) {
    auto* fake = new FakeDataObject;
    fake->text = L"Hello, World!";
    auto text = GetDroppedText(fake);
    fake->Release();
    EXPECT_TRUE(text == L"Hello, World!");
}

TEST(DragDrop, GetDroppedText_Multiline_PreservesNewlines) {
    auto* fake = new FakeDataObject;
    fake->text = L"line1\r\nline2";
    auto text = GetDroppedText(fake);
    fake->Release();
    EXPECT_TRUE(text == L"line1\r\nline2");
}

TEST(DragDrop, GetDroppedText_NoText_ReturnsEmpty) {
    // FakeDataObject with only files set, no text.
    auto* fake = new FakeDataObject;
    fake->files = { L"C:\\file.txt" };
    auto text = GetDroppedText(fake);
    fake->Release();
    EXPECT_TRUE(text.empty());
}

// --- DragDropEffect values match Win32 ------------------------------------

TEST(DragDrop, EffectEnumMatchesDropEffect) {
    EXPECT_EQ(static_cast<DWORD>(DragDropEffect::None), static_cast<DWORD>(DROPEFFECT_NONE));
    EXPECT_EQ(static_cast<DWORD>(DragDropEffect::Copy), static_cast<DWORD>(DROPEFFECT_COPY));
    EXPECT_EQ(static_cast<DWORD>(DragDropEffect::Move), static_cast<DWORD>(DROPEFFECT_MOVE));
    EXPECT_EQ(static_cast<DWORD>(DragDropEffect::Link), static_cast<DWORD>(DROPEFFECT_LINK));
}

// --- Routing: which element receives the drop ------------------------------
//
// The data helpers above say nothing about whether a drop reaches the right
// control. That is HitTestDropTarget's job, and it is the part with a real
// decision in it: an element only qualifies when it has a handler installed,
// so a decorative Border sitting on top of a TextArea must NOT swallow the
// drop. These tests drive that function directly (no window, no OLE).

namespace {

// Records what it was told, so a test can assert the routing actually landed.
class RecordingDropTarget : public IFluentDropTarget {
public:
    int enters = 0, overs = 0, leaves = 0, drops = 0;
    Point lastPos{};
    DragDropEffect lastEffect = DragDropEffect::None;

    DragDropEffect OnDragEnter(const DragEventArgs& e) override {
        ++enters; lastPos = e.pos; lastEffect = e.effect;
        return DragDropEffect::Copy;
    }
    DragDropEffect OnDragOver(const DragEventArgs& e) override {
        ++overs; lastPos = e.pos; lastEffect = e.effect;
        return DragDropEffect::Copy;
    }
    void OnDrop(const DragEventArgs& e) override {
        ++drops; lastPos = e.pos; lastEffect = e.effect;
    }
    void OnDragLeave() override { ++leaves; }
};

} // namespace

TEST(DragDropRouting, ElementWithoutHandlerIsNotATarget) {
    Button btn;
    btn.Arrange(RectDip{0, 0, 100, 40});
    EXPECT_FALSE(btn.AcceptsDrop());
    EXPECT_TRUE(btn.HitTestDropTarget(50.0f, 20.0f) == nullptr);
}

TEST(DragDropRouting, ElementWithHandlerIsHitInsideItsBounds) {
    Button btn;
    btn.Arrange(RectDip{0, 0, 100, 40});
    btn.SetDropTarget(std::make_shared<RecordingDropTarget>());
    EXPECT_TRUE(btn.AcceptsDrop());
    EXPECT_TRUE(btn.HitTestDropTarget(50.0f, 20.0f) == &btn);
}

TEST(DragDropRouting, PointOutsideBoundsMisses) {
    Button btn;
    btn.Arrange(RectDip{0, 0, 100, 40});
    btn.SetDropTarget(std::make_shared<RecordingDropTarget>());
    EXPECT_TRUE(btn.HitTestDropTarget(150.0f, 20.0f) == nullptr);
    EXPECT_TRUE(btn.HitTestDropTarget(50.0f, 90.0f) == nullptr);
}

TEST(DragDropRouting, ClearingTheHandlerStopsAcceptingDrops) {
    Button btn;
    btn.Arrange(RectDip{0, 0, 100, 40});
    btn.SetDropTarget(std::make_shared<RecordingDropTarget>());
    EXPECT_TRUE(btn.AcceptsDrop());
    btn.SetDropTarget(nullptr);
    EXPECT_FALSE(btn.AcceptsDrop());
    EXPECT_TRUE(btn.HitTestDropTarget(50.0f, 20.0f) == nullptr);
}

TEST(DragDropRouting, DisabledElementDoesNotAcceptDrops) {
    Button btn;
    btn.Arrange(RectDip{0, 0, 100, 40});
    btn.SetDropTarget(std::make_shared<RecordingDropTarget>());
    btn.SetEnabled(false);
    EXPECT_FALSE(btn.AcceptsDrop());
    EXPECT_TRUE(btn.HitTestDropTarget(50.0f, 20.0f) == nullptr);
}

TEST(DragDropRouting, InvisibleElementDoesNotAcceptDrops) {
    Button btn;
    btn.Arrange(RectDip{0, 0, 100, 40});
    btn.SetDropTarget(std::make_shared<RecordingDropTarget>());
    btn.SetVisible(false);
    EXPECT_TRUE(btn.HitTestDropTarget(50.0f, 20.0f) == nullptr);
}

TEST(DragDropRouting, PanelReturnsTheChildThatHasTheHandler) {
    StackPanel panel;
    auto child = std::make_unique<Button>();
    Button* childPtr = child.get();
    childPtr->SetDropTarget(std::make_shared<RecordingDropTarget>());
    panel.Add(std::move(child));

    panel.Arrange(RectDip{0, 0, 200, 100});
    childPtr->Arrange(RectDip{0, 0, 50, 30});

    // Inside the child -> the child, not the panel.
    EXPECT_TRUE(panel.HitTestDropTarget(25.0f, 15.0f) == childPtr);
    // Inside the panel but outside the child, and the panel has no handler.
    EXPECT_TRUE(panel.HitTestDropTarget(150.0f, 80.0f) == nullptr);
}

TEST(DragDropRouting, PanelItselfCanBeTheTarget) {
    // A whole-panel drop zone is legitimate; the panel is checked after its
    // children so a child handler still wins.
    StackPanel panel;
    panel.SetDropTarget(std::make_shared<RecordingDropTarget>());
    panel.Arrange(RectDip{0, 0, 200, 100});
    EXPECT_TRUE(panel.HitTestDropTarget(150.0f, 80.0f) == &panel);
}

TEST(DragDropRouting, ChildHandlerWinsOverPanelHandler) {
    // Both have handlers: the deepest must win, or a container swallows every
    // drop meant for the control inside it.
    StackPanel panel;
    panel.SetDropTarget(std::make_shared<RecordingDropTarget>());

    auto child = std::make_unique<Button>();
    Button* childPtr = child.get();
    childPtr->SetDropTarget(std::make_shared<RecordingDropTarget>());
    panel.Add(std::move(child));

    panel.Arrange(RectDip{0, 0, 200, 100});
    childPtr->Arrange(RectDip{0, 0, 50, 30});

    EXPECT_TRUE(panel.HitTestDropTarget(25.0f, 15.0f) == childPtr);
}

TEST(DragDropRouting, ChildWithoutHandlerFallsThroughToThePanel) {
    // The Border-over-TextArea case, inverted: a child with no handler must not
    // block the panel that does have one.
    StackPanel panel;
    panel.SetDropTarget(std::make_shared<RecordingDropTarget>());

    auto child = std::make_unique<Button>();   // no handler
    Button* childPtr = child.get();
    panel.Add(std::move(child));

    panel.Arrange(RectDip{0, 0, 200, 100});
    childPtr->Arrange(RectDip{0, 0, 50, 30});

    EXPECT_TRUE(panel.HitTestDropTarget(25.0f, 15.0f) == &panel);
}

TEST(DragDropRouting, HandlerReceivesThePositionAndPayload) {
    // The interface is what the window calls; verify the args survive the trip.
    RecordingDropTarget rec;
    auto* fake = new FakeDataObject;
    fake->files = { L"C:\\dropped.txt" };

    DragEventArgs args{Point{12.5f, 34.0f}, DragDropEffect::Copy, fake};
    rec.OnDrop(args);

    EXPECT_EQ(rec.drops, 1);
    EXPECT_NEAR(rec.lastPos.x, 12.5f, 0.01);
    EXPECT_NEAR(rec.lastPos.y, 34.0f, 0.01);
    EXPECT_TRUE(rec.lastEffect == DragDropEffect::Copy);

    // And the payload the handler was given still parses.
    auto files = GetDroppedFiles(args.data);
    EXPECT_EQ(static_cast<int>(files.size()), 1);
    EXPECT_TRUE(files[0] == L"C:\\dropped.txt");
    fake->Release();
}

TEST(DragDropRouting, FileDropHelperAcceptsFilesAndInvokesCallback) {
    std::vector<std::wstring> received;
    auto target = MakeFileDropTarget([&](std::vector<std::wstring> files) {
        received = std::move(files);
    });
    auto* fake = new FakeDataObject;
    fake->files = {L"C:\\firmware.bin"};
    DragEventArgs args{Point{}, DragDropEffect::Copy, fake};

    EXPECT_TRUE(target->OnDragEnter(args) == DragDropEffect::Copy);
    EXPECT_TRUE(target->OnDragOver(args) == DragDropEffect::Copy);
    target->OnDrop(args);
    EXPECT_EQ(received.size(), static_cast<size_t>(1));
    EXPECT_EQ(received[0], std::wstring(L"C:\\firmware.bin"));
    fake->Release();
}
