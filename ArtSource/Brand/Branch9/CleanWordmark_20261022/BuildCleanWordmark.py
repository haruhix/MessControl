"""Extract Branch9 pixels, with local N-edge cleanup and the user's red 9.

Dependencies: NumPy, Pillow and OpenCV. All final files stay beside this script;
validation/temporary previews stay on E:/CodexWork/Branch9CleanIntro_20261022.
N retains the original A-over-N diagonal cut and original letter positions;
only its visible upper left edge becomes vertical. Other glyphs are unchanged.
"""
from pathlib import Path
from collections import defaultdict
import argparse
import hashlib
import json
import math

import cv2
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
REF = ROOT / 'Reference_Original.png'
SOURCE_SHA = 'dcffd5038af4c2191359879e4a4e74ee80e7bcb81c163c364b2857ff37d272c2'
CANVAS = (2048, 768)
ATLAS = (2048, 1024)
LOGO_WIDTH = 1840.0
GLYPHS = ('B', 'R', 'A', 'N', 'C', 'H', '9')
LEVEL = 127.5
RED_9 = (0xE3, 0x26, 0x36)
SOURCE_SHIFT = dict.fromkeys(GLYPHS, 0)
N_LEFT = 714.51556
N_A_CUT_START = (725.5436166, 447.8992207)
N_A_CUT_END = (776.8072964, 563.07101)
N_A_CUT_INTERSECTION_Y = N_A_CUT_START[1] + ((N_LEFT - N_A_CUT_START[0]) *
    (N_A_CUT_END[1] - N_A_CUT_START[1]) / (N_A_CUT_END[0] - N_A_CUT_START[0]))
N_POLYGON = [(N_LEFT, 333.42857), (800.6648549878855, 333.42857),
             (881.1375, 446.0324590002352), (881.1375, 333.42857),
             (956.92188, 333.42857), (956.92188, 563.07101),
             (872.6396623914759, 563.07101), (790.229725, 442.832095986712),
             (790.229725, 563.07101), (776.8072963818058, 563.07101),
             (N_LEFT, N_A_CUT_INTERSECTION_Y)]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def bbox(alpha, threshold=127):
    yy, xx = np.where(alpha > threshold)
    return [int(xx.min()), int(yy.min()), int(xx.max()) + 1, int(yy.max()) + 1] if xx.size else None


def rgba(alpha):
    image = Image.new('RGBA', (alpha.shape[1], alpha.shape[0]), (255, 255, 255, 0))
    image.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    return image


def colored_rgba(alpha, nine_alpha):
    """Straight alpha; propagate red RGB beyond 9's AA edge to avoid white halos."""
    result = np.full((*alpha.shape, 4), 255, np.uint8)
    result[..., 3] = alpha
    # Transparent RGB extends the nearest glyph's colour. Covered pixels of the
    # other glyphs always stay white, even where their bounding boxes overlap.
    other = (alpha.astype(np.int16) - nine_alpha.astype(np.int16)) > 0
    to_nine = cv2.distanceTransform((nine_alpha == 0).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    to_other = cv2.distanceTransform((~other).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    red = (nine_alpha > 0) | ((alpha == 0) & (to_nine < to_other))
    assert not np.any((nine_alpha > 0) & other), 'Coloured glyph alpha overlaps another glyph'
    result[red, :3] = RED_9
    return Image.fromarray(result)


def contour_loops(alpha):
    """Interpolated marching squares at half coverage; holes remain separate loops."""
    a = np.pad(alpha.astype(np.float32), 1)
    cases = ((a[:-1, :-1] >= LEVEL).astype(np.uint8) +
             (a[:-1, 1:] >= LEVEL).astype(np.uint8) * 2 +
             (a[1:, 1:] >= LEVEL).astype(np.uint8) * 4 +
             (a[1:, :-1] >= LEVEL).astype(np.uint8) * 8)
    yy, xx = np.where((cases != 0) & (cases != 15))
    lookup = {1: [(3, 0)], 2: [(0, 1)], 3: [(3, 1)], 4: [(1, 2)],
              6: [(0, 2)], 7: [(3, 2)], 8: [(2, 3)], 9: [(0, 2)],
              11: [(1, 2)], 12: [(1, 3)], 13: [(0, 1)], 14: [(0, 3)]}
    graph, positions, edges = defaultdict(list), {}, []
    for y, x in zip(yy.tolist(), xx.tolist()):
        values = (a[y, x], a[y, x + 1], a[y + 1, x + 1], a[y + 1, x])
        case = int(cases[y, x])
        if case == 5:
            pairs = [(0, 1), (2, 3)] if sum(values) / 4 >= LEVEL else [(3, 0), (1, 2)]
        elif case == 10:
            pairs = [(3, 0), (1, 2)] if sum(values) / 4 >= LEVEL else [(0, 1), (2, 3)]
        else:
            pairs = lookup[case]

        def node(edge):
            if edge == 0:
                key, first, second, start, axis = ('h', x, y), values[0], values[1], (x - .5, y - .5), (1, 0)
            elif edge == 1:
                key, first, second, start, axis = ('v', x + 1, y), values[1], values[2], (x + .5, y - .5), (0, 1)
            elif edge == 2:
                key, first, second, start, axis = ('h', x, y + 1), values[3], values[2], (x - .5, y + .5), (1, 0)
            else:
                key, first, second, start, axis = ('v', x, y), values[0], values[3], (x - .5, y - .5), (0, 1)
            fraction = float((LEVEL - first) / (second - first))
            positions[key] = (start[0] + fraction * axis[0], start[1] + fraction * axis[1])
            return key

        for e0, e1 in pairs:
            n0, n1 = node(e0), node(e1)
            index = len(edges)
            edges.append((n0, n1))
            graph[n0].append((n1, index))
            graph[n1].append((n0, index))
    assert all(len(adjacent) == 2 for adjacent in graph.values()), 'Open SVG contour'
    visited, loops = set(), []
    for index, (first, _) in enumerate(edges):
        if index in visited:
            continue
        current, loop = first, [positions[first]]
        while True:
            choices = [(other, edge) for other, edge in graph[current] if edge not in visited]
            if not choices:
                break
            current, edge = choices[0]
            visited.add(edge)
            if current == first:
                break
            loop.append(positions[current])
        assert current == first, 'Incomplete SVG contour'
        # Remove only exactly collinear vertices; no shape smoothing or curve fitting.
        simple = []
        for j, point in enumerate(loop):
            previous, following = loop[j - 1], loop[(j + 1) % len(loop)]
            cross = ((point[0] - previous[0]) * (following[1] - point[1]) -
                     (point[1] - previous[1]) * (following[0] - point[0]))
            if abs(cross) > 1e-9:
                simple.append(point)
        if len(simple) >= 3:
            loops.append(simple)
    return loops


def raster_loops(loops, size, supersample=4):
    mask = Image.new('1', (size[0] * supersample, size[1] * supersample), 0)
    for loop in loops:
        layer = Image.new('1', mask.size, 0)
        ImageDraw.Draw(layer).polygon([(x * supersample, y * supersample) for x, y in loop], fill=1)
        mask = ImageChops.logical_xor(mask, layer)
    return np.asarray(mask.convert('L').resize(size, Image.Resampling.LANCZOS))


def iou(a, b):
    a, b = a > 127, b > 127
    return float(np.count_nonzero(a & b) / np.count_nonzero(a | b))


def transformed_bbox(box, scale, tx, ty):
    return [box[0] * scale + tx, box[1] * scale + ty,
            box[2] * scale + tx, box[3] * scale + ty]


def preview_sheet(source, revised_source, delivered, source_box, work, report):
    sheet = Image.new('RGB', (1600, 1020), (17, 20, 25))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=21)
    panels = [
        ('SUPPLIED PIXELS - same scale, crop only', source.crop(tuple(source_box)).convert('RGB')),
        ('LOCAL REVISION - vertical visible N edge, original A-over-N cut/kerning, red 9', Image.alpha_composite(Image.new('RGBA', source.size, 'black'), revised_source).crop(tuple(source_box)).convert('RGB')),
        ('DELIVERED 2048x768 - centered, uniform scale, width 1840', Image.alpha_composite(Image.new('RGBA', CANVAS, 'black'), delivered).convert('RGB')),
    ]
    for row, (label, image) in enumerate(panels):
        y = row * 290
        draw.text((26, y + 14), label, fill=(225, 233, 238), font=font)
        ratio = min(1536 / image.width, 220 / image.height)
        size = (round(image.width * ratio), round(image.height * ratio))
        image = image.resize(size, Image.Resampling.LANCZOS)
        sheet.paste(image, ((1600 - size[0]) // 2, y + 54 + (220 - size[1]) // 2))
    draw.text((26, 891), 'Original cleanup IoU: %.9f; six retained silhouettes unchanged; N intentionally replaced.' % report['source_cleanup_iou'],
              fill=(159, 230, 196), font=font)
    draw.text((26, 927), 'N retains its diagonal A cut. Original positions/scale restored. Atlas reconstruction is pixel exact.',
              fill=(206, 216, 224), font=font)
    sheet.save(work / 'Original_vs_Clean_Comparison.png')


def run(work):
    assert ROOT.drive.upper() == 'E:' and work.drive.upper() == 'E:'
    work.mkdir(parents=True, exist_ok=True)
    assert hashlib.sha256(REF.read_bytes()).hexdigest() == SOURCE_SHA, 'The supplied original changed'
    source = Image.open(REF).convert('RGB')
    luminance = np.asarray(source.convert('L'))
    count, labels, stats, _ = cv2.connectedComponentsWithStats((luminance >= 128).astype(np.uint8), 8)
    components = [index for index in range(1, count) if int(stats[index, cv2.CC_STAT_AREA]) >= 1000]
    components.sort(key=lambda index: int(stats[index, cv2.CC_STAT_LEFT]))
    assert len(components) == 7, 'Expected exactly seven isolated glyph components'
    write_json(ROOT / 'reference_manifest.json', dict(source='Reference_Original.png', source_sha256=SOURCE_SHA,
        role='Unmodified user-supplied reference; local N edge cleanup/red-9 revision is recorded separately', source_size=list(source.size), expected_glyphs=list(GLYPHS),
        target_canvas=list(CANVAS), target_mark_width=LOGO_WIDTH, uniform_scaling=True,
        gates=dict(exact_source_copy=True, glyph_count=7, source_cleanup_silhouette_iou=1.,
                   source_roundtrip_iou_min=.995, svg_silhouette_iou_min=.995)))
    # Voronoi ownership retains AA near interlocking diagonals without assigning
    # a neighbour's pixels to the same glyph. Do not split by x rectangles.
    nearest = np.full(luminance.shape, np.inf, np.float32)
    owner = np.zeros(luminance.shape, np.uint8)
    for index, component in enumerate(components, 1):
        distance = cv2.distanceTransform((labels != component).astype(np.uint8), cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
        closer = distance < nearest
        nearest[closer] = distance[closer]
        owner[closer] = index
    alpha = np.where((nearest <= 3.) & (luminance >= 4), luminance, 0).astype(np.uint8)
    original_mask = np.isin(labels, components).astype(np.uint8) * 255
    assert iou(alpha, original_mask) == 1., 'Foreground cleanup changed the half-coverage silhouette'
    reference_alpha = alpha.copy()
    reference_glyphs = [np.where(owner == index, alpha, 0).astype(np.uint8) for index in range(1, 8)]
    source_glyphs = []
    retained_shapes = {}
    for name, original in zip(GLYPHS, reference_glyphs):
        if name == 'N':
            revised = raster_loops([N_POLYGON], source.size, supersample=8)
        else:
            shift = SOURCE_SHIFT[name]
            if shift:
                assert not np.any(original[:, -shift:]), 'Shift would clip the source glyph'
            revised = np.roll(original, shift, axis=1)
            retained_shapes[name] = bool(np.array_equal(np.roll(revised, -shift, axis=1), original))
            assert retained_shapes[name], 'A retained source silhouette changed'
        source_glyphs.append(revised)
    alpha = np.clip(np.sum(np.stack(source_glyphs).astype(np.uint16), axis=0), 0, 255).astype(np.uint8)
    source_box = bbox(alpha)
    width, height = source_box[2] - source_box[0], source_box[3] - source_box[1]
    scale = LOGO_WIDTH / width
    tx = (CANVAS[0] - width * scale) / 2 - source_box[0] * scale
    ty = (CANVAS[1] - height * scale) / 2 - source_box[1] * scale
    # OpenCV transforms pixel centres (index coordinates), SVG transforms pixel
    # corners. This half-pixel compensation keeps both in the same contract.
    affine = np.array([[scale, 0, tx + (scale - 1) / 2],
                       [0, scale, ty + (scale - 1) / 2]], np.float64)
    atlas = np.zeros((ATLAS[1], ATLAS[0]), np.uint8)
    atlas_nine = np.zeros_like(atlas)
    layers, glyph_records, all_source_loops, svg_groups = [], [], [], []
    for index, (name, component) in enumerate(zip(GLYPHS, components), 1):
        glyph_alpha = source_glyphs[index - 1]
        target = cv2.warpAffine(glyph_alpha, affine, CANVAS, flags=cv2.INTER_LANCZOS4,
                                borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        layers.append(target)
        box, support = bbox(target), bbox(target, 0)
        center = [(box[0] + box[2]) / 2, (box[1] + box[3]) / 2]
        tile_origin = [((index - 1) % 4) * 512, ((index - 1) // 4) * 512]
        sw, sh = support[2] - support[0], support[3] - support[1]
        assert sw <= 512 and sh <= 512
        destination = [tile_origin[0] + (512 - sw) // 2, tile_origin[1] + (512 - sh) // 2]
        atlas[destination[1]:destination[1] + sh, destination[0]:destination[0] + sw] = target[support[1]:support[3], support[0]:support[2]]
        if name == '9':
            atlas_nine[destination[1]:destination[1] + sh, destination[0]:destination[0] + sw] = target[support[1]:support[3], support[0]:support[2]]
        offset = [destination[0] - support[0], destination[1] - support[1]]
        atlas_box = [box[0] + offset[0], box[1] + offset[1], box[2] + offset[0], box[3] + offset[1]]
        source_glyph_box = bbox(glyph_alpha)
        record = dict(name=name, bbox=box, center=center,
                      atlas_center=[center[0] + offset[0], center[1] + offset[1]], atlas_bbox=atlas_box,
                      width=box[2] - box[0], height=box[3] - box[1],
                      alpha_support_bbox=support,
                      atlas_support_bbox=[*destination, destination[0] + sw, destination[1] + sh],
                      atlas_tile_origin=tile_origin, atlas_tile_center=[tile_origin[0] + 256, tile_origin[1] + 256],
                      full_to_atlas_pixel_offset=offset, source_bbox=source_glyph_box,
                      source_component_ids=[] if name == 'N' else [component],
                      source_solid_pixels=int(np.count_nonzero(glyph_alpha > 127)),
                      reference_component_solid_pixels=int(stats[component, cv2.CC_STAT_AREA]),
                      reference_source_bbox=bbox(reference_glyphs[index - 1]),
                      source_translation=[SOURCE_SHIFT[name], 0],
                      silhouette_origin='Eleven-vertex fitted source N; upper left vertical; original A separator cut retained' if name == 'N' else 'Unchanged supplied coverage and source position',
                      color_srgb='#E32636' if name == '9' else '#FFFFFF')
        if name == 'N':
            record['source_polygon'] = N_POLYGON
            record['A_overlap_cut_source_line'] = [N_A_CUT_START, N_A_CUT_END]
            record['vertical_left_A_cut_intersection_source'] = [N_LEFT, N_A_CUT_INTERSECTION_Y]
        glyph_records.append(record)
        loops = [N_POLYGON] if name == 'N' else contour_loops(glyph_alpha)
        record['svg_closed_loops'] = len(loops)
        all_source_loops.extend(loops)
        path = ' '.join('M' + ' L'.join('%.5f,%.5f' % point for point in loop) + ' Z' for loop in loops)
        svg_groups.append('    <path id="Glyph_%s" fill="%s" fill-rule="evenodd" d="%s"/>' % (name, '#E32636' if name == '9' else '#FFFFFF', path))
    clean = np.clip(np.sum(np.stack(layers).astype(np.uint16), axis=0), 0, 255).astype(np.uint8)
    delivered = colored_rgba(clean, layers[-1])
    atlas_image = colored_rgba(atlas, atlas_nine)
    revised_source = colored_rgba(alpha, source_glyphs[-1])
    delivered.save(ROOT / 'T_Branch9_CleanWordmark.png')
    atlas_image.save(ROOT / 'T_Branch9_CleanGlyphAtlas.png')
    rgba(np.zeros((16, 16), np.uint8)).save(ROOT / 'T_Branch9_Transparent.png')
    rgba(reference_alpha).save(work / 'Exact_Reference_Alpha.png')
    revised_source.save(work / 'Revised_Source_RGBA.png')
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="2048" height="768" viewBox="0 0 2048 768">\n'
           '  <title>Branch9 - local vertical N edge, original A overlap, red 9</title>\n'
           '  <desc>N retains its original A separator diagonal and kerning; only the visible upper left edge is made vertical. All source positions are restored. Other six silhouettes retain supplied contours; 9 is sRGB #E32636. Counters use evenodd.</desc>\n'
           '  <g fill="#fff" transform="translate(%.9f %.9f) scale(%.9f)">\n' % (tx, ty, scale) +
           '\n'.join(svg_groups) + '\n  </g>\n</svg>\n')
    (ROOT / 'Branch9_CleanWordmark.svg').write_text(svg, encoding='utf8')
    # Atlas identity is reconstructed by exact pixel copying, not another resize.
    assembled = np.zeros_like(clean, dtype=np.uint16)
    for glyph in glyph_records:
        sx0, sy0, sx1, sy1 = glyph['alpha_support_bbox']
        ax0, ay0, ax1, ay1 = glyph['atlas_support_bbox']
        assembled[sy0:sy1, sx0:sx1] += atlas[ay0:ay1, ax0:ax1]
    assert np.array_equal(clean, np.clip(assembled, 0, 255).astype(np.uint8)), 'Atlas identity does not match the delivered full logo'
    full_pixels, atlas_pixels = np.asarray(delivered), np.asarray(atlas_image)
    assembled_rgba = np.zeros_like(full_pixels, dtype=np.uint16)
    for glyph in glyph_records:
        sx0, sy0, sx1, sy1 = glyph['alpha_support_bbox']
        ax0, ay0, ax1, ay1 = glyph['atlas_support_bbox']
        tile = atlas_pixels[ay0:ay1, ax0:ax1]
        active = tile[..., 3] > 0
        destination = assembled_rgba[sy0:sy1, sx0:sx1]
        overlap = active & (destination[..., 3] > 0)
        assert np.array_equal(destination[overlap, :3], tile[overlap, :3]), 'Differently coloured glyph coverage overlaps'
        destination[active, :3] = tile[active, :3]
        destination[..., 3] += tile[..., 3]
    assembled_rgba = np.clip(assembled_rgba, 0, 255).astype(np.uint8)
    assert np.array_equal(full_pixels[clean > 0], assembled_rgba[clean > 0]), 'Atlas visible RGBA differs from full image'
    nine_pixels = layers[-1] > 0
    nine_edge = nine_pixels & (layers[-1] < 255)
    assert np.all(full_pixels[nine_pixels, :3] == RED_9), 'Nine coverage has a wrong RGB tint'
    assert np.all(full_pixels[nine_edge, :3] == RED_9), 'Nine AA edge RGB must remain unassociated red'
    assert np.all(atlas_pixels[atlas_nine > 0, :3] == RED_9), 'Atlas nine RGB differs from full image'
    assert np.all(full_pixels[(clean > 0) & ~nine_pixels, :3] == 255), 'Another glyph was recoloured'
    recovered = cv2.warpAffine(clean, cv2.invertAffineTransform(affine), source.size, flags=cv2.INTER_LANCZOS4)
    traced = raster_loops(all_source_loops, source.size, supersample=4)
    report = dict(source_cleanup_iou=iou(reference_alpha, original_mask),
                  source_roundtrip_iou=iou(recovered, alpha), svg_source_iou=iou(traced, alpha),
                  atlas_identity_pixel_equal=True, source_bbox=source_box, target_bbox=bbox(clean),
                  atlas_visible_rgba_identity_pixel_equal=True,
                  nine_full_and_atlas_coverage_rgb_equal=True,
                  nine_nonopaque_coverage_pixels=int(np.count_nonzero(nine_edge)),
                  uniform_scale=scale, target_ideal_bbox=transformed_bbox(source_box, scale, tx, ty),
                  source_bbox_aspect_ratio=width / height,
                  target_integer_bbox_aspect_ratio=(bbox(clean)[2] - bbox(clean)[0]) / (bbox(clean)[3] - bbox(clean)[1]),
                  integer_bbox_aspect_rounding_percent=100 * (((bbox(clean)[2] - bbox(clean)[0]) / (bbox(clean)[3] - bbox(clean)[1])) / (width / height) - 1),
                  source_size=list(source.size), glyph_count=len(glyph_records),
                  retained_source_coverage_pixel_equal=retained_shapes,
                  n_visible_upper_left_source_x=N_LEFT, n_polygon_vertex_count=len(N_POLYGON),
                  n_vertical_left_A_cut_intersection_source=[N_LEFT, N_A_CUT_INTERSECTION_Y],
                  nine_srgb='#E32636', nine_alpha_unchanged_before_uniform_scale=retained_shapes['9'],
                  global_uniform_scale_change_percent=100 * (scale / (1840. / 1487.) - 1),
                  user_revision='Original kerning/source positions/scale restored; N upper left edge vertical until existing A separator cut; 9 recoloured #E32636.',
                  cleanup='Keep original luminance coverage within three source pixels of seven solid glyphs; remove luminance below 4. Half-coverage silhouette unchanged.',
                  limitations=['Supplied raster, not a recovered original master vector.',
                               'Only N contour cleanup and red 9 intentionally differ from the supplied reference; no source letter positions or kerning are shifted.',
                               'Other six PNG silhouettes retain supplied coverage; their SVG represents the interpolated 127.5 contour only.',
                               'Resampling uses one identical X/Y scale; measured integer bounds can round by one pixel.',
                               'Bounding boxes overlap for slanted/interlocking letters. Use atlas ownership, never x-cell clipping.'])
    assert report['source_roundtrip_iou'] >= .995 and report['svg_source_iou'] >= .995, report
    # Logical cells are animation timing guides, never glyph segmentation cuts.
    cell_edges = [float(bbox(clean)[0])] + [(a['center'][0] + b['center'][0]) / 2 for a, b in zip(glyph_records, glyph_records[1:])] + [float(bbox(clean)[2])]
    for index, record in enumerate(glyph_records):
        record['logical_cell_x'] = cell_edges[index:index + 2]
        ideal = transformed_bbox(record['source_bbox'], scale, tx, ty)
        record['ideal_uniform_bbox'] = ideal
        record['raster_bbox_rounding_delta'] = [actual - expected for actual, expected in zip(record['bbox'], ideal)]
    manifest = dict(canvas=list(CANVAS), atlas_size=list(ATLAS), atlas_tile_size=[512, 512],
                    glyphs=glyph_records, logo_width_requested=LOGO_WIDTH,
                    source_sha256=SOURCE_SHA, source_size=list(source.size), source_bbox=source_box,
                    uniform_transform=dict(scale=scale, translate=[tx, ty]),
                    mapping='atlasPixel = fullCanvasPixel - glyph.center + glyph.atlas_center; use atlas_support_bbox for AA-safe sampling.',
                    png_format='straight-alpha RGBA; six white glyphs and sRGB #E32636 nine; nearest glyph RGB extends into transparent edges', validation=report)
    manifest['files'] = {name: dict(sha256=hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), bytes=(ROOT / name).stat().st_size)
                         for name in ('Reference_Original.png', 'T_Branch9_CleanWordmark.png', 'T_Branch9_CleanGlyphAtlas.png', 'T_Branch9_Transparent.png', 'Branch9_CleanWordmark.svg')}
    write_json(ROOT / 'OutputManifest.json', manifest)
    write_json(work / 'Validation.json', report)
    preview_sheet(source, revised_source, delivered, source_box, work, report)
    Image.alpha_composite(Image.new('RGBA', ATLAS, (22, 25, 30, 255)), atlas_image).convert('RGB').save(work / 'GlyphAtlas_Preview.png')
    Image.alpha_composite(Image.new('RGBA', CANVAS, 'black'), delivered).convert('RGB').save(work / 'CleanWordmark_Preview.png')
    n_sheet = Image.new('RGB', (1060, 620), (17, 20, 25))
    n_draw = ImageDraw.Draw(n_sheet)
    n_font = ImageFont.load_default(size=25)
    for column, (title, mask) in enumerate((('BEFORE - supplied N', reference_glyphs[3]), ('AFTER - vertical edge, original A cut', source_glyphs[3]))):
        crop = rgba(mask).crop(tuple(bbox(mask, 0)))
        crop.thumbnail((450, 500), Image.Resampling.LANCZOS)
        crop = crop.resize((round(crop.width * min(450 / crop.width, 500 / crop.height)), round(crop.height * min(450 / crop.width, 500 / crop.height))), Image.Resampling.LANCZOS)
        n_draw.text((column * 530 + 18, 20), title, fill='white', font=n_font)
        background = Image.new('RGBA', crop.size, (17, 20, 25, 255))
        n_sheet.paste(Image.alpha_composite(background, crop).convert('RGB'), (column * 530 + (530 - crop.width) // 2, 90 + (500 - crop.height) // 2))
    n_sheet.save(work / 'N_Before_After.png')
    overlay = np.zeros((*alpha.shape, 3), np.uint8)
    same, reference_only, trace_only = (alpha > 127) & (traced > 127), (alpha > 127) & (traced <= 127), (alpha <= 127) & (traced > 127)
    overlay[same], overlay[reference_only], overlay[trace_only] = (255, 255, 255), (255, 35, 35), (0, 225, 255)
    Image.fromarray(overlay).save(work / 'Source_vs_SVG_Overlay.png')
    print(json.dumps(dict(status='passed', source_root=str(ROOT), manifest=str(ROOT / 'OutputManifest.json'),
                          preview=str(work / 'Original_vs_Clean_Comparison.png'), validation=report), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path, default=Path('E:/CodexWork/Branch9CleanIntro_20261022/SourceValidation'))
    run(parser.parse_args().work.resolve())
