# Buffer and watergun working reference

Buffer and watergun now transition from their existing carry poses to the two-handed working poses shown in the supplied front/back images. The reference comes from the open `Character_tools.blend` scene. Buffer's left glove and both watergun gloves are separate static objects in that scene, so their poses were recovered by rigid alignment of 106 corresponding glove vertices to the rig's bind geometry. Alignment residuals are below 0.00002 cm. Using the original posed bones alone would leave a hand near the face. The recovered wrist transforms, full glove rotations, tool transforms, and grip points are recorded in `BlenderReference.json` and `Calibration.json`.

`DA_Equipment` has separate working poses, attachment transforms, and support grips for these tools. Working grip transitions blend at 12/s. Buffer retains the calculus contact solver and adds small motor motion to the bit, body, and head. Its shoulder/body pitch vibration spans approximately 1.6 degrees and roll approximately 0.7 degrees. The ordinary pickaxe and other upgraded tools retain their existing use paths.

Watergun care mode uses `NS_WatergunMist`, derived from the existing spray Niagara system. It emits narrow, translucent blue water droplets over the existing 10 m care reach: 420 particles/s, 1900–2200 cm/s, 0.55–0.62 s lifetime, and a 7-degree cone. Smooth sphere droplets face their velocity; the liquid material adds wet highlights and lifetime fading.

The pressure shot takes the muzzle burst, narrow fast impulse, and pressure-ring shapes from the linked video's 3–6 s segment, with white/cyan water colors. A moving water slug and short foamy wake replace the static full-length beam. The watergun recoils backward 6 cm and pitches up 6 degrees, with a smaller body/shoulder kick. Recovery lasts 0.42 s; the working grip stays active through release and recovery before blending to carry. Existing charge, damage, range, and 4 s cooldown remain in the production gameplay path.

Validation:

- UE 5.8.1 native editor build succeeded, including the final recovery-state refinement.
- All 13 existing Inventory and ToolBoosters checks passed (`Tests/index.json`). The final build also passed focused Mechanics and hidden-tool hand-contact checks (`TestsFinal/index.json`). Existing engine PhysicsControl warnings occur in some gameplay fixtures.
- Live single-player pose, grip, vibration, care spray, charged shot, recoil, and cooldown checks passed (`Runtime.json`). Working tool poses match the reference within 0.7 cm/0.6 degrees for vibrating Buffer and within 0.001 cm for stationary watergun. Both hand contacts remain within 0.001 cm after settling. The measured recoil sample moved the tool backward 6.24 cm and rotated it 5.18 degrees; the sampled peak curve was 0.864.
- Two-player PIE verified working grips, Buffer shoulder vibration, calculus removal, care Niagara activation/deactivation, and the replicated pressure-shot effect, recoil, and cooldown (`Network.json`). Both peers sampled full recoil and identical cooldown timestamps. The fixture initiates production actions on the authority pawn and checks replicated client presentation/state.
- The final Niagara material compiled without errors. Scene captures were visually inspected after the detached-glove and droplet corrections.
- Saved asset index refresh succeeded: all 1324 assets current, no export errors, C++ build current.

The editor remains open. PIE is stopped, single-player play settings and foreground performance settings are restored, and no packages are dirty. `Before/DA_Equipment.uasset` preserves the original profile.
