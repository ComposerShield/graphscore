# Historical Pass Summaries (passes 4–6)

Preserved for provenance. Current state is in `../README.md`.

## Pass 6 (2026-07-19)

### 1. Pinned Noto Sans Latin Text Font
- Added Noto Sans Regular from notofonts/noto-fonts at immutable SHA `ffebf8c1...`
- Downloaded via `file(DOWNLOAD)` with `EXPECTED_HASH SHA256=d78a4640...`
- Removed all system-font fallback (Helvetica `/System/Library/Fonts/Helvetica.ttc`)
- Added deterministic HarfBuzz metric assertions: glyph count, GID non-zero, UPEM-scaled advances
- Added golden pixel hash assertion
- Bravura/BravuraText remain exclusively for SMuFL music glyph use

### 2. Green Standard CTest
- `WILL_FAIL` for headless smoke failure
- Display smoke PENDING

### 3. Documentation and Evidence Reconciliation
- 298 tests; old hash `0x468ffc4d78e9d2fc` references eliminated

---

## Pass 5 (2026-07-19)

### Face-Correct Shaping and Rasterization
- `FontManager` used distinct `FT_Face` handles for text (BravuraText.otf) and music (Bravura.otf)
- `RasterizeGlyphByIndex` takes explicit `FontFace` parameter
- `ShapeAndCacheText` calls `RasterizeGlyphInternal(m_textFace, ...)` directly

### Non-Null Rasterizer Glyph Cache
- `Rasterizer` constructor requires `const std::vector<GlyphBitmap>&`

### True Manhattan Fillets
- `ComputeFilletArc` helper, quadrant-specific pixel assertions

### Unified Physical Input Path
- `RunInputLog` routes each `SDL_Event` through same `ProcessSDLEvent`
- Source preserved honestly: Finger, PinchGesture, Unknown

### Native macOS Actions (NSAccessibility)
- Press via `NSAccessibilityPressAction`; select/zoom via `NSAccessibilityCustomAction`

### Semantic Hierarchy Integrity
- `AddNodeResult` rejects duplicates; `Reparent`/`DetachChild` atomic

### Exact Frame Proof
- Y-invert → `convertRect:toView:nil` → `convertRectToScreen:`

### Strict Smoke/Native/Report Errors
- `PresentToWindow()` returns bool; smoke returns nonzero on failure

### Evidence/Doc Reconciliation
- 289/289 tests; 4/5 CTest passed (smoke headless failure expected)

### HarfBuzz UPEM Metrics Fix
- `hb_font_set_scale(upem, upem)` → positions are font units
- Removed spurious `/64.0f`

### Signed Arc Raster Containment Fix

### Real Event-Dispatch Test Seam
- Created `viewport_event_handler.hpp/.cpp`

### Text Pixel Proof
- Used system font (Helvetica) for Latin outlines before Noto Sans pinning
- BravuraText.otf lacks Latin glyph outlines

### Exact AppKit Frame Gate
- Frame tolerances tightened to ≤1.5pt

### Accessibility Results (at pass 5 time)
- CTest: 4/5 passed (smoke headless failure expected)
- ASan+UBSan: 4/5 passed, zero findings

---

## Pass 4 (earlier)

- 179 tests; old deterministic hash `0x468ffc4d78e9d2fc`
