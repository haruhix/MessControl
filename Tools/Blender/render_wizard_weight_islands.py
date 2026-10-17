"""Offline inspection color render only; does not save a blend."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]
report=json.loads((ROOT/'Saved/Codex/WizardWeightIslandInspection.json').read_text())
mesh=bpy.data.objects['SM_NutWizard'];rig=bpy.data.objects['rig'];scene=bpy.context.scene
rig.data.pose_position='REST'
palette=[(1,.8,.04,1),(.03,.85,.9,1),(.03,.85,.9,1),(.4,.4,.4,1),(.4,.4,.4,1),
         (1,.15,.15,1),(.18,.35,1,1),(.4,.4,.4,1),(.4,.4,.4,1),(1,.4,.03,1),(1,.4,.03,1)]
mesh.data.materials.clear()
vertex_island={v:i['id'] for i in report['islands'] for v in i['vertex_indices']}
for idx,color in enumerate(palette):
    mat=bpy.data.materials.new('DIAG_Island_'+str(idx));mat.diffuse_color=color
    mesh.data.materials.append(mat)
for face in mesh.data.polygons:
    face.material_index=vertex_island[face.vertices[0]]
scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=1000;scene.render.resolution_y=1000
scene.render.resolution_percentage=100;scene.display.shading.color_type='MATERIAL'
scene.display.shading.light='STUDIO';scene.display.shading.studio_light='paint.sl'
scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
scene.display.shading.background_type='WORLD';scene.world.color=(.07,.07,.07)
camera=scene.camera;camera.data.type='ORTHO';camera.data.ortho_scale=3.2
for label,pos in [('Front',(0,-4,.8)),('ThreeQuarter',(2.5,-4,2)),('Rear',(0,4,.8))]:
    camera.location=pos;camera.rotation_euler=(Vector((0,.12,.78))-camera.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(ROOT/'Saved/Codex'/('WizardIslands_'+label+'.png'))
    bpy.ops.render.render(write_still=True)
print('OFFLINE_ISLAND_RENDERS_COMPLETE',flush=True)
