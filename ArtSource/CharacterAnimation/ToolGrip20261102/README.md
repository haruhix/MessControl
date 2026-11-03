# Ordinary tool grip reference

Source: `C:/Users/vasil/Desktop/tools_position1.blend`, frame 43; the source file hash is recorded in `source_sha256.txt`. The source scene supplies poses for the knife (`Cube`), pickaxe (`SM_Pick`) and spray (`SM_Spray`).

The three FBX files are baked onto the 107-bone gameplay rig. Frame 0 is the reference pose and frames 1–2 hold the artist's pose; the rest frame ensures that FBX retains the gameplay bind pose. `reference.json` records the Blender transforms, `calibration.json` records the conversion to the game mesh origins and wrist attachment, and `unreal_poses.json` records the sampled Unreal local finger rotations.

The ordinary tool attachments are stored in `/Game/Data/DA_Equipment`. The right-hand finger rotations are applied in `MCToothAnimInstance.cpp` while the corresponding ordinary tool is presented. Upgraded tools and other hand interactions retain their own presentation. The export script records the original workspace paths used for this conversion and may need path adjustments when rerun elsewhere.
