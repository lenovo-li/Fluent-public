// ShowcaseHelpers.cpp — shared building blocks for the per-control demo pages.
//
// WHY THESE EXIST. Every control page wants to answer the same four questions, and before
// this they answered at most one of them:
//
//   1. What are its states?            (already covered by most pages)
//   2. How does it behave under LAYOUT? Stretch vs fixed vs Min/Max vs alignment.
//   3. What can I RESTYLE on it?       The eight Control-level properties.
//   4. How does it look NEXT TO other controls?
//
// Questions 2 and 3 were previously answered on separate "styling showcase" pages, which
// split every control across two places in the nav. Folding them back means each control's
// page is the single place to look -- but writing the same four cards by hand twenty times
// would guarantee they drift apart, so the repeated shapes live here.
//
// These are deliberately plain functions taking a parent panel, not a base class or a
// template: a demo page is a script, and the moment a helper needs to know about the
// specific control it is decorating, the caller should just write the code inline.

#include "../GalleryMain.h"

#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Separator.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/GroupBox.h"

namespace fluent {

// A dimmed, wrapping explanatory paragraph. Used constantly; worth one line.
TextBlock* GalleryApp::AddNote(Panel* parent, std::wstring text) {
    auto* t = parent->Add(std::make_unique<TextBlock>());
    t->SetText(std::move(text));
    t->SetWrap(true);
    t->SetDimmed(true);
    return t;
}

// A labelled sub-section inside a card, so one card can hold several related demos
// without the reader losing track of which is which.
StackPanel* GalleryApp::AddSubSection(Panel* parent, std::wstring label) {
    auto* box = parent->Add(std::make_unique<GroupBox>());
    box->SetHeader(std::move(label));
    auto* inner = box->SetChild(std::make_unique<StackPanel>());
    inner->SetOrientation(StackPanel::Orientation::Vertical);
    inner->SetSpacing(8.0f);
    return inner;
}

// A fixed-width bordered container that visibly bounds whatever is dropped in it.
//
// Needed because "this control stretches" and "this control is 120 DIP wide" look
// IDENTICAL on a page whose content area is 2000 DIP wide -- the stretched one just fills
// something the reader cannot see. Drawing the container's edge is what makes the
// difference legible.
StackPanel* GalleryApp::AddBoundedBox(Panel* parent, float width, std::wstring caption) {
    if (!caption.empty()) {
        auto* cap = parent->Add(std::make_unique<TextBlock>());
        cap->SetText(std::move(caption));
        cap->SetFontSize(12.0f);
        cap->SetDimmed(true);
    }
    auto* frame = parent->Add(std::make_unique<Border>());
    frame->SetBorderThickness(1.0f);
    frame->SetCornerRadius(4.0f);
    frame->SetPadding(Thickness{8.0f});
    frame->SetWidth(width);
    frame->SetHAlign(HAlign::Left);
    frame->SetMargin(Thickness{0, 2.0f, 0, 6.0f});

    auto* inner = frame->SetChild(std::make_unique<StackPanel>());
    inner->SetOrientation(StackPanel::Orientation::Vertical);
    inner->SetSpacing(6.0f);
    return inner;
}

// A horizontal row for placing several controls side by side, which is how alignment and
// intrinsic-width differences become visible.
StackPanel* GalleryApp::AddRow(Panel* parent, float spacing) {
    auto* row = parent->Add(std::make_unique<StackPanel>());
    row->SetOrientation(StackPanel::Orientation::Horizontal);
    row->SetSpacing(spacing);
    return row;
}

// An N-column Grid of equal Star tracks.
//
// Star tracks rather than UniformGrid, deliberately: UniformGrid sizes every cell to the
// LARGEST CHILD'S DESIRED size and never expands cells to fill the arranged width, so four
// items in an 800 DIP row get ~63 DIP each. Star tracks divide the available width, which
// is what a comparison row actually wants. Auto row height, because typing a row height is
// what sheared the text off a Metric earlier in this project.
Grid* GalleryApp::AddEqualColumns(Panel* parent, int columns) {
    auto* g = parent->Add(std::make_unique<Grid>());
    g->AddRow(GridLength::Auto());
    for (int i = 0; i < columns; ++i) g->AddColumn(GridLength::Star(1.0f));
    return g;
}

// The five accent colours reused by every "AccentColor" demo, in one place so the pages
// cannot disagree about which five they show.
const AccentSwatch* GalleryApp::AccentSwatches(int& count) {
    static const AccentSwatch kSwatches[] = {
        {L"蓝", 0x0078D4},
        {L"绿", 0x107C10},
        {L"橙", 0xF7630C},
        {L"红", 0xC42B1C},
        {L"紫", 0x8764B8},
    };
    count = 5;
    return kSwatches;
}

}  // namespace fluent
