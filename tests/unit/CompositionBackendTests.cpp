// CompositionBackendTests.cpp — unit tests for the ICompositionBackend contract
// exercised through the headless FakeCompositionBackend (Phase 2).
//
// These pin the semantics a real backend must honour and the product code relies
// on: CreateVisual hands back independent nodes, offset/opacity/clip round-trip,
// StartOffsetSweep records a sweep on OffsetX, a static SetOffset replaces a
// running animation, StopAnimation clears it, child add/remove is order-preserving
// and idempotent-on-remove, and root membership tracks add/remove.

#include "../framework/Test.h"
#include "../framework/FakeCompositionBackend.h"

using namespace fluent;
using fltest::FakeCompositionBackend;
using fltest::FakeCompositionVisual;

TEST(CompositionBackend, CreateVisualCountsAndIsIndependent) {
    FakeCompositionBackend backend;
    auto a = backend.CreateVisual();
    auto b = backend.CreateVisual();
    EXPECT_EQ(backend.createdVisuals, 2);
    EXPECT_TRUE(a != nullptr);
    EXPECT_TRUE(b != nullptr);
    EXPECT_TRUE(a.get() != b.get());
}

TEST(CompositionBackend, OffsetOpacityClipRoundTrip) {
    FakeCompositionBackend backend;
    auto v = backend.CreateVisual();
    auto* f = static_cast<FakeCompositionVisual*>(v.get());

    v->SetOffset(12.0f, -3.0f);
    EXPECT_NEAR(f->offsetX, 12.0f, 0.001f);
    EXPECT_NEAR(f->offsetY, -3.0f, 0.001f);

    v->SetOpacity(0.25f);
    EXPECT_NEAR(f->opacity, 0.25f, 0.001f);

    EXPECT_FALSE(f->hasClip);
    v->SetClip(1.0f, 2.0f, 30.0f, 40.0f);
    EXPECT_TRUE(f->hasClip);
    EXPECT_NEAR(f->clipR, 30.0f, 0.001f);
    v->ClearClip();
    EXPECT_FALSE(f->hasClip);
}

TEST(CompositionBackend, StartOffsetSweepRecordsSpec) {
    FakeCompositionBackend backend;
    auto v = backend.CreateVisual();
    auto* f = static_cast<FakeCompositionVisual*>(v.get());

    EXPECT_FALSE(f->IsAnimatingOffsetX());
    SweepSpec spec;
    spec.minX = -20.0f;
    spec.maxX = 100.0f;
    spec.cycleSec = 1.4;
    spec.phaseDeg = -90.0f;
    v->StartOffsetSweep(spec);

    EXPECT_TRUE(f->IsAnimatingOffsetX());
    EXPECT_TRUE(f->offsetXAnim->isSweep);
    EXPECT_EQ(f->offsetXAnim->property, CompositionProperty::OffsetX);
    EXPECT_NEAR(f->offsetXAnim->sweep.minX, -20.0f, 0.001f);
    EXPECT_NEAR(f->offsetXAnim->sweep.maxX, 100.0f, 0.001f);
    EXPECT_NEAR(f->offsetXAnim->sweep.cycleSec, 1.4, 0.001f);
}

TEST(CompositionBackend, StaticOffsetReplacesAnimation) {
    FakeCompositionBackend backend;
    auto v = backend.CreateVisual();
    auto* f = static_cast<FakeCompositionVisual*>(v.get());

    SweepSpec spec;
    spec.minX = 0.0f; spec.maxX = 50.0f;
    v->StartOffsetSweep(spec);
    EXPECT_TRUE(f->IsAnimatingOffsetX());

    v->SetOffset(-7.0f, 0.0f);  // pin: cancels the sweep
    EXPECT_FALSE(f->IsAnimatingOffsetX());
    EXPECT_NEAR(f->offsetX, -7.0f, 0.001f);
}

TEST(CompositionBackend, StopAnimationClearsSweep) {
    FakeCompositionBackend backend;
    auto v = backend.CreateVisual();
    auto* f = static_cast<FakeCompositionVisual*>(v.get());
    SweepSpec spec; spec.maxX = 10.0f;
    v->StartOffsetSweep(spec);
    v->StopAnimation(CompositionProperty::OffsetX);
    EXPECT_FALSE(f->IsAnimatingOffsetX());
}

TEST(CompositionBackend, DrawSurfaceRecordsSizeAndInvokesCallback) {
    FakeCompositionBackend backend;
    auto v = backend.CreateVisual();
    auto* f = static_cast<FakeCompositionVisual*>(v.get());

    int cbCalls = 0;
    bool sawNullDc = false;
    v->DrawSurface(64, 8, [&](ID2D1DeviceContext* dc, float, float) {
        ++cbCalls;
        sawNullDc = (dc == nullptr);
    });
    EXPECT_EQ(f->drawCount, 1);
    EXPECT_EQ(static_cast<int>(f->lastDrawW), 64);
    EXPECT_EQ(static_cast<int>(f->lastDrawH), 8);
    EXPECT_EQ(cbCalls, 1);
    EXPECT_TRUE(sawNullDc);  // headless backend passes a null DC
}

TEST(CompositionBackend, ChildAddRemoveIsOrderedAndIdempotent) {
    FakeCompositionBackend backend;
    auto parent = backend.CreateVisual();
    auto child1 = backend.CreateVisual();
    auto child2 = backend.CreateVisual();
    auto* pf = static_cast<FakeCompositionVisual*>(parent.get());

    parent->AddChild(child1.get());
    parent->AddChild(child2.get());
    EXPECT_EQ(static_cast<int>(pf->children.size()), 2);
    EXPECT_TRUE(pf->children[0] == child1.get());

    parent->RemoveChild(child1.get());
    EXPECT_EQ(static_cast<int>(pf->children.size()), 1);
    EXPECT_TRUE(pf->children[0] == child2.get());

    parent->RemoveChild(child1.get());  // already gone: no-op
    EXPECT_EQ(static_cast<int>(pf->children.size()), 1);
}

TEST(CompositionBackend, RootMembershipAndCommit) {
    FakeCompositionBackend backend;
    auto v = backend.CreateVisual();

    EXPECT_EQ(backend.RootCount(), 0);
    backend.AddToRoot(v.get());
    EXPECT_EQ(backend.RootCount(), 1);

    backend.RequestCommit();
    backend.RequestCommit();
    EXPECT_EQ(backend.commitRequests, 2);

    backend.RemoveFromRoot(v.get());
    EXPECT_EQ(backend.RootCount(), 0);
}
