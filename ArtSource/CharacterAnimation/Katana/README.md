# Katana animation master

`KatanaCutVariants.blend` contains the six clips at 30 fps. The two diagonals and right-to-left horizontal cut share the same entry and exit guard. They form the ordinary attack pool. Overhead remains a separate imported clip. Heavy Carry supplies the upper-body hold while the game drives locomotion. Charged Series supplies the hold and release phases.

The blade is 186.56 cm long above its 11 cm heel. Its width increases above the collar while the handle, guard and both grip guides keep their dimensions. Carry and the shared guard raise the blade 18 degrees around the right grip, carrying both hand IK targets with it. Preparation and recovery blend into the authored cuts. The source floor clearance is at least 4 cm through the six clips.

Run `Tools/Blender/export_katana_gameplay.py` from Blender 5.2 with this master open and an explicit E: output directory. The bake keeps the gameplay gloves at their bind dimensions while placing the joints at the authored locations. Run `Tools/Unreal/import_katana_gameplay.py` in Unreal with `MC_KATANA_FBX_DIR` pointing to that directory. The import calibrates the sword mount curves against the Unreal hand transform. Run `Tools/Unreal/bind_katana_profile.py` to connect the clips to `DA_ToothAnimation` and `Tools/Unreal/validate_katana_assets.py` to check the imported poses.

In a development arena, open F3 and choose **Катана — себе**, then select slot 3. Tap primary to select one of three ordinary cuts. Hold primary for at least 0.38 s and release for the charged series. The grant lasts until respawn. It occupies the prior chainsaw upgrade ID to preserve serialized references.

During each active cut, the server follows the blade's cutting edge. The edge must reach the enemy's gameplay collision body or the food mesh. The food cut plane passes through the contact point and follows the blade's orientation. A stroke cannot cut the pieces it just created. The overhead clip remains available in the animation profile.
