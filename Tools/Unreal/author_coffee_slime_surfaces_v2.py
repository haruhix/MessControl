"""Author the owned coffee trail and rupture gel; never changes gameplay masks.

Uses the wet-film and seated gel techniques reviewed in Docs/VFXReferences.md.
Run only in idle MessControl. Backups and a graph report precede package edits.
"""
import datetime
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/CoffeeSlimeV2_20261015/SurfaceAuthoring.json'
BACKUP = ROOT / 'Saved/CoffeeSlimeV2/SurfaceBackup'
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
PATHS = {
    'trail': ('/Game/Cataclysm/Jelly/M_CataclysmPaintTrail', 'MCCataclysmPaintAuthor', 'mask_tile_v1'),
    'gel': ('/Game/Cataclysm/Jelly/M_CataclysmJellyGel', 'MCCataclysmJellyAuthor', 'gel_v1'),
}
REVISION = 'coffee_surfaces_v2:2'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def save_report(report):
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.row = 0

    def node(self, cls):
        item = edit.create_material_expression(self.mat, cls, -1000, self.row)
        self.row += 130
        return item

    def scalar(self, name, value):
        item = self.node(u.MaterialExpressionScalarParameter)
        item.set_editor_property('parameter_name', name)
        item.set_editor_property('default_value', value)
        item.set_editor_property('group', 'Coffee Slime')
        return item

    def vector(self, name, rgb):
        item = self.node(u.MaterialExpressionVectorParameter)
        item.set_editor_property('parameter_name', name)
        item.set_editor_property('default_value', u.LinearColor(*rgb, 1))
        item.set_editor_property('group', 'Coffee Slime')
        return item

    def custom(self, description, code, inputs, count=1):
        item = self.node(u.MaterialExpressionCustom)
        item.set_editor_property('description', description)
        item.set_editor_property('code', code)
        item.set_editor_property('output_type', getattr(u.CustomMaterialOutputType, 'CMOT_FLOAT'+str(count)))
        entries = []
        for name in inputs:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', name)
            entries.append(entry)
        item.set_editor_property('inputs', entries)
        for name, source in inputs.items():
            obj, pin = source if isinstance(source, tuple) else (source, '')
            require(edit.connect_material_expressions(obj, pin, item, name), 'Graph input '+name)
        return item

    def out(self, item, target):
        require(edit.connect_material_property(item, '', target), 'Graph output '+str(target))


def reset(mat, masked):
    for node in list(edit.get_material_expressions(mat)):
        edit.delete_material_expression(mat, node)
    require(edit.get_num_material_expressions(mat) == 0, 'Old graph nodes remain')
    mat.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED if masked else u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property('two_sided', False)
    if masked:
        mat.set_editor_property('opacity_mask_clip_value', .25)


def trail(mat):
    reset(mat, True)
    g = Graph(mat)
    uv = g.node(u.MaterialExpressionTextureCoordinate)
    wipe = g.node(u.MaterialExpressionTextureObjectParameter)
    wipe.set_editor_property('parameter_name', 'WipeMask')
    wipe.set_editor_property('texture', lib.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    wipe.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    finish = g.scalar('Finish', 0)
    size = g.scalar('WorldSize', 1200)
    coverage = g.custom('Unmodified cleanable coverage',
        'return Texture2DSampleLevel(Wipe,WipeSampler,UV,0).r*(1-saturate(Finish));',
        dict(UV=uv, Wipe=wipe, Finish=finish))
    g.out(coverage, u.MaterialProperty.MP_OPACITY_MASK)
    normal = g.custom('Same-mask wet meniscus, six centimeter shading height', '''
float e=1.0/64.0;
float x=Texture2DSampleLevel(Wipe,WipeSampler,UV+float2(e,0),0).r-Texture2DSampleLevel(Wipe,WipeSampler,UV-float2(e,0),0).r;
float y=Texture2DSampleLevel(Wipe,WipeSampler,UV+float2(0,e),0).r-Texture2DSampleLevel(Wipe,WipeSampler,UV-float2(0,e),0).r;
return normalize(float3(-x*6/max(WorldSize*e*2,1),-y*6/max(WorldSize*e*2,1),1));
''', dict(UV=uv, Wipe=wipe, WorldSize=size), 3)
    g.out(normal, u.MaterialProperty.MP_NORMAL)
    pos = g.node(u.MaterialExpressionWorldPosition)
    tint = g.vector('LiquidColor', (.24, .075, .021))
    crema = g.vector('CoffeeCrema', (.52, .245, .07))
    pigment = g.custom('World-stable warm coffee pigment and thin edge', '''
float broad=.5+.5*sin(P.x*.016+sin(P.y*.012)*1.4);
float edge=(1-smoothstep(.30,.9,Coverage))*.24;
return lerp(Tint.rgb*(.87+.13*broad),Crema.rgb,edge);
''', {'P': pos, 'Tint': (tint, 'RGB'), 'Crema': (crema, 'RGB'), 'Coverage': coverage}, 3)
    g.out(pigment, u.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Roughness', .20), u.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .5), u.MaterialProperty.MP_SPECULAR)
    g.out(g.scalar('Metallic', 0), u.MaterialProperty.MP_METALLIC)
    # A bounded, view-dependent warm sheen restores wet-film readability in
    # the enclosed arena. The world-stable breakup adds no moving opacity.
    view = g.node(u.MaterialExpressionCameraVectorWS)
    pixel_normal = g.node(u.MaterialExpressionPixelNormalWS)
    sheen = g.custom('Restrained coffee wet sheen and pigment floor', '''
float3 reflected=reflect(-normalize(View),normalize(Normal));
float glint=pow(saturate(dot(reflected,normalize(float3(.35,-.45,.82)))),42);
float breakup=.75+.25*sin(P.x*.055+P.y*.031);
return Pigment*.035+float3(.62,.35,.15)*glint*breakup*.16;
''', dict(Pigment=pigment, View=view, Normal=pixel_normal, P=pos), 3)
    g.out(sheen, u.MaterialProperty.MP_EMISSIVE_COLOR)


def gel(mat):
    reset(mat, False)
    g = Graph(mat)
    world = g.node(u.MaterialExpressionWorldPosition)
    center = g.node(u.MaterialExpressionObjectPositionWS)
    p = g.node(u.MaterialExpressionSubtract)
    require(edit.connect_material_expressions(world, '', p, 'A'), 'World position')
    require(edit.connect_material_expressions(center, '', p, 'B'), 'Object center')
    normal = g.node(u.MaterialExpressionVertexNormalWS)
    time = g.node(u.MaterialExpressionTime)
    fresnel = g.node(u.MaterialExpressionFresnel)
    fresnel.set_editor_property('exponent', 3.5)
    fresnel.set_editor_property('base_reflect_fraction', .04)
    old_base = g.vector('JellyColor', (.12, .22, .033))
    old_rim = g.vector('JellyRimColor', (.39, .56, .11))
    theme = g.scalar('CoffeeTheme', 1)
    coffee = g.vector('CoffeeColor', (.29, .095, .027))
    crema = g.vector('CoffeeCrema', (.57, .29, .09))
    base = g.custom('Coffee profile overrides legacy green runtime tint',
        'return lerp(Legacy.rgb,Coffee.rgb,saturate(Theme));',
        {'Legacy': (old_base, 'RGB'), 'Coffee': (coffee, 'RGB'), 'Theme': theme}, 3)
    rim = g.custom('Coffee crema rim profile',
        'return lerp(Legacy.rgb,Coffee.rgb,saturate(Theme));',
        {'Legacy': (old_rim, 'RGB'), 'Coffee': (crema, 'RGB'), 'Theme': theme}, 3)
    warning = g.scalar('JellyWarning', 0)
    wobble = g.scalar('JellyWobble', 2)
    hit = g.scalar('JellyHit', 0)
    color = g.custom('Warm opaque gel with retained danger timing', '''
float bands=.94+.04*sin(P.z*.065+sin(P.x*.035));
float3 gel=lerp(Base,Rim,saturate(F*.25))*bands;
float pulse=.5+.5*sin(T*17);
gel=lerp(gel,float3(.62,.25,.025),saturate(Warning)*(.38+.16*pulse));
return lerp(gel,float3(.75,.56,.27),saturate(Hit)*.45);
''', dict(P=p, Base=base, Rim=rim, F=fresnel, T=time, Warning=warning, Hit=hit), 3)
    g.out(color, u.MaterialProperty.MP_BASE_COLOR)
    deform = g.custom('Retained seated gel wobble', '''
float anchor=smoothstep(-47,-12,P.z);
float wave=sin(P.x*.071+T*4.8)*.46+sin(P.y*.093-T*3.7)*.34+sin(P.z*.066+T*5.1)*.20;
return N*wave*anchor*clamp(Wobble,0,10);
''', dict(P=p, N=normal, T=time, Wobble=wobble), 3)
    g.out(deform, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    emission = g.custom('Quiet pigment floor and warning', '''
float pulse=.5+.5*sin(T*17);
return Base*.035+Rim*(F*.025+saturate(Warning)*pulse*.09+saturate(Hit)*.08);
''', dict(Base=base, Rim=rim, F=fresnel, T=time, Warning=warning, Hit=hit), 3)
    g.out(emission, u.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.scalar('JellyRoughness', .27), u.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('JellySpecular', .35), u.MaterialProperty.MP_SPECULAR)
    g.out(g.scalar('JellyMetallic', 0), u.MaterialProperty.MP_METALLIC)


def main():
    require(ROOT.name == 'MessControl' and Path(u.Paths.get_project_file_path()).stem == 'MessControl', 'Wrong project')
    require(not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active PIE')
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    require(not dirty, 'Preserve unsaved editor packages')
    mats = {}
    report = dict(status='preflight', revision=REVISION, sources={}, assets={},
        mask_contract='Exact bilinear 64x64 WipeMask, clip .25; no UV distortion, coverage expansion or cleaning changes')
    BACKUP.mkdir(parents=True, exist_ok=True)
    for key, (path, tag, owner) in PATHS.items():
        mat = lib.load_asset(path)
        require(isinstance(mat, u.Material) and lib.get_metadata_tag(mat, tag) == owner, 'Preserve unowned '+path)
        mats[key] = mat
        disk = ROOT / ('Content/'+path.removeprefix('/Game/')+'.uasset')
        require(disk.is_file(), 'Missing saved package '+str(disk))
        backup = BACKUP / disk.name
        if not backup.exists():
            shutil.copy2(disk, backup)
        report['assets'][path] = dict(before_sha256=hashlib.sha256(disk.read_bytes()).hexdigest(), backup=str(backup))
    save_report(report)
    for key, fn in [('trail', trail), ('gel', gel)]:
        fn(mats[key])
        result = edit.recompile_material(mats[key])
        require(not result, 'Shader diagnostics: '+str(result))
        lib.set_metadata_tag(mats[key], 'MCCoffeeSurfaceRevision', REVISION)
    for key, mat in mats.items():
        require(lib.save_loaded_asset(mat, False), 'Failed saving '+key)
        path = PATHS[key][0]
        disk = ROOT / ('Content/'+path.removeprefix('/Game/')+'.uasset')
        report['assets'][path].update(after_sha256=hashlib.sha256(disk.read_bytes()).hexdigest(),
            nodes=edit.get_num_material_expressions(mat), compiled=True)
    report['status'] = 'authored'
    report['gameplay_visual_review'] = 'pending'
    report['utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save_report(report)
    print(json.dumps(report))


if __name__ == '__main__':
    main()
