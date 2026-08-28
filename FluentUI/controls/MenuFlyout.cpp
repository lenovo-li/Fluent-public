// MenuFlyout.cpp

#include "MenuFlyout.h"
#include "../window/WindowServices.h"
#include "../services/PopupHost.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/ResourceCache.h"
#include <algorithm>

namespace fluent {

namespace {
const char* kTag = "MenuFlyout";

constexpr float kItemH = 32.0f;       // DIP, height of a normal item row
constexpr float kSepH = 9.0f;         // DIP, height of a separator row
constexpr float kMinWidth = 160.0f;   // DIP
constexpr float kMaxWidth = 360.0f;   // DIP
constexpr float kTextInsetL = 34.0f;  // DIP, left inset (leaves room for check)
constexpr float kTextInsetR = 32.0f;  // DIP, right inset (accelerator / chevron)
constexpr float kCheckColW = 34.0f;   // DIP, width of the leading check column
constexpr float kFontSize = 13.0f;    // DIP
constexpr float kAccelFontSize = 12.0f;

// Callbacks the list view uses to talk back to the flyout, keyed by level.
struct MenuListCallbacks {
    std::function<void(int)> onInvoke;    // leaf chosen (itemIndex)
    std::function<void(int)> onOpenSub;   // parent item entered (itemIndex)
    std::function<void(int)> onHover;     // hovered item changed (itemIndex or -1)
    std::function<void()> onCloseAll;     // Esc / dismiss
    std::function<void(bool)> onLeftRight; // Right(true)/Left(false) navigation
};

} // namespace

// ---------------------------------------------------------------------------
// MenuListView — the popup content element for one menu level.
// ---------------------------------------------------------------------------

class MenuListView : public Control {
public:
    MenuListView() { SetClickable(true); }
    void SetItems(const std::vector<MenuItem>* items) { items_ = items; Invalidate(); }
    void SetCallbacks(MenuListCallbacks cb) { cb_ = std::move(cb); }

    int ItemCount() const { return items_ ? static_cast<int>(items_->size()) : 0; }

    // The natural card size for all items (used by MenuFlyout::MeasureLevel).
    void ComputeDesired(float& outW, float& outH) const {
        float h = 0.0f, maxTextW = 0.0f;
        if (items_ && Dwrite()) {
            for (const MenuItem& it : *items_) {
                h += it.separator ? kSepH : kItemH;
                float tw = MeasureRowWidth(it);
                maxTextW = std::max(maxTextW, tw);
            }
        }
        outW = std::clamp(maxTextW, kMinWidth, kMaxWidth);
        outH = h;
    }

    // Keyboard navigation within this level (routed).
    void OnKeyDownRouted(KeyEventArgs& e) override {
        if (!items_ || items_->empty()) return;
        switch (e.vk) {
            case VK_UP:   MoveHover(-1); e.handled = true; break;
            case VK_DOWN: MoveHover(+1); e.handled = true; break;
            case VK_HOME: SetHoverTo(FirstSelectable()); e.handled = true; break;
            case VK_END:  SetHoverTo(LastSelectable()); e.handled = true; break;
            case VK_RIGHT:
                if (cb_.onLeftRight) cb_.onLeftRight(true);
                e.handled = true; break;
            case VK_LEFT:
                if (cb_.onLeftRight) cb_.onLeftRight(false);
                e.handled = true; break;
            case VK_RETURN:
            case VK_SPACE:
                ActivateHovered();
                e.handled = true; break;
            default:
                break;
        }
    }

    void SetHover(int idx) {
        if (idx == hovered_) return;
        hovered_ = idx;
        Invalidate();
    }
    int Hovered() const { return hovered_; }

    // DIP Y of the top of item `idx` within the popup window (includes the
    // 8-dip interior padding that PopupHost applies above the content).
    float ItemYOffsetDip(int idx) const {
        float y = bounds_.y;  // bounds_.y == kPadding == 8dip set by PopupHost
        if (!items_) return y;
        for (int i = 0; i < idx && i < ItemCount(); ++i)
            y += (*items_)[i].separator ? kSepH : kItemH;
        return y;
    }

    void Render(const DrawingContext& dc) override {
        if (!items_ || !Dwrite()) return;
        const ColorTokens& pal = Theme().colors;

        IDWriteTextFormat* fmt = Dwrite()->Format(
            kFontSize, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteTextFormat* accFmt = Dwrite()->Format(
            kAccelFontSize, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return;

        float y = bounds_.y;
        for (int i = 0; i < static_cast<int>(items_->size()); ++i) {
            const MenuItem& it = (*items_)[i];
            if (it.separator) {
                float sy = y + kSepH * 0.5f;
                dc.DrawLine(D2D1::Point2F(bounds_.x + 8.0f, sy),
                            D2D1::Point2F(bounds_.right() - 8.0f, sy), pal.controlStrokeDefault, 1.0f);
                y += kSepH;
                continue;
            }

            D2D1_RECT_F row = D2D1::RectF(bounds_.x + 4.0f, y + 2.0f,
                                          bounds_.right() - 4.0f, y + kItemH - 2.0f);

            // Hover highlight (only for enabled rows).
            if (i == hovered_ && it.enabled) {
                dc.FillRoundedRect(D2D1::RoundedRect(row, 4.0f, 4.0f),
                                   D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.15f));
            }

            const D2D1_COLOR_F itemColor = it.enabled ? pal.textPrimary : pal.textSecondary;
            ResourceCache* cache = Context().resourceCache;

            // Leading check glyph (static two-segment tick → cached geometry).
            if (it.checked) {
                float cx = bounds_.x + kCheckColW * 0.5f;
                float cy = y + kItemH * 0.5f;
                ComPtr<ID2D1PathGeometry> check;
                if (cache) {
                    check = cache->GetGeometry(GlyphId::CheckMark, 1.0f,
                        [](ID2D1GeometrySink* sink) {
                            // Unit space relative to the glyph center.
                            sink->BeginFigure(D2D1::Point2F(-4.0f, 0.5f), D2D1_FIGURE_BEGIN_HOLLOW);
                            sink->AddLine(D2D1::Point2F(-1.0f, 4.0f));
                            sink->AddLine(D2D1::Point2F(5.0f, -4.0f));
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        });
                }
                if (check) {
                    TransformGuard tg = dc.PushTransform(D2D1::Matrix3x2F::Translation(cx, cy));
                    dc.DrawGeometry(check.Get(), itemColor, 1.5f);
                } else {
                    dc.DrawLine(D2D1::Point2F(cx - 4.0f, cy + 0.5f),
                                D2D1::Point2F(cx - 1.0f, cy + 4.0f), itemColor, 1.5f);
                    dc.DrawLine(D2D1::Point2F(cx - 1.0f, cy + 4.0f),
                                D2D1::Point2F(cx + 5.0f, cy - 4.0f), itemColor, 1.5f);
                }
            }

            // Item text.
            D2D1_RECT_F textRect = D2D1::RectF(bounds_.x + kTextInsetL, y,
                                               bounds_.right() - kTextInsetR, y + kItemH);
            D2D1_COLOR_F textColor = EffectiveForeground(itemColor);
            dc.DrawText(it.text.c_str(), static_cast<UINT32>(it.text.size()), fmt,
                        textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

            // Right side: accelerator text, or a submenu chevron (static ">"
            // shape → cached geometry).
            if (it.HasSubmenu()) {
                float cx = bounds_.right() - 16.0f;
                float cy = y + kItemH * 0.5f;
                ComPtr<ID2D1PathGeometry> chevron;
                if (cache) {
                    chevron = cache->GetGeometry(GlyphId::ChevronRight, 1.0f,
                        [](ID2D1GeometrySink* sink) {
                            sink->BeginFigure(D2D1::Point2F(-2.0f, -4.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                            sink->AddLine(D2D1::Point2F(2.0f, 0.0f));
                            sink->AddLine(D2D1::Point2F(-2.0f, 4.0f));
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        });
                }
                if (chevron) {
                    TransformGuard tg = dc.PushTransform(D2D1::Matrix3x2F::Translation(cx, cy));
                    dc.DrawGeometry(chevron.Get(), itemColor, 1.5f);
                } else {
                    dc.DrawLine(D2D1::Point2F(cx - 2.0f, cy - 4.0f),
                                D2D1::Point2F(cx + 2.0f, cy), itemColor, 1.5f);
                    dc.DrawLine(D2D1::Point2F(cx + 2.0f, cy),
                                D2D1::Point2F(cx - 2.0f, cy + 4.0f), itemColor, 1.5f);
                }
            } else if (!it.accelerator.empty() && accFmt) {
                D2D1_RECT_F accRect = D2D1::RectF(bounds_.x + kTextInsetL, y,
                                                  bounds_.right() - 12.0f, y + kItemH);
                D2D1_COLOR_F accColor = EffectiveForeground(pal.textSecondary);
                dc.DrawText(it.accelerator.c_str(),
                            static_cast<UINT32>(it.accelerator.size()), accFmt,
                            accRect, accColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }

            y += kItemH;
        }
    }

protected:
    void OnPointerMoved(PointerEventArgs& e) override {
        int idx = HitItem(e.position.y);
        if (idx != hovered_) {
            hovered_ = idx;
            Invalidate();
            if (cb_.onHover) cb_.onHover(idx);
        }
    }

    void OnClickRouted(PointerEventArgs& e) override {
        int idx = HitItem(e.position.y);
        if (idx < 0) return;
        const MenuItem& it = (*items_)[idx];
        if (!it.Selectable()) return;
        if (it.HasSubmenu()) {
            if (cb_.onOpenSub) cb_.onOpenSub(idx);
        } else {
            if (cb_.onInvoke) cb_.onInvoke(idx);
        }
    }

private:
    float MeasureRowWidth(const MenuItem& it) const {
        if (it.separator || !Dwrite()) return kMinWidth;
        float w = kTextInsetL + kTextInsetR;
        w += MeasureText(it.text, kFontSize);
        if (!it.accelerator.empty())
            w += MeasureText(it.accelerator, kAccelFontSize) + 24.0f;
        return w;
    }

    // Through the shared, epoch-versioned layout cache rather than a fresh
    // CreateTextLayout per call. This is the heaviest of the throwaway-layout sites
    // in the library because it is O(items) PER MeasureLevel, not one per control: a
    // 20-item menu with accelerators built up to 40 IDWriteTextLayouts, read one
    // float off each, and dropped them all — and MeasureLevel runs again on every
    // submenu open and every reflow of an open menu. Menu strings are fixed at
    // build time, so after the first open every row is a hash lookup.
    //
    // maxWidth is the 100000 sentinel (a menu row never wraps; the row width is the
    // measurement being taken), which keeps the key stable across reflows — unlike
    // the label caches, this one does not miss when the offered width changes.
    float MeasureText(const std::wstring& s, float size) const {
        if (s.empty() || !Dwrite()) return 0.0f;
        constexpr float kMaxW = 100000.0f;

        if (ResourceCache* cache = Context().resourceCache) {
            TextLayoutKey key;
            key.text = s;
            key.fontSize = size;
            key.weight = DWRITE_FONT_WEIGHT_NORMAL;
            // Format(size)'s defaults — the key must name the layout it builds.
            key.textAlign = DWRITE_TEXT_ALIGNMENT_CENTER;
            key.paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
            key.wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
            key.maxWidth = kMaxW;
            key.maxHeight = kItemH;
            ComPtr<IDWriteTextLayout> cached = cache->GetTextLayout(std::move(key));
            if (!cached) return 0.0f;
            DWRITE_TEXT_METRICS cm{};
            if (FAILED(cached->GetMetrics(&cm))) return 0.0f;
            return cm.widthIncludingTrailingWhitespace;
        }

        // Cache-less context (headless tests): build directly, same inputs.
        IDWriteTextFormat* fmt = Dwrite()->Format(size);
        if (!fmt) return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(Dwrite()->Factory()->CreateTextLayout(
                s.c_str(), static_cast<UINT32>(s.size()), fmt,
                kMaxW, kItemH, layout.GetAddressOf())))
            return 0.0f;
        DWRITE_TEXT_METRICS m{};
        if (FAILED(layout->GetMetrics(&m))) return 0.0f;
        return m.widthIncludingTrailingWhitespace;
    }

    int HitItem(float dipY) const {
        if (!items_) return -1;
        float y = bounds_.y;
        for (int i = 0; i < static_cast<int>(items_->size()); ++i) {
            const MenuItem& it = (*items_)[i];
            float rh = it.separator ? kSepH : kItemH;
            if (dipY >= y && dipY < y + rh)
                return it.Selectable() ? i : -1;
            y += rh;
        }
        return -1;
    }

    int FirstSelectable() const {
        if (!items_) return -1;
        for (int i = 0; i < static_cast<int>(items_->size()); ++i)
            if ((*items_)[i].Selectable()) return i;
        return -1;
    }
    int LastSelectable() const {
        if (!items_) return -1;
        for (int i = static_cast<int>(items_->size()) - 1; i >= 0; --i)
            if ((*items_)[i].Selectable()) return i;
        return -1;
    }

    void MoveHover(int dir) {
        if (!items_ || items_->empty()) return;
        int n = static_cast<int>(items_->size());
        int start = (hovered_ < 0) ? (dir > 0 ? -1 : n) : hovered_;
        for (int step = 0; step < n; ++step) {
            start = ((start + dir) % n + n) % n;
            if ((*items_)[start].Selectable()) { SetHoverTo(start); return; }
        }
    }

    void SetHoverTo(int idx) {
        if (idx == hovered_) return;
        hovered_ = idx;
        Invalidate();
        if (cb_.onHover) cb_.onHover(idx);
    }

    void ActivateHovered() {
        if (hovered_ < 0 || hovered_ >= ItemCount()) return;
        const MenuItem& it = (*items_)[hovered_];
        if (!it.Selectable()) return;
        if (it.HasSubmenu()) {
            if (cb_.onOpenSub) cb_.onOpenSub(hovered_);
        } else {
            if (cb_.onInvoke) cb_.onInvoke(hovered_);
        }
    }

    const std::vector<MenuItem>* items_ = nullptr;
    int hovered_ = -1;
    MenuListCallbacks cb_;
};

// ---------------------------------------------------------------------------
// MenuFlyout implementation
// ---------------------------------------------------------------------------

MenuFlyout::MenuFlyout() = default;
MenuFlyout::~MenuFlyout() {
    // Close the whole chain and unregister the window's key/dismiss hooks before
    // members are torn down, so the window can never invoke a [this] callback
    // into a half-destroyed flyout. Close() is idempotent (no-op if not open).
    Close();
}

void MenuFlyout::SetItems(std::vector<MenuItem> items) {
    Close();
    items_ = std::move(items);
}

bool MenuFlyout::IsOpen() const {
    return !levels_.empty();
}

std::unique_ptr<Control> MenuFlyout::MakeListView(const std::vector<MenuItem>* items,
                                                  int levelIndex) {
    auto view = std::make_unique<MenuListView>();
    view->AttachToContext(ctx_);
    view->SetItems(items);

    MenuListCallbacks cb;
    cb.onInvoke = [this, levelIndex](int idx) { InvokeItem(levelIndex, idx); };
    cb.onOpenSub = [this, levelIndex](int idx) { OpenSubmenu(levelIndex, idx); };
    cb.onHover = [this, levelIndex](int idx) {
        // Stage 3 will open/close submenus on hover; stage 2 root has no submenus,
        // but wire it now so cascading works without touching the list view.
        OnItemHovered(levelIndex, idx);
    };
    cb.onCloseAll = [this]() { Close(); };
    cb.onLeftRight = [this, levelIndex](bool right) { OnLeftRight(levelIndex, right); };
    view->SetCallbacks(std::move(cb));
    return view;
}

void MenuFlyout::MeasureLevel(Control* list, float& outW, float& outH) const {
    outW = kMinWidth;
    outH = kItemH;
    if (auto* mv = dynamic_cast<MenuListView*>(list))
        mv->ComputeDesired(outW, outH);
}

void MenuFlyout::ShowAt(int screenX, int screenY) {
    if (!window_ || items_.empty()) return;
    Close();
    // Zero-size anchor at the cursor: PopupHost places the card with its top-left
    // at the anchor's bottom-left, i.e. right at the cursor point.
    RECT anchor = {screenX, screenY, screenX, screenY};
    OpenRoot(anchor);
}

void MenuFlyout::ShowBelow(const RECT& anchorScreenRect) {
    if (!window_ || items_.empty()) return;
    Close();
    OpenRoot(anchorScreenRect);
}

void MenuFlyout::OpenRoot(const RECT& anchor) {
    // Free any levels retired by a previous close. Safe here: opening is driven by
    // a fresh user action (right-click / MenuBar click), not from inside a popup's
    // own message handler, so no retired PopupHost's WndProc is on the stack.
    ReapRetired();

    Level level;
    level.items = &items_;
    level.parentIndex = -1;
    level.list = MakeListView(&items_, 0);

    float wDip = 0.0f, hDip = 0.0f;
    MeasureLevel(level.list.get(), wDip, hDip);

    level.popup = std::make_unique<PopupHost>();
    if (FAILED(level.popup->Create(window_->Instance(), window_->Hwnd(),
                                   &window_->D2D(), &window_->DWrite()))) {
        TraceMsg(kTag, "OpenRoot: PopupHost::Create failed");
        return;
    }
    level.popup->SetResourceCache(ctx_.resourceCache);  // share caches (§13.3)
    level.popup->SetTheme(ctx_.theme);            // share theme snapshot (§11)
    level.popup->SetLightDismiss(false);  // the flyout manages the whole chain
    ApplyOpacityTo(level);                // before Open: Open paints the first frame
    level.popup->SetContent(level.list.get());

    levels_.push_back(std::move(level));

    // PopupHost adds an 8dip interior pad on all sides; add it to the card size.
    constexpr float kPad = 8.0f;
    if (FAILED(levels_.back().popup->Open(anchor, wDip + kPad * 2.0f,
                                          hDip + kPad * 2.0f))) {
        TraceMsg(kTag, "OpenRoot: PopupHost::Open failed");
        levels_.pop_back();
        return;
    }

    RegisterWindowHooks();
    TraceMsg(kTag, "OpenRoot: menu shown");
}

void MenuFlyout::OpenSubmenu(int levelIndex, int itemIndex) {
    if (levelIndex < 0 || levelIndex >= static_cast<int>(levels_.size())) return;
    Level& parent = levels_[levelIndex];
    if (!parent.items || itemIndex < 0 ||
        itemIndex >= static_cast<int>(parent.items->size())) return;
    const MenuItem& item = (*parent.items)[itemIndex];
    if (!item.HasSubmenu()) return;

    // Close any sub-levels that are already open deeper than this one.
    CloseFrom(levelIndex + 1);

    // Compute the anchor: right edge of the parent popup, at the item's row in
    // screen pixels. The submenu opens to the right (PopupHost may flip left if
    // it would overflow the work area).
    HWND parentHwnd = parent.popup->Hwnd();
    float s = parent.popup->DpiScale();
    RECT parentRect;
    GetWindowRect(parentHwnd, &parentRect);

    auto* lv = dynamic_cast<MenuListView*>(parent.list.get());
    float itemTopDip = lv ? lv->ItemYOffsetDip(itemIndex) : 0.0f;
    int itemTopPx = static_cast<int>(itemTopDip * s + 0.5f);

    // Align the submenu's first row with the hovered parent row: PopupHost places
    // the card's top-left at (anchor.left, anchor.bottom), and its content is
    // inset by 8dip. So the popup top must sit 8dip above the parent item's top
    // for the first submenu row to line up. Use a zero-size anchor at the parent
    // window's right edge; PopupHost clamps horizontally into the work area if the
    // submenu would overflow the right screen edge (v1: it may overlap the parent).
    int padPx = static_cast<int>(8.0f * s + 0.5f);
    int popupTopY = parentRect.top + itemTopPx - padPx;
    RECT anchor;
    anchor.left = anchor.right = parentRect.right;
    anchor.top = anchor.bottom = popupTopY;

    // Build the new level.
    Level level;
    level.items = &item.submenu;
    level.parentIndex = itemIndex;
    level.list = MakeListView(&item.submenu, levelIndex + 1);

    float wDip = 0.0f, hDip = 0.0f;
    MeasureLevel(level.list.get(), wDip, hDip);

    level.popup = std::make_unique<PopupHost>();
    if (FAILED(level.popup->Create(window_->Instance(), window_->Hwnd(),
                                   &window_->D2D(), &window_->DWrite()))) {
        TraceMsg(kTag, "OpenSubmenu: PopupHost::Create failed");
        return;
    }
    level.popup->SetResourceCache(ctx_.resourceCache);  // share caches (§13.3)
    level.popup->SetTheme(ctx_.theme);            // share theme snapshot (§11)
    level.popup->SetLightDismiss(false);
    ApplyOpacityTo(level);                // before Open: Open paints the first frame
    level.popup->SetContent(level.list.get());
    levels_.push_back(std::move(level));

    constexpr float kPad = 8.0f;
    if (FAILED(levels_.back().popup->Open(anchor, wDip + kPad * 2.0f,
                                          hDip + kPad * 2.0f))) {
        TraceMsg(kTag, "OpenSubmenu: PopupHost::Open failed");
        levels_.pop_back();
    }
}

void MenuFlyout::ApplyOpacityTo(Level& level) {
    if (!level.popup) return;
    level.popup->SetCardOpacity(cardOpacity_);
    level.popup->SetContentOpacity(contentOpacity_);
}

void MenuFlyout::SetCardOpacity(float opacity) {
    cardOpacity_ = std::clamp(opacity, 0.0f, 1.0f);
    // Apply to any level already on screen; later levels pick it up in ApplyOpacityTo.
    for (Level& l : levels_) {
        if (!l.popup) continue;
        l.popup->SetCardOpacity(cardOpacity_);
        l.popup->Render();
    }
}

void MenuFlyout::SetContentOpacity(float opacity) {
    contentOpacity_ = std::clamp(opacity, 0.0f, 1.0f);
    for (Level& l : levels_) {
        if (!l.popup) continue;
        l.popup->SetContentOpacity(contentOpacity_);
        l.popup->Render();
    }
}

void MenuFlyout::OnItemHovered(int levelIndex, int itemIndex) {
    // Close all levels deeper than this one first (the pointer moved to a new row).
    CloseFrom(levelIndex + 1);

    // If the new hovered item has a submenu, open it immediately.
    if (levelIndex < 0 || levelIndex >= static_cast<int>(levels_.size())) return;
    if (itemIndex < 0) return;
    const std::vector<MenuItem>* items = levels_[levelIndex].items;
    if (!items || itemIndex >= static_cast<int>(items->size())) return;
    if ((*items)[itemIndex].HasSubmenu())
        OpenSubmenu(levelIndex, itemIndex);
}

void MenuFlyout::OnLeftRight(int levelIndex, bool right) {
    if (right) {
        // Right arrow: open the submenu of the currently hovered item (if any).
        if (levelIndex < 0 || levelIndex >= static_cast<int>(levels_.size())) return;
        auto* lv = dynamic_cast<MenuListView*>(levels_[levelIndex].list.get());
        if (!lv) return;
        int hovered = lv->Hovered();
        if (hovered < 0) return;
        const std::vector<MenuItem>* items = levels_[levelIndex].items;
        if (!items || hovered >= static_cast<int>(items->size())) return;
        if ((*items)[hovered].HasSubmenu())
            OpenSubmenu(levelIndex, hovered);
    } else {
        // Left arrow: close the deepest submenu level (keep at least the root).
        if (static_cast<int>(levels_.size()) > 1)
            CloseFrom(static_cast<int>(levels_.size()) - 1);
    }
}

void MenuFlyout::ReapRetired() {
    // Destroy parked levels here, where PopupHost is a complete type and — the
    // point of the deferral — no retired PopupHost's WndProc is on the call stack.
    retired_.clear();
}

void MenuFlyout::CloseFrom(int fromLevel) {
    if (fromLevel < 0) fromLevel = 0;
    while (static_cast<int>(levels_.size()) > fromLevel) {
        // Hide now, but DON'T destroy: closing frequently runs inside the popup's
        // own WndProc (item click -> onInvoke -> Close), where deleting the
        // PopupHost + its InputManager would unwind through freed memory. Park the
        // level in retired_ and free it later (ReapRetired) off the popup's stack.
        if (levels_.back().popup) levels_.back().popup->Close();
        retired_.push_back(std::move(levels_.back()));
        levels_.pop_back();
    }
}

void MenuFlyout::InvokeItem(int levelIndex, int itemIndex) {
    if (levelIndex < 0 || levelIndex >= static_cast<int>(levels_.size())) return;
    const std::vector<MenuItem>* items = levels_[levelIndex].items;
    if (!items || itemIndex < 0 || itemIndex >= static_cast<int>(items->size())) return;
    // Copy the callback before closing (closing destroys the levels + list views).
    std::function<void()> action = (*items)[itemIndex].onInvoke;
    Close();
    if (action) action();
}

void MenuFlyout::Close() {
    if (levels_.empty()) return;
    CloseFrom(0);
    ClearWindowHooks();
    if (onClosed_) onClosed_();
    TraceMsg(kTag, "Close: menu chain closed");
}

// ---------------------------------------------------------------------------
// Window hooks: dismiss + key routing (single registration for the whole chain)
// ---------------------------------------------------------------------------

void MenuFlyout::RegisterWindowHooks() {
    if (!window_) return;
    keySub_ = window_->RegisterActivePopupKeyHandler(
        [this](UINT vk) { return HandleKey(vk); });
    dismissSub_ = window_->RegisterActivePopupDismiss(
        [this](PopupDismissReason reason, HWND other, int sx, int sy) {
            int r = static_cast<int>(reason);
            return HandleDismiss(r, other, sx, sy);
        });
}

void MenuFlyout::ClearWindowHooks() {
    // Resetting each Subscription clears its slot (generation-guarded, so it
    // never wipes a newer registration). Safe to call when never registered.
    keySub_.reset();
    dismissSub_.reset();
}

bool MenuFlyout::HandleKey(UINT vk) {
    if (levels_.empty()) return false;
    if (vk == VK_ESCAPE) {
        // Esc closes the deepest level (or the whole menu if only the root is open).
        if (levels_.size() > 1)
            CloseFrom(static_cast<int>(levels_.size()) - 1);
        else
            Close();
        return true;
    }
    // Route navigation to the deepest (active) level's list view (routed key).
    Control* list = levels_.back().list.get();
    if (auto* mv = dynamic_cast<MenuListView*>(list)) {
        KeyEventArgs args;
        args.vk = vk;
        args.source = mv;
        args.originalSource = mv;
        mv->OnKeyDownRouted(args);
        return args.handled;
    }
    return false;
}

bool MenuFlyout::HandleDismiss(int reason, HWND otherHwnd, int screenX, int screenY) {
    using Reason = PopupDismissReason;
    Reason r = static_cast<Reason>(reason);

    if (r == Reason::Deactivate) {
        // Losing activation to one of our own popup windows must NOT dismiss.
        if (IsOwnPopup(otherHwnd)) return false;
        Close();
        return true;
    }
    if (r == Reason::Click) {
        // A click inside any level's window is handled by that popup; ignore it
        // here. A click on an owner-guarded region (e.g. a MenuBar title) is left
        // for the owner to handle (toggle/switch). Anything else closes the chain.
        if (PointInAnyPopup(screenX, screenY)) return false;
        if (dismissGuard_ && dismissGuard_(screenX, screenY)) return false;
        Close();
        return true;
    }
    // Move / Resize: always dismiss.
    Close();
    return true;
}

bool MenuFlyout::PointInAnyPopup(int screenX, int screenY) const {
    for (const Level& lv : levels_)
        if (lv.popup && lv.popup->ContainsScreenPoint(screenX, screenY))
            return true;
    return false;
}

bool MenuFlyout::IsOwnPopup(HWND hwnd) const {
    for (const Level& lv : levels_)
        if (lv.popup && lv.popup->Hwnd() == hwnd) return true;
    return false;
}

} // namespace fluent
