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
