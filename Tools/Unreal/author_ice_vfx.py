"""Author isolated, bounded cold-event art in the running Unreal Editor.

Run after compiling MCIceVFXAssetLibrary with PIE stopped. Only packages below
/Game/Gameplay/Cold/VFX are saved; other open projects and dirty art survive.
Existing authored graphs/systems are preserved unless owned material rebuilding
is explicitly requested. No marketplace pack or additional plugin is required.
"""
import json
from pathlib import Path
import unreal as u

FOLDER = "/Game/Gameplay/Cold/VFX"
OWNER_TAG = "MC.IceVFX.Authoring"
VERSION = "1"
SYSTEM_NAMES = (
    "NS_IceWindWisps", "NS_IceWindSnow", "NS_IceImpactMist",
    "NS_IceShardBurst", "NS_IceChargeMotes",
)

NOISE = r"""
struct ColdNoise {
    float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
    float value(float2 p) {
        float2 i=floor(p), f=frac(p); f=f*f*(3-2*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),
                    lerp(hash(i+float2(0,1)),hash(i+float2(1,1)),f.x),f.y);
    }
    float fbm(float2 p) { return value(p)*.65+value(p*2.07+17.4)*.25+value(p*4.1-9.2)*.10; }
};
ColdNoise N;
"""


def main(rebuild_owned_materials=False):
    level = u.get_editor_subsystem(u.LevelEditorSubsystem)
    if level and level.is_in_play_in_editor():
        raise RuntimeError("Stop PIE before cold VFX authoring")
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    lib.make_directory(FOLDER)
    dirty_before = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty_before += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if any(p.get_name().startswith(FOLDER + "/") for p in dirty_before):
        raise RuntimeError("Preserve unsaved cold VFX packages before authoring")
    reports = []

    def save(asset):
        if not lib.save_loaded_asset(asset, only_if_is_dirty=True):
            raise RuntimeError("Could not save owned cold VFX asset " + asset.get_path_name())

    def node(mat, kind, x=0, y=0, **props):
        expression = edit.create_material_expression(mat, kind, x, y)
        expression.set_editor_properties(props)
        return expression

    def scalar(mat, name, value):
        return node(mat, u.MaterialExpressionScalarParameter, -850, 600,
                    parameter_name=name, default_value=value, group="Cold VFX")

    def vector(mat, name, color):
        return node(mat, u.MaterialExpressionVectorParameter, -850, -300,
                    parameter_name=name, default_value=u.LinearColor(*color), group="Cold VFX")

    def custom(mat, description, code, inputs, output=u.CustomMaterialOutputType.CMOT_FLOAT1):
        expression = node(mat, u.MaterialExpressionCustom, -300, 0,
                          description=description, code=code, output_type=output)
        entries = []
        for name in inputs:
            entry = u.CustomInput()
            entry.set_editor_property("input_name", name)
            entries.append(entry)
        expression.set_editor_property("inputs", entries)
        for name, (source, pin) in inputs.items():
            if not edit.connect_material_expressions(source, pin, expression, name):
                raise RuntimeError("Cannot connect cold material input " + name)
        return expression

    def output(mat, field, particles=True, depth=12):
        color = custom(mat, "Cold emissive color", "return Field.rgb;", {"Field": (field, "")},
                       u.CustomMaterialOutputType.CMOT_FLOAT3)
        alpha = custom(mat, "Cold soft opacity", "return saturate(Field.a);", {"Field": (field, "")})
        if particles:
            position = node(mat, u.MaterialExpressionWorldPosition, -900, 750)
            camera = node(mat, u.MaterialExpressionCameraPositionWS, -900, 900)
            alpha = custom(mat, "Fade particles close to camera", "return A*smoothstep(35,95,length(P-C));",
                           {"A": (alpha, ""), "P": (position, ""), "C": (camera, "")})
        fade = node(mat, u.MaterialExpressionDepthFade, 100, 100, fade_distance_default=depth)
        if not edit.connect_material_expressions(alpha, "", fade, "Opacity"):
            raise RuntimeError("Cannot connect depth fade")
        if not edit.connect_material_property(color, "", u.MaterialProperty.MP_EMISSIVE_COLOR):
            raise RuntimeError("Cannot connect cold emissive")
        if not edit.connect_material_property(fade, "", u.MaterialProperty.MP_OPACITY):
            raise RuntimeError("Cannot connect cold opacity")

    def particle_inputs(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate, -1100, -100)
        color = node(mat, u.MaterialExpressionParticleColor, -1100, 200)
        age = node(mat, u.MaterialExpressionDynamicParameter, -1100, 350,
                   param_names=["Normalized Age", "Unused Y", "Unused Z", "Unused W"])
        return {"UV": (uv, ""), "Color": (color, "RGBA"), "Age": (age, "")}

    def wind_wisp(mat):
        inputs = particle_inputs(mat)
        inputs["T"] = (node(mat, u.MaterialExpressionTime, -1100, 500), "")
        inputs["Opacity"] = (scalar(mat, "Opacity", .34), "")
        field = custom(mat, "Layered translucent cold wisps with moving erosion", NOISE + r"""
            float2 p=UV*2-1;
            float2 q=float2(UV.x*3.5,UV.y*6-T*1.6);
            q.x+=sin(q.y*1.3+T*.7)*.34;
            float cloud=N.fbm(q);
            float soft=pow(saturate(1-dot(p*float2(1.05,.87),p*float2(1.05,.87))),1.4);
            float tendril=smoothstep(.30,.69,cloud);
            float life=smoothstep(0,.10,Age)*(1-smoothstep(.55,1,Age));
            float alpha=soft*tendril*life*Color.a*Opacity;
            float3 tint=lerp(float3(.36,.66,.83),float3(.73,.93,1.02),cloud);
            return float4(tint*Color.rgb*1.2,alpha);
        """, inputs, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(mat, field, depth=18)

    def snow_streak(mat):
        inputs = particle_inputs(mat)
        inputs["Opacity"] = (scalar(mat, "Opacity", .72), "")
        field = custom(mat, "Sparse tapered snow streak", r"""
            float2 p=UV*2-1;
            float thin=pow(saturate(1-abs(p.x)),2.5);
            float tip=pow(saturate(1-abs(p.y)),.7);
            float life=smoothstep(0,.045,Age)*(1-smoothstep(.72,1,Age));
            float alpha=thin*tip*life*Color.a*Opacity;
            return float4(float3(.73,.93,1.06)*Color.rgb*1.35,alpha);
        """, inputs, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(mat, field, depth=9)

    def impact_mist(mat):
        texture = u.load_asset("/Game/Gameplay/VFX/T_FireSmoke")
        if not isinstance(texture, u.Texture2D):
            raise RuntimeError("Existing project smoke atlas is missing")
        inputs = particle_inputs(mat)
        uv, age = inputs["UV"][0], inputs["Age"][0]
        coords = custom(mat, "Use existing 8 by 8 dust flipbook", r"""
            float frame=floor(saturate(Age)*63);
            return (UV+float2(fmod(frame,8),floor(frame/8)))/8;
        """, {"UV": (uv, ""), "Age": (age, "")}, u.CustomMaterialOutputType.CMOT_FLOAT2)
        sample = node(mat, u.MaterialExpressionTextureSampleParameter2D, -700, -200,
                      parameter_name="SmokeAtlas", texture=texture,
                      sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if not edit.connect_material_expressions(coords, "", sample, "UVs"):
            raise RuntimeError("Cannot connect cold dust atlas")
        inputs["S"] = (sample, "RGBA")
        inputs["Opacity"] = (scalar(mat, "Opacity", .52), "")
        field = custom(mat, "White blue impact powder with soft expanding silhouette", r"""
            float2 p=UV*2-1;
            float border=1-smoothstep(.73,.99,length(p));
            float powder=saturate(pow(max(S.a-.008,0),.65)*1.45)*smoothstep(.025,.13,S.a);
            float life=smoothstep(0,.055,Age)*(1-smoothstep(.32,1,Age));
            float alpha=powder*border*life*Color.a*Opacity;
            float3 tint=lerp(float3(.27,.54,.67),float3(.77,.94,1.02),saturate(S.a*1.8));
            return float4(tint*Color.rgb,alpha);
        """, inputs, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(mat, field, depth=16)

    def charge_mote(mat):
        inputs = particle_inputs(mat)
        inputs["Opacity"] = (scalar(mat, "Opacity", .64), "")
        field = custom(mat, "Small crystalline charge glint", r"""
            float2 p=UV*2-1;
            float diamond=pow(saturate(1-abs(p.x)-abs(p.y)),1.7);
            float core=pow(saturate(1-length(p)*3),3);
            float life=smoothstep(0,.08,Age)*(1-smoothstep(.46,1,Age));
            float alpha=(diamond*.7+core*.3)*life*Color.a*Opacity;
            return float4(lerp(float3(.32,.73,1),float3(.85,1.04,1.1),core)*Color.rgb*1.7,alpha);
        """, inputs, u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(mat, field, depth=7)

    def wind_sheet(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate, -1100, -100)
        vertex = node(mat, u.MaterialExpressionVertexColor, -1100, 200)
        tint = vector(mat, "Tint", (.34,.78,1,1))
        age = scalar(mat, "Age", 0)
        flow = scalar(mat, "FlowSpeed", 1)
        opacity = scalar(mat, "Opacity", .32)
        field = custom(mat, "Cold wind sheets with animated wisps and broken edges", NOISE + r"""
            float2 q=float2(UV.x*11-Age*FlowSpeed*2.8,UV.y*3.3);
            q.y+=sin(q.x*.65+Age*.7)*.35;
            float field=N.fbm(q);
            float filament=pow(saturate(1-abs(sin(q.y*5+field*2.5))*.85),3)*.26;
            float wisp=smoothstep(.36,.73,field);
            float sides=pow(saturate(1-abs(UV.y*2-1)),1.2);
            float ends=smoothstep(0,.08,UV.x)*(1-smoothstep(.88,1,UV.x));
            float alpha=(wisp*.65+filament)*sides*ends*Alpha*Opacity;
            float3 color=lerp(Tint,float3(.72,.94,1.06),field*.40)*VertexRGB;
            return float4(color*1.2,saturate(alpha));
        """, {"UV": (uv, ""), "VertexRGB": (vertex, ""), "Alpha": (vertex, "A"), "Tint": (tint, "RGB"),
              "Age": (age, ""), "FlowSpeed": (flow, ""), "Opacity": (opacity, "")},
              u.CustomMaterialOutputType.CMOT_FLOAT4)
        output(mat, field, particles=False, depth=22)

    def shard(mat):
        position = node(mat, u.MaterialExpressionWorldPosition, -1100, 100)
        normal = node(mat, u.MaterialExpressionVertexNormalWS, -1100, 300)
        view = node(mat, u.MaterialExpressionCameraVectorWS, -1100, 500)
        particle = node(mat, u.MaterialExpressionParticleColor, -1100, 700)
        tint = vector(mat, "Tint", (.065,.34,.53,1))
        color = custom(mat, "Faceted blue splinter with small crystalline fractures", r"""
            float rim=pow(1-saturate(abs(dot(normalize(N),normalize(V)))),3);
            float fissure=pow(saturate(.5+.5*sin(P.x*.23+P.y*.19+sin(P.z*.13))),24);
            return lerp(Tint,float3(.60,.86,.98),saturate(rim*.62+fissure*.22))*Particle;
        """, {"P": (position, ""), "N": (normal, ""), "V": (view, ""), "Tint": (tint, "RGB"),
              "Particle": (particle, "RGB")},
              u.CustomMaterialOutputType.CMOT_FLOAT3)
        for expr, prop in ((color,u.MaterialProperty.MP_BASE_COLOR),
                           (scalar(mat,"Roughness",.17),u.MaterialProperty.MP_ROUGHNESS),
                           (scalar(mat,"Specular",.68),u.MaterialProperty.MP_SPECULAR)):
            if not edit.connect_material_property(expr, "", prop):
                raise RuntimeError("Cannot connect shard material output")

    def material(name, build, particles=False, opaque=False):
        path = FOLDER + "/" + name
        asset = u.load_asset(path)
        created = asset is None
        if created:
            asset = tools.create_asset(name, FOLDER, u.Material, u.MaterialFactoryNew())
            lib.set_metadata_tag(asset, OWNER_TAG, VERSION)
        if not isinstance(asset, u.Material):
            raise RuntimeError("Unexpected material type at " + path)
        rebuild = rebuild_owned_materials and lib.get_metadata_tag(asset, OWNER_TAG) == VERSION
        if created or rebuild:
            if rebuild:
                edit.delete_all_material_expressions(asset)
            asset.set_editor_properties(dict(
                blend_mode=u.BlendMode.BLEND_OPAQUE if opaque else u.BlendMode.BLEND_TRANSLUCENT,
                shading_model=u.MaterialShadingModel.MSM_DEFAULT_LIT if opaque else u.MaterialShadingModel.MSM_UNLIT,
                two_sided=not opaque))
            if particles:
                edit.set_base_material_usage(asset, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
            if opaque:
                edit.set_base_material_usage(asset, u.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
            build(asset)
            edit.layout_material_expressions(asset)
        errors = edit.recompile_material(asset)
        if errors:
            raise RuntimeError("Cold material compile failed " + path + ": " + str(errors))
        save(asset)
        reports.append(dict(path=asset.get_path_name(), created=created, rebuilt=bool(rebuild),
                            compile_errors=errors or []))
        return asset

    material("M_IceWindWisp", wind_wisp, particles=True)
    material("M_IceSnowStreak", snow_streak, particles=True)
    material("M_IceImpactMist", impact_mist, particles=True)
    material("M_IceChargeMote", charge_mote, particles=True)
    material("M_IceWindSheet", wind_sheet)
    shard_material = material("M_IceShard", shard, opaque=True)

    source = Path(u.Paths.project_dir()) / "ArtSource/VFX/Ice/SM_IceShard.obj"
    if not source.is_file():
        raise RuntimeError("Reproducible faceted shard source is missing: " + str(source))
    mesh_path = FOLDER + "/SM_IceShard"
    mesh = u.load_asset(mesh_path)
    imported = mesh is None
    if imported:
        task = u.AssetImportTask()
        task.set_editor_properties(dict(filename=str(source), destination_path=FOLDER,
            destination_name="SM_IceShard", automated=True, replace_existing=False, save=False))
        options = u.FbxImportUI()
        options.set_editor_properties(dict(import_mesh=True, import_materials=False, import_textures=False,
            import_as_skeletal=False, mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH))
        data = options.get_editor_property("static_mesh_import_data")
        data.set_editor_properties(dict(auto_generate_collision=False, generate_lightmap_u_vs=False,
            normal_import_method=u.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS,
            convert_scene=False, convert_scene_unit=False,
            import_rotation=u.Rotator(0,0,0), import_uniform_scale=1.0))
        task.set_editor_property("options", options)
        tools.import_asset_tasks([task])
        mesh = u.load_asset(mesh_path)
    if not isinstance(mesh, u.StaticMesh):
        raise RuntimeError("Cold shard mesh import failed")
    if imported:
        mesh.set_material(0, shard_material)
        lib.set_metadata_tag(mesh, OWNER_TAG, VERSION)
        save(mesh)
    box = mesh.get_bounding_box()
    bounds_min = [box.min.x, box.min.y, box.min.z]
    bounds_max = [box.max.x, box.max.y, box.max.z]
    dimensions = [high-low for high, low in zip(bounds_max, bounds_min)]
    dominant_axis = "XYZ"[max(range(3), key=lambda axis: dimensions[axis])]

    if not u.MCIceVFXAssetLibrary.wait_for_material_shaders():
        raise RuntimeError("Cold material shaders were incomplete or failed after the compilation barrier")
    if not u.MCIceVFXAssetLibrary.author_assets():
        raise RuntimeError("Native cold Niagara authoring failed")
    systems = []
    for name in SYSTEM_NAMES:
        system = u.load_asset(FOLDER + "/" + name)
        if not isinstance(system, u.NiagaraSystem):
            raise RuntimeError("Missing cold Niagara system " + name)
        systems.append(dict(path=system.get_path_name(),
            parameters=[dict(name=str(info.parameter_name), type=str(info.type_name))
                        for info in u.NiagaraFunctionLibrary.get_all_user_parameters(system)]))
    remaining = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    remaining += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    owned_dirty = [p.get_name() for p in remaining if p.get_name().startswith(FOLDER + "/")]
    if owned_dirty:
        raise RuntimeError("Cold VFX authoring left unsaved owned packages: " + ", ".join(owned_dirty))
    report = dict(complete=True, materials=reports, niagara=systems,
        shard=dict(path=mesh.get_path_name(), imported=imported, source=str(source), triangles=12,
                   bounds_min_cm=bounds_min, bounds_max_cm=bounds_max,
                   dimensions_cm=dimensions, dominant_axis=dominant_axis),
        budgets=dict(wind_wisp_rate=28, wind_snow_rate=42, impact_mist_burst=18,
                     shard_burst=22, charge_rate=14, system_pool_size=16),
        preserved_dirty_packages=[p.get_name() for p in dirty_before],
        additional_marketplace_assets_required=False)
    path = Path(u.Paths.project_saved_dir()) / "IceVFX/Authoring.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    u.log("MC_ICE_VFX_AUTHOR_PASS " + json.dumps(report))
    return report



if __name__ == "__main__":
    main()
