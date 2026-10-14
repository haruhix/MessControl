# Cataclysm raft — final asset evidence

The separate warm wooden raft is complete. Six rounded deck planks, two log floats, two transverse supports and visible rope lashings use one original 1024² PBR atlas. Final authored, freshly imported GLB and freshly imported FBX heroes, plus all seven contact-sheet views, were visually reviewed and accepted. Wood grain, knots, solid log end caps, supports and rope assembly remain consistent across exports.

- Source: `Tools/Blender/author_cataclysm_raft.py`; SHA256 `6c813859f8a34dba77690d3f4890be31f39873871d113084aedd4b5c41290343`. Python compilation passed.
- Assets: `ArtSource/CataclysmRaft/SM_CataclysmRaft.blend`, `.glb`, `.fbx`; separate BaseColor/Roughness/Normal PNGs in `Textures/`.
- Hero evidence: `hero.png`, `fresh_glb_hero.png`, `fresh_fbx_hero.png`; fixed front/back/left/right/top/bottom and perspective views in `contact_sheet.png`.
- Fresh imports: 22,000 render triangles, one material and one UV map; zero invalid vertices, degenerate faces or missing render materials. GLB has 15,768 vertices and 8,704 attribute-seam boundary edges; FBX has 11,044 vertices and zero boundary/nonmanifold edges. The GLB counters arise from split export attributes, rather than holes; visual comparison and the closed FBX import support that interpretation.
- FBX collision: three named UCX hulls, all closed, convex and positive volume; 24 vertices/36 triangles total. Raw missing-material count 18 is recorded as `not_applicable_nonvisual_collision`, not hidden or classified as a render failure.
- Dimensions: waterline origin `(0,0,0)`; deck top +37.5 cm; intended walkable design rectangle 240 × 170 cm; visual deck 250 × 176 cm. Full rendered bounds, including floats/rope, are 277.7 × 195.6 × 67 cm.
- Rendering: Blender 5.1.0, EEVEE, AgX Medium High Contrast, exposure 0; fresh independent imports. PNG heroes 1024² and fixed views 512². Final FBX hero saved `2026-10-14T02:17:16Z`, contact sheet `2026-10-14T02:17:17Z`.

Metrics: `authored_metrics.json`, `fresh_glb_metrics.json`, `fresh_fbx_metrics.json`, `collision_metrics.json`. Final file hashes and timestamps are in `final_file_manifest.json`. Import guidance is in `ArtSource/CataclysmRaft/UnrealHandoff.md`; normal map is OpenGL +Y, requiring a single green-channel conversion for Unreal.

Unreal import, runtime capsule fit, buoyancy, collision behavior and the two-player gameplay limit are deferred. No gameplay/editor changes, Unreal build or Git actions were performed for this asset. Museum photographs were viewed only as construction analogues; all delivered geometry and texture pixels are original.
