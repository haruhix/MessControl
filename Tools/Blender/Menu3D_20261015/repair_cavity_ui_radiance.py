"""CPU-only bounded removal of UI-shaped inferred radiance plates.

The source UI hides real mucosa. Harmonic continuation is INFERRED radiance,
not observed source or physical albedo. No UE, DCC, geometry or PBR mutation.
"""
from pathlib import Path
import copy
import importlib.util
import json
import hashlib
import time
import cv2
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2] / 'ArtSource/UI/MainMenu3D/20261015_refB_v1'
BASE = ROOT / 'Textures/Revisions/v42_SymmetricLower_Radiance_CPU/material_binding_open_SymmetricLower_Radiance_NOT_ALBEDO.json'
OLD = ROOT / 'Textures/Revisions/v39b_Open_Radiance_NOT_ALBEDO_Tight'
OUT = ROOT / 'Textures/Revisions/v43d_Cavity_UI_ConvergedHarmonic_Radiance_CPU'
W, H, N = 963, 410, 2048
cv2.setNumThreads(2)
spec = importlib.util.spec_from_file_location('open_radiance', HERE / 'transfer_open_radiance_to_uv0.py')
mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
helper = mod.helper


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(path): return json.loads(Path(path).read_text(encoding='utf8'))


def converged_harmonic(old_linear, unknown):
    """Solve Laplace to convergence, retaining values outside hole exactly.

    CG operates only on unknowns; the exterior is a hard color constraint.
    No source brightness is converted into normals.
    """
    yy, xx = np.where(unknown)
    x0, x1 = max(0, xx.min()-4), min(W, xx.max()+5)
    y0, y1 = max(0, yy.min()-4), min(H, yy.max()+5)
    mask = unknown[y0:y1, x0:x1]
    old = old_linear[y0:y1, x0:x1].astype(np.float64)
    # Positive-definite Laplacian enforces the maximum principle. The earlier
    # thin-plate candidate overshot the red-channel boundary range, so it is
    # retained as rejected evidence rather than used in the binding.
    kernel = np.array([[0,-1,0], [-1,4,-1], [0,-1,0]], np.float64)
    def apply(values):
        full = np.zeros_like(old); full[mask] = values
        return cv2.filter2D(full, -1, kernel, borderType=cv2.BORDER_CONSTANT)[mask]
    fixed = old.copy(); fixed[mask] = 0
    b = -cv2.filter2D(fixed, -1, kernel, borderType=cv2.BORDER_CONSTANT)[mask]
    x = old[mask].copy()
    residual = b-apply(x); direction = residual.copy()
    rr = np.sum(residual*residual, axis=0)
    initial = np.sqrt(rr)
    for iteration in range(2000):
        ap = apply(direction)
        alpha = rr/np.maximum(np.sum(direction*ap, axis=0), 1e-30)
        x += alpha*direction
        residual -= alpha*ap
        next_rr = np.sum(residual*residual, axis=0)
        if np.max(np.sqrt(next_rr)) < 2e-7:
            break
        direction = residual + (next_rr/np.maximum(rr, 1e-30))*direction
        rr = next_rr
    # Physical radiance cannot be negative; no global color or gamma change.
    x = np.clip(x, 0, 1)
    result = old_linear.copy()
    result[y0:y1, x0:x1][mask] = x
    return result, {'iterations': iteration+1, 'initial_residual_l2': initial.tolist(),
                    'final_residual_l2': np.sqrt(next_rr).tolist(),
                    'outside_hole_exact': bool(np.array_equal(result[~unknown], old_linear[~unknown]))}


def main():
    if OUT.exists() and any(OUT.iterdir()): raise RuntimeError('Preserve previous candidates')
    OUT.mkdir(parents=True, exist_ok=True)
    started = time.time()
    binding = load(BASE); receipt = load(OLD/'open_radiance_transfer_receipt.json')
    packet_receipt = load(receipt['receipt'])
    names = packet_receipt['objects']
    names = list(names)
    cache = np.load(OLD/'actual_open_camera_first_hit_UV0.npz')
    owner, depth, source_uv = cache['object_id'], cache['depth_Y'], cache['UV0']
    source = np.asarray(Image.open(mod.SOURCE).convert('RGB'))
    current = np.asarray(Image.open(OLD/'open_first_hit_UV0_Radiance_NOT_ALBEDO_CPU.png').convert('RGB'))
    excluded = np.asarray(Image.open(OLD/'Source_UI_Worker_OldShadow_NoTransfer.png').convert('L')) > 0
    yy, xx = np.mgrid[:H, :W]
    central_ui = excluded & (xx > 300) & (xx < 655) & (yy > 112) & (yy < 330)
    # Bounded fringe guard removes the retained UI antialias/shadow halo.
    guard = cv2.dilate(central_ui.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (17,17))).astype(bool)
    cavity_ids = [names.index(n) for n in ['Interior_OrganicBuccalPalate_Closed', 'Interior_BroadDorsalTongue']]
    guard &= np.isin(owner, cavity_ids)
    field, solve = converged_harmonic(helper.linear(current), guard)
    Image.fromarray(np.uint8(guard)*255).save(OUT/'QualifiedUIAndFringe_RepairMask.png')
    candidate = current.copy(); candidate[guard] = np.uint8(np.rint(helper.srgb(field[guard])*255))
    Image.fromarray(candidate).save(OUT/'source_domain_harmonic_candidate.png')
    # Side-by-side diagnostic also amplifies shadows as the observed gamma bug did.
    lifted = lambda rgb: np.uint8(np.rint(helper.srgb(np.asarray(rgb,np.float32)/255)*255))
    sheet = np.concatenate([np.concatenate([current,candidate],axis=1),
                            np.concatenate([lifted(current),lifted(candidate)],axis=1)],axis=0)
    Image.fromarray(sheet).save(OUT/'CavityPlate_before_after_and_gamma_stress.png')

    data = np.load(receipt['packet'])
    changed = {}; outputs = {}
    for name, material in [('Interior_OrganicBuccalPalate_Closed','MC_Interior'), ('Interior_BroadDorsalTongue','MC_Tongue')]:
        object_id = names.index(name)
        prefix = 'open::'+name+'::'
        triangles = data[prefix+'triangles']
        world = data[prefix+'world_vertices'][triangles]
        uv = data[prefix+'UV0_triangles']
        original = cv2.imread(binding['materials'][material]['radiance_not_albedo'], cv2.IMREAD_UNCHANGED)
        atlas = original.copy(); changed_mask = np.zeros((N,N),bool)
        occupied = np.zeros((N,N),bool)
        for ids, pixels, weights in helper.fragments(uv*[N,-N]+[0,N], N, N, 1024):
            occupied.reshape(-1)[pixels] = True
            xyz = np.einsum('ij,ijk->ik',weights,world[ids])
            projected = helper.projected(xyz)
            sx,sy = projected[:,0]-.5,projected[:,1]-.5
            valid = (sx>=0)&(sx<W-1)&(sy>=0)&(sy<H-1)
            sampled_owner = helper.remap(owner,sx[None],sy[None],True)[0]
            # The source first-hit depth is a smooth Y field inside the cavity.
            # Nearest depth introduces artificial diagonal rejection cracks on
            # steep individual triangles. Bilinear depth is the correct local
            # interpolation; still require the exact actual first-hit owner.
            sampled_depth = helper.remap(np.where(np.isfinite(depth),depth,1e8),sx[None],sy[None])[0]
            mask = helper.remap(guard,sx[None],sy[None],True)[0]>.5
            valid &= mask & (sampled_owner==object_id) & (np.abs(xyz[:,1]-sampled_depth)<.05)
            values = helper.remap(field,sx[None],sy[None])[0]
            selected = pixels[valid]
            atlas.reshape(-1,3)[selected] = np.uint16(np.rint(helper.srgb(values[valid])[:,::-1]*65535))
            changed_mask.reshape(-1)[selected] = True
        # Smart-UV seams sample the padding beside each island. Update only
        # padding whose nearest occupied texel belongs to this bounded repair;
        # leaving the old UI-fill padding causes artificial diagonal seams.
        distance,labels=cv2.distanceTransformWithLabels((~occupied).astype(np.uint8),cv2.DIST_L2,5,labelType=cv2.DIST_LABEL_PIXEL)
        ys,xs=np.where(occupied)
        colors=np.zeros((len(ys)+1,3),np.uint16); colors[1:]=atlas[ys,xs]
        changed_lookup=np.zeros(len(ys)+1,bool); changed_lookup[1:]=changed_mask[ys,xs]
        padding=(distance<=7)&~occupied&changed_lookup[labels]
        atlas[padding]=colors[labels[padding]]; changed_mask|=padding
        folder = OUT/material; folder.mkdir()
        path = folder/(material+'_Open_UIRepair_Radiance_NOT_ALBEDO_16bit.png')
        cv2.imwrite(str(path),atlas)
        Image.fromarray(np.uint8(changed_mask)*255).save(folder/'ChangedUV0Texels.png')
        if changed_mask.any():
            binding['materials'][material]['radiance_not_albedo'] = str(path)
        binding['materials'][material]['UI_repair_provenance'] = 'Converged maximum-principle harmonic inferred hidden mucosa; only source UI+8px fringe, actual visible UV0; not observed source.'
        changed[material] = {'changed_uv0_pixels':int(changed_mask.sum()),
            'outside_changed_texels_byte_exact':bool(np.array_equal(atlas[~changed_mask],original[~changed_mask])),
            'input_sha256':sha(load(BASE)['materials'][material]['radiance_not_albedo']), 'output_sha256':sha(path)}
        outputs[material] = atlas

    binding['status'] = 'CPU_CAVITY_UI_PLATE_REPAIR_PENDING_NATIVE_REVIEW'
    binding['limits'].append('Cavity UI-hidden surface has smooth inferred radiance, not an exact observed source match.')
    binding_path=OUT/'material_binding_open_CavityUIRepair_Radiance_NOT_ALBEDO.json'
    binding_path.write_text(json.dumps(binding,indent=2),encoding='utf8')
    closed=load(ROOT/'Textures/Revisions/v42_SymmetricLower_Radiance_CPU/material_binding_closed_SymmetricLower_Radiance_NOT_ALBEDO.json')
    closed['materials']['MC_Interior']=copy.deepcopy(binding['materials']['MC_Interior'])
    closed['status']='CPU_CAVITY_UI_REPAIR_CLOSED_CARRY_FORWARD_PENDING_NATIVE_REVIEW'
    (OUT/'material_binding_closed_CavityUIRepair_Radiance_NOT_ALBEDO.json').write_text(json.dumps(closed,indent=2),encoding='utf8')
    # Reproject new real UV0 atlases on the exact original first-hit packet.
    result=current.copy(); sx=(source_uv[...,0]*N-.5).astype(np.float32); sy=((1-source_uv[...,1])*N-.5).astype(np.float32)
    for material, atlas in outputs.items():
        sampled=cv2.remap(atlas[...,::-1].astype(np.float32)/65535,sx,sy,cv2.INTER_LINEAR)
        selected=np.isin(owner,[i for i,n in enumerate(names) if mod.object_material(n,packet_receipt)==material])
        result[selected]=np.uint8(np.rint(np.clip(sampled[selected],0,1)*255))
    Image.fromarray(result).save(OUT/'actual_firsthit_UV0_CavityUIRepair_CPU.png')
    Image.fromarray(result).crop((255,105,710,340)).save(OUT/'Cavity_only_repair_crop.png')
    for material,atlas in outputs.items():
        if changed[material]['changed_uv0_pixels']:
            Image.fromarray(np.uint8(np.rint(atlas[...,::-1].astype(np.float32)/257))).save(OUT/material/(material+'_Open_UIRepair_Radiance_NOT_ALBEDO_8bit.png'))
    report={'status':binding['status'],'base_binding':str(BASE),'new_binding':str(binding_path),
        'source_UI_pixels':int(central_ui.sum()),'bounded_UI_fringe_pixels':int(guard.sum()),'repair_mask_fringe_radius_source_px':8,
        'source_actual_firsthit_packet':receipt['packet'],'cavity_geometry_unchanged_since_packet':True,
        'solve':solve,'changed':changed,'other_material_paths_unchanged':True,
        'UE_DCC_Painter_GPU_used':False,'elapsed_seconds':time.time()-started,
        'review_limit':'Surrounding original first-hit enamel/lower geometry in this diagnostic is not the current symmetric v42. New binding preserves v42; only cavity proof is authoritative.',
        'cause':'Inherited finite-iteration harmonic fill carries a UI-shaped flat plate and retained fringe; converged bounded harmonic continuation is limited to qualified hidden UI+fringe. Earlier thin-plate overshoot candidate was rejected.'}
    (OUT/'cavity_repair_receipt.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__': main()
