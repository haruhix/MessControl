# New arena integration — 2026-10-17

Latest tooth-material choice: the user requested the exact `M_ArenaTooth` asset.
It now supplies previews, mesh defaults and spawned teeth; see
[ArenaToothMaterial_20261017.md](ArenaToothMaterial_20261017.md). This supersedes
the initial `M_ArenaToothCareV10` assignment described below.

Follow-up: the separate external tissue borders now use their own geometry-derived
opacity masks. The four gum/wall components use copied surface parameters and
derived assets in `V10/Edges`; see [ArenaEdges_20261017.md](ArenaEdges_20261017.md).
The original gum material and source packages remain preserved.

The new `FromBlender10` arena in `L_Mouth` now has 12 active lower side teeth,
six on each side. The 12 upper side teeth and both front tooth rows remain
decorative. The artist's placement, gum material and imported source assets
are retained.

## Teeth and gum contact

The lower side actors are replaced with `MCArenaToothSocket` markers, numbered
1–12 from front to back, alternating left/right. Native GameMode spawns their
ordinary replicated teeth with cleaning, health, loosening and respawn reserve
behavior. `DA_RunRules.InitialArenaTeeth` is 12; `DA_ArenaTooth.GameplayMaterial`
points to `/Game/Gameplay/Arena/V10/M_ArenaToothCareV10`.

Derived meshes and the material live in `/Game/Gameplay/Arena/V10`. The material
extends the existing `M_ArenaToothCareV2`, preserving cleaning, hit, crack and
climate inputs. All 26 tooth meshes receive the material, including decorations.

The requested mask means a smooth tooth/gum junction, as clarified by the user.
The mask is baked against the actual lower/upper gum triangles at their authored
transforms. UV0 remains the original enamel UV. UV1 holds the closest gum UV;
UV2.x holds signed contact distance in world centimeters. A 40 cm color and
roughness blend samples the existing gum textures. A narrow temporal coverage
blend softens the polygon intersection. The mask follows the tooth mesh during
its ordinary motion and requires no global distance-field renderer setting,
new texture atlas or gum shader modification. `Gum Seam Width` is editable in
the new material.

The original meshes in `FromBlender10` are not edited. Actor conversion and
material assignment run through a scoped Unreal transaction. Disk backups and
the pre-edit live actor/geometry snapshots are in the evidence folder.

## Tongue

The new tongue was imported under the name `SM_Gum_Up` and already had the old
`/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft` component override. It is now the
source of `GAMEPLAY | Moving tongue`, with its exact placement and scale and the
existing `DA_Tongue` profile. A derived CPU-readable `V10/SM_TongueSurface`
supports the normal native procedural render/collision surface, pressure,
breathing and event motion. The old pressure material is assigned to both the
gameplay surface and the derived mesh's default slot.

`SM_Gum_Bot`, the upper gum `SM_Gum_Up.001`, walls and their material assignments
are retained. The user's existing unsaved edits to `MI_Gum_v2` and its three
textures are left open and unsaved; only the task's map, profiles and derived
assets are saved.

## Validation and evidence

`Artifacts/ArenaRefresh_20261017/SoloCheck.json` passes all nine checks in one
standalone solo PIE session: twelve live teeth and reserves, correct material,
14 decorative actors, the previous tongue material, six tongue floor traces,
coffee status on all teeth, genuine held Brush input removing grime from a new
mesh, and ordinary native damage accepted by all twelve teeth.

The short probe pauses directors, removes temporary food/flood actors, positions
the observer beside a tooth, restores observer HP and applies temporary coffee
and 1 HP test damage. These fixture changes exist only in PIE. The mask sum falls
from 1,044,480 to 1,044,457 through real Brush input; this checks accepted cleaning
contact rather than a full cleaning playthrough. Failed probe setup/API reads
are retained separately; the root report is the successful run.

`FinalAudit.json` confirms material compilation, three UV channels and signed
mask data for every tooth, preserved triangle counts/positions/UV0, 12 saved
sockets and rules, the correct tongue, unchanged live gum parameters and
byte-identical hashes of 35 original assets. Existing unsaved gum edits are
preserved. The probe's frame-rate settings are restored and its PIE is stopped;
the user's editor and Blender remain open.

`Overview.png`, `GumContact_Left.png`, `GumContact_Right.png` and `Brushing.png`
are genuine captures under the saved arena lighting. The enlarged enclosure is
dark under that lighting; no lighting changes are authored by this task.
`GumSeam_DiagnosticLight.png` uses one temporary neutral review light to make the
material junction readable. That light/camera is removed and never saved.

Reproduction tools: `Tools/Unreal/integrate_arena_v10.py`,
`Tools/Blender/bake_arena_v10_gum_seams.py`,
`Tools/Unreal/check_arena_v10_solo.py`, `Tools/Unreal/audit_arena_v10.py`.
The Blender bake runs in a separate factory-startup background process and
does not load/save the user's open blend. No native source, Windows build,
cook/package, multiplayer or broad automation suite is part of this change.
