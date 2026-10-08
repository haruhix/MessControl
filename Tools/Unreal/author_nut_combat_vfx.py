"""Create bounded nut-combat materials and Niagara systems in the owned editor.

Run after the common Editor build. Re-running validates existing assets without
overwriting artist edits. Imported walnut geometry and existing fire art survive.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

FOLDER = "/Game/Gameplay/VFX/NutCombat"
WALNUT_SOURCE = "/Game/Fab/Stylized_Walnut_-_Game_Ready/stylized_walnut_game_ready/Materials/Walnut"
RUNTIME_SOURCE_TAG = "MCWalnutRuntimeSource"
OLD_CHEVRON = "abs(x-.5-abs(p.y)*.36)"
NEW_CHEVRON = "abs(x-.5+abs(p.y)*.36)"


def imported_hashes():
    content = Path(u.Paths.project_content_dir())
    folder = content / "Fab/Stylized_Walnut_-_Game_Ready"
    return {str(path.relative_to(content)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(folder.rglob("*.uasset"))}


def runtime_walnut(lib, edit, tools):
    """Preserve the imported MIC's full shader/override chain; only owned copies change."""
    before = imported_hashes()
    source = u.load_asset(WALNUT_SOURCE)
    if not isinstance(source, u.MaterialInterface):
        raise RuntimeError("The saved imported walnut material is missing")
    chain, cursor = [], source
    while isinstance(cursor, u.MaterialInstanceConstant):
        if cursor in chain or len(chain) >= 8:
            raise RuntimeError("Unexpected walnut material parent chain")
        chain.append(cursor)
        cursor = cursor.get_editor_property("parent")
    if not isinstance(cursor, u.Material):
        raise RuntimeError("The walnut parent chain has no editable base material")

    def duplicate(original, name, expected_type):
        path = FOLDER + "/" + name
        owned = u.load_asset(path)
        created = owned is None
        if created:
            owned = tools.duplicate_asset(name, FOLDER, original)
            if owned:
                lib.set_metadata_tag(owned, RUNTIME_SOURCE_TAG, original.get_path_name())
        if not isinstance(owned, expected_type) or lib.get_metadata_tag(owned, RUNTIME_SOURCE_TAG) != original.get_path_name():
            raise RuntimeError("Preserve unrelated material occupying runtime walnut path: " + path)
        return owned, created

    base, created = duplicate(cursor, "M_WalnutRuntime", u.Material)
    usage_changed = not base.get_editor_property("used_with_instanced_static_meshes")
    if usage_changed:
        base.set_editor_property("used_with_instanced_static_meshes", True)
    if created or usage_changed:
        errors = edit.recompile_material(base)
        if errors:
            raise RuntimeError("Runtime walnut base compile failed: " + str(errors))
        if not lib.save_loaded_asset(base, only_if_is_dirty=True):
            raise RuntimeError("Could not save owned runtime walnut base")
    parent, copied = base, [base.get_path_name()]
    for index, original in enumerate(reversed(chain)):
        name = "MI_WalnutRuntime" if original == source else "MI_WalnutRuntimeParent_" + str(index)
        instance, created = duplicate(original, name, u.MaterialInstanceConstant)
        if created or instance.get_editor_property("parent") != parent:
            edit.set_material_instance_parent(instance, parent)
            edit.update_material_instance(instance)
            if not lib.save_loaded_asset(instance, only_if_is_dirty=True):
                raise RuntimeError("Could not save owned runtime walnut instance")
        parent = instance
        copied.append(instance.get_path_name())

    meshes = []
    for name in ("SM_Walnut_Whole", "SM_Walnut_HalfShell", "SM_Walnut_Kernel"):
        mesh = u.load_asset("/Game/Gameplay/CoreLoop/" + name)
        if not isinstance(mesh, u.StaticMesh) or not lib.get_metadata_tag(mesh, "MCWalnutDerivation"):
            raise RuntimeError("Expected an owned isolated walnut mesh: " + name)
        changed, preserved = [], []
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            current = slot.get_editor_property("material_interface")
            if current == source:
                mesh.set_material(index, parent)
                changed.append(index)
            elif current != parent:
                preserved.append(index)
        if changed and not lib.save_loaded_asset(mesh, only_if_is_dirty=True):
            raise RuntimeError("Could not save isolated walnut material slots")
        meshes.append(dict(path=mesh.get_path_name(), migrated_slots=changed, preserved_custom_slots=preserved))
    if before != imported_hashes():
        raise RuntimeError("Runtime walnut material migration changed imported Fab packages")
    return dict(material_chain=copied, instanced_usage=True, migrated_meshes=meshes,
                imported_packages_unchanged=True, source_hashes=before)


def save_owned_walnut_collision(lib):
    """Finish mesh callbacks by saving only this event menu and its three source profiles."""
    menu = u.load_asset("/Game/Gameplay/CoreLoop/DT_NutRainMenu")
    if not isinstance(menu, u.DataTable):
        raise RuntimeError("The owned walnut collision menu is missing")
    allowed = {"/Game/Gameplay/CoreLoop/" + name for name in
               ("SM_Walnut_Whole", "SM_Walnut_HalfShell", "SM_Walnut_Kernel")}
    def object_path(value):
        # DataTable hard references export Class'/Game/Path.Object'; soft mesh
        # references export the bare object path.
        return value.split("'", 1)[1].rstrip("'") if "'" in value else value
    rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(menu))
    for row in rows:
        sources = row.get("WholeMeshes", []) + row.get("FragmentMeshes", [])
        if any(object_path(path).split(".")[0] not in allowed for path in sources):
            raise RuntimeError("Refusing to rebake custom non-walnut source in owned event menu")
    if not u.MCFoodCollisionEditorLibrary.bake_menu_collision(menu, True):
        raise RuntimeError("Owned walnut collision bake failed after material migration")
    if not u.MCFoodCollisionEditorLibrary.is_menu_current(menu):
        raise RuntimeError("Owned walnut collision is stale after material migration")
    rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(menu))
    profiles = []
    for path in sorted({object_path(path) for row in rows for path in row.get("CollisionData", [])}):
        if not path.startswith("/Game/Generated/FoodCollision/SM_Walnut_"):
            raise RuntimeError("Refusing to save an unrelated collision profile: " + path)
        profile = u.load_asset(path)
        if not isinstance(profile, u.MCFoodCollisionData):
            raise RuntimeError("Owned collision reference is missing: " + path)
        source = profile.get_editor_property("source_mesh")
        if not isinstance(source, u.StaticMesh) or source.get_path_name().split(".")[0] not in allowed:
            raise RuntimeError("Collision profile is not for an isolated walnut: " + path)
        if not lib.save_loaded_asset(profile, only_if_is_dirty=True):
            raise RuntimeError("Could not save owned collision profile: " + path)
        profiles.append(profile.get_path_name())
    # A targeted menu pre-save also removes this menu's deferred auto-bake;
    # saving inside BakeMenu's authoring guard alone does not clear that queue.
    if not lib.save_loaded_asset(menu, only_if_is_dirty=False):
        raise RuntimeError("Could not save owned walnut collision menu")
    return dict(current=True, menu=menu.get_path_name(), profiles=profiles)


def main(migrate_legacy_walnut=False):
    if u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop the owned PIE before nut combat VFX authoring")
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if dirty:
        raise RuntimeError("Preserve unsaved packages first: " + ", ".join(p.get_name() for p in dirty))
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    reports = []
    # The runtime walnut migration and targeted collision bake were completed
    # earlier. Boss VFX authoring has no reason to repeat those asset changes.
    runtime_material = runtime_walnut(lib, edit, tools) if migrate_legacy_walnut else dict(skipped=True)

    def node(mat, kind, x=0, y=0):
        return edit.create_material_expression(mat, kind, x, y)

    def custom(mat, code, inputs, output=u.CustomMaterialOutputType.CMOT_FLOAT1):
        expr = node(mat, u.MaterialExpressionCustom)
        expr.set_editor_property("code", code)
        expr.set_editor_property("output_type", output)
        entries = []
        for name in inputs:
            item = u.CustomInput()
            item.set_editor_property("input_name", name)
            entries.append(item)
        expr.set_editor_property("inputs", entries)
        for name, (source, pin) in inputs.items():
            if not edit.connect_material_expressions(source, pin, expr, name):
                raise RuntimeError("Cannot connect material input " + name)
        return expr

    def scalar(mat, name, value):
        expr = node(mat, u.MaterialExpressionScalarParameter, -700, 300)
        expr.set_editor_property("parameter_name", name)
        expr.set_editor_property("default_value", value)
        return expr

    def output(expr, prop, pin=""):
        if not edit.connect_material_property(expr, pin, prop):
            raise RuntimeError("Cannot connect material output " + str(prop))

    def material(name, build, particles=False):
        path = FOLDER + "/" + name
        mat = u.load_asset(path)
        created = mat is None
        if created:
            mat = tools.create_asset(name, FOLDER, u.Material, u.MaterialFactoryNew())
            mat.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
            mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
            mat.set_editor_property("two_sided", True)
            if particles:
                mat.set_editor_property("used_with_niagara_sprites", True)
            build(mat)
        if not isinstance(mat, u.Material):
            raise RuntimeError("Unexpected VFX material type: " + path)
        migrated = False
        if name == "M_NutTelegraph" and not created:
            # Only the prior generated corridor expression receives this saved
            # migration. Other artist expressions and parameters survive reruns.
            for expression in edit.get_material_expressions(mat):
                if not isinstance(expression, u.MaterialExpressionCustom):
                    continue
                code = expression.get_editor_property("code")
                if OLD_CHEVRON in code and "float corridor=(edge+arrow*.24+.045)" in code:
                    expression.set_editor_property("code", code.replace(OLD_CHEVRON, NEW_CHEVRON))
                    migrated = True
        errors = edit.recompile_material(mat)
        if errors:
            raise RuntimeError("Material compile failed: " + path + ": " + str(errors))
        # RecompileMaterial marks even an existing material dirty. Its graph is
        # preserved above; save this owned compilation result so reruns stay clean.
        if not lib.save_loaded_asset(mat, only_if_is_dirty=True):
            raise RuntimeError("Could not save " + path)
        reports.append(dict(path=mat.get_path_name(), created=created, chevron_migrated=migrated, compile_errors=errors or []))

    def telegraph(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate, -1000, 0)
        alpha = node(mat, u.MaterialExpressionVertexColor, -1000, 200)
        tint = node(mat, u.MaterialExpressionVectorParameter, -700, -200)
        tint.set_editor_property("parameter_name", "Tint")
        tint.set_editor_property("default_value", u.LinearColor(1, .42, .055, 1))
        age, progress = scalar(mat, "Age", 0), scalar(mat, "Progress", 0)
        line, impact = scalar(mat, "IsLine", 0), scalar(mat, "Impact", 0)
        field = custom(mat, r'''
            float2 p=UV*2-1;
            float d=length(p);
            float ring=smoothstep(.78,.84,d)*(1-smoothstep(.91,.98,d));
            float rim=ring*(.75+.25*sin(atan2(p.y,p.x)*12-Age*5));
            float inner=(1-smoothstep(.71,.84,d))*.07;
            float scan=pow(saturate(1-abs(d-frac(Age*.6))/.09),2)*.20;
            float arc=smoothstep(.58,.62,d)*(1-smoothstep(.66,.70,d))*.26;
            float circle=(rim+inner+scan+arc)*(1-smoothstep(.98,1,d));
            float edge=smoothstep(.72,.79,abs(p.y))*(1-smoothstep(.92,.99,abs(p.y)));
            float x=frac(UV.x*8-Age*1.1);
            float arrow=1-smoothstep(.06,.12,abs(x-.5+abs(p.y)*.36));
            float corridor=(edge+arrow*.24+.045)*(1-smoothstep(.94,1,abs(p.x)));
            float density=lerp(circle,corridor,IsLine);
            density*=lerp(1,.72,Impact)*(1+.13*sin(Age*9))*(.65+.35*Progress);
            return float4(Tint*1.65,saturate(density)*Alpha);
        ''', {"UV": (uv, ""), "Tint": (tint, "RGB"), "Alpha": (alpha, "A"),
              "Age": (age, ""), "Progress": (progress, ""), "IsLine": (line, ""), "Impact": (impact, "")},
                       u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(custom(mat, "return F.rgb;", {"F": (field, "")}, u.CustomMaterialOutputType.CMOT_FLOAT3), u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(custom(mat, "return F.a;", {"F": (field, "")}), u.MaterialProperty.MP_OPACITY)

    def shadow(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        vertex = node(mat, u.MaterialExpressionVertexColor)
        density = custom(mat, "float r=length(UV*2-1); return pow(saturate(1-r*r),3)*Alpha*.38;",
                         {"UV": (uv, ""), "Alpha": (vertex, "A")})
        black = node(mat, u.MaterialExpressionConstant3Vector)
        black.set_editor_property("constant", u.LinearColor(.005, .001, .001, 1))
        output(black, u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(density, u.MaterialProperty.MP_OPACITY)

    def dust(mat):
        texture = u.load_asset("/Game/Gameplay/VFX/T_FireSmoke")
        if not texture:
            raise RuntimeError("Existing smoke flipbook is missing")
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        age = node(mat, u.MaterialExpressionParticleRelativeTime)
        particle = node(mat, u.MaterialExpressionParticleColor)
        coords = custom(mat, "float frame=floor(saturate(Age)*63); return (UV+float2(fmod(frame,8),floor(frame/8)))/8;",
                        {"UV": (uv, ""), "Age": (age, "")}, u.CustomMaterialOutputType.CMOT_FLOAT2)
        sample = node(mat, u.MaterialExpressionTextureSampleParameter2D)
        sample.set_editor_property("parameter_name", "SmokeAtlas")
        sample.set_editor_property("texture", texture)
        sample.set_editor_property("sampler_type", u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if not edit.connect_material_expressions(coords, "", sample, "UVs"):
            raise RuntimeError("Cannot connect dust flipbook UV")
        color = node(mat, u.MaterialExpressionConstant3Vector)
        color.set_editor_property("constant", u.LinearColor(.34, .18, .065, 1))
        output(color, u.MaterialProperty.MP_EMISSIVE_COLOR)
        density = custom(mat, "float border=1-smoothstep(.72,.99,length(UV*2-1)); return saturate(pow(max(S.a-.008,0),.65)*1.45)*smoothstep(.025,.13,S.a)*border*smoothstep(0,.09,Age)*(1-smoothstep(.45,1,Age))*A*.60;",
                         {"S": (sample, "RGBA"), "UV": (uv, ""), "Age": (age, ""), "A": (particle, "A")})
        fade = node(mat, u.MaterialExpressionDepthFade)
        fade.set_editor_property("fade_distance_default", 8)
        edit.connect_material_expressions(density, "", fade, "Opacity")
        output(fade, u.MaterialProperty.MP_OPACITY)

    def ember(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        age = node(mat, u.MaterialExpressionParticleRelativeTime)
        particle = node(mat, u.MaterialExpressionParticleColor)
        field = custom(mat, "float r=length(UV*2-1); float heat=pow(saturate(1-r),2); return float4(lerp(float3(1,.10,.008),float3(2,.92,.23),heat)*1.5,heat*(1-smoothstep(.25,1,Age))*A);",
                       {"UV": (uv, ""), "Age": (age, ""), "A": (particle, "A")}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(custom(mat, "return F.rgb;", {"F": (field, "")}, u.CustomMaterialOutputType.CMOT_FLOAT3), u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(custom(mat, "return F.a;", {"F": (field, "")}), u.MaterialProperty.MP_OPACITY)

    def arcane(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        vertex = node(mat, u.MaterialExpressionVertexColor)
        tint = node(mat, u.MaterialExpressionVectorParameter)
        tint.set_editor_property("parameter_name", "Tint")
        tint.set_editor_property("default_value", u.LinearColor(.56, .19, 1, 1))
        age, progress, beam = scalar(mat, "Age", 0), scalar(mat, "Progress", 0), scalar(mat, "Beam", 0)
        field = custom(mat, r'''
            float2 p=UV*2-1; float r=length(p);
            float angle=atan2(p.y,p.x)/6.283185;
            float rim=smoothstep(.66,.70,r)*(1-smoothstep(.77,.82,r));
            float outer=smoothstep(.91,.93,r)*(1-smoothstep(.96,.99,r))*.48;
            float tick=pow(saturate(1-abs(frac(angle*12-Age*.13)-.5)*7),3);
            float glyph=tick*smoothstep(.44,.48,r)*(1-smoothstep(.60,.64,r));
            float spiral=pow(saturate(1-abs(frac(angle*3-r*1.8+Age*.35)-.5)*13),2);
            float focus=pow(saturate(1-r*2.5),4);
            float circular=(rim+outer+glyph*.75+spiral*.14+focus*.5)*(1-smoothstep(.96,1,r));
            float stream=pow(saturate(1-abs(p.y)),3)*(1-smoothstep(.88,1,abs(p.x)));
            stream*=.72+.28*sin(UV.x*20-Age*17);
            float alpha=lerp(circular,stream,Beam)*Alpha*(.68+.32*Progress);
            float3 color=lerp(Tint,float3(.62,1.15,1.22),focus*.65);
            return float4(color*2.1,saturate(alpha)*.85);
        ''', {"UV": (uv, ""), "Tint": (tint, "RGB"), "Alpha": (vertex, "A"),
              "Age": (age, ""), "Progress": (progress, ""), "Beam": (beam, "")},
                       u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(custom(mat, "return F.rgb;", {"F": (field, "")}, u.CustomMaterialOutputType.CMOT_FLOAT3), u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(custom(mat, "return F.a;", {"F": (field, "")}), u.MaterialProperty.MP_OPACITY)

    def arcane_spark(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        age = node(mat, u.MaterialExpressionParticleRelativeTime)
        particle = node(mat, u.MaterialExpressionParticleColor)
        field = custom(mat, r'''
            float2 p=UV*2-1; float r=length(p);
            float diamond=pow(saturate(1-abs(p.x)-abs(p.y)),2);
            float core=pow(saturate(1-r*2.4),3);
            float alpha=(diamond*.65+core*.7)*smoothstep(0,.06,Age)*(1-smoothstep(.3,1,Age))*A;
            return float4(lerp(float3(.50,.14,1.2),float3(.42,1.2,1.3),core)*2.1,saturate(alpha));
        ''', {"UV": (uv, ""), "Age": (age, ""), "A": (particle, "A")}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(custom(mat, "return F.rgb;", {"F": (field, "")}, u.CustomMaterialOutputType.CMOT_FLOAT3), u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(custom(mat, "return F.a;", {"F": (field, "")}), u.MaterialProperty.MP_OPACITY)

    def fire_core(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        vertex = node(mat, u.MaterialExpressionVertexColor)
        age = scalar(mat, "Age", 0)
        field = custom(mat, r'''
            float2 p=UV*2-1; float r=length(p);
            float hot=pow(saturate(1-r*1.75),2);
            float halo=pow(saturate(1-r),3);
            float ripple=(.9+.1*sin(r*28-Age*18));
            float alpha=(hot*.82+halo*.28)*ripple*Alpha;
            return float4(lerp(float3(1.8,.38,.025),float3(3.1,2.35,.8),hot),saturate(alpha));
        ''', {"UV": (uv, ""), "Age": (age, ""), "Alpha": (vertex, "A")}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(custom(mat, "return F.rgb;", {"F": (field, "")}, u.CustomMaterialOutputType.CMOT_FLOAT3), u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(custom(mat, "return F.a;", {"F": (field, "")}), u.MaterialProperty.MP_OPACITY)

    def storm(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        vertex = node(mat, u.MaterialExpressionVertexColor)
        age = scalar(mat, "Age", 0)
        field = custom(mat, r'''
            float2 p=UV*2-1; float r=length(p);
            float wisps=sin(UV.x*15+Age*.6)*sin(UV.y*11-Age*.4);
            wisps+=sin((UV.x+UV.y)*22-Age*.7)*.35;
            float cloud=(1-smoothstep(.48,1,r))*saturate(.55+wisps*.3);
            float rim=pow(saturate(1-abs(r-.7)/.16),2)*.15;
            return float4(float3(.15,.08,.29)+float3(.22,.35,.48)*rim,
                          saturate(cloud+rim)*Alpha*.55);
        ''', {"UV": (uv, ""), "Age": (age, ""), "Alpha": (vertex, "A")}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(custom(mat, "return F.rgb;", {"F": (field, "")}, u.CustomMaterialOutputType.CMOT_FLOAT3), u.MaterialProperty.MP_EMISSIVE_COLOR)
        output(custom(mat, "return F.a;", {"F": (field, "")}), u.MaterialProperty.MP_OPACITY)

    material("M_NutTelegraph", telegraph)
    material("M_NutSoftShadow", shadow)
    material("M_NutDust", dust, True)
    material("M_NutEmber", ember, True)
    material("M_NutArcane", arcane)
    material("M_NutArcaneSpark", arcane_spark, True)
    material("M_NutFireCore", fire_core)
    material("M_NutStorm", storm)
    if not u.MCNutCombatEffect.author_niagara_assets():
        raise RuntimeError("Native bounded Niagara asset authoring failed")
    collision = save_owned_walnut_collision(lib) if migrate_legacy_walnut else dict(skipped=True)
    systems = []
    for name in ("NS_NutDustImpact", "NS_NutFireImpact", "NS_NutEmberTrail", "NS_NutArcaneBurst",
                 "NS_NutCastCharge", "NS_NutMotionDust", "NS_NutStormMotes", "NS_NutShieldHit"):
        asset = u.load_asset(FOLDER + "/" + name)
        if not isinstance(asset, u.NiagaraSystem):
            raise RuntimeError("Missing authored Niagara system: " + name)
        systems.append(asset.get_path_name())
    remaining = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    remaining += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if remaining:
        raise RuntimeError("VFX authoring left unsaved packages: " + ", ".join(p.get_name() for p in remaining))
    report = dict(complete=True, materials=reports, niagara=systems, runtime_walnut=runtime_material, collision=collision,
                  budgets=dict(dust_burst=24, fire_burst=32, ember_rate=36, arcane_burst=28,
                               charge_rate=24, motion_dust_rate=28, storm_rate=24, shield_burst=18,
                               max_active_bursts_per_cue=8, max_visual_rain_drops=24,
                               max_cloud_panels=5, max_storm_streaks=24, projectile_wake_segments=12))
    report_path = Path(u.Paths.project_saved_dir()) / "NutCombat" / "VFXAuthoring.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_NUT_COMBAT_VFX_AUTHOR_PASS " + json.dumps(report))
    return report


if __name__ == "__main__":
    main()
