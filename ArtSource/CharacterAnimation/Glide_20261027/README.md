# Authored slippery-ground reactions

These FBXs contain only the existing gameplay armature and its 107 bones. They
do not contain a character mesh, hat, lashes, equipment, or customization meshes.
Source: the saved `C:/Users/user/Downloads/slide.blend`, SHA256
`9414109ffd1b74263dd629d7aa5f5279f37e193ecd99124a8e0d635b608cdeaf`.
The source and the unsaved interactive Blender scene were preserved.

`A_Teeth_Glide1` is the artist's attempt to recover balance (39 keys, 24 fps,
1.583333 s). A new slippery surface/movement/skid onset above 120 cm/s may play
it while movement remains controlled. Occupied hands keep their contact poses.
It never loops continuously during steady travel.

`A_Teeth_Glide2` is a failed balance attempt followed by a fall (42 keys,
1.708333 s). After at least 0.55 seconds of balancing and 0.30 seconds of severe
skidding at 260 cm/s or more, the server may enter the replicated `Slipping`
body state. It plays the complete fall, releases existing contacts through the
native input/food/grip path, locks control and moves the blocking capsule with
sweeps and decaying momentum. A 0.20-second settle is followed by the existing
finite recovery pose blend. A 6.5-second cooldown protects against repeat falls.
No HP loss, random roll or QTE is added. Unsupported ground hands off to the
native Chaos ragdoll and its existing boundary/recovery rules.

`Glide2_ContactCurves.json` records a current-hero mesh minimum-height curve
(maximum 22.348 cm) so the full authored recline can contact the ground rather
than penetrate it. The pose is aligned to the replicated supporting floor
plane. No source animation frame is trimmed and no root motion is extracted.

The adjacent facial JSONs are the exact matching saved NLA timeline samples.
Sad/Happy use the existing Frown/Smile aliases. They become `MCSlide_Mouth_*`
float curves in the AnimSequences and are blended by the existing expression
owner, with speech and hurt retaining priority. They require no mesh carrier.

After building the updated Editor C++ target, the owning task can run
`Tools/Unreal/import_glide_animations_20261027.py`. It verifies the current
`SK_CharacterGameplay` and existing `SM_Teeth_rig_Skeleton`, imports animation
only, saves the two clips, and assigns `DA_ToothAnimation.Glide/GlideStrong`.
The reflected hard references belong to the loading/prewarm dependency tree.

Validation before UE integration: exact current-hero bone names, hierarchy and
rest matrices; fresh FBX imports with zero meshes; maximum bone endpoint error
below 0.00048 cm; inspected original/current hero front, profile and facial
contact sheets. Full evidence and MP4 previews remain under
`E:/CodexWork/Batch6_20261027/Glide/`.

Added narrow tests: `MessControl.Animation.AuthoredGlide.*` cover onset, steady
travel, cooldown, separation of balance from the fall body state, release,
priority suppression, missing assets and authoritative fall preparation.
These tests and a short solo gameplay check await
the owning task's build/import. No multiplayer validation is claimed.
