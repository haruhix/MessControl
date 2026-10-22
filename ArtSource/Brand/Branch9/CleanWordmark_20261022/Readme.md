# Branch9 wordmark — restored A/N overlap and red 9, 2026-10-22

`Reference_Original.png` remains the byte-identical supplied 1672 × 941
raster. The original letter positions, A-over-N relationship, black divider
and overall layout are restored. The intermediate separated-N layout was
rejected and is not the current source.

- N is the eleven-vertex fitted source contour with only its visible upper
  left edge made strictly vertical at source X `714.51556`. That edge meets
  the original diagonal A separator cut at source Y `423.12298504780125`.
  The lower left diagonal and narrow visible foot remain the original
  overlap cut beneath A; they are not a separate thin complete stem.
- The diagonal cut runs through source points `[725.5436166,447.8992207]`
  and `[776.8072964,563.07101]`. The source divider is approximately 13–14
  pixels wide measured perpendicular to A's right edge. This raster source
  contains the visible pre-cut N, not hidden reconstructed geometry under A.
- B/R/A/C/H/9 retain their exact extracted coverage and original positions.
  There is no +66 shift and no added A/N spacing. All six silhouettes are
  unchanged before and after the restored shared uniform transform.
- 9 is sRGB `#E32636` in PNG and SVG. Its alpha remains unchanged. PNGs use
  straight alpha with red RGB propagated beyond the edge for clean bilinear
  filtering. The other six glyphs are white.
- The complete wordmark uses the original identical X/Y scale
  `1840 / 1487 = 1.2373907195696032`, centred on 2048 × 768. The target
  half-coverage bounding box is again `[104,226,1944,543]`, exactly 1840
  pixels wide. Maximum bounds are exclusive.

Files:

- `T_Branch9_CleanWordmark.png`: full-colour straight-alpha RGBA, 2048 × 768.
- `T_Branch9_CleanGlyphAtlas.png`: 2048 × 1024, seven 512 × 512 cells in
  reading order B, R, A, N / C, H, 9, empty. Each holds its independent
  visible glyph mask at the same pixel size as the full logo.
- `T_Branch9_Transparent.png`: unchanged transparent 16 × 16 placeholder
  suppressing the native splash's legacy branch/leaf texture fallback.
- `Branch9_CleanWordmark.svg`: seven named paths with evenodd holes. N uses
  the explicit eleven-vertex contour; other paths retain supplied contours.
- `OutputManifest.json`: current bounds, centres, atlas support, geometry,
  source positions, colours, hashes and validation.
- `BuildCleanWordmark.py`: reproducible extraction, local repair, atlas,
  SVG and validation. Requires NumPy, Pillow and OpenCV; set process
  TEMP/TMP and PYTHONPYCACHEPREFIX to E:.

Atlas pixel mapping:

`atlasPixel = fullCanvasPixel - glyph.center + glyph.atlas_center`

Divide by `[2048,1024]` for UV. Use each `atlas_support_bbox` for antialiasing.
Use this current manifest, not the intermediate separated-N mapping. Logical
X cells are timing guides, not segmentation cuts. A/N should share the same
animation transform/beat to preserve their pre-cut overlapping relationship.
Independent atlas masks reconstruct full alpha and visible RGBA exactly.
Transparent RGB can differ outside glyph coverage.

Checks cover unchanged original reference, exact retained source coverage,
red 9 RGB including nonopaque coverage, atlas/full visible RGBA identity,
source/output silhouette IoU approximately 0.999886 and SVG/revised-source
IoU approximately 0.998628. The IoUs compare against the locally revised
source; they do not claim the modified N is identical to the original.

Original-source backup:
`E:/CodexWork/Branch9CleanIntro_20261022/NRefine/BeforeSource/`.
Discarded separate-N source backup:
`E:/CodexWork/Branch9CleanIntro_20261022/NRefine/SeparateN_BeforeOverlapFix/`.
Current full-word and A/N comparisons:
`E:/CodexWork/Branch9CleanIntro_20261022/ANOverlap/AfterSource/`.

This step edits source artwork only. Unreal assets, material constants,
animations, C++ and Tools are untouched; integration remains separate.
