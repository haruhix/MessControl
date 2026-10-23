# Cosmetic examples

The cap comes from the user's `C:/Users/user/Downloads/Character.blend`, copied to
`E:/CodexWork/HubSystems_20261023/Cosmetics/Character_Source.blend` for isolated
authoring. The original file and the dirty live menu scene were not modified.

`SM_Cosmetic_WorkCap.fbx` supplies `Hat_WorkCap` and `Hat_WorkCapRed` through two
material sets. `SM_Cosmetic_SportGlasses.fbx` supplies `Face_SportGlasses`; its
open lenses keep pupils and eyelids visible. Both are static accessories on the
current `head_x`, with no collision or physics. These are fitting examples for
the new customization system, not replacements for the character art.

The 4006-vertex, 107-bone saved gameplay character remains unchanged. Accessory
vertices retain the same reference mesh space. Runtime converts the attachment
through the actual Unreal reference bone transform, rather than a guessed Euler
offset. Adding another category uses another `Slot` identifier in the table.

The cap preserves the artist's original proportions, placement and backwards
brim. Its small crown is intentional; it is not stretched to cover both lobes
of the tooth. The former X-only 1.9 fit and extra 180-degree turn were removed.
All 1042 source vertices and polygon smooth flags are retained after uniform
unit conversion. The source and runtime tooth crown width/top already match,
so no additional size or position correction is needed.

Reproduce in a separate factory-startup Blender process; the exporter appends
only the needed source cap and never changes the dirty live Blender scene:

```powershell
$env:TEMP='E:/CodexTemp/HubSystems_20261023/Cosmetics'
$env:TMP=$env:TEMP
& D:/BLENDER_TEST/blender.exe --background --factory-startup --disable-autoexec --threads 2 --python-exit-code 1 --python E:/DEVGAME/MessControl/Tools/Blender/Cosmetics/export_character_cosmetics.py
& D:/BLENDER_TEST/blender.exe --background --factory-startup --disable-autoexec --threads 2 --python-exit-code 1 --python E:/DEVGAME/MessControl/Tools/Blender/Cosmetics/validate_cosmetic_exports.py
```

`CosmeticsReport.json` records source/rig hashes, unchanged skin/rig counts, part
triangles and expected dimensions. Fresh-process FBX import validated scale and
the two material slots for each mesh. Front, profile, three-quarter, smile,
blink and head-turn fit renders are under
`E:/CodexWork/HubSystems_20261023/Cosmetics/Preview`.
That earlier preview set predates the cap shape repair. Current source/body
comparisons, front/profile/three-quarter views, an overlay, vertex/edge shape
proof and fresh-FBX source comparison are in
`E:/CodexWork/HubWardrobePolish_20261023/CapRepair`.
The cap-only repair preserves the glasses FBX byte-for-byte and never saves
the original user file or the gameplay body/rig.

The available isolated Blender is 5.1.0; the new user source reports the newer
file format 502.45.
Only the inspected static cap geometry is reused. No new-source rig or character
data is adopted, and the source file is never resaved.

After an approved Editor build, root runs `Tools/Unreal/Hub/author_cosmetics.py`
and `author_wardrobe_cosmetics.py`. They back up owned existing packages, import
the examples, merge the three example rows into `/Game/Data/DT_CharacterCosmetics`
and append editable wardrobe controls without deleting existing palette/skin
widgets or graphs. Purchases/equipped ownership are handled by the personal
economy; buying an accessory does not silently equip it.

Unreal import, the named native test, appearance in solo PIE and save/travel
checks remain for the root's integrated validation. No network validation was
performed by the accessory authoring.
