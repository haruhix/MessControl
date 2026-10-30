# Active-skill geometry sources

Selected compact meshes for the six active skills. Props/{sponge,hook,grenade,air,rocket}/ retains the normalized editable Blender mesh and FBX used by the authoring workflow. SuperCar/ retains the nine-part editable car assembly plus its pivot/part handoff and fresh-import validation. SourceManifest.json records byte-exact provenance and hashes.

These are geometry masters, not replacement player characters or final authored action animations. Runtime assets live in Content/Gameplay/ActiveSkills; current procedural hand/body poses and grip frames live in C++. The original large Buster/SuperCar guide scenes and generated review movies remain local on E:.

Tools/Unreal/author_active_skill_props.py and import_supercar_update.py still expect their documented retained E:/CodexWork export folders; this checkpoint does not make those import paths portable. Re-export the car's individual parts from the Blender assembly using assembly_handoff_9.json before reauthoring its runtime meshes. Do not substitute the combined assembly FBX for the nine moving parts.

SuperCar's three external PBR textures are not supplied; the 3D artist will add them. Existing runtime materials are interim. These sources do not imply VFX or animation art approval.
