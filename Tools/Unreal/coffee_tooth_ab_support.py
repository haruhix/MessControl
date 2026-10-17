"""Isolated, synthetic future-coffee A/B support. Importing this module is inert.

Root runs this helper in solo PIE, with the material factories from
coffee_surface_blend_material.py. A is a separate native StaticMeshActor using
the same tooth asset, offset0.6cm along world normals by its coat shader;
B blends the same synthetic field into that tooth's enamel. This is NOT the
native GrimeRelief domain, a native brush replay, or a gameplay migration.
No package is created/saved, no tooth is damaged, and no native mask is written.

    fixture = build_fixture(tooth, output, build_tooth_pair,
                            configure_tooth_mid)
    fixture.set_state('before'); fixture.show_mode('A')
    # Capture, then show_mode('B') with the same state/camera/pose.
    # If root applies native damage, call rebuild_current_mesh() afterwards.
    fixture.restore()  # also use from the caller's finally/PIE-stop cleanup

Pure mask/domain functions also run under ordinary Python without Unreal.
"""
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path


SIZE, ATLAS, COUNT = 16, 64, 4096
PARAM_VECTORS = ('AB_ToothMin', 'AB_ToothSize', 'AB_ToothScale',
                 'AB_ToothDomainCenter', 'AB_ToothDomainU', 'AB_ToothDomainV',
                 'AB_ToothDomainN', 'AB_ToothDomainRadii')


def _require(condition, message):
    if not condition:
        raise RuntimeError(message)


def _xyz(value):
    if hasattr(value, 'x'):
        return (float(value.x), float(value.y), float(value.z))
    return tuple(float(x) for x in value[:3])


def _add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def _sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def _mul(a, factor):
    return tuple(x*factor for x in a)


def _dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def _unit(a):
    length = math.sqrt(_dot(a, a))
    _require(length > 1.e-9, 'Degenerate fixture direction')
    return _mul(a, 1./length)


def _cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0])


def _smoothstep(a, b, value):
    t = max(0., min(1., (value-a)/(b-a)))
    return t*t*(3.-2.*t)


def atlas_index(x, y, z):
    """Exactly FMCSurfaceWipe::Index, not UV0 and not a 2D tongue mask."""
    return x+(z % 4)*SIZE+(y+(z//4)*SIZE)*ATLAS


def mask_sample(mask, uv):
    """FMCSurfaceWipe::Sample's trilinear lookup, with identical atlas packing."""
    _require(len(mask) == COUNT, 'Expected the native 16^3 mask')
    p = [max(0., min(1., float(v)))*(SIZE-1) for v in uv]
    xyz = [math.floor(v) for v in p]
    value = 0.
    for k in range(2):
        for j in range(2):
            for i in range(2):
                weight = ((p[0]-xyz[0]) if i else (1.-p[0]+xyz[0]))
                weight *= ((p[1]-xyz[1]) if j else (1.-p[1]+xyz[1]))
                weight *= ((p[2]-xyz[2]) if k else (1.-p[2]+xyz[2]))
                value += mask[atlas_index(min(xyz[0]+i, 15),
                                         min(xyz[1]+j, 15),
                                         min(xyz[2]+k, 15))]/255.*weight
    return value


def domain_value(point, domain):
    """The SAME local-space ellipsoid expression used by both shader variants.

    Each basis is orthonormal in the original mesh's local coordinates. Radii
    are local units. The analytic term is exactly zero outside the ellipsoid;
    multiplying it after atlas sampling prevents interpolation bleeding out.
    """
    delta = _sub(point, domain['center'])
    q = [_dot(delta, domain[axis])/radius for axis, radius in
         zip(('u', 'v', 'n'), domain['radii'])]
    return 1.-_smoothstep(.8, 1., math.sqrt(_dot(q, q)))


def _point_segment_distance(point, start, end):
    segment = _sub(end, start)
    denominator = _dot(segment, segment)
    along = max(0., min(1., _dot(_sub(point, start), segment)/denominator)) if denominator > 1.e-12 else 0.
    return math.dist(point, _add(start, _mul(segment, along)))


def native_stroke(mask, precise, start_uv, end_uv, dimensions, radius,
                  seconds=.1, rate=1.):
    """Python copy of native Stroke math; this does not call the native brush.

    Carry fractional work in precise, then use UE RoundToInt (floor(v+.5)),
    rather than Python's ties-to-even round(). No accelerated mask algorithm.
    """
    _require(len(mask) == len(precise) == COUNT, 'Wrong mask/precise size')
    _require(radius > 0 and seconds > 0 and min(dimensions) > 0, 'Invalid synthetic stroke')
    start = tuple(a*b for a, b in zip(start_uv, dimensions))
    end = tuple(a*b for a, b in zip(end_uv, dimensions))
    changed = 0
    for z in range(SIZE):
        for y in range(SIZE):
            for x in range(SIZE):
                point = tuple(a/15.*b for a, b in zip((x, y, z), dimensions))
                distance = _point_segment_distance(point, start, end)
                weight = 1.-_smoothstep(radius*.45, radius, distance)
                index = atlas_index(x, y, z)
                precise[index] = max(0., precise[index]-weight*min(seconds, .1)*max(1., min(2., rate))*950.)
                value = max(0, min(255, math.floor(precise[index]+.5)))
                changed += value != mask[index]
                mask[index] = value
    return changed


def rgba_png(mask):
    """Lossless 64-square RGBA PNG, raster order equals native atlas order."""
    _require(len(mask) == COUNT, 'Wrong atlas size')
    def chunk(kind, data):
        return (struct.pack('>I', len(data))+kind+data+
                struct.pack('>I', zlib.crc32(kind+data)))
    rows = b''.join(b'\0'+b''.join(bytes((v, v, v, 255))
                    for v in mask[y*ATLAS:(y+1)*ATLAS]) for y in range(ATLAS))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', ATLAS, ATLAS, 8, 6, 0, 0, 0))+
            chunk(b'IDAT', zlib.compress(rows, 9))+chunk(b'IEND', b''))


def _domain_mask(minimum, size, domain):
    before = [0]*COUNT
    for z in range(SIZE):
        for y in range(SIZE):
            for x in range(SIZE):
                p = tuple(a+b*c/15. for a, b, c in zip(minimum, size, (x, y, z)))
                before[atlas_index(x, y, z)] = math.floor(domain_value(p, domain)*255.+.5)
    return before


def generate_masks(minimum, size, scale, domain):
    """Synthetic initial deposition + deterministic partial native-formula scrub."""
    _require(min(size) > 0 and min(scale) > 0, 'Invalid original-crown dimensions')
    before = _domain_mask(minimum, size, domain)
    _require(any(before) and min(before) == 0 and max(before) > 200,
             'Ellipsoid needs both painted and clear voxels at native resolution')
    after = list(before)
    precise = [float(v) for v in after]
    start = _add(domain['center'], _add(_mul(domain['u'], -.6*domain['radii'][0]),
                                      _mul(domain['v'], -.25*domain['radii'][1])))
    end = _add(domain['center'], _add(_mul(domain['u'], .55*domain['radii'][0]),
                                    _mul(domain['v'], .30*domain['radii'][1])))
    uv = lambda p: tuple((p[i]-minimum[i])/size[i] for i in range(3))
    dimensions = tuple(a*b for a, b in zip(size, scale))
    world_v_radius = math.sqrt(sum((domain['v'][i]*domain['radii'][1]*scale[i])**2 for i in range(3)))
    radius = max(12., min(45., world_v_radius*.38))
    changes = 0
    steps = 16
    for i in range(steps):
        a = _add(start, _mul(_sub(end, start), i/steps))
        b = _add(start, _mul(_sub(end, start), (i+1)/steps))
        changes += native_stroke(after, precise, uv(a), uv(b), dimensions, radius)
    _require(changes > 0 and 0 < sum(after) < sum(before), 'Expected a partial synthetic scrub')
    return {'before': before, 'partial': after}, dict(
        scope='Synthetic Python FMCSurfaceWipe::Stroke formula; NOT native gameplay brush contact',
        start_local=start, end_local=end, radius_cm=radius, steps=steps,
        seconds_per_step=.1, rate=1., changed_voxel_writes=changes)


def _path(obj):
    return obj.get_path_name() if obj else None


class ToothABFixture:
    """Reversible, caller-owned runtime fixture. Does not own a PIE/capture loop."""

    def __init__(self, tooth, output, pair_factory, mid_factory,
                 original_mesh=None, domain=None, inward_world=None):
        import unreal as u
        self.u, self.tooth, self.visual = u, tooth, tooth.get_editor_property('visual')
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.pair_factory, self.mid_factory = pair_factory, mid_factory
        self.overlay, self.original_slots, self.current_mesh = None, {}, None
        self.overlay_actor = None
        self.mid_enabled = {}
        self.material_records, self.texture_records = [], {}
        self.compiled_pairs = {}
        self.mids, self.textures, self.mode, self.state = [], {}, 'A', 'before'
        self.restored = False
        worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
        _require(worlds, 'Run this isolated fixture in an existing solo PIE world')
        self.world = tooth.get_world()
        _require(self.world in worlds and self.visual.get_editor_property('static_mesh'), 'Expected an actual runtime tooth')
        cache_path = Path(u.Paths.project_dir()).resolve()/'Saved/CoffeeArchitectureAB/ToothMeshSlots.json'
        _require(cache_path.is_file(), 'Root must run cache_coffee_tooth_ab_mesh_slots.py in idle Editor before PIE')
        cache_bytes = cache_path.read_bytes()
        self.slot_cache = json.loads(cache_bytes)
        _require(self.slot_cache.get('schema_version') == 1 and self.slot_cache.get('passed') is True,
                 'Invalid tooth section-map cache')
        self.slot_cache_evidence = dict(file=str(cache_path), sha256=hashlib.sha256(cache_bytes).hexdigest(),
                                        generated_utc=self.slot_cache.get('generated_utc'))
        if original_mesh is None:
            _require('INTACT' in str(tooth.damage_stage()).upper(),
                     'Start on Intact or explicitly provide the original intact mesh')
            original_mesh = self.visual.get_editor_property('static_mesh')
        self.original_mesh = original_mesh
        bounds = original_mesh.get_bounding_box()
        self.minimum, self.size = _xyz(bounds.min), _sub(_xyz(bounds.max), _xyz(bounds.min))
        self.scale = tuple(abs(v) for v in _xyz(self.visual.get_world_scale()))
        _require(min(self.scale) > .0001, 'Zero or reflected tooth scale unsupported')
        self.native_snapshot = self._native_state()
        self.native_relief = tooth.get_editor_property('grime_relief')
        self.native_relief_hidden = bool(self.native_relief.get_editor_property('hidden_in_game'))
        self.domain = domain or self._choose_domain(inward_world)
        self._validate_domain()
        self.parameters = dict(zip(PARAM_VECTORS, (self.minimum, self.size, self.scale,
            self.domain['center'], self.domain['u'], self.domain['v'], self.domain['n'], self.domain['radii'])))
        if hasattr(self, 'validated_domain_masks'):
            masks, stroke = self.validated_domain_masks
        else:
            masks, stroke = generate_masks(self.minimum, self.size, self.scale, self.domain)
        self.mask_arrays = masks
        source = Path(u.Paths.project_dir()).resolve()/'Source/MessControl/Public/MCSurfaceWipe.h'
        self.report = dict(schema_version=1, scope='Isolated SYNTHETIC future tooth coffee renderer prototype; no native brush claim',
            tooth=_path(tooth), original_intact_mesh=_path(original_mesh), original_min=self.minimum,
            original_size=self.size, original_scale=self.scale, domain=self.domain,
            offset_world_cm=.6, mask_resolution=[SIZE, SIZE, SIZE], atlas_resolution=[ATLAS, ATLAS],
            mask_coordinates='original intact local XYZ; A pre-WPO World-to-Local XYZ; B static-component World-to-Local XYZ (no WPO); same asset/local transform, no UV channels modified',
            native_stroke_source=str(source), native_stroke_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            source_section_map_cache=self.slot_cache_evidence,
            synthetic_partial_stroke=stroke, native_state_before=self.native_snapshot,
            assertions=[], modes=[], meshes=[], material_pairs=self.material_records, textures=self.texture_records)
        try:
            for name, mask in masks.items():
                data = rgba_png(mask)
                file = self.output/(name+'_native16_atlas64.png')
                file.write_bytes(data)
                texture = u.RenderingLibrary.import_buffer_as_texture2d(self.world, list(data))
                _require(texture and _path(texture).startswith('/Engine/Transient.'), 'Expected a transient atlas texture')
                for key, value in dict(srgb=False, filter=u.TextureFilter.TF_BILINEAR,
                    address_x=u.TextureAddress.TA_CLAMP, address_y=u.TextureAddress.TA_CLAMP, never_stream=True).items():
                    texture.set_editor_property(key, value)
                _require(texture.blueprint_get_size_x() == ATLAS and texture.blueprint_get_size_y() == ATLAS,
                         'Imported atlas dimensions differ')
                self.textures[name] = texture
                self.texture_records[name] = dict(path=_path(texture), source=str(file),
                    png_sha256=hashlib.sha256(data).hexdigest(), mask_sha256=hashlib.sha256(bytes(mask)).hexdigest(),
                    nonzero_voxels=sum(v > 0 for v in mask), mass=sum(mask), srgb=False, linear=True,
                    filter=str(texture.get_editor_property('filter')))
            # Native SetVisibility in UpdateGrime cannot unhide a component hidden in game.
            self.native_relief.set_hidden_in_game(True, False)
            self.rebuild_current_mesh()
            self._write()
        except Exception:
            self.restore()
            raise

    def _native_state(self):
        mask = bytes(self.tooth.get_editor_property('grime_mask'))
        state = self.tooth.get_editor_property('status').get_editor_property('state')
        return dict(mask_sha256=hashlib.sha256(mask).hexdigest(), mask_length=len(mask),
                    coffee_left=int(state.get_editor_property('coffee_left')),
                    coffee_total=int(state.get_editor_property('coffee_total')))

    def _sections(self, mesh):
        u = self.u
        for section in range(mesh.get_num_sections(0)):
            data = u.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, section)
            _require(len(data) == 5, 'Unexpected GetSectionFromStaticMesh outputs')
            vertices, triangles, normals, uv0, tangents = [list(a) for a in data]
            _require(vertices and len(vertices) == len(normals) == len(uv0) == len(tangents),
                     'Need CPU-readable mesh vertices, UV0, normals, and tangents')
            _require(len(triangles) % 3 == 0, 'Invalid source mesh topology')
            yield section, vertices, triangles, normals, uv0, tangents

    def _choose_domain(self, inward_world):
        # Select a real FRONT mesh vertex around the lower-middle crown. No proxy
        # sphere or presumed source pivot is substituted for the current mesh.
        u = self.u
        if inward_world is None:
            location = _xyz(self.tooth.get_actor_location())
            inward_world = _unit((-location[0], -location[1], 0.))
        transform = self.visual.get_world_transform()
        scale = self.scale
        # Rotation+inverse scale yields the local-space vector for this world direction.
        inward_local = _unit(_xyz(u.MathLibrary.inverse_transform_direction(transform, u.Vector(*inward_world))))
        ranking_u = _unit(_cross((0., 0., 1.), inward_local))
        # The lower-middle crown can sit below the actual tongue/gum surface.
        # This synthetic future renderer fixture belongs on exposed enamel.
        wanted_z = self.minimum[2]+self.size[2]*.70
        candidates, geometry = [], []
        for _, vertices, _, normals, _, _ in self._sections(self.visual.get_editor_property('static_mesh')):
            for p, normal in zip(vertices, normals):
                p, normal = _xyz(p), _unit(_xyz(normal))
                geometry.append((p, normal))
                if _dot(normal, inward_local) < .50:
                    continue
                score = abs(p[2]-wanted_z)/max(self.size[2], 1.)
                score += abs(_dot(_sub(p, _add(self.minimum, _mul(self.size, .5))), ranking_u))/max(max(self.size), 1.)*.3
                candidates.append((score, p, normal))
        _require(candidates, 'No inward-facing current tooth surface')
        attempts = []
        tested = set()
        for _, center, surface_normal in sorted(candidates, key=lambda row: row[0]):
            unique = tuple(round(v, 5) for v in center+surface_normal)
            if unique in tested:
                continue
            tested.add(unique)
            # Inward chooses the FRONT only. The surface's actual local vertex
            # normal defines its stable coating basis, independent of runtime
            # actor wobble/tilt relative to a world-horizontal inward vector.
            local_n = surface_normal
            up = (0., 0., 1.) if abs(local_n[2]) < .95 else (1., 0., 0.)
            local_v = _unit(_sub(up, _mul(local_n, _dot(up, local_n))))
            local_u = _unit(_cross(local_v, local_n))
            rear_points = [p for p, normal in geometry if _dot(normal, local_n) < -.25]
            spans = [sum(abs(axis[i])*self.size[i] for i in range(3)) for axis in (local_u, local_v, local_n)]
            radii = (spans[0]*.38, spans[1]*.22, spans[2]*.27)
            # A wide tangent ellipse can contain a folded/rear-facing crown
            # surface quite close along N. Keep width/height, but cap the depth
            # BELOW the closest actual rear projection, with a .75 margin.
            # A coplanar reverse vertex cannot be made safe by changing depth;
            # try another genuine front vertex instead of ignoring that vertex.
            depths = []
            for point in rear_points:
                delta = _sub(point, center)
                tangent_r2 = (_dot(delta, local_u)/radii[0])**2+(_dot(delta, local_v)/radii[1])**2
                if tangent_r2 < 1.:
                    depths.append(abs(_dot(delta, local_n)))
            nearest = min(depths) if depths else None
            attempt = dict(center=center, actual_rear_vertices=len(rear_points),
                actual_surface_normal=surface_normal,
                rear_vertices_in_tangent_ellipse=len(depths), nearest_reverse_depth_local=nearest,
                requested_depth_local=radii[2], safety_margin=.75)
            attempts.append(attempt)
            if nearest is not None and nearest <= 1.e-5:
                attempt['rejected'] = 'Coplanar reverse vertex; choose another real front center'
                continue
            depth = min(radii[2], nearest*.75) if nearest is not None else radii[2]
            voxel_pitch_n = math.sqrt(sum((local_n[i]*self.size[i]/15.)**2 for i in range(3)))
            attempt.update(selected_depth_local=depth, native_voxel_pitch_along_normal=voxel_pitch_n)
            if depth < voxel_pitch_n*.20:
                attempt['rejected'] = 'Subvoxel thin domain; choose another actual front surface'
                continue
            domain = dict(center=center, u=local_u, v=local_v, n=local_n, radii=(radii[0], radii[1], depth),
                selection='Actual inward-facing LOD0 vertex and its own local surface normal; exposed upper-middle crown at70% original height; synthetic future domain',
                depth_restriction=dict(attempt, selected_depth_local=depth,
                    reason='Actual reverse vertices inside tangent ellipse bound this thin front coat; no native grime domain is reused'))
            before = dict(domain, radii=radii)
            attempt['analytic_painted_rear_before'] = sum(domain_value(p, before) > 0 for p in rear_points)
            attempt['analytic_painted_rear_after'] = sum(domain_value(p, domain) > 0 for p in rear_points)
            if attempt['analytic_painted_rear_after']:
                attempt['rejected'] = 'Actual reverse geometry still enters domain'
                continue
            mask = _domain_mask(self.minimum, self.size, domain)
            attempt.update(native_mask_max=max(mask), native_nonzero_voxels=sum(v > 0 for v in mask))
            if max(mask) <= 200 or attempt['native_nonzero_voxels'] < 8:
                attempt['rejected'] = 'Empty or too sparse native16 mask; existing max>200 gate retained'
                continue
            front = 0
            peak = 0.
            for point, normal in geometry:
                if _dot(normal, local_n) < -.25:
                    continue
                alpha = domain_value(point, domain)
                if alpha <= 0:
                    continue
                uv = tuple((point[i]-self.minimum[i])/self.size[i] for i in range(3))
                alpha *= mask_sample(mask, uv)
                peak = max(peak, alpha)
                front += alpha > .05
            attempt.update(actual_front_painted_vertices=front, actual_front_peak_alpha=peak)
            if front < 24 or peak < .30:
                attempt['rejected'] = 'Too little actual front coverage; choose another real front surface'
                continue
            try:
                # Exercise the complete unchanged native-formula mask/stroke
                # recipe before accepting this basis, including partial work.
                self.validated_domain_masks = generate_masks(self.minimum, self.size, self.scale, domain)
            except RuntimeError as error:
                attempt['rejected'] = 'Native16 before/partial recipe: '+str(error)
                continue
            domain['depth_restriction'].update(attempt)
            (self.output/'DomainSelection.json').write_text(json.dumps(dict(passed=True, attempts=attempts,
                selected_domain=domain), indent=2), encoding='utf-8')
            return domain
        (self.output/'DomainSelection.json').write_text(json.dumps(dict(passed=False, attempts=attempts,
            error='No actual front basis passes native16 support, visible coverage and reverse0'), indent=2), encoding='utf-8')
        raise RuntimeError('No safe front-only ellipsoid center on the actual tooth')

    def _validate_domain(self):
        d = self.domain
        _require(min(d['radii']) > 0, 'Invalid domain radii')
        for name in ('u', 'v', 'n'):
            _require(abs(_dot(d[name], d[name])-1.) < 1.e-4, 'Domain basis must be unit local vectors')
        for a, b in (('u', 'v'), ('u', 'n'), ('v', 'n')):
            _require(abs(_dot(d[a], d[b])) < 1.e-4, 'Domain basis must be orthogonal')
        _require(domain_value(d['center'], d) == 1., 'Domain center must be fully covered')
        for name, radius in zip(('u', 'v', 'n'), d['radii']):
            _require(domain_value(_add(d['center'], _mul(d[name], radius*1.01)), d) == 0.,
                     'Domain must be exactly zero outside')

    def _destroy_overlay(self):
        if self.overlay_actor is not None and self.u.SystemLibrary.is_valid(self.overlay_actor):
            self.overlay_actor.destroy_actor()
        self.overlay_actor, self.overlay = None, None

    def _ensure_overlay(self):
        """Use an owned native actor/component; no UCS or component API hacks."""
        u = self.u
        if self.overlay is not None and u.SystemLibrary.is_valid(self.overlay):
            return
        statics = u.get_default_object(u.GameplayStatics)
        pose = self.visual.get_world_transform()
        self.overlay_actor = statics.call_method('BeginDeferredActorSpawnFromClass', args=(
            self.world, u.StaticMeshActor, pose, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            self.tooth, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        _require(isinstance(self.overlay_actor, u.StaticMeshActor), 'Could not spawn the owned coat StaticMeshActor')
        statics.call_method('FinishSpawningActor', args=(self.overlay_actor, pose,
            u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        self.overlay = self.overlay_actor.get_editor_property('static_mesh_component')
        _require(isinstance(self.overlay, u.StaticMeshComponent), 'Owned coat needs its native StaticMeshComponent')
        self.overlay.set_mobility(u.ComponentMobility.MOVABLE)
        self.overlay.call_method('K2_AttachToComponent', args=(self.visual, 'None',
            u.AttachmentRule.KEEP_RELATIVE, u.AttachmentRule.KEEP_RELATIVE,
            u.AttachmentRule.KEEP_RELATIVE, False))
        self.overlay.set_relative_transform(u.Transform(), False, True)
        self.overlay.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        self.overlay.set_editor_property('cast_shadow', False)
        self.overlay_actor.set_actor_tick_enabled(False)
        self.report.setdefault('overlay_components', []).append(dict(actor=_path(self.overlay_actor),
            path=_path(self.overlay), component_kind='StaticMeshActor native StaticMeshComponent',
            creation_order='Compiled material pairs and configured MIDs first; owned native actor afterwards',
            shader_displacement='VertexNormalWS *0.6cm WPO, pre-offset World-to-Local domain sampling'))

    def rebuild_current_mesh(self):
        """Root may change native damage first; this never damages/repairs a tooth."""
        u = self.u
        mesh = self.visual.get_editor_property('static_mesh')
        _require(mesh, 'Current tooth mesh was removed')
        key = _path(mesh)
        if key not in self.original_slots:
            self.original_slots[key] = [self.visual.get_material(i) for i in range(self.visual.get_num_materials())]
        sources = self.original_slots[key]
        for slot, source in enumerate(sources):
            self.visual.set_material(slot, source)
        slots = list(mesh.get_editor_property('static_materials'))
        # The Editor subsystem refuses PIE. Consume its actual pre-PIE LOD0
        # mapping with saved-package SHA/slot-name guards, never section==slot.
        cached = self.slot_cache.get('meshes', {}).get(key)
        _require(cached and cached.get('lod') == 0, 'Current mesh absent from idle-Editor section cache: '+key)
        project = Path(u.Paths.project_dir()).resolve()
        for filename, digest in cached['package_sha256'].items():
            _require(hashlib.sha256((project/filename).read_bytes()).hexdigest() == digest,
                     'Saved tooth mesh changed after section-cache generation: '+filename)
        _require([str(row.material_slot_name) for row in slots] == [row['name'] for row in cached['slots']],
                 'Current tooth slot names differ from the guarded cache')
        section_map = {row['section']: row['material_slot'] for row in cached['sections']}
        _require(sorted(section_map) == list(range(mesh.get_num_sections(0))), 'Cached LOD0 section count differs')
        sections = []
        for section, vertices, triangles, normals, uv0, tangents in self._sections(mesh):
            slot = section_map[section]
            _require(0 <= slot < len(sources), 'Invalid source LOD0 section material slot')
            enamel = mesh == self.original_mesh or str(slots[slot].material_slot_name) == 'MC_Damage_Enamel'
            sections.append((section, slot, enamel, vertices, triangles, normals, uv0, tangents))
        self.mids = []
        self.mid_enabled = {}
        pair_by_slot = {}
        enamel_slots = sorted({row[1] for row in sections if row[2]})
        # Never compile a newly encountered graph while our overlay exists.
        # The native enamel MID normally stays identical across damage stages,
        # so Chipped/Broken reuse the already compiled original graph pair.
        if any(_path(sources[slot]) not in self.compiled_pairs for slot in enamel_slots):
            self._destroy_overlay()
        for slot in enamel_slots:
            pair_key = _path(sources[slot])
            reused = pair_key in self.compiled_pairs
            if not reused:
                pair = self.pair_factory(sources[slot])
                _require(len(pair) == 3, 'Expected (A material, B material, report)')
                self.compiled_pairs[pair_key] = pair
            pair = self.compiled_pairs[pair_key]
            a_mid, a_report = self.mid_factory(self.world, pair[0], sources[slot], self.textures[self.state], self.parameters)
            b_mid, b_report = self.mid_factory(self.world, pair[1], sources[slot], self.textures[self.state], self.parameters)
            pair_by_slot[slot] = (a_mid, b_mid)
            self.mids.extend((a_mid, b_mid))
            for mid in (a_mid, b_mid):
                mid.set_scalar_parameter_value('AB_ToothEnabled', 1.)
                self.mid_enabled[_path(mid)] = 1.
            self.material_records.append(dict(mesh=key, slot=slot, source=_path(sources[slot]),
                pair=pair[2], compiled_pair_reused=reused, A=a_report, B=b_report))
        _require(pair_by_slot, 'Current tooth has no enamel section for this prototype')
        # Reuse the existing A graph for fully masked non-enamel slots. B keeps
        # the native Dentin/Crack interfaces; no invisible material asset exists.
        disabled_mid = None
        if any(not row[2] for row in sections):
            source_slot = enamel_slots[0]
            pair = self.compiled_pairs[_path(sources[source_slot])]
            disabled_mid, disabled_report = self.mid_factory(self.world, pair[0], sources[source_slot],
                self.textures[self.state], self.parameters)
            disabled_mid.set_scalar_parameter_value('AB_ToothEnabled', 0.)
            self.mid_enabled[_path(disabled_mid)] = 0.
            self.mids.append(disabled_mid)
            self.material_records.append(dict(mesh=key, scope='A non-enamel fully masked slots',
                enabled=0., source=_path(sources[source_slot]), A=disabled_report, compiled_pair_reused=True))
        self._ensure_overlay()
        if self.overlay.get_editor_property('static_mesh') != mesh:
            _require(self.overlay.call_method('SetStaticMesh', args=(mesh,)), 'Owned coat rejected the actual current mesh')
        self.overlay_materials = {}
        for slot in range(len(sources)):
            material = pair_by_slot[slot][0] if slot in pair_by_slot else disabled_mid
            _require(material is not None, 'Non-enamel coat slot needs a fully masked A MID')
            self.overlay.set_material(slot, material)
            self.overlay_materials[slot] = material
        mesh_report = dict(mesh=key, sections=[], original_bounds_preserved=True,
            geometry='Same current StaticMesh asset in an owned native component, no copied/rebuilt vertices',
            offset='Shader world-normal WPO0.6cm; original local position sampled before WPO')
        for section, slot, enamel, vertices, triangles, normals, uv0, tangents in sections:
            mesh_report['sections'].append(dict(section=section, source_slot=slot, enamel_coated=enamel,
                vertices=len(vertices), triangles=len(triangles)//3, uv0_preserved=True,
                all_original_uv_channels_preserved=True, normals_and_tangents_preserved=True,
                coat_enabled=1. if enamel else 0.))
        self.current_mesh, self.pair_by_slot = mesh, pair_by_slot
        self.report['meshes'].append(mesh_report)
        self._verify_reverse_side(sections)
        self.show_mode(self.mode)

    def _verify_reverse_side(self, sections):
        d, mask = self.domain, self.mask_arrays[self.state]
        rear, rear_painted, front = 0, 0, 0
        for _, _, enamel, vertices, _, normals, _, _ in sections:
            if not enamel:
                continue
            for p, normal in zip(vertices, normals):
                p3 = _xyz(p)
                alpha = domain_value(p3, d)*mask_sample(mask, tuple((p3[i]-self.minimum[i])/self.size[i] for i in range(3)))
                if _dot(_unit(_xyz(normal)), d['n']) < -.25:
                    rear += 1
                    rear_painted += alpha > 1.e-6
                if alpha > .05:
                    front += 1
        passed = rear > 0 and rear_painted == 0 and front > 0
        self.report['assertions'].append(dict(gate='reverse_face_unchanged', mesh=_path(self.visual.get_editor_property('static_mesh')),
            rear_vertices=rear, rear_painted=rear_painted, front_painted_vertices=front, passed=passed))
        self._write()
        _require(passed, 'Synthetic front domain leaked onto a reverse face or missed visible enamel')

    def set_state(self, state):
        _require(state in self.textures, 'State must be before or partial')
        self.state = state
        for mid in self.mids:
            mid.set_texture_parameter_value('AB_ToothWipe', self.textures[state])
        self._write()
        return self.readback()

    def show_mode(self, mode):
        _require(mode in ('A', 'B'), 'Mode must be A or B')
        _require(self.visual.get_editor_property('static_mesh') == self.current_mesh, 'Native mesh changed; call rebuild_current_mesh first')
        self.mode = mode
        originals = self.original_slots[_path(self.current_mesh)]
        for slot, source in enumerate(originals):
            self.visual.set_material(slot, self.pair_by_slot[slot][1] if mode == 'B' and slot in self.pair_by_slot else source)
        self.overlay.set_visibility(mode == 'A', False)
        self.overlay.set_hidden_in_game(mode != 'A', False)
        self.native_relief.set_hidden_in_game(True, False)
        snapshot = self.readback()
        self.report['modes'].append(snapshot)
        self._write()
        return snapshot

    def readback(self):
        """JSON-safe actual MID/slot checks, not an assertion based on requests."""
        _require(self._native_state() == self.native_snapshot, 'Native grime state changed during synthetic A/B')
        mids = []
        for mid in self.mids:
            actual_texture = mid.get_texture_parameter_value('AB_ToothWipe')
            _require(actual_texture == self.textures[self.state], 'A/B mask swap failed')
            vectors = {}
            for name, expected in self.parameters.items():
                value = mid.get_vector_parameter_value(name)
                actual = (float(value.r), float(value.g), float(value.b))
                _require(max(abs(a-b) for a, b in zip(actual, expected)) < .001, 'A/B vector mismatch: '+name)
                vectors[name] = actual
            enabled = float(mid.get_scalar_parameter_value('AB_ToothEnabled'))
            _require(abs(enabled-self.mid_enabled[_path(mid)]) < 1.e-6, 'A non-enamel masking gate mismatch')
            mids.append(dict(mid=_path(mid), actual_texture=_path(actual_texture), vectors=vectors,
                             actual_tooth_enabled=enabled))
        actual_materials = [_path(self.visual.get_material(i)) for i in range(self.visual.get_num_materials())]
        _require(self.overlay.get_collision_enabled() == self.u.CollisionEnabled.NO_COLLISION,
                 'Prototype must not alter collision')
        _require(self.overlay.get_editor_property('static_mesh') == self.current_mesh,
                 'A must use the exact current original/damage StaticMesh asset')
        _require(self.overlay_actor.get_owner() == self.tooth, 'Prototype actor must be owned by this tooth')
        original_pose, coat_pose = self.visual.get_world_transform(), self.overlay.get_world_transform()
        pose_error = max(math.dist(_xyz(original_pose.transform_location(self.u.Vector(*point))),
                                  _xyz(coat_pose.transform_location(self.u.Vector(*point))))
                         for point in ((0.,0.,0.), (1.,0.,0.), (0.,1.,0.), (0.,0.,1.)))
        _require(pose_error < .001, 'A and B component-local frames differ')
        for slot, expected in self.overlay_materials.items():
            _require(self.overlay.get_material(slot) == expected, 'A coating material slot mismatch')
        for slot, pair in self.pair_by_slot.items():
            expected = pair[1] if self.mode == 'B' else self.original_slots[_path(self.current_mesh)][slot]
            _require(self.visual.get_material(slot) == expected, 'Requested A/B mode was not applied to enamel')
        return dict(mode=self.mode, state=self.state, current_mesh=_path(self.current_mesh),
            native_state=self._native_state(), shared_mask_sha256=self.texture_records[self.state]['mask_sha256'],
            overlay_visible=self.overlay.is_visible(), overlay_collision=str(self.overlay.get_collision_enabled()),
            overlay_actor=_path(self.overlay_actor), overlay_mesh=_path(self.overlay.get_editor_property('static_mesh')),
            overlay_geometry='Owned native StaticMeshActor, same asset, shader WPO0.6cm',
            original_and_coat_basis_error_cm=pose_error,
            actual_visual_materials=actual_materials, mid_readbacks=mids,
            visual_transform=str(self.visual.get_world_transform()),
            overlay_transform=str(self.overlay.get_world_transform()),
            synthetic=True, native_brush_claim=False)

    def _write(self):
        if hasattr(self, 'report'):
            (self.output/'ToothFixture.json').write_text(json.dumps(self.report, indent=2), encoding='utf-8')

    def restore(self):
        if self.restored:
            return
        self.restored = True
        errors = []
        try:
            mesh_key = _path(self.visual.get_editor_property('static_mesh'))
            for slot, original in enumerate(self.original_slots.get(mesh_key, [])):
                self.visual.set_material(slot, original)
        except Exception as error:
            errors.append('restore materials: '+str(error))
        try:
            self.native_relief.set_hidden_in_game(self.native_relief_hidden, False)
        except Exception as error:
            errors.append('restore native relief: '+str(error))
        if self.overlay:
            try:
                self._destroy_overlay()
            except Exception as error:
                errors.append('destroy prototype StaticMeshActor: '+str(error))
        if hasattr(self, 'report'):
            self.report['restored'] = not errors
            self.report['restore_errors'] = errors
            self._write()
        return errors


def build_fixture(tooth, output, pair_factory, mid_factory, original_mesh=None,
                  domain=None, inward_world=None):
    return ToothABFixture(tooth, output, pair_factory, mid_factory,
                          original_mesh, domain, inward_world)
