# Nut Knight body volume fix

`BeforeAfter.png` compares identical head-turn poses before and after enabling
volume-preserving GPU skinning, with and without the shield.

The source uses Blender Preserve Volume. Linear skinning compressed the body
despite normal bone scale. Both meshes now use the engine's dual-quaternion
deformer, and the hit overlay is compiled for it.

Final evidence: `Verification.json`, `EditorRestored.json`, `SoloCheck.json`
(21 passing native gameplay checks), `OverlayCheck/ReactionCheck.json` (three
passing real-hit checks), and `Comparison/Comparison.json` (six same-pose pairs).
`SkinningProbe.json` reproduces the cause in Blender without changing the source.

`preview.mp4` shows the full native gameplay pass; the later OverlayCheck
captures verify the overlay usage fix. Source and protected files are preserved.
Original changed files are under `Backup`. No build/package or multiplayer test
was performed. Details: `Docs/NutKnightBodyVolume_20261017.md`.
