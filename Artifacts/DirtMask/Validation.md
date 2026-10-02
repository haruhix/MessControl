# Player dirt mask validation

- Unreal Engine 5.8.1; current CharacterCurrent model and MI_Character.
- M_TeethGameplay compiled in the rendering editor with no errors and was saved.
- Full, partial and clean states were rendered from the same camera in PIE:
  Coffee 1.0, 0.5 and 0.0. Frames are DirtFull.png, DirtHalf.png and DirtClean.png.
- Visual review: irregular warm brown patches, a darker center and a matte
  surface; partial cleaning reduces patch coverage; zero Coffee restores enamel.
  Eyes, mouth and the blue costume retain their original appearance.
- Original Damage/HitFlash shader tail preserved exactly. Artist texture inputs,
  normals and BodyStretch remain connected.
- Saved asset index refreshed for the master and all three material instances;
  38 current snapshots, no export errors, current native build.
- Preview-only camera, pause, time dilation and material changes were discarded
  by restarting PIE. The saved map was reloaded after the temporary bone probe,
  and the spray tool was selected again.

Material defaults and compilation result: StylizedCharacterDirt.json.
Reapply through Tools/Unreal/build_stylized_player_dirt.py with PIE stopped.
