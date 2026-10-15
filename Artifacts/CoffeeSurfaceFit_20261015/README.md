# Coffee paint surface fit — 2026-10-15

The old coarse paint grid intersected the deforming tongue and exposed straight
polygonal cutouts. Paint now clips the tongue's actual triangles to each tile,
retains their barycentric deformation bindings and cleaning UVs, and follows
each tongue revision with a uniform 0.6 cm world-Z offset. Collision, pressure
deformation and ordinary puddles are unchanged.

The baseline observed 9 negative-clearance samples out of 3469 comparisons. The
fixed solo run passed all 18 checks: 4663 comparisons, zero intersections and a
consistent 0.6 cm clearance. Both fixed player snapshots remained Walking on a
blocking, walkable TongueSurface. The visible dent is existing weight pressure,
about 6 cm in these observations, rather than a capsule falling through the floor.

`Summary.json` contains the compact comparison; full compacted reports retain
all original telemetry. `ReportProvenance.json` records original/artifact hashes
and parsed-data equality. `BaselineBinary.json` distinguishes the old running DLL
from working source already containing the unbuilt fix. `FixedBuild.json` and
`BuildEditor.txt` identify the successful authorized Windows Editor build.

The lead opened gameplay, trail, post-rupture and brush stills. Short MP4 previews
use genuine 12 Hz captures repeated at 30 fps, without generated motion. Four
initial editor-window frames were excluded with recorded metadata; subsequent
captures use the verified game client. Continuous video playback is not claimed
as reviewed. The probe uses existing position/tool fixtures and two synchronous
geometry snapshots. Paths differ between runs. No multiplayer, GPU profiling,
cook or packaging validation was performed.

Reproduce in an idle, clean `L_Mouth` Editor with
`Tools/Unreal/probe_coffee_surface_fit_solo.py`. Its default checks the fixed
geometry; `MC_PAINT_FIT_EXPECT_CONFORMING=False` records an old-binary baseline.
Reports and frames remain separate under `Saved/CoffeeSurfaceFit`.
