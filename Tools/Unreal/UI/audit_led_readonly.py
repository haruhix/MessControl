import json
import unreal as u

material = u.load_asset('/Game/Gameplay/UI/Materials/M_UI_LEDGrid')
report = {'material': material.get_path_name() if material else None}
if material:
    for key in ('material_domain', 'blend_mode', 'two_sided', 'opacity_mask_clip_value'):
        try:
            report[key] = str(material.get_editor_property(key))
        except Exception as exc:
            report[key] = str(exc)
    report['expression_api'] = [x for x in dir(u.MaterialEditingLibrary) if any(y in x for y in ('expression', 'input', 'output', 'statistic'))]
    report['api_docs'] = {name: getattr(u.MaterialEditingLibrary, name).__doc__ for name in ('get_material_expressions', 'get_inputs_for_material_expression', 'get_input_node_output_name_for_material_expression', 'get_material_property_input_node', 'get_statistics')}
    report['graph'] = []
    for expression in u.MaterialEditingLibrary.get_material_expressions(material):
        entry = {'name': expression.get_name(), 'class': expression.get_class().get_name()}
        for key in ('code', 'inputs', 'output_type', 'coordinate_index', 'u_tiling', 'v_tiling', 'input', 'r', 'g', 'b', 'a'):
            try:
                entry[key] = str(expression.get_editor_property(key))
            except Exception:
                pass
        try:
            entry['input_nodes'] = [str(x) for x in u.MaterialEditingLibrary.get_inputs_for_material_expression(material, expression)]
        except Exception as exc:
            entry['input_error'] = str(exc)
        if expression.get_class().get_name() == 'MaterialExpressionCustom':
            entry['custom_inputs_explicit'] = []
            for custom_input in expression.get_editor_property('inputs'):
                item = {}
                for key in ('input_name', 'input'):
                    try:
                        item[key] = str(custom_input.get_editor_property(key))
                    except Exception as exc:
                        item[key] = str(exc)
                entry['custom_inputs_explicit'].append(item)
        report['graph'].append(entry)
    report['outputs'] = {}
    props = [u.MaterialProperty.MP_EMISSIVE_COLOR, u.MaterialProperty.MP_OPACITY]
    if hasattr(u.MaterialProperty, 'MP_FRONT_MATERIAL'):
        props.append(u.MaterialProperty.MP_FRONT_MATERIAL)
    for prop in props:
        node = u.MaterialEditingLibrary.get_material_property_input_node(material, prop)
        report['outputs'][str(prop)] = {'node': str(node), 'pin': str(u.MaterialEditingLibrary.get_material_property_input_node_output_name(material, prop))}
    report['stats'] = str(u.MaterialEditingLibrary.get_statistics(material))
    for expression in u.MaterialEditingLibrary.get_material_expressions(material):
        if expression.get_class().get_name() == 'MaterialExpressionVertexColor':
            report['vertex_outputs'] = str(u.MaterialEditingLibrary.get_material_expression_output_names(expression))
    for key in ('expressions', 'expression_collection', 'editor_only_data'):
        try:
            value = material.get_editor_property(key)
            report[key] = str(value)
        except Exception as exc:
            report[key] = str(exc)
    if hasattr(u, 'get_objects_with_outer'):
        try:
            objects = u.get_objects_with_outer(material, include_nested_objects=True)
            report['objects'] = []
            for obj in objects:
                entry = {'name': obj.get_name(), 'class': obj.get_class().get_name()}
                for key in ('code', 'inputs', 'output_type', 'coordinate_index', 'u_tiling', 'v_tiling', 'expression_collection', 'emissive_color', 'opacity', 'input', 'r', 'g', 'b', 'a'):
                    try:
                        entry[key] = str(obj.get_editor_property(key))
                    except Exception:
                        pass
                report['objects'].append(entry)
        except Exception as exc:
            report['objects_error'] = str(exc)
u.log('LED_READONLY_AUDIT ' + json.dumps(report, ensure_ascii=False))
