---
name: messcontrol-held-prop-animation
description: Author, fix, or review a held prop on the MessControl player rig. Use for grip fit, hand and wrist poses, tool or active-skill animation, and checking visible hand contact or prop clearance in Blender and Unreal.
---

# MessControl held prop animation

## Locate the correct rig and prop

- Use the current user-supplied scene and prop master when available. For an editable player rig and a worked held-tool example, inspect ArtSource/CharacterAnimation/ToothHits_20261102/Character3.blend. The armature is rig, the primary skinned mesh is SM_Character, and the working Action Slot is OBrig. Confirm the action, slot, NLA, scale and evaluated constraints in the file before editing.
- The ARP hand controls are c_hand_ik.r and c_hand_ik.l, with c_arms_pole.r and c_arms_pole.l for the elbows. The deform bones are hand.r and hand.l. On the right glove, inspect the palm group hand.r and the index and thumb chains on the actual skinned mesh. c_index1.r through c_index3.r are editable curl controls, and c_index1_base.r has a bend_all property. Check available thumb controls and the effect on the mesh before using them. The corresponding Blender gameplay export bones are hand_r and hand_l. The source-to-gameplay hand conversion for a previous tool is in Tools/Blender/export_tooth_weapon_reference.py.
- ArtSource/CharacterAnimation/ToolGrip20261102/README.md describes the ordinary knife, pickaxe and spray poses and recorded offsets. Use them as examples of the hand posture and attachment convention. Derive the new prop's grip from its own visible geometry, handle axis and contact length.

## Fit the visible glove first

1. Set the role of each hand and contact intervals. For one-handed use, compose the free hand as part of balance or recovery. Fit the prop at its authored scale in a representative held pose before refining timing.
2. Orient the handle along the visible palm and fingers, then pose the finger and thumb controls so they wrap it. Check the glove silhouette against the handle from both sides. Look for a clear opposing thumb and finger contact, a palm supporting the handle, and visible space between the grip assembly and the body. Sample several meaningful regions of evaluated skin against the actual grip surface. A nearest-vertex pair describes one point. A shared-parent pair of coincident markers describes only attachment stability.
3. Judge forearm, wrist, palm and handle together. Compare evaluated hand placement to its IK target, examine elbow and forearm twist, and adjust pole, shoulder or wrist orientation where reach limits force a distorted pose. Preserve the intended prop scale through the constraints. Inspect pose transitions around the fastest movement and recovery for rotation pops and sudden changes in the wrist bend.
4. Check clearance of the whole prop against the evaluated character mesh, including guard, collar, handle, pommel and blade for the katana. Classify contacts by part and body region. Finger compression at the grip can be intentional. Check the cheek, torso, other hand and forearm separately so grip contact never hides an unrelated collision.

## Review the result

- Inspect close views from the palm and the back of the glove, a side view showing forearm-to-wrist continuity, and a playblast from the gameplay camera. Include the main held pose, load, use, follow-through and recovery, plus in-between samples where clearance is tight. Use the existing blender-animation-contacts skill for sampled position and orientation checks, and pair its output with visible skin and clearance evidence.
- Prefer a captured in-game camera transform when validating gameplay. For Blender-only previews, derive the orbit or follow view from Docs/InventoryAndHUD.md and record the camera mode and framing used. Keep close diagnostic views separate from the gameplay angle.
- For an integrated action, use the actual mesh and attachment path in solo PIE. Verify contact after blend-in, through the action and at return to locomotion. Check interruption and attachment ownership when the change touches them. Report Blender preview, Unreal import and runtime playback according to what was actually exercised.
