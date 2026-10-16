"""Finalize an explicitly lead-approved family handoff without editing DCC data.

This is a CPU-only metadata/hash operation. It refuses to rewrite a frozen
receipt. Geometry, UVs, morphs, FBX and Painter OBJ remain byte-identical.
"""
import argparse
import hashlib
import json
from pathlib import Path


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("--handoff", required=True)
parser.add_argument("--lead-approved", action="store_true", required=True)
args = parser.parse_args()
folder = Path(args.handoff).resolve()
receipt_path = folder / "frozen_handoff_receipt.json"
if receipt_path.exists():
    raise RuntimeError("Frozen receipt already exists; do not mutate this handoff")

meta_path = folder / "unreal_mouth_handoff.json"
meta = read_json(meta_path)
day = meta["day"]
core_names = [
    f"MC_{day}_UnrealMorphSource.blend", f"SK_{day}.fbx",
    f"MC_{day}_Painter_Open.obj", f"MC_{day}_Painter_Open.mtl",
    "texture_mesh_packet.npz", "source_world_poses_cm.npz",
]
before = {name: sha(folder / name) for name in core_names}
assert sha(Path(meta["parent_source"])) == meta["parent_source_sha256"]
technical = read_json(folder / "QA_Producer/fresh_fbx_nine_pose_validation.json")
assert technical["status"] == "PRODUCER_TECHNICAL_PASS"
assert technical["source"]["sha256"] == before[f"MC_{day}_UnrealMorphSource.blend"]

status = "FROZEN_FOR_NATIVE_AND_PAINTER_HANDOFF_FINAL_UNREAL_ART_PENDING"
scope = (
    "Lead accepted shape candidate and actual UV front/oblique checkers plus both atlases. "
    "Source/fresh nine-pose producer technical gates pass; independent family gate "
    "and actual native material/motion remain pending."
)
uv_path = folder / "painter_uv_handoff.json"
uv = read_json(uv_path)
uv.update(status=status, approval_scope=scope)
write_json(uv_path, uv)
meta.update(
    status=status,
    UV_status="LEAD_ACCEPTED_SEAM_UV_FROZEN_PENDING_INDEPENDENT_REVIEW",
    final_export=True,
    final_art_accepted=False,
    approval_scope=scope,
)
write_json(meta_path, meta)
quality_path = folder / "QA_Producer/uv_quality_report.json"
quality = read_json(quality_path)
quality.update(
    independent=False,
    verification_role=(
        "Producer execution of shared UV measurement implementation; "
        "separate independent review remains pending"
    ),
)
write_json(quality_path, quality)

# Keep the original export-time receipt, explicitly labelled as historical.
# Its listed metadata hashes intentionally describe the original candidate.
producer_path = folder / "producer_handoff_receipt.json"
producer = read_json(producer_path)
producer.update(
    status="EXPORT_TIME_SNAPSHOT_SUPERSEDED_BY_FROZEN_HANDOFF_RECEIPT",
    current_authority="frozen_handoff_receipt.json",
    note="Listed hashes preserve the pre-review export snapshot; current complete hashes are in frozen_handoff_receipt.json",
)
write_json(producer_path, producer)

assert before == {name: sha(folder / name) for name in core_names}
files = {
    p.relative_to(folder).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p))
    for p in sorted(folder.rglob("*")) if p.is_file() and p != receipt_path
}
receipt = dict(
    schema="messcontrol.menu3d.frozen_family_handoff.v1",
    day=day,
    status="FROZEN_FOR_NATIVE_PAINTER_NOT_FINAL_ART",
    normal_texture_convention_expected="DirectX; physical Painter maps not produced by this handoff",
    source_units_to_native_cm=100.0,
    geometry_scale=1.0,
    materials=meta["materials"],
    root_bone=meta["root_bone"],
    morphs=meta["morphs"],
    lead_shape_source=dict(path=meta["parent_source"], sha256=meta["parent_source_sha256"]),
    preservation="All five absolute geometry/morph arrays bit-exact accepted shape; fresh seam UVs for this geometry, no other-day UV copy",
    validation=dict(
        producer="QA_Producer/fresh_fbx_nine_pose_validation.json",
        atlas="QA_Producer/uv_atlas_report.json",
        distortion_density="QA_Producer/uv_quality_report.json",
        independent_family="pending",
        Unreal_material_and_motion="pending",
    ),
    limits=[
        "No final 1:1 art acceptance claim",
        "No Painter project/maps authored by family agent",
        "No Unreal asset/gameflow changes made by family agent",
    ],
    FBX_export=dict(
        axis_forward="-Y", axis_up="Z", apply_unit_scale=False,
        apply_scale_options="FBX_SCALE_NONE", use_mesh_modifiers=False,
        add_leaf_bones=False, use_armature_deform_only=True, bake_anim=False,
        mesh_smooth_type="FACE", use_tspace=False,
    ),
    native_import=dict(
        convert_scene=False, convert_scene_unit=False, force_front_x_axis=False,
        import_morph_targets=True, import_materials=False, import_textures=False,
        import_animations=False, create_physics_asset=False,
        normal_import_method="IMPORT_NORMALS",
    ),
    files=files,
)
write_json(receipt_path, receipt)
for rel, record in files.items():
    assert sha(folder / rel) == record["sha256"]
print(json.dumps(dict(receipt=str(receipt_path), count=len(files), core=before), indent=2))
