"""Export approved r06/UV candidate only after actual layout/checker gates."""
import bpy
import hashlib
import json
import shutil
import sys
from pathlib import Path
import numpy as np

BASE = Path('E:/DEVGAME/MessControl/ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish/Interior')
OUT = BASE/'UV_r06_Refine03_Candidate01'
FROZEN = BASE/'UV_Organic_r05_Candidate01'
SOURCE = OUT/'MC_DAY03_InteriorSource.blend'


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    assert bpy.app.background
    repair = json.loads((OUT/'uv_repair_report.json').read_text())
    checker = json.loads((OUT/'checker_review_receipt.json').read_text())
    visual = json.loads((OUT/'visual_gate.json').read_text())
    quality = json.loads((OUT/'uv_deformation_review.json').read_text())
    assert visual['pass'] and checker['source_unchanged']
    assert sha(SOURCE) == repair['source_sha256'] == checker['source_sha256'] == quality['source_sha256']
    dorsal = next(s for s in quality['surfaces'] if s['surface'] == 'dorsal')
    assert dorsal['kappa_p95'] <= 1.3 and dorsal['angle_error_degrees_p95'] <= 15
    assert dorsal['density_intrachart_spread'] <= 1.3
    assert not (OUT/'SM_DAY03_Tongue.fbx').exists(), 'Refusing overwrite'
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    objects = [bpy.data.objects[n] for n in ('SM_DAY03_Interior', 'SM_DAY03_Tongue')]
    frozen_hashes = {n: sha(FROZEN/n) for n in ['MC_DAY03_InteriorSource.blend','SM_DAY03_Interior.fbx','SM_DAY03_Tongue.fbx','MC_DAY03_Interior_Painter_Open.obj','texture_mesh_packet.npz']}
    # Preserve the unchanged cavity export byte-for-byte, rather than silently
    # creating a new FBX serialization for geometry that did not change.
    shutil.copyfile(FROZEN/'SM_DAY03_Interior.fbx', OUT/'SM_DAY03_Interior.fbx')
    tongue = objects[1]
    bpy.ops.object.select_all(action='DESELECT')
    tongue.select_set(True)
    bpy.context.view_layer.objects.active = tongue
    bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_DAY03_Tongue.fbx'), use_selection=True,
                            object_types={'MESH'}, use_mesh_modifiers=False, bake_anim=False,
                            path_mode='STRIP', axis_forward='-Y', axis_up='Z',
                            apply_unit_scale=False, apply_scale_options='FBX_SCALE_NONE',
                            mesh_smooth_type='FACE', use_tspace=False)
    allco, allfaces, alluv, allmat, parts = [], [], [], [], []
    offset = 0
    for slot, ob in enumerate(objects):
        me = ob.data
        co = np.array([v.co[:] for v in me.vertices], np.float32)
        uv = np.array([l.uv[:] for l in me.uv_layers['UV0'].data], np.float32)
        parts.append({'mesh':ob.name,'vertex_start':offset,'vertex_count':len(co),'material_slot':slot})
        allco.extend(co.tolist())
        for face in me.polygons:
            allfaces.append([int(v)+offset for v in face.vertices])
            alluv.append([uv[li].tolist() for li in face.loop_indices])
            allmat.append(slot)
        offset += len(co)
    me = bpy.data.meshes.new('R06_Painter_Export_Surface')
    me.from_pydata(allco, [], allfaces)
    for ob in objects:
        me.materials.append(ob.data.materials[0])
    uv_layer = me.uv_layers.new(name='UV0')
    for face, uvs, slot in zip(me.polygons, alluv, allmat):
        face.material_index = slot
        face.use_smooth = True
        for li, val in zip(face.loop_indices, uvs):
            uv_layer.data[li].uv = val
    me.update()
    me.calc_loop_triangles()
    co = np.array(allco, np.float32)
    lv = np.array([l.vertex_index for l in me.loops], np.int32)
    uv = np.array([l.uv[:] for l in me.uv_layers['UV0'].data], np.float32)
    tri = np.array([t.loops[:] for t in me.loop_triangles], np.int32)
    trimat = np.array([t.material_index for t in me.loop_triangles], np.int32)
    polymat = np.array(allmat, np.int32)
    normal = np.array([n.vector[:] for n in me.corner_normals], np.float32)
    packet = {k: v.copy() for k,v in np.load(FROZEN/'texture_mesh_packet.npz').items()}
    assert np.array_equal(packet['vertices'][:16386], co[:16386])
    for name, data in [('loop_vertex_indices',lv),('triangle_loop_indices',tri),('triangle_material_indices',trimat),('polygon_material_indices',polymat)]:
        assert np.array_equal(packet[name], data), name
    packet['vertices'] = co
    packet['loop_uv'] = uv
    np.savez_compressed(OUT/'texture_mesh_packet.npz', **packet)
    objname = 'MC_DAY03_Interior_Painter_Open'
    material_names = ['MC_Interior','MC_Tongue']
    with (OUT/(objname+'.obj')).open('w', newline='\n') as f:
        f.write('# Approved Refine03 geometry + dorsal UV repair, explicit loop normals\nmtllib '+objname+'.mtl\n')
        for value in co:
            f.write('v %.8f %.8f %.8f\n'%tuple(value))
        for value in uv:
            f.write('vt %.8f %.8f\n'%tuple(value))
        for value in normal:
            f.write('vn %.8f %.8f %.8f\n'%tuple(value))
        f.write('s 1\n')
        last = -1
        for face in me.polygons:
            slot = face.material_index
            if slot != last:
                f.write('g '+objects[slot].name+'\nusemtl '+material_names[slot]+'\n')
                last = slot
            f.write('f '+' '.join(str(int(lv[i])+1)+'/'+str(i+1)+'/'+str(i+1) for i in face.loop_indices)+'\n')
    (OUT/(objname+'.mtl')).write_text('newmtl MC_Interior\nKd 0.3 0.1 0.07\nnewmtl MC_Tongue\nKd 0.4 0.15 0.1\n')
    old_manifest = json.loads((FROZEN/'interior_handoff.json').read_text())
    manifest = {'parent_source': repair['parent_source'],
                'parent_source_sha256': repair['parent_source_sha256'],
                'source': str(SOURCE), 'source_sha256': sha(SOURCE),
                'status': 'EXPORTED_UV_CANDIDATE: fresh FBX identity pending; no Painter/native promotion until receipt release',
                'approval_scope': 'Root visual choice Refine03 for next native candidate; NOT final 1:1/look approval',
                'materials':material_names,'parts':parts,
                'geometry_bitexact':True,'geometry_bitexact_to_approved_refine03':True,
                'cavity_geometry_uv_bitexact_r05':True, 'cavity_fbx_sha256_unchanged':sha(OUT/'SM_DAY03_Interior.fbx'),
                'UV_records':old_manifest['UV_records'][:1]+[{'mesh':'SM_DAY03_Tongue','logical_charts':['ventral','dorsal'],'ventral_uv_unchanged':True,'dorsal_repair':repair['method'],'technical_metrics':dorsal}],
                'anatomy_weights':old_manifest['anatomy_weights'],
                'anatomy_weight_semantics':old_manifest['anatomy_weight_semantics'],
                'texture_resolution':4096,'pixel_gap_target':32,
                'origins':old_manifest['origins'],
                'character_contact_anchor':old_manifest['character_contact_anchor'],
                'assembly_contract':old_manifest['assembly_contract'],
                'texture_transfer':'Same vertex/loop order; see dorsal_uv_transfer.npz for authored paint reprojection. Re-bake tongue mesh maps; cavity geometry/UV/maps unchanged.',
                'unchanged_cavity_parent':str(FROZEN),'frozen_r05_hashes':frozen_hashes}
    manifest['assembly_contract']['runtime_character_lower_readability'] = 'Root owns native nine-pose/character verification. Geometry exact to approved Refine03; r05 cavity unchanged.'
    (OUT/'interior_handoff.json').write_text(json.dumps(manifest, indent=2))
    assert sha(SOURCE) == repair['source_sha256']
    assert all(sha(FROZEN/name) == digest for name,digest in frozen_hashes.items())
    files = ['MC_DAY03_InteriorSource.blend','SM_DAY03_Interior.fbx','SM_DAY03_Tongue.fbx',objname+'.obj',objname+'.mtl','texture_mesh_packet.npz','dorsal_uv_transfer.npz','interior_handoff.json']
    receipt = {'status':'EXPORTED_PENDING_FRESH_IMPORT_QA','files':{n:sha(OUT/n) for n in files},
               'approved_geometry_exact':True,'frozen_parents_unchanged':True,'cavity_fbx_byteexact':True,
               'cavity_parent_sha256':frozen_hashes['SM_DAY03_Interior.fbx'],'texture_rebake_required':True}
    (OUT/'producer_handoff_receipt.json').write_text(json.dumps(receipt, indent=2))
    print('R06_EXPORT_PACKET', json.dumps(receipt))


if __name__ == '__main__':
    main()
