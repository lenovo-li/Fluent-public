// ListBoxTests.cpp — Unit tests for ListBox control.

#include "../framework/Test.h"
#include "../../FluentUI/controls/ListBox.h"

using namespace fluent;

namespace {
void CountSel(void* ctx, ListBox&, int& idx) {
    int* p = static_cast<int*>(ctx);
    p[0]++;      // fired count
    p[1] = idx;  // last index
}
} // namespace

TEST(ListBox, EmptyList_NoSelection)
{
    ListBox list;
    EXPECT_EQ(list.SelectedIndex(), -1);
    EXPECT_EQ(list.ItemCount(), 0);
}

TEST(ListBox, SetItems_SingleItem)
{
    ListBox list;
    list.SetItems({L"Item1"});
    EXPECT_EQ(list.ItemCount(), 1);
    EXPECT_EQ(list.SelectedIndex(), -1);  // no auto-select
}

TEST(ListBox, SetItems_MultipleItems)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C", L"D", L"E"});
    EXPECT_EQ(list.ItemCount(), 5);
}

TEST(ListBox, SetSelectedIndex_ValidIndex)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetSelectedIndex(1);
    EXPECT_EQ(list.SelectedIndex(), 1);
}

TEST(ListBox, SetSelectedIndex_ClampsNegative)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetSelectedIndex(-5);
    EXPECT_EQ(list.SelectedIndex(), -1);
}

TEST(ListBox, SetSelectedIndex_ClampsOverflow)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetSelectedIndex(100);
    EXPECT_EQ(list.SelectedIndex(), 2);  // clamped to count-1
}

TEST(ListBox, SetSelectedIndex_NoChange_NoEvent)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetSelectedIndex(1);
    int state[2] = {0, -1};
    auto sub = list.SelectionChanged().Subscribe(state, CountSel);
    list.SetSelectedIndex(1);  // same index
    EXPECT_EQ(state[0], 0);
}

TEST(ListBox, SelectionChanged_EventFired)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    int state[2] = {0, -1};
    auto sub = list.SelectionChanged().Subscribe(state, CountSel);
    list.SetSelectedIndex(2);
    EXPECT_EQ(state[0], 1);
    EXPECT_EQ(state[1], 2);
}

TEST(ListBox, VirtualMode_ItemCount)
{
    ListBox list;
    list.SetItemCount(100000);
    EXPECT_EQ(list.ItemCount(), 100000);
    EXPECT_EQ(static_cast<int>(list.Items().size()), 0);  // no items vector in virtual mode
}

TEST(ListBox, VirtualMode_SelectionClampedOnSetItemCount)
{
    ListBox list;
    list.SetItemCount(10);
    list.SetSelectedIndex(5);
    list.SetItemCount(3);  // shrink
    EXPECT_EQ(list.SelectedIndex(), 2);  // clamped to new count-1
}

TEST(ListBox, KeyDown_ArrowDown_SelectsNext)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetSelectedIndex(0);
    KeyEventArgs e;
    e.vk = VK_DOWN;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(list.SelectedIndex(), 1);
}

TEST(ListBox, KeyDown_ArrowUp_SelectsPrevious)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    list.SetSelectedIndex(2);
    KeyEventArgs e;
    e.vk = VK_UP;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(list.SelectedIndex(), 1);
}

TEST(ListBox, KeyDown_Home_SelectsFirst)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C", L"D"});
    list.SetSelectedIndex(3);
    KeyEventArgs e;
    e.vk = VK_HOME;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(list.SelectedIndex(), 0);
}

TEST(ListBox, KeyDown_End_SelectsLast)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C", L"D"});
    list.SetSelectedIndex(0);
    KeyEventArgs e;
    e.vk = VK_END;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(list.SelectedIndex(), 3);
}

TEST(ListBox, KeyDown_ArrowDown_ClampsAtBottom)
{
    ListBox list;
    list.SetItems({L"A", L"B"});
    list.SetSelectedIndex(1);
    KeyEventArgs e;
    e.vk = VK_DOWN;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_EQ(list.SelectedIndex(), 1);  // stays at last
}

TEST(ListBox, KeyDown_ArrowUp_ClampsAtTop)
{
    ListBox list;
    list.SetItems({L"A", L"B"});
    list.SetSelectedIndex(0);
    KeyEventArgs e;
    e.vk = VK_UP;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_EQ(list.SelectedIndex(), 0);  // stays at first
}

TEST(ListBox, KeyDown_NoSelection_ArrowDown_SelectsFirst)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C"});
    KeyEventArgs e;
    e.vk = VK_DOWN;
    e.modifiers = ModifierKeys::None;
    list.OnKeyDownRouted(e);
    EXPECT_EQ(list.SelectedIndex(), 0);
}

TEST(ListBox, ItemTextProvider_VirtualMode)
{
    ListBox list;
    list.SetItemCount(5);
    list.ItemTextProvider = [](size_t i) {
        return L"Virtual item " + std::to_wstring(i);
    };
    // Verify GetItemText actually uses the provider.
    EXPECT_TRUE(list.GetItemText(2) == L"Virtual item 2");
}

TEST(ListBox, SetItems_ClearsVirtualMode)
{
    ListBox list;
    list.SetItemCount(100);
    EXPECT_EQ(list.ItemCount(), 100);
    list.SetItems({L"A", L"B"});
    EXPECT_EQ(list.ItemCount(), 2);  // switched to direct mode
}

TEST(ListBox, SetItemHeight_UpdatesHeight)
{
    ListBox list;
    list.SetItems({L"A", L"B", L"C", L"D", L"E"});  // 5 items
    list.SetItemHeight(40.0f);
    EXPECT_EQ(list.ItemHeight(), 40.0f);
}

// --- GetItemText(i, scratch): the zero-allocation render path ------------------
// Render calls this once per visible row per frame. The value-returning overload
// copied every time, which virtualization bounds to O(visible) but still means tens
// of discarded heap allocations per frame during a scroll — and in direct-items mode
// every copy duplicates a string the control already owns. These tests pin the two
// properties the render loop depends on: direct mode returns a reference INTO items_
// (no copy exists to make), and virtualized mode reuses the caller's one buffer.

TEST(ListBox, GetItemTextScratch_DirectModeAliasesItemsStorage)
{
    ListBox list;
    list.SetItems({L"Alpha", L"Beta", L"Gamma"});

    std::wstring scratch = L"sentinel";
    const std::wstring& text = list.GetItemText(1, scratch);

    EXPECT_TRUE(text == L"Beta");
    // The returned reference must point at the control's own storage, not at scratch:
    // that is the whole claim of the overload in direct mode. Comparing addresses is
    // the only way to tell a reference from a copy — an == on the value passes either
    // way, which is exactly how a regression to copying would slip through.
    EXPECT_TRUE(&text != &scratch);
    // And scratch must be left completely alone, not cleared or overwritten.
    EXPECT_TRUE(scratch == L"sentinel");
}

TEST(ListBox, GetItemTextScratch_VirtualModeUsesCallerBuffer)
{
    ListBox list;
    list.SetItemCount(5);
    list.ItemTextProvider = [](size_t i) {
        return L"Virtual item " + std::to_wstring(i);
    };

    std::wstring scratch;
    const std::wstring& text = list.GetItemText(3, scratch);

    EXPECT_TRUE(text == L"Virtual item 3");
    // The provider returns by value, so there is nothing stable to alias — the result
    // must land in the caller's buffer, and the reference must name that buffer. If it
    // named a function-local instead, this reference would dangle.
    EXPECT_TRUE(&text == &scratch);
}

TEST(ListBox, GetItemTextScratch_BufferIsReusedAcrossRows)
{
    // The render loop hoists ONE scratch string outside the row loop; this is that
    // pattern. After the buffer has grown to the longest row, subsequent rows must
    // reuse the capacity rather than reallocate — that reuse is the actual saving,
    // and it only happens because Render declares the buffer outside the loop.
    ListBox list;
    list.SetItemCount(4);
    list.ItemTextProvider = [](size_t i) {
        return std::wstring(10 + i * 5, L'x');   // 10, 15, 20, 25 chars
    };

    std::wstring scratch;
    list.GetItemText(3, scratch);                // longest row first: grows once
    const size_t capAfterLongest = scratch.capacity();

    for (int i = 0; i < 4; ++i) {
        const std::wstring& text = list.GetItemText(i, scratch);
        EXPECT_EQ(text.size(), size_t(10 + i * 5));
        // No row after the longest may force a reallocation.
        EXPECT_TRUE(scratch.capacity() == capAfterLongest);
    }
}

TEST(ListBox, GetItemTextScratch_OutOfRangeIsEmptyInBothModes)
{
    // Out of range must be empty from the scratch overload too, and must not leave
    // a previous row's text behind — a stale scratch would silently paint the wrong
    // string on the row past the end.
    ListBox direct;
    direct.SetItems({L"A", L"B"});
    std::wstring scratch = L"stale";
    EXPECT_TRUE(direct.GetItemText(7, scratch).empty());
    EXPECT_TRUE(direct.GetItemText(-1, scratch).empty());

    ListBox virt;
    virt.SetItemCount(2);
    virt.ItemTextProvider = [](size_t i) { return L"item" + std::to_wstring(i); };
    EXPECT_TRUE(virt.GetItemText(9, scratch).empty());
    EXPECT_TRUE(virt.GetItemText(-1, scratch).empty());
}

TEST(ListBox, GetItemTextScratch_ProviderWinsOverItemsInBothOverloads)
{
    // SetItemCount switches to virtualized mode, but items_ may still hold strings
    // from an earlier direct-mode use. Both overloads must consult the provider, or
    // the two disagree and only the untested one is wrong.
    ListBox list;
    list.SetItems({L"direct0", L"direct1", L"direct2"});
    list.SetItemCount(3);
    list.ItemTextProvider = [](size_t i) { return L"provided" + std::to_wstring(i); };

    std::wstring scratch;
    EXPECT_TRUE(list.GetItemText(1, scratch) == L"provided1");
    EXPECT_TRUE(list.GetItemText(1) == L"provided1");   // value form agrees
}
