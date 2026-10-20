"""Author only the isolated Frost Nova packages, with the Editor idle.

Requires the new MCFrostNovaAssetLibrary native class. Reuses the project's
existing Kenney white puff and low-poly shard; never overwrites shared ice art.
Default is create-missing/preserve-existing. Explicit rebuild_owned=True backs
up and rebuilds only materials and the burst system owned by this script.
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import unreal as u

FOLDER = '/Game/Gameplay/Cold/VFX/FrostNova'
OWNER = 'MC.FrostNova.Authoring'
VERSION = '3'
OWNED_VERSIONS = ('1', '2', VERSION)
NAMES = ('M_FrostNovaFront', 'M_FrostNovaCrystal', 'M_FrostNovaPowder', 'M_FrostNovaSnow')


def main(output='E:/CodexWork/MessControlFrostNova_20261020/Authoring', rebuild_owned=False,
         rebuild_materials=()):
    output = Path(output).resolve()
    if output.drive.upper() != 'E:':
        raise RuntimeError('Frost Nova authoring outputs must remain on E:')
    output.mkdir(parents=True, exist_ok=True)
    if any(name not in NAMES for name in rebuild_materials):
        raise RuntimeError('Only isolated Frost Nova materials can be rebuilt')
    commandlet = '-run=pythonscript' in u.SystemLibrary.get_command_line().lower()
    level = None if commandlet else u.get_editor_subsystem(u.LevelEditorSubsystem)
    if level and level.is_in_play_in_editor():
        raise RuntimeError('Stop PIE before authoring Frost Nova assets')
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    dirty_before = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty_before += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    if any(package.get_name().startswith(FOLDER + '/') for package in dirty_before):
        raise RuntimeError('Preserve unsaved Frost Nova packages before authoring')
    texture = u.load_asset('/Game/Gameplay/Cold/VFX/T_KenneyWhitePuff12')
    shard = u.load_asset('/Game/Gameplay/Cold/VFX/SM_IceShard')
    if not isinstance(texture, u.Texture2D) or not isinstance(shard, u.StaticMesh):
        raise RuntimeError('Existing Kenney white puff / owned ice shard is unavailable')
    lib.make_directory(FOLDER)
    report = dict(passed=False, status='authoring', folder=FOLDER, version=VERSION,
                  rebuilt_owned=bool(rebuild_owned), rebuilt_materials=list(rebuild_materials),
                  shared_assets_modified=False, packages=[])

    def node(mat, kind, **properties):
        expression = edit.create_material_expression(mat, kind)
        if not expression:
            raise RuntimeError('Could not create ' + kind.__name__)
        expression.set_editor_properties(properties)
        return expression

    def connect(source, output_pin, destination, input_pin):
        # UE's VertexColor has unnamed RGB and individual R/G/B/A outputs,
        # whereas ParticleColor and TextureSample also expose a named RGBA.
        # Report the actual available pins rather than guessing a vector shape.
        outputs = list(edit.get_material_expression_output_names(source))
        if output_pin and output_pin not in outputs:
            raise RuntimeError('Material output is unavailable: ' + source.get_class().get_name()
                               + '.' + output_pin + '; actual outputs=' + repr(outputs))
        if not edit.connect_material_expressions(source, output_pin, destination, input_pin):
            raise RuntimeError('Could not connect ' + source.get_class().get_name() + '.'
                               + (output_pin or '<default>') + ' to material input ' + input_pin)

    def custom(mat, label, code, inputs, kind=u.CustomMaterialOutputType.CMOT_FLOAT1):
        expression = node(mat, u.MaterialExpressionCustom, description=label, code=code, output_type=kind)
        entries = []
        for name in inputs:
            entry = u.CustomInput()
            entry.set_editor_property('input_name', name)
            entries.append(entry)
        expression.set_editor_property('inputs', entries)
        for name, (source, pin) in inputs.items():
            connect(source, pin, expression, name)
        return expression

    def scalar(mat, name, value):
        return node(mat, u.MaterialExpressionScalarParameter, parameter_name=name,
                    default_value=value, group='Frost Nova')

    def vector(mat, name, color):
        return node(mat, u.MaterialExpressionVectorParameter, parameter_name=name,
                    default_value=u.LinearColor(*color), group='Frost Nova')

    def output_property(expression, prop):
        if not edit.connect_material_property(expression, '', prop):
            raise RuntimeError('Could not connect material output ' + str(prop))

    def separate(mat, field, alpha=True):
        color = custom(mat, 'Frost color', 'return Field.rgb;', {'Field': (field, '')}, u.CustomMaterialOutputType.CMOT_FLOAT3)
        opacity = custom(mat, 'Soft ice opacity', 'return saturate(Field.a);', {'Field': (field, '')}) if alpha else None
        return color, opacity

    def front(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        world = node(mat, u.MaterialExpressionWorldPosition)
        vertex = node(mat, u.MaterialExpressionVertexColor)
        tint = vector(mat, 'Tint', (.72, .94, 1., 1.))
        age = scalar(mat, 'Age', 0.)
        opacity = scalar(mat, 'Opacity', .95)
        warning = scalar(mat, 'Warning', 0.)
        field = custom(mat, 'Irregular tissue-anchored frost with a clear travelling edge', r'''
            // World-space value noise avoids periodic ribs in angular UVs.
            // Both scales use the same floor coordinates on adjacent sectors.
            float2 p=P.xy*.018;
            float2 cell=floor(p), f=frac(p);
            f=f*f*(3-2*f);
            float4 h=frac(sin(float4(dot(cell,float2(127.1,311.7)),
                dot(cell+float2(1,0),float2(127.1,311.7)),
                dot(cell+float2(0,1),float2(127.1,311.7)),
                dot(cell+1,float2(127.1,311.7))))*43758.5453);
            float coarse=lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);
            p=P.xy*.073+float2(coarse*1.4,-coarse*.9);
            cell=floor(p); f=frac(p); f=f*f*(3-2*f);
            h=frac(sin(float4(dot(cell,float2(127.1,311.7)),
                dot(cell+float2(1,0),float2(127.1,311.7)),
                dot(cell+float2(0,1),float2(127.1,311.7)),
                dot(cell+1,float2(127.1,311.7))))*43758.5453);
            float detail=lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);
            float y=saturate(UV.y);
            float frost=coarse*.64+detail*.36;
            float leading=smoothstep(.90,.985,y)*(1-smoothstep(.996,1,y));
            float wake=smoothstep(.03,.24+coarse*.18,y)*(1-smoothstep(.77,.99,y));
            float crystal=1-smoothstep(.025,.10,abs(detail-.52+coarse*.19));
            float patches=smoothstep(.24,.72,frost);
            float ground=leading*(.62+.25*coarse)+wake*(.035+.28*patches+.16*crystal);
            float veil=smoothstep(0,.12,y)*(1-smoothstep(.22+coarse*.2,.95,y))
                *(.055+.19*patches+.065*crystal);
            float alpha=lerp(ground,veil,VertexLayer);
            alpha=lerp(alpha,leading*.8,Warning);
            float white=saturate(leading*.8+crystal*.25+patches*.4);
            float3 rgb=lerp(float3(.28,.63,.79),float3(.89,1.02,1.09),white);
            rgb*=Tint.rgb*lerp(1.3,1.05,Warning);
            return float4(rgb,alpha*Opacity*VertexAlpha);
        ''', {'UV': (uv, ''), 'P': (world, ''), 'VertexAlpha': (vertex, 'A'), 'VertexLayer': (vertex, 'R'), 'Tint': (tint, 'RGB'),
              'Age': (age, ''), 'Opacity': (opacity, ''), 'Warning': (warning, '')}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        color, alpha = separate(mat, field)
        output_property(color, u.MaterialProperty.MP_EMISSIVE_COLOR)
        output_property(alpha, u.MaterialProperty.MP_OPACITY)

    def crystal(mat):
        position = node(mat, u.MaterialExpressionWorldPosition)
        data = node(mat, u.MaterialExpressionPerInstanceCustomData, data_index=0, const_default_value=1.)
        interpolated = node(mat, u.MaterialExpressionVertexInterpolator)
        connect(data, '', interpolated, 'VS')
        fade = custom(mat, 'Faceted ice dissolves in place instead of retracting', r'''
            float3 q=floor(P*.24);
            float grain=frac(sin(dot(q,float3(12.9898,78.233,37.719)))*43758.5453);
            return saturate((Fade-grain)*8+.5)*step(.001,Fade);
        ''', {'P': (position, ''), 'Fade': (interpolated, '')})
        normal = node(mat, u.MaterialExpressionPixelNormalWS)
        color = custom(mat, 'Solid blue facets with pale upward planes', r'''
            return lerp(float3(.10,.37,.62),float3(.79,.96,1.0),saturate(N.z*.5+.5));
        ''', {'N': (normal, '')}, u.CustomMaterialOutputType.CMOT_FLOAT3)
        fresnel = node(mat, u.MaterialExpressionFresnel, exponent=3., base_reflect_fraction=.08)
        glint = custom(mat, 'Pale facet fill remains readable in red mouth lighting',
                       'return Ice*.48+float3(.23,.65,.88)*R*.65;',
                       {'Ice': (color, ''), 'R': (fresnel, '')}, u.CustomMaterialOutputType.CMOT_FLOAT3)
        output_property(color, u.MaterialProperty.MP_BASE_COLOR)
        output_property(glint, u.MaterialProperty.MP_EMISSIVE_COLOR)
        output_property(fade, u.MaterialProperty.MP_OPACITY_MASK)
        output_property(node(mat, u.MaterialExpressionConstant, r=.22), u.MaterialProperty.MP_ROUGHNESS)
        output_property(node(mat, u.MaterialExpressionConstant, r=.65), u.MaterialProperty.MP_SPECULAR)

    def powder(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        sample = node(mat, u.MaterialExpressionTextureSampleParameter2D,
                      parameter_name='PowderTexture', texture=texture, group='Frost Nova')
        connect(uv, '', sample, 'UVs')
        color = node(mat, u.MaterialExpressionParticleColor)
        dynamic = node(mat, u.MaterialExpressionDynamicParameter,
                       param_names=['Normalized Age', 'Unused Y', 'Unused Z', 'Unused W'])
        world = node(mat, u.MaterialExpressionWorldPosition)
        camera = node(mat, u.MaterialExpressionCameraPositionWS)
        field = custom(mat, 'Short low-density directional powder', r'''
            float2 p=UV*2-1;
            float life=smoothstep(0,.055,Age)*(1-smoothstep(.32,1,Age));
            float nearCamera=smoothstep(60,155,length(P-C));
            float density=S.a*pow(saturate(1-dot(p,p)*.72),.65);
            float detail=saturate(dot(S.rgb,float3(.2126,.7152,.0722)));
            return float4(lerp(float3(.55,.76,.91),float3(.92,1.02,1.08),detail)*Color.rgb,
                          density*Color.a*life*nearCamera*.38);
        ''', {'UV': (uv, ''), 'S': (sample, 'RGBA'), 'Color': (color, 'RGBA'), 'Age': (dynamic, ''),
              'P': (world, ''), 'C': (camera, '')}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        base, alpha = separate(mat, field)
        normal = custom(mat, 'Rounded sprite normals suggest a powder volume', r'''
            float2 p=UV*2-1;
            return normalize(float3(p*.72,sqrt(saturate(1-dot(p,p)*.75))+.25));
        ''', {'UV': (uv, '')}, u.CustomMaterialOutputType.CMOT_FLOAT3)
        ambient = custom(mat, 'Pale powder fill without an opaque luminous cloud', 'return Base*.38;',
                         {'Base': (base, '')}, u.CustomMaterialOutputType.CMOT_FLOAT3)
        depth = node(mat, u.MaterialExpressionDepthFade, fade_distance_default=12.)
        connect(alpha, '', depth, 'Opacity')
        output_property(base, u.MaterialProperty.MP_BASE_COLOR)
        output_property(ambient, u.MaterialProperty.MP_EMISSIVE_COLOR)
        output_property(normal, u.MaterialProperty.MP_NORMAL)
        output_property(depth, u.MaterialProperty.MP_OPACITY)
        output_property(node(mat, u.MaterialExpressionConstant, r=1.), u.MaterialProperty.MP_ROUGHNESS)
        output_property(node(mat, u.MaterialExpressionConstant, r=0.), u.MaterialProperty.MP_SPECULAR)

    def snow(mat):
        uv = node(mat, u.MaterialExpressionTextureCoordinate)
        color = node(mat, u.MaterialExpressionParticleColor)
        age = node(mat, u.MaterialExpressionDynamicParameter,
                   param_names=['Normalized Age', 'Unused Y', 'Unused Z', 'Unused W'])
        field = custom(mat, 'Tiny tapered ice flecks', r'''
            float2 p=UV*2-1;
            float grain=pow(saturate(1-abs(p.x)),3.5)*pow(saturate(1-abs(p.y)),.65);
            float life=smoothstep(0,.025,Age)*(1-smoothstep(.35,1,Age));
            return float4(float3(.74,.94,1.1)*Color.rgb*1.5,grain*Color.a*life*.85);
        ''', {'UV': (uv, ''), 'Color': (color, 'RGBA'), 'Age': (age, '')}, u.CustomMaterialOutputType.CMOT_FLOAT4)
        rgb, alpha = separate(mat, field)
        output_property(rgb, u.MaterialProperty.MP_EMISSIVE_COLOR)
        output_property(alpha, u.MaterialProperty.MP_OPACITY)

    builders = dict(zip(NAMES, (front, crystal, powder, snow)))
    try:
        system_path = FOLDER + '/NS_FrostNovaBurst'
        previous_system = u.load_asset(system_path)
        if previous_system and not isinstance(previous_system, u.NiagaraSystem):
            raise RuntimeError('Unexpected existing burst system class: ' + system_path)
        if previous_system and rebuild_owned:
            # Native authoring additionally verifies the ownership tag or the
            # exact two-emitter signature of this script's untagged v1 output.
            disk = Path(u.Paths.project_content_dir()) / 'Gameplay/Cold/VFX/FrostNova/NS_FrostNovaBurst.uasset'
            if not disk.is_file():
                raise RuntimeError('Saved burst system backup source is missing: ' + str(disk))
            backup = output / 'BeforeOwnedRebuild' / disk.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            if backup.exists():
                raise RuntimeError('Preserve the previous backup; use a fresh report output directory')
            shutil.copy2(disk, backup)
            report['system_backup'] = str(backup)
        for name, build in builders.items():
            rebuild_material = bool(rebuild_owned or name in rebuild_materials)
            path = FOLDER + '/' + name
            asset = u.load_asset(path)
            created = asset is None
            if asset and not isinstance(asset, u.Material):
                raise RuntimeError('Unexpected existing asset class: ' + path)
            if asset and rebuild_material:
                if lib.get_metadata_tag(asset, OWNER) not in OWNED_VERSIONS:
                    raise RuntimeError('Refusing to rebuild a material without the exact ownership tag: ' + path)
                disk = Path(u.Paths.project_content_dir()) / 'Gameplay/Cold/VFX/FrostNova' / (name + '.uasset')
                if disk.is_file():
                    backup = output / 'BeforeOwnedRebuild' / disk.name
                    backup.parent.mkdir(parents=True, exist_ok=True)
                    if backup.exists():
                        raise RuntimeError('Preserve the previous backup; use a fresh report output directory')
                    shutil.copy2(disk, backup)
            if created:
                asset = tools.create_asset(name, FOLDER, u.Material, u.MaterialFactoryNew())
                if not asset:
                    raise RuntimeError('Could not create ' + path)
            if created or rebuild_material:
                if rebuild_material:
                    edit.delete_all_material_expressions(asset)
                asset.set_editor_properties(dict(
                    blend_mode=u.BlendMode.BLEND_MASKED if name == 'M_FrostNovaCrystal' else u.BlendMode.BLEND_TRANSLUCENT,
                    shading_model=u.MaterialShadingModel.MSM_DEFAULT_LIT if name in ('M_FrostNovaCrystal', 'M_FrostNovaPowder') else u.MaterialShadingModel.MSM_UNLIT,
                    two_sided=True))
                if name == 'M_FrostNovaPowder':
                    asset.set_editor_properties(dict(translucency_lighting_mode=u.TranslucencyLightingMode.TLM_VOLUMETRIC_DIRECTIONAL,
                        translucency_directional_lighting_intensity=1.3))
                if name in ('M_FrostNovaPowder', 'M_FrostNovaSnow'):
                    edit.set_base_material_usage(asset, u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
                if name == 'M_FrostNovaCrystal':
                    edit.set_base_material_usage(asset, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
                build(asset)
                edit.layout_material_expressions(asset)
                errors = edit.recompile_material(asset)
                if errors:
                    raise RuntimeError('Material compile failed: ' + path + ': ' + str(errors))
                lib.set_metadata_tag(asset, OWNER, VERSION)
                if not lib.save_loaded_asset(asset, only_if_is_dirty=True):
                    raise RuntimeError('Could not save ' + path)
            report['packages'].append(dict(path=asset.get_path_name(), created=created,
                                            rebuilt=rebuild_material, preserved=not created and not rebuild_material))
        if not u.MCFrostNovaAssetLibrary.wait_for_material_shaders():
            raise RuntimeError('New Frost Nova material shader validation failed')
        if not u.MCFrostNovaAssetLibrary.author_assets(rebuild_owned):
            raise RuntimeError('Isolated Frost Nova Niagara authoring failed')
        system = u.load_asset(FOLDER + '/NS_FrostNovaBurst')
        if not isinstance(system, u.NiagaraSystem):
            raise RuntimeError('New burst system is unavailable after authoring')
        dirty_after = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        dirty_after += list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        before_names = sorted(package.get_name() for package in dirty_before)
        after_names = sorted(package.get_name() for package in dirty_after)
        if before_names != after_names:
            raise RuntimeError('Authoring left a changed set of unsaved packages: ' + repr(after_names))
        report.update(passed=True, status='passed', system=system.get_path_name(),
                      dirty_before=before_names, dirty_after=after_names,
                      maximum_burst_particles=44, maximum_burst_lifetime_seconds=.78,
                      deferred=['Gameplay-camera visual acceptance', 'Runtime frame-cost measurement', 'Packaged or multiplayer validation'])
        return report
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (output / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
