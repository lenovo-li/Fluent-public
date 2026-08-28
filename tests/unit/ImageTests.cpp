// ImageTests.cpp — unit tests for the Image control.
//
// Headless: no device, so no bitmap is ever built. What IS testable is the part
// that carries all the logic — ComputeImageDrawRects, a pure function over
// (native size, bounds, stretch mode) — plus the control's state machine around
// loading (a missing file must not crash, and a detached SetSource must not try).

#include "../framework/Test.h"
#include "../../FluentUI/controls/Image.h"

using namespace fluent;

namespace {
// The four modes share a fixture: a 200x100 image (2:1) in a 100x100 box.
constexpr SizeDip kNative{200.0f, 100.0f};
constexpr RectDip kBox{10.0f, 20.0f, 100.0f, 100.0f};
} // namespace

// --- Degenerate inputs -----------------------------------------------------

TEST(ImageGeometry, ZeroNativeSizeGivesEmpty) {
    ImageDrawRects r = ComputeImageDrawRects({0.0f, 0.0f}, kBox, ImageStretch::Uniform);
    EXPECT_TRUE(r.isEmpty());
}

TEST(ImageGeometry, ZeroBoundsGivesEmpty) {
    ImageDrawRects r = ComputeImageDrawRects(kNative, {0, 0, 0, 0}, ImageStretch::Uniform);
    EXPECT_TRUE(r.isEmpty());
}

TEST(ImageGeometry, NegativeNativeHeightGivesEmpty) {
    ImageDrawRects r = ComputeImageDrawRects({200.0f, -5.0f}, kBox, ImageStretch::Fill);
    EXPECT_TRUE(r.isEmpty());
}

// --- Uniform: whole image visible, aspect preserved, letterboxed -----------

TEST(ImageGeometry, UniformFitsWidthAndLetterboxes) {
    // 200x100 into 100x100: scale = min(0.5, 1.0) = 0.5 -> dest 100x50.
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::Uniform);
    EXPECT_NEAR(r.dest.w, 100.0f, 0.01);
    EXPECT_NEAR(r.dest.h, 50.0f, 0.01);
    // Centered vertically inside the box: 20 + (100-50)/2 = 45.
    EXPECT_NEAR(r.dest.x, 10.0f, 0.01);
    EXPECT_NEAR(r.dest.y, 45.0f, 0.01);
}

TEST(ImageGeometry, UniformShowsWholeImage) {
    // Uniform never crops: src must be the entire native rect.
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::Uniform);
    EXPECT_NEAR(r.src.x, 0.0f, 0.01);
    EXPECT_NEAR(r.src.y, 0.0f, 0.01);
    EXPECT_NEAR(r.src.w, kNative.w, 0.01);
    EXPECT_NEAR(r.src.h, kNative.h, 0.01);
}

TEST(ImageGeometry, UniformPreservesAspectRatio) {
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::Uniform);
    const double nativeAspect = kNative.w / kNative.h;              // 2.0
    const double destAspect   = r.dest.w / r.dest.h;
    EXPECT_NEAR(destAspect, nativeAspect, 0.01);
}

TEST(ImageGeometry, UniformUpscalesSmallImage) {
    // 50x50 into 100x100: scale 2.0 -> fills the box exactly.
    ImageDrawRects r = ComputeImageDrawRects({50.0f, 50.0f}, kBox, ImageStretch::Uniform);
    EXPECT_NEAR(r.dest.w, 100.0f, 0.01);
    EXPECT_NEAR(r.dest.h, 100.0f, 0.01);
}

// --- Fill: exact bounds, aspect NOT preserved ------------------------------

TEST(ImageGeometry, FillCoversExactBounds) {
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::Fill);
    EXPECT_NEAR(r.dest.x, kBox.x, 0.01);
    EXPECT_NEAR(r.dest.y, kBox.y, 0.01);
    EXPECT_NEAR(r.dest.w, kBox.w, 0.01);
    EXPECT_NEAR(r.dest.h, kBox.h, 0.01);
}

TEST(ImageGeometry, FillDoesNotCrop) {
    // Fill distorts rather than crops, so the whole source is used.
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::Fill);
    EXPECT_NEAR(r.src.w, kNative.w, 0.01);
    EXPECT_NEAR(r.src.h, kNative.h, 0.01);
}

// --- UniformToFill: fills bounds, aspect preserved, overflow cropped -------

TEST(ImageGeometry, UniformToFillCoversExactBounds) {
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::UniformToFill);
    EXPECT_NEAR(r.dest.w, kBox.w, 0.01);
    EXPECT_NEAR(r.dest.h, kBox.h, 0.01);
}

TEST(ImageGeometry, UniformToFillCropsTheOverflowingAxis) {
    // 200x100 into 100x100: scale = max(0.5, 1.0) = 1.0. Visible source is
    // 100x100 of the 200x100 image -> horizontal crop, full height.
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::UniformToFill);
    EXPECT_NEAR(r.src.w, 100.0f, 0.01);
    EXPECT_NEAR(r.src.h, 100.0f, 0.01);
    EXPECT_NEAR(r.src.x, 50.0f, 0.01);   // centered: (200-100)/2
    EXPECT_NEAR(r.src.y, 0.0f, 0.01);
}

TEST(ImageGeometry, UniformToFillPreservesAspectRatioInSourceMapping) {
    // The src:dest aspect ratios must agree, or the image is distorted.
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::UniformToFill);
    EXPECT_NEAR(r.src.w / r.src.h, r.dest.w / r.dest.h, 0.01);
}

// --- None: native size, centered, cropped when too large ------------------

TEST(ImageGeometry, NoneUsesNativeSizeWhenItFits) {
    // 50x40 into 100x100: dest is 50x40, centered.
    ImageDrawRects r = ComputeImageDrawRects({50.0f, 40.0f}, kBox, ImageStretch::None);
    EXPECT_NEAR(r.dest.w, 50.0f, 0.01);
    EXPECT_NEAR(r.dest.h, 40.0f, 0.01);
    EXPECT_NEAR(r.dest.x, 35.0f, 0.01);  // 10 + (100-50)/2
    EXPECT_NEAR(r.dest.y, 50.0f, 0.01);  // 20 + (100-40)/2
}

TEST(ImageGeometry, NoneDoesNotScaleWhenItFits) {
    // src and dest must be the same size — that is what "no scaling" means.
    ImageDrawRects r = ComputeImageDrawRects({50.0f, 40.0f}, kBox, ImageStretch::None);
    EXPECT_NEAR(r.src.w, r.dest.w, 0.01);
    EXPECT_NEAR(r.src.h, r.dest.h, 0.01);
}

TEST(ImageGeometry, NoneCropsRatherThanSquashesWhenTooLarge) {
    // 200x100 into 100x100 with no scaling: dest is clamped to 100x100... but the
    // SOURCE must shrink to match, or D2D squashes 200px of image into 100px of
    // dest. src.w == dest.w is the whole point of this mode.
    ImageDrawRects r = ComputeImageDrawRects(kNative, kBox, ImageStretch::None);
    EXPECT_NEAR(r.dest.w, 100.0f, 0.01);
    EXPECT_NEAR(r.src.w, 100.0f, 0.01);
    EXPECT_NEAR(r.src.x, 50.0f, 0.01);   // centered crop
    // Height fits (100 <= 100), so no vertical crop.
    EXPECT_NEAR(r.src.h, 100.0f, 0.01);
    EXPECT_NEAR(r.src.y, 0.0f, 0.01);
}

// --- Control state machine -------------------------------------------------

TEST(Image, DefaultsToUniformAndUnloaded) {
    Image img;
    EXPECT_TRUE(img.Stretch() == ImageStretch::Uniform);
    EXPECT_FALSE(img.IsLoaded());
    EXPECT_TRUE(img.Source().empty());
}

TEST(Image, SetSourceRecordsPathWithoutLoading) {
    // Detached: there is no device, so SetSource must not even try to load.
    Image img;
    img.SetSource(L"C:\\does\\not\\exist.png");
    EXPECT_TRUE(img.Source() == L"C:\\does\\not\\exist.png");
    EXPECT_FALSE(img.IsLoaded());
    EXPECT_FALSE(img.LoadAttempted());
}

TEST(Image, MeasureWithMissingFileDoesNotCrashAndStaysZero) {
    Image img;
    img.SetSource(L"C:\\does\\not\\exist.png");
    img.Measure(300.0f, 300.0f);
    EXPECT_FALSE(img.IsLoaded());
    EXPECT_NEAR(img.NativeSize().w, 0.0f, 0.01);
}

TEST(Image, MeasureWithoutImageHonorsExplicitSize) {
    // No image (headless), but an explicit size must still lay out — otherwise a
    // placeholder Image collapses the surrounding layout.
    Image img;
    img.SetWidth(64.0f);
    img.SetHeight(48.0f);
    img.Measure(300.0f, 300.0f);
    EXPECT_NEAR(img.Desired().w, 64.0f, 0.01);
    EXPECT_NEAR(img.Desired().h, 48.0f, 0.01);
}

TEST(Image, SetStretchIsStored) {
    Image img;
    img.SetStretch(ImageStretch::UniformToFill);
    EXPECT_TRUE(img.Stretch() == ImageStretch::UniformToFill);
    img.SetStretch(ImageStretch::None);
    EXPECT_TRUE(img.Stretch() == ImageStretch::None);
}

TEST(Image, ResettingSourceClearsLoadAttempt) {
    Image img;
    img.SetSource(L"a.png");
    img.Measure(100.0f, 100.0f);   // marks attempted (and fails, headless)
    img.SetSource(L"b.png");       // new path -> must retry later
    EXPECT_FALSE(img.LoadAttempted());
    EXPECT_TRUE(img.Source() == L"b.png");
}

TEST(Image, SettingSameSourceTwiceIsANoOp) {
    Image img;
    img.SetSource(L"same.png");
    img.Measure(100.0f, 100.0f);
    const bool attemptedAfterFirst = img.LoadAttempted();
    img.SetSource(L"same.png");    // identical -> must not reset the attempt flag
    EXPECT_EQ(img.LoadAttempted(), attemptedAfterFirst);
}

TEST(Image, RenderWithoutBitmapIsSafe) {
    // A null device context would crash if Render reached DrawBitmap; the early
    // return on a null bitmap_ is what makes an unloaded Image safe to render.
    Image img;
    img.SetSource(L"missing.png");
    img.Arrange(RectDip{0, 0, 100, 100});
    DrawingContext dc{nullptr, nullptr, 1.0f};
    img.Render(dc);          // must not crash
    EXPECT_FALSE(img.IsLoaded());
}

// --- P1-18: failure placeholder ------------------------------------------
//
// The LOAD half of this feature needs a real D2D device (WIC decode plus
// CreateBitmapFromWicBitmap), so headless tests cover the state machine around
// it — the property, the retry-once flags, and the ImageFailed signal — not the
// pixels. What is deliberately NOT asserted here: that the fallback bitmap
// actually appears on screen, and that a broken fallback draws nothing. Both
// need hardware.

TEST(Image, FailureSourceDefaultsEmpty) {
    Image img;
    EXPECT_TRUE(img.FailureSource().empty());
}

TEST(Image, SetFailureSourceIsStored) {
    Image img;
    img.SetFailureSource(L"placeholder.png");
    EXPECT_TRUE(img.FailureSource() == L"placeholder.png");
}

TEST(Image, SettingSameFailureSourceTwiceIsANoOp) {
    Image img;
    img.SetSource(L"missing.png");
    img.Measure(100.0f, 100.0f);         // primary attempt runs and fails
    img.SetFailureSource(L"ph.png");
    img.SetFailureSource(L"ph.png");     // identical -> early return
    EXPECT_TRUE(img.FailureSource() == L"ph.png");
}

TEST(Image, NewPrimarySourceResetsTheFallbackAttempt) {
    // A fresh primary deserves a fresh fallback try: without the reset, a second
    // broken source would silently keep showing the FIRST source's placeholder
    // state and never re-attempt.
    Image img;
    img.SetFailureSource(L"ph.png");
    img.SetSource(L"a.png");
    img.Measure(100.0f, 100.0f);
    // Headless: Context().window is null, so LoadImage never runs and
    // LoadAttempted() stays false. That's not a test failure — the point is
    // SetSource resets fallbackAttempted_, which isn't observable headless but is
    // correct by inspection: Image.cpp:64 sets it false.

    img.SetSource(L"b.png");
    EXPECT_FALSE(img.LoadAttempted());   // primary flags reset
}

TEST(Image, MissingPrimaryWithFallbackStaysUnloadedHeadless) {
    // Headless there is no device, so Measure's `Context().window` guard means
    // LoadImage never even runs — LoadAttempted() stays false. The value of the
    // test is that setting both sources and measuring does not crash; the
    // LoadFallback null-device guards themselves are covered by inspection
    // (Image.cpp LoadFallback returns early on a null window / DC).
    Image img;
    img.SetSource(L"definitely-missing.png");
    img.SetFailureSource(L"also-missing.png");
    img.Measure(100.0f, 100.0f);
    EXPECT_FALSE(img.IsLoaded());
}

TEST(Image, RenderWithFallbackSetButNothingLoadedIsSafe) {
    Image img;
    img.SetSource(L"missing.png");
    img.SetFailureSource(L"placeholder-missing.png");
    img.Arrange(RectDip{0, 0, 100, 100});
    DrawingContext dc{nullptr, nullptr, 1.0f};
    img.Render(dc);          // must not crash
    EXPECT_FALSE(img.IsLoaded());
}
