# Character gameplay integration

Source: `C:\Users\user\Downloads\Telegram Desktop\Character.blend`.
The original file and the separate zombie character are not modified.
`CharacterReport.json` records the source SHA-256, weight mapping and morph list.

Build with Blender 5.1:

```powershell
& D:\BLENDER_TEST\blender.exe --factory-startup --background --disable-autoexec 'C:\Users\user\Downloads\Telegram Desktop\Character.blend' --python E:\DEVGAME\MessControl\Tools\Blender\build_character_gameplay.py
```

After building the Unreal Editor C++ target, import with the editor closed:

```powershell
& E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe E:\DEVGAME\MessControl\MessControl.uproject -run=pythonscript -script=E:\DEVGAME\MessControl\Tools\Unreal\import_character_gameplay.py -unattended -nullrhi -nop4
```

The 4006-vertex skin is rebound to the saved 107-bone Blender game rig
(108 reference bones in UE, including the armature object). Reference matrices,
physics, animation clips, character transform and bone map are retained.
Eye weights are normalized to their eye bones; authored eyelids and brows follow
the head. Other weights are mapped by bone name and normalized.

Runtime Mouth_* forms preserve authored facial motion but remove eyeball
deformation. Happy/Sad are aliased to Smile/Frown. Author_* retain the original
forms for comparison. Eyes_Scary and Eyes_delight remain available as artist
references; gameplay selects the canonical expression forms. Pupil_Dilate and
Pupil_Contract affect only the eyeballs. Their extreme coordinates preserve the
eye radius within 0.0001 cm.

The artist's closed M/B/P shape differs from Basis by less than 0.01 cm. Unreal
discards this numerical noise; the Closed viseme suppresses the other mouth
poses and uses the neutral mesh. The import validates every meaningful morph
against the recorded maximum delta rather than requiring noise-only targets.

Eyes_Blink replaces the current eyelid pose using BlinkCancel_* correctives.
The component drives each corrective with expression_weight * blink_weight.
The mouth continues its current expression while the eyelids close.

Character and bag textures are extracted from packed images. Their material
instances inherit the existing gameplay shader and BodyStretch deformation;
the main material retains Coffee, Damage and HitFlash.

Validation: `Tools/Test.ps1 -Mode Unit`, `-Mode Pupils`, and `-Mode GazeNetwork`.
Rendered pupil frames are written to `Saved/PupilFrames`.

## Held tools

The same artist file supplies `SM_Brush`, `SM_Pick` and `SM_Spray`. Export with
`Tools/Blender/export_character_tools.py`, then run
`Tools/Unreal/import_character_tools.py` after the Editor target is built.
The FBXs, nine packed texture maps and bounds/source hash report live in `Tools/`.
Unreal assets live under `/Game/Gameplay/CharacterCurrent/Tools`, with one
material slot per mesh and material instances/textures in their own subfolders.
Instances inherit `/Game/Art/Materials/MM_Standart`.

`DA_PlayerAppearance.BrushMesh` selects the new brush. Its dimensions, grip,
local +X handle axis and -Z bristle direction match the old brush; procedural
cleaning retains its `(72,0,-20)` bristle point. `DA_Equipment` selects the new
pick and treatment bottle in slots 2 and 4. The pick's longest axis remains
85 cm after scaling, with the palm on its handle. The bottle keeps the artist's
size, emits mist from its `SprayNozzle` socket above the cap and uses the
existing procedural treatment pose. The knife and water-jet upgrade continue
to use their existing assets/fallbacks.

Checks: `Tools/Test.ps1 -Mode Unit`, `Tools/TestInventory.ps1`,
`Tools/TestInventoryNetwork.ps1`, and `Tools/TestBrush.ps1 -Solo -Capture`.
The `Pickaxe` scenario in `Tools/CaptureApproval.ps1` also checks posed mesh
clearance at four headings and beside the curved enamel. Use its normal
capture time scale: its fixture rotates the actor after skeletal evaluation,
so recording with sparse simulation frames can measure the previous pose.
