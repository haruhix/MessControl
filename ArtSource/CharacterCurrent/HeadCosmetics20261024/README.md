# Head cosmetics — 2026-10-24

Source: `D:/PROJECT/GAME/TEETH CONTROL/teeth/head_cosmetic.blend`.
The original is read-only and its SHA-256 is recorded in `ExportReport.json`.
Only seven authored accessories are exported; character reference fragments,
empty meshes, rig controls and unused packed textures are excluded.

| Source object | Wardrobe product | Slot |
| --- | --- | --- |
| Cube.001 | Ковбойская шляпа | Hat |
| glass_1 | Прямоугольные очки | Face |
| Plane | Перо | Hat |
| SM_Character.001 | Ресницы | Face |
| Sphere | Коническая шляпа | Hat |
| Sphere.001 | Авторская кепка | Hat |
| Sphere.002 | Спортивный шлем | Hat |

The parked cap is translated to the previously verified cap reference fit;
its proportions, topology and orientation are unchanged. Unassigned source
surfaces use neutral grey, the glasses retain their source black, and eyelashes
use a dark colour. The internal Brows asset/ID remains stable. Existing shop
cap/glasses assets and prices are retained.

Derived assets: `/Game/Gameplay/CharacterCurrent/Cosmetics/Head20261024`.
Each item has a thumbnail and a zero-price `Cosmetic` entry in `DT_HubCatalog`,
plus its `DT_CharacterCosmetics` fitting row. They are obtained through the
normal wardrobe purchase flow with no coin deduction, then equipped normally.
They are not granted automatically. Free items display `Бесплатно` / `Получить`;
the new copy also has English translations.
Seven alpha-bounds UV regions fit the new renders into the existing card frames;
the wardrobe layout and three original thumbnail regions are preserved.

Meshes are baked into the actual runtime `head_x` reference-bone space. The
exact inspected frame is retained in `ReferenceHead.json` for reproduction. The
rows use identity socket transforms with `bMeshSpaceFit=false`. This preserves
the order of head rotation and nonuniform bone scale, avoiding the shear/scale
axis error of a rotated static child fitted in mesh space.

`M_HeadCosmeticDeform` applies the exact existing body shader stretch after
bone attachment, in the player's mesh frame: height `clamp(1+S,.65,1.4)` and
width `rsqrt(height)`. Native player and fitting-room components supply S,
the body origin, quaternion and scale in custom primitive data slots 0..12.
No per-item dynamic material is created at first equip. The existing preload
roots collect both catalogs and all new meshes, materials and thumbnails.
Mesh bounds include 80 cm shader displacement padding. The fitting camera
uses visible geometry bounds without that padding so tall headwear fits.

Reproduction:

- `Tools/Blender/Cosmetics/inspect_head_cosmetics_20261024.py`
- `Tools/Blender/Cosmetics/export_head_cosmetics_20261024.py`
- `Tools/Unreal/Hub/import_head_cosmetics_20261024.py`
- `Tools/Unreal/Hub/fit_head_cosmetic_thumbnails_20261024.py`
- `Tools/Unreal/Hub/check_head_cosmetics_solo_20261024.py`

Validation evidence: `Saved/HeadCosmetics20261024`; native camera captures:
`Artifacts/HeadCosmetics20261024`. Solo checks use the real wardrobe tile
purchase delegates and authority equip RPCs. Pose stress checks deliberately
rotate/scale the fitting display head and drive material stretch to -0.35 and
+0.4; the real pawn actor tick is held for deterministic material stress tests.
The player's profile and editor play preferences are restored afterwards.
The editor map, lighting and postprocess are not modified. Native changes were
compiled into the running editor with Live Coding. No packaged build, cook,
network run or cold packaged first-use validation is claimed.

Final solo result: seven zero-cost purchases, 21 pose cases, zero measured
vertex-to-head attachment error, and 42 observed runtime shader-data samples
covering both the player and preview at stretch -0.35, 0 and +0.4. The final
card check also confirmed the corrected Eyelashes label and all seven thumbnail
UV regions in live widget instances. Hub preload completed with 777 resources
and 348 streaming targets. Existing user profile bytes, play preferences and
the original temporary compiler configuration were restored.

The walking follow-up found that restarting the editor had loaded the older
base native DLL: accessories had empty custom primitive data, so their material
could not follow the live body stretch. The existing deformation implementation
was compiled into the main `UnrealEditor-MessControl.dll` (incremental runtime
module compilation only), and the editor was restarted without unsaved packages.
The correction is no longer limited to the previous Live Coding session.

`check_head_cosmetics_walking_20261024.py` then exercised the actual player with
normal movement and animation ticks enabled, walking out and back with a real
180-degree turn. All seven accessories were covered at up to 340 cm/s; measured
head motion relative to the actor reached 5.83 cm, with zero socket attachment
error. The real locomotion stretch varied from about -0.0484 to +0.0484. All 28
observed shader data samples matched that frame's body data (maximum float
rounding difference 0.000037). This check ran after restart without Live Coding.
Evidence is in `Saved/HeadCosmetics20261024/WalkFollow`, with three gameplay
captures in `Artifacts/HeadCosmetics20261024/WalkFollow`. The current 17 profile
files and play settings were restored, retaining the player's recent purchases.
The editor returned to the original main-menu map with no dirty packages.
