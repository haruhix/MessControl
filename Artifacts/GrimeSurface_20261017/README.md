# Plaque surface, normal and opacity controls

Implemented 2026-10-17 in `MI_ArenaToothGrime` / `M_ArenaToothRelief`.

- `04 Normal`: `GrimeNormalIntensity` controls the complete normal. Zero disables
  it; `GrimePigmentRelief` adds bump following the exact randomized pigment, and
  `GrimeNormalStrength` / `GrimeMicroRelief` control the existing micro textures.
- `02 Opacity Mask`: threshold, softness, contrast, inversion, mask strength and
  overall opacity. The default source is the pigment. Set
  `GrimeMaskTextureStrength=1` to use the `GrimeOpacityMask` texture instead.
- `05 Surface Fit`: optional additional separation, limited to 0–3 cm. Default0;
  native geometry already sits 0.55 cm above the original enamel faces.
- User's existing instance values, including size339.5200195, seed48 and warp
  0.097599998, were retained and saved. Existing tint/texture overrides survive.

The old independently projected grid crossed enamel creases. The visual layer
now subdivides the actual render mesh faces with their authored smooth normals.
The original grid still supplies the same brush/contact samples. Normal shading
no longer differentiates the vertex coverage or the cleaning-volume mask.
Existing arena meshes already enable CPU access; no mesh asset was modified.
The material authoring script is `Tools/Unreal/refine_plaque_surface.py`; run it
after earlier master/instance authoring scripts. It preserves instance overrides.

Validation: Live Coding succeeded; final material shaders compiled without errors.
One focused solo PIE run verified all12 teeth inherit the saved instance and their
meshes allow the native fit. The ordinary held brush completed in10.333334 seconds.
No direct cleaning calls or repeated pawn teleport were used by the fixture.
The existing solo fixture pauses directors and removes transient food/flood only
inside PIE; engine timing/throttle settings were restored and PIE stopped.

`Solo/SurfaceAudit.json` passed five numerical/image checks. For2048 sampled
vertices and2048 sampled triangle centroids, peeling off the0.55cm normal offset
recovers points on source enamel faces within0.001local cm (observed9.31e-9).
The target coating now has6930 vertices /4962 triangles, versus the old16641 /
28442. Normal-disabled/strong and mask-threshold probes visibly change shading
and coating; the optional reference mask follows the same stochastic mapping.
Actual default/flat/strong/mask and ordinary cleaning stills are in `Solo`.
`FitProbe` records the earlier grid diagnosis; it is not the final geometry.

`FinalAudit.json` verifies saved parent/runtime inheritance, preservation of
previous artist values, protected enamel/map/brush source, unrelated unsaved
content, engine settings and exact compiler-configuration restoration.
The saved instance is open in Unreal. No multiplayer, cook, package or GPU
profiling was requested or performed.
