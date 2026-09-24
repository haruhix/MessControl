"""Rebuild only the generated tooth Physics Asset, without reimporting the mesh or rig.
Run outside PIE after compiling the editor module. Replaces authored joint/body settings.
"""
import unreal as ue

mesh = ue.EditorAssetLibrary.load_asset('/Game/Art/Rig/SK_ToothHero')
if not mesh:
    raise RuntimeError('Import SK_ToothHero first')
asset = ue.MCPhysicsAssetBuilder.build_tooth_physics_asset(mesh)
if not asset:
    raise RuntimeError('Could not rebuild PA_ToothHero')
ue.log('MC_PHYSICS_REBUILT: saved hard joint limits and default constraint profiles')
