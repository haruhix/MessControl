"""32 Blender motion studies assembled from three original Wizard performances.

These are edits/compositions of existing retargeted artist actions, not 32 newly
downloaded Mixamo animations. This module only describes clips; it does not touch
Blender, the rig, source files or Unreal assets.

Consumer contract:
* fps=30; frame ranges are inclusive.
* Frames 1..9 use the same neutral pose for every study.
* The first source is the lower-body foundation. Segments from other sources
  affect upper controls only; lower_source/lower_source_frame identify the held
  foundation pose. Preserve original feet and keyed IK/FK properties.
* Sample timing_points as monotone cubic absolute (output frame, source frame)
  pairs, then crossfade from the preceding evaluated performance for blend_in
  frames. Adjacent same-source strokes that continue in the same direction are
  merged into one timing map; internal source beats have continuous tangents.
  Reversed source ranges are intentional. Repeated endpoint values make timing
  velocity zero only at holds, reversals and cross-source boundaries.
* The final 20 frames blend to the same neutral pose. Do not cut to identity.
* Source holds/pingpongs below are short animation studies, not verified seamless
  gameplay channels. Their source section and explicit entry/exit are retained.
* No rig edits, scaling/stretch amplification or new large arm rotations.
"""

FPS = 30
NEUTRAL_IN_FRAMES = 9
NEUTRAL_OUT_FRAMES = 20

ARTIST_ACTIONS = {
    "Firenut": {"action": "NUT_W_Firenut", "frame_start": 1, "frame_end": 70},
    "CastSummon": {"action": "NUT_W_CastSummon", "frame_start": 1, "frame_end": 66},
    "NutRain": {"action": "NUT_W_NutRain", "frame_start": 1, "frame_end": 99},
}

GROUPS = (
    "Направленные заклинания",
    "Тяжёлые заклинания",
    "Ритуалы земли",
    "Поддержание магии",
    "Защитные жесты",
    "Служебная магия",
    "Отмена и срыв",
    "Комбинации",
)


def _continuous_steps(first, second):
    """A shared beat is continuous only if both motions keep the same direction."""
    a_source, a_first, a_last, _ = first
    b_source, b_first, b_last, _ = second
    return (a_source == b_source and a_last == b_first
            and (a_last - a_first) * (b_last - b_first) > 0)


def _timing_points(source, first, last, start, end, *, hold_start=True, hold_end=True):
    """Source beats with short endpoint holds; compatible with existing remap()."""
    motion_start = start + 2 if hold_start else start
    motion_end = end - 2 if hold_end else end
    points = [(start, first)]
    if motion_start != start:
        points.append((motion_start, first))
    beats = {
        "Firenut": (18, 27, 36, 44, 53, 61),
        "CastSummon": (9, 17, 25, 34, 42, 50, 58),
        "NutRain": (13, 26, 38, 50, 62, 74, 87),
    }[source]
    # Full-source studies give preparation a little more space. Partial strokes
    # preserve source-relative timing; recipes express holds as explicit segments.
    full_timing = {
        ("Firenut", 1, 70): {18: .32, 27: .44, 36: .58, 44: .70, 53: .82, 61: .92},
        ("CastSummon", 1, 66): {9: .15, 17: .26, 25: .40, 34: .54, 42: .68, 50: .80, 58: .91},
        ("NutRain", 1, 99): {13: .14, 26: .27, 38: .40, 50: .58, 62: .73, 74: .84, 87: .94},
    }.get((source, first, last), {})
    if first != last:
        interior = [f for f in beats if min(first, last) < f < max(first, last)]
        if last < first:
            interior.reverse()
        for source_frame in interior:
            fraction = full_timing.get(source_frame, (source_frame - first) / (last - first))
            frame = round(motion_start + fraction * (motion_end - motion_start))
            if points[-1][0] < frame < motion_end:
                points.append((frame, source_frame))
    points.append((motion_end, last))
    if motion_end != end:
        points.append((end, last))
    return points


def _recipe(name, label, group_index, use, steps, *, loop=None):
    """steps: (source, source_start, source_end, output_frame_count)."""
    cursor = NEUTRAL_IN_FRAMES + 1
    segments = []
    foundation_source = steps[0][0]
    foundation_frame = steps[0][1]
    loop_range = loop.get("source_frames") if loop else None
    loop_flags = [bool(loop and loop.get("mode") == "source_pingpong"
        and source == loop.get("source") and loop_range
        and min(loop_range) <= first <= max(loop_range)
        and min(loop_range) <= last <= max(loop_range))
        for source, first, last, _ in steps]
    parts = ["upper" if flag or step[0] != foundation_source else "all"
        for step, flag in zip(steps, loop_flags)]
    for index, (source, first, last, count) in enumerate(steps):
        continuous_in = (index > 0 and _continuous_steps(steps[index - 1], steps[index])
            and parts[index - 1] == parts[index] and loop_flags[index - 1] == loop_flags[index])
        continuous_out = (index + 1 < len(steps) and _continuous_steps(steps[index], steps[index + 1])
            and parts[index] == parts[index + 1] and loop_flags[index] == loop_flags[index + 1])
        is_loop_stroke = loop_flags[index]
        part = parts[index]
        segment = {
            "start": cursor,
            "end": cursor + count - 1,
            "source": source,
            "source_start": first,
            "source_end": last,
            "blend_in": 6 if first == last else 8,
            "part": part,
            "timing_points": _timing_points(source, first, last, cursor, cursor + count - 1,
                hold_start=not continuous_in, hold_end=not continuous_out),
            "ease": "MONOTONE_CUBIC_WITH_ZERO_ENDPOINT_VELOCITY",
        }
        if part == "upper":
            segment["lower_source"] = source if is_loop_stroke else foundation_source
            segment["lower_source_frame"] = loop_range[0] if is_loop_stroke else foundation_frame
            if is_loop_stroke:
                segment["loop_upper_only"] = True
        else:
            foundation_frame = last
        if continuous_in:
            previous = segments[-1]
            previous["end"] = segment["end"]
            previous["source_end"] = segment["source_end"]
            # The previous endpoint already describes the common source beat.
            # Keeping the next start as well would create a one-frame plateau,
            # forcing the monotone cubic tangent to zero despite a smooth stroke.
            previous["timing_points"].extend(segment["timing_points"][1:])
            previous["continuous_source_beats"] = True
        else:
            segments.append(segment)
        cursor += count
    group = GROUPS[group_index]
    result = {
        "name": name,
        "labelRU": label,
        "groupRU": group,
        "useRU": use,
        # Plain aliases keep simple existing preview/composition helpers usable.
        "label": label,
        "group": group,
        "use": use,
        "fps": FPS,
        "frames": cursor - 1 + NEUTRAL_OUT_FRAMES,
        "neutral_in_frames": NEUTRAL_IN_FRAMES,
        "neutral_out_frames": NEUTRAL_OUT_FRAMES,
        "segments": segments,
        "lower_foundation_source": foundation_source,
        "provenance": "Composition of three existing original artist actions",
        "study_only": True,
    }
    if loop:
        result["loop"] = loop
    return result


RECIPES = [
    # 01..04: establish the original release, then vary its surrounding gestures.
    _recipe("Fire_Quick", "Быстрый выпуск", 0,
        "Короткий направленный каст с полной исходной дугой и возвратом.", [
            ("Firenut", 1, 70, 60),
        ]),
    _recipe("Fire_AimThenRelease", "Подготовить и выпустить", 0,
        "Отделить подготовку, выпуск и возврат, сохранив исходные руки.", [
            ("Firenut", 1, 18, 24),
            ("Firenut", 18, 36, 20),
            ("Firenut", 36, 70, 26),
        ]),
    _recipe("Fire_DoublePulse", "Двойной импульс", 0,
        "Повторить исходный выпуск после повторного короткого набора.", [
            ("Firenut", 1, 18, 20),
            ("Firenut", 18, 36, 18),
            ("Firenut", 36, 18, 18),
            ("Firenut", 18, 36, 18),
            ("Firenut", 36, 70, 26),
        ]),
    _recipe("Fire_LiftThenThrow", "Поднять и направить", 0,
        "Начать подъёмом из призыва и перейти в направленный выпуск.", [
            ("CastSummon", 1, 17, 26),
            ("Firenut", 18, 44, 24),
            ("Firenut", 44, 70, 26),
        ]),

    # 05..08: power is expressed through source anticipation and composition.
    _recipe("Heavy_ChargedFire", "Заряженный выпуск", 1,
        "Подержать исходную подготовку и выпустить с полноценным возвратом.", [
            ("Firenut", 1, 18, 26),
            ("Firenut", 18, 18, 12),
            ("Firenut", 18, 36, 18),
            ("Firenut", 36, 70, 34),
        ]),
    _recipe("Heavy_GroundCharge", "Набор от земли", 1,
        "Низкая загрузка из дождя перед направленным силовым выпуском.", [
            ("NutRain", 1, 38, 28),
            ("Firenut", 18, 36, 24),
            ("Firenut", 36, 70, 30),
        ]),
    _recipe("Heavy_LiftAndLaunch", "Поднять и запустить", 1,
        "Полный подъём из призыва переходит в бросок исходного огненного каста.", [
            ("CastSummon", 1, 42, 36),
            ("Firenut", 18, 36, 22),
            ("Firenut", 36, 70, 30),
        ]),
    _recipe("Heavy_RadialRelease", "Мощное раскрытие", 1,
        "Односторонняя подготовка переходит в широкое раскрытие обеих рук.", [
            ("Firenut", 1, 18, 24),
            ("NutRain", 50, 62, 30),
            ("NutRain", 62, 99, 37),
        ]),

    # 09..12: the original Summon has a distinct low-to-high source gesture.
    _recipe("Ground_SummonOriginalArc", "Поднять из земли", 2,
        "Исходный призыв целиком: поднять, собрать внизу, поднять и вернуть руки.", [
            ("CastSummon", 1, 66, 66),
        ]),
    _recipe("Ground_RainToSummon", "Низкий ритуал и подъём", 2,
        "Глубокая загрузка дождя переходит в узнаваемый подъём призыва.", [
            ("NutRain", 1, 38, 26),
            ("CastSummon", 25, 42, 20),
            ("CastSummon", 42, 66, 24),
        ]),
    _recipe("Ground_SealAndLift", "Печать и подъём", 2,
        "Короткое низкое движение туда и обратно перед исходным подъёмом.", [
            ("CastSummon", 1, 25, 24),
            ("CastSummon", 25, 34, 16),
            ("CastSummon", 34, 25, 20),
            ("CastSummon", 25, 66, 36),
        ]),
    _recipe("Ground_LiftAndPress", "Подъём, опускание и второй подъём", 2,
        "Первый подъём переходит в опускание и самостоятельный второй подъём.", [
            ("CastSummon", 1, 42, 28),
            ("CastSummon", 42, 25, 22),
            ("CastSummon", 25, 66, 28),
        ]),

    # 13..16: explicit holds/short source reversals; no arbitrary modulo loop.
    _recipe("Channel_RainOriginalArc", "Собрать и раскрыть бурю", 3,
        "Исходный дождь целиком с нейтральным входом и выходом.", [
            ("NutRain", 1, 99, 99),
        ]),
    _recipe("Channel_RainHold", "Удержать раскрытие", 3,
        "После исходного раскрытия удержать широкую позу и выйти по исходнику.", [
            ("NutRain", 1, 62, 46),
            ("NutRain", 62, 62, 24),
            ("NutRain", 62, 99, 37),
        ], loop={
            "mode": "hold", "source": "NutRain", "source_frame": 62,
            "timeline_frames": [56, 79], "seam_blend_frames": 6,
        }),
    _recipe("Channel_RainBreathing", "Два импульса бури", 3,
        "Дважды пройти короткий исходный участок раскрытия вперёд и обратно.", [
            ("NutRain", 1, 62, 46),
            ("NutRain", 62, 68, 12),
            ("NutRain", 68, 62, 12),
            ("NutRain", 62, 68, 12),
            ("NutRain", 68, 62, 12),
            ("NutRain", 62, 99, 37),
        ], loop={
            "mode": "source_pingpong", "source": "NutRain",
            "source_frames": [62, 68], "timeline_frames": [56, 103],
            "repetitions": 2, "seam_blend_frames": 8,
        }),
    _recipe("Channel_SummonSustain", "Поддержать поднятую магию", 3,
        "Малый исходный участок поднятых рук, затем исходное завершение призыва.", [
            ("CastSummon", 1, 42, 34),
            ("CastSummon", 42, 50, 12),
            ("CastSummon", 50, 42, 12),
            ("CastSummon", 42, 50, 12),
            ("CastSummon", 50, 66, 20),
        ], loop={
            "mode": "source_pingpong", "source": "CastSummon",
            "source_frames": [42, 50], "timeline_frames": [44, 67],
            "repetitions": 1, "seam_blend_frames": 8,
        }),

    # 17..20: gesture studies only, not authored blocking/dodge gameplay.
    _recipe("Defense_GuardAndRelax", "Выставить и убрать защиту", 4,
        "Удержать исходную направленную подготовку и мягко убрать руки.", [
            ("Firenut", 1, 18, 22),
            ("Firenut", 18, 18, 16),
            ("Firenut", 18, 1, 26),
        ]),
    _recipe("Defense_LowBrace", "Низкая защитная стойка", 4,
        "Собрать исходную низкую позу дождя и вернуться без выпуска.", [
            ("NutRain", 1, 38, 32),
            ("NutRain", 38, 38, 12),
            ("NutRain", 38, 1, 32),
        ]),
    _recipe("Defense_RaiseDeflectSettle", "Поднять, отвести, успокоить", 4,
        "Поднятие рук из призыва, направленная дуга огня, завершение призыва.", [
            ("CastSummon", 1, 17, 20),
            ("Firenut", 18, 36, 22),
            ("CastSummon", 42, 66, 28),
        ]),
    _recipe("Defense_CounterImpulse", "Защитный ответ", 4,
        "Направленная подготовка переходит в подъём и исходный возврат огня.", [
            ("Firenut", 1, 18, 24),
            ("CastSummon", 25, 42, 24),
            ("Firenut", 36, 70, 30),
        ]),

    # 21..24: retain source poses but make their ordering tell another intent.
    _recipe("Utility_DirectedSignal", "Направить сигнал", 5,
        "Сначала обозначить направление, затем поднять руку и завершить дугу.", [
            ("Firenut", 1, 18, 24),
            ("NutRain", 13, 26, 28),
            ("NutRain", 26, 99, 58),
        ]),
    _recipe("Utility_MarkArea", "Обозначить область", 5,
        "Опустить руки к области и раскрыть обе руки по исходному дождю.", [
            ("CastSummon", 1, 25, 22),
            ("NutRain", 50, 62, 30),
            ("NutRain", 62, 99, 30),
        ]),
    _recipe("Utility_PointAndOffer", "Указать и предложить", 5,
        "Направленная дуга сменяется низким жестом предложения и возвратом.", [
            ("Firenut", 1, 36, 24),
            ("CastSummon", 25, 34, 28),
            ("Firenut", 44, 70, 26),
        ]),
    _recipe("Utility_DrawAndRaise", "Собрать предмет и поднять", 5,
        "Низкий жест призыва, выбор направления и исходный подъём рук.", [
            ("CastSummon", 1, 25, 24),
            ("Firenut", 1, 18, 26),
            ("CastSummon", 34, 66, 28),
        ]),

    # 25..28: reversible source preparation makes cancellation readable.
    _recipe("Interrupt_CancelFire", "Отменить направленный каст", 6,
        "Начать исходный набор, замереть и убрать жест по обратному исходнику.", [
            ("Firenut", 1, 18, 26),
            ("Firenut", 18, 18, 6),
            ("Firenut", 18, 1, 26),
        ]),
    _recipe("Interrupt_FailedSummon", "Призыв не состоялся", 6,
        "Дойти до низкой подготовки, задержаться и отказаться от подъёма.", [
            ("CastSummon", 1, 25, 28),
            ("CastSummon", 25, 25, 10),
            ("CastSummon", 25, 1, 28),
        ]),
    _recipe("Interrupt_CancelStorm", "Отменить бурю", 6,
        "Пройти низкий сбор дождя, остановиться перед раскрытием и вернуться.", [
            ("NutRain", 1, 50, 35),
            ("NutRain", 50, 50, 8),
            ("NutRain", 50, 1, 35),
        ]),
    _recipe("Interrupt_MisfireAndWithdraw", "Сорвать выпуск и убрать руки", 6,
        "Начать выпуск, провалить жест в низкую позу и отозвать подготовку.", [
            ("Firenut", 1, 36, 28),
            ("NutRain", 50, 38, 20),
            ("Firenut", 18, 1, 26),
        ]),

    # 29..32: distinct multi-performance sequences, with stable lower foundations.
    _recipe("Combo_FireThenSummon", "Выпуск, затем призыв", 7,
        "Полный направленный каст и самостоятельный призыв в одной последовательности.", [
            ("Firenut", 1, 70, 55),
            ("CastSummon", 1, 66, 55),
        ]),
    _recipe("Combo_FireThenStorm", "Выпуск, затем буря", 7,
        "После направленного выпуска сразу собрать и раскрыть область.", [
            ("Firenut", 1, 44, 40),
            ("NutRain", 26, 99, 65),
        ]),
    _recipe("Combo_SummonThenFire", "Поднять, затем выпустить", 7,
        "Довести призыв до поднятых рук и направить следующий импульс вперёд.", [
            ("CastSummon", 1, 50, 44),
            ("Firenut", 18, 70, 44),
        ]),
    _recipe("Combo_StormFireSettle", "Раскрытие, выпуск, завершение", 7,
        "Широкое раскрытие, направленный выпуск и спокойное завершение призыва.", [
            ("NutRain", 1, 62, 48),
            ("Firenut", 18, 44, 20),
            ("CastSummon", 42, 66, 25),
        ]),
]

WIZARD_MIXAMO_POOL_RECIPES = RECIPES


def validate_recipes():
    """Return contract errors without authoring or evaluating a Blender scene."""
    errors = []
    if len(RECIPES) != 32:
        errors.append("Expected 32 recipes")
    names = [r["name"] for r in RECIPES]
    if len(names) != len(set(names)):
        errors.append("Recipe names must be unique")
    for group in GROUPS:
        if sum(r["groupRU"] == group for r in RECIPES) != 4:
            errors.append("Expected four recipes in " + group)
    for recipe in RECIPES:
        name = recipe["name"]
        if not 75 <= recipe["frames"] <= 180:
            errors.append(name + ": duration outside 2.5..6 seconds")
        cursor = NEUTRAL_IN_FRAMES + 1
        for segment in recipe["segments"]:
            source = ARTIST_ACTIONS.get(segment["source"])
            if not source:
                errors.append(name + ": unknown source")
                continue
            if segment["start"] != cursor or segment["end"] < cursor:
                errors.append(name + ": noncontiguous segment range")
            for key in ("source_start", "source_end"):
                if not source["frame_start"] <= segment[key] <= source["frame_end"]:
                    errors.append(name + ": source frame outside artist action")
            count = segment["end"] - segment["start"] + 1
            if not 6 <= segment["blend_in"] <= min(12, count):
                errors.append(name + ": invalid segment entry blend")
            if segment["part"] not in ("all", "upper"):
                errors.append(name + ": unknown pose layer")
            if segment["part"] == "upper":
                lower = ARTIST_ACTIONS.get(segment.get("lower_source"))
                frame = segment.get("lower_source_frame", -1)
                if not lower or not lower["frame_start"] <= frame <= lower["frame_end"]:
                    errors.append(name + ": invalid lower foundation")
            points = segment["timing_points"]
            if points[0] != (segment["start"], segment["source_start"]) or points[-1] != (segment["end"], segment["source_end"]):
                errors.append(name + ": timing points miss source endpoints")
            if any(b[0] <= a[0] for a, b in zip(points, points[1:])):
                errors.append(name + ": nonascending timing frame coordinates")
            cursor = segment["end"] + 1
        if cursor - 1 + NEUTRAL_OUT_FRAMES != recipe["frames"]:
            errors.append(name + ": missing complete neutral exit")
        if "loop" in recipe:
            first, last = recipe["loop"]["timeline_frames"]
            if first < NEUTRAL_IN_FRAMES + 1 or last >= cursor or last < first:
                errors.append(name + ": loop interval outside motion segments")
    return errors
