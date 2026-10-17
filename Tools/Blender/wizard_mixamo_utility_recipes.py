"""Twelve Blender-only missing-gesture studies on the original Wizard rig.

The foundation is the three existing native artist performances, not additional
Mixamo downloads. These studies add small authored control arcs with explicit
semantic beats. They do not rename attack sequences as reactions or locomotion.
Death and an actual backward step need new source motion and are excluded.

Consumer contract:
* RECIPES uses the same inclusive segments/timing_points as the pool recipes.
* Apply accents after source sampling and segment blends, before baking.
  Each entry is {bone, axis: 'X'|'Y'|'Z'|'WORLD_X'|'WORLD_Y', keys: [(frame, degrees), ...]}.
  Multiply source quaternion by the local axis offset quaternion. Interpolate
  each interval smoothly with zero slope at keys; never accumulate offsets.
* Neutral frames and the last frame have zero accents. Directional hits tilt
  the body root while the original foot IK targets remain planted. Scale,
  stretch and world-space hand placement are unchanged.
* event_frames describes gesture anchors, not implemented VFX or gameplay.
  Teleport visibility/gameplay displacement is an independent integration step.
* Three directional hits deliberately hold a native standing foundation and
  author a short upper-control recoil. They are not full-body knockback/death.
* Palm intent is conveyed by modest wrist articulation, not a new palm-facing
  IK solve. Inspect silhouette/hand intersections before gameplay integration.
"""

import importlib.util
from pathlib import Path


_spec = importlib.util.spec_from_file_location(
    "wizard_mixamo_utility_base", Path(__file__).with_name("wizard_mixamo_pool_recipes.py"))
_base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_base)

FPS = _base.FPS
ARTIST_ACTIONS = _base.ARTIST_ACTIONS

LIMITS_DEGREES = {
    "c_arm_fk.l": 20., "c_arm_fk.r": 20.,
    "c_forearm_fk.l": 10., "c_forearm_fk.r": 10.,
    "c_hand_fk.l": 12., "c_hand_fk.r": 12.,
    "c_head.x": 20., "c_root.x": 20., "c_shoulder.l": 4., "c_shoulder.r": 4.,
}


def _accent(bone, axis, keys, frames):
    """An offset envelope; source controls remain authoritative underneath."""
    return {"bone": bone, "axis": axis,
            "keys": [(1, 0.), (9, 0.)] + keys + [(frames, 0.)]}


def _study(name, label, group, use, steps, concept, event_frames, accents,
           *, standing_foundation=False, calibration=None):
    recipe = _base._recipe(name, label, 5, use, steps)
    recipe["groupRU"] = recipe["group"] = group
    recipe["conceptRU"] = concept
    recipe["event_frames"] = event_frames
    recipe["accents"] = [_accent(bone, axis, keys, recipe["frames"])
                         for bone, axis, keys in accents]
    recipe["provenance"] = "Original artist source with bounded authored local-control arcs"
    recipe["needs_new_world_ik"] = False
    recipe["preserve_native_root_and_feet"] = not any(bone.startswith('c_root') for bone,_,_ in accents)
    recipe["preserve_native_foot_targets"] = True
    recipe["calibrationRU"] = calibration or (
        "Проверить руки в профиль и спереди; не заменять малый наклон кисти большим IK-поворотом.")
    if standing_foundation:
        for segment in recipe["segments"]:
            segment.update(part="upper", lower_source="Firenut", lower_source_frame=1)
        recipe["lower_foundation_source"] = "Firenut"
    return recipe


RECIPES = [
    _study("TeleportOut", "Телепорт: собрать и исчезнуть", "Телепорт",
        "Подготовка ухода: собрать руки внутрь, пригнуться, спрятать силуэт.",
        [("NutRain", 1, 38, 34), ("NutRain", 38, 38, 14)],
        "Голова опережает закрытие рук; локти собираются, кисти прикрывают низ корпуса. "
        "В низкой позе тело готово исчезнуть, без жеста броска.",
        {"gather": 27, "conceal": 42, "vanish": 48, "preview_return": 58}, [
            ("c_head.x", "X", [(18, 2), (31, 8), (42, 10), (56, 6)]),
            ("c_arm_fk.l", "Z", [(20, -4), (34, -16), (44, -20), (56, -16)]),
            ("c_arm_fk.r", "Z", [(20, 3), (34, 14), (44, 18), (56, 14)]),
            ("c_forearm_fk.l", "Z", [(26, 3), (42, 8), (56, 6)]),
            ("c_forearm_fk.r", "Z", [(29, -3), (44, -8), (56, -6)]),
            ("c_hand_fk.l", "X", [(28, 3), (42, 10), (56, 7)]),
            ("c_hand_fk.r", "X", [(31, -3), (44, -9), (56, -6)]),
        ], calibration="Низкая поза на source38. Исчезновение на 48 — только маркер; "
        "возврат в видео служит общей позе покоя. Тайминг .65 с в игре настроить после оценки силуэта."),

    _study("TeleportIn", "Телепорт: появиться и раскрыться", "Телепорт",
        "Выход из телепорта: низкая посадка, разведение рук, быстрый взгляд на цель.",
        [("NutRain", 38, 50, 16), ("NutRain", 50, 62, 18), ("NutRain", 62, 99, 34)],
        "Начать компактно, затем раскрыть ладони в стороны; взгляд на цель появляется "
        "раньше, чем завершится расправление рук.",
        {"appear": 17, "unfold": 35, "face_target": 45, "settle": 68}, [
            ("c_head.x", "X", [(16, 8), (26, 3), (38, -4), (54, 0)]),
            ("c_head.x", "Y", [(22, -3), (39, -8), (54, -3), (72, 0)]),
            ("c_arm_fk.l", "Z", [(16, -14), (27, -6), (40, 10), (56, 4), (74, 0)]),
            ("c_arm_fk.r", "Z", [(16, 12), (27, 4), (40, -9), (56, -3), (74, 0)]),
            ("c_hand_fk.l", "X", [(17, 7), (32, 2), (43, -10), (60, -3), (76, 0)]),
            ("c_hand_fk.r", "X", [(17, -6), (32, -2), (43, 9), (60, 3), (76, 0)]),
        ], calibration="Это раскрытие по горизонтали из исходного дождя. Стартовая посадка "
        "блендится из нейтрали; для игры скрыть вход до appear и отдельно оценить .55 с recovery."),

    _study("TeleportCancel", "Телепорт: прервать уход", "Телепорт",
        "Остановить сбор телепорта: закрыть руки, передумать, распустить жест.",
        [("NutRain", 1, 38, 30), ("NutRain", 38, 38, 6),
         ("NutRain", 38, 26, 12), ("NutRain", 26, 1, 22)],
        "После сбора короткая заминка. Голова проверяет обстановку, левая кисть "
        "отпускает раньше правой; энергия не выпускается.",
        {"gather": 29, "hesitate": 42, "abort": 49, "release_grip": 57}, [
            ("c_head.x", "X", [(22, 3), (36, 8), (44, 7), (57, -2), (76, 0)]),
            ("c_head.x", "Y", [(32, 0), (44, 7), (51, -5), (67, 0)]),
            ("c_arm_fk.l", "Z", [(24, -5), (39, -17), (46, -14), (57, 5), (74, 0)]),
            ("c_arm_fk.r", "Z", [(27, 5), (40, 15), (49, 13), (62, -4), (77, 0)]),
            ("c_hand_fk.l", "X", [(32, 5), (42, 9), (54, -7), (71, 0)]),
            ("c_hand_fk.r", "X", [(35, -5), (45, -8), (59, 6), (75, 0)]),
        ]),

    _study("ShortBlink", "Блинк: закрыться и раскрыться", "Телепорт",
        "Короткий блинк с одним сжатием и немедленным раскрытием, без повторного каста.",
        [("NutRain", 26, 38, 12), ("NutRain", 38, 50, 12), ("NutRain", 50, 50, 10)],
        "Сначала взгляд в точку выхода, затем быстрое складывание локтей. "
        "Вместо броска — частичное раскрытие компактной пары рук.",
        {"look_exit": 15, "blink_out": 21, "blink_in": 25, "open": 32}, [
            ("c_head.x", "Y", [(14, 7), (22, 9), (30, 4), (43, 0)]),
            ("c_head.x", "X", [(15, 3), (21, 8), (29, -3), (43, 0)]),
            ("c_arm_fk.l", "Z", [(15, -7), (21, -18), (29, 8), (42, 0)]),
            ("c_arm_fk.r", "Z", [(15, 6), (21, 16), (29, -7), (42, 0)]),
            ("c_hand_fk.l", "X", [(16, 4), (21, 9), (30, -8), (43, 0)]),
            ("c_hand_fk.r", "X", [(16, -4), (21, -8), (30, 7), (43, 0)]),
        ]),

    _study("MeleeShove", "Толчок ладонью", "Ближний бой",
        "Один короткий толчок правой ладонью: подготовить руку, вытянуть перед собой и вернуть.",
        [("Firenut", 1, 18, 18), ("Firenut", 18, 27, 12),
         ("Firenut", 27, 36, 8), ("Firenut", 36, 44, 10),
         ("Firenut", 44, 70, 26)],
        "Исходная подготовка source18→27 переходит в ускоренное вытягивание "
        "правой руки source27→36 к фронту -Y. Контакт совпадает с source36; "
        "исходные плечо и локоть сохраняют эту траекторию, кисть лишь подчёркивает ладонный импульс.",
        {"load": 27, "thrust_start": 39, "contact": 47, "recover": 57, "settle": 81}, [
            ("c_head.x", "X", [(24, 0), (37, 1), (47, -2), (58, -1), (75, 0)]),
            ("c_hand_fk.r", "X", [(24, 0), (34, 4), (47, -5), (57, -2), (75, 0)]),
        ], calibration="Контакт frame47 соответствует source36: правая кисть движется "
        "по нативной дуге worldY -.83→-.92 между source27 и36. Arm/forearm offsets "
        "не добавляются. В профиль проверить только небольшой акцент запястья."),

    _study("HitFront", "Попадание спереди", "Реакции на удар",
        "Короткое фронтальное попадание из стойки: голова отстаёт, плечи и руки отскакивают.",
        [("Firenut", 1, 1, 50)],
        "Никакого взмаха заклинания: резкий recoil, слабый противоположный отскок "
        "и плавное возвращение взгляда к противнику.",
        {"impact": 14, "recoil_peak": 17, "overshoot": 28, "settle": 48}, [
            ("c_head.x", "X", [(11, 0), (15, -10), (20, -7), (29, 3), (43, 0)]),
            ("c_root.x", "WORLD_X", [(11, 0), (14, -20), (21, -13), (29, 5), (44, 0)]),
            ("c_head.x", "WORLD_X", [(12, 0), (16, -18), (22, -13), (31, 4), (46, 0)]),
            ("c_shoulder.l", "X", [(11, 0), (14, -4), (21, -2), (31, 1), (46, 0)]),
            ("c_shoulder.r", "X", [(11, 0), (15, -4), (22, -2), (32, 1), (47, 0)]),
            ("c_arm_fk.l", "X", [(11, 0), (16, -11), (23, -6), (33, 3), (48, 0)]),
            ("c_arm_fk.r", "X", [(11, 0), (17, -14), (24, -7), (34, 3), (49, 0)]),
            ("c_hand_fk.l", "Z", [(13, 0), (19, 7), (29, -2), (46, 0)]),
            ("c_hand_fk.r", "Z", [(13, 0), (20, -8), (30, 2), (47, 0)]),
        ], standing_foundation=True),

    _study("HitLeft", "Попадание слева", "Реакции на удар",
        "Направленная реакция: левое плечо принимает удар, голова и свободная кисть догоняют.",
        [("Firenut", 1, 1, 50)],
        "Левый край силуэта сжимается, взгляд запаздывает; асимметрия рук "
        "отличает реакцию от зеркального качания всего тела.",
        {"impact": 14, "recoil_peak": 18, "overshoot": 30, "settle": 49}, [
            ("c_shoulder.l", "Z", [(11, 0), (14, -4), (21, -2), (30, 1), (45, 0)]),
            ("c_root.x", "WORLD_Y", [(11, 0), (14, 20), (21, 13), (30, -5), (46, 0)]),
            ("c_head.x", "WORLD_Y", [(12, 0), (17, 18), (24, 12), (33, -4), (48, 0)]),
            ("c_arm_fk.l", "Z", [(11, 0), (16, -16), (24, -8), (34, 3), (49, 0)]),
            ("c_arm_fk.r", "Z", [(13, 0), (19, -6), (27, -3), (37, 2), (50, 0)]),
            ("c_head.x", "Z", [(12, 0), (17, -8), (24, -5), (32, 3), (48, 0)]),
            ("c_head.x", "Y", [(13, 0), (20, -6), (30, 2), (48, 0)]),
            ("c_hand_fk.l", "X", [(14, 0), (20, 9), (31, -2), (47, 0)]),
        ], standing_foundation=True),

    _study("HitRight", "Попадание справа", "Реакции на удар",
        "Направленная реакция с правым плечом впереди: рука отводится, взгляд возвращается позже.",
        [("Firenut", 1, 1, 50)],
        "Из-за исходной асимметричной стойки правая реакция имеет другую амплитуду "
        "и фазу кисти, а не слепое зеркалирование левой.",
        {"impact": 14, "recoil_peak": 19, "overshoot": 31, "settle": 50}, [
            ("c_shoulder.r", "Z", [(11, 0), (15, 4), (22, 2), (31, -1), (46, 0)]),
            ("c_root.x", "WORLD_Y", [(11, 0), (15, -20), (22, -13), (31, 5), (47, 0)]),
            ("c_head.x", "WORLD_Y", [(12, 0), (18, -18), (25, -12), (34, 4), (49, 0)]),
            ("c_arm_fk.r", "Z", [(11, 0), (17, 13), (25, 7), (35, -3), (49, 0)]),
            ("c_arm_fk.l", "Z", [(13, 0), (20, 8), (28, 4), (38, -2), (50, 0)]),
            ("c_head.x", "Z", [(12, 0), (18, 8), (25, 5), (33, -3), (49, 0)]),
            ("c_head.x", "Y", [(13, 0), (21, 7), (31, -2), (49, 0)]),
            ("c_hand_fk.r", "X", [(14, 0), (22, -8), (33, 2), (48, 0)]),
        ], standing_foundation=True),

    _study("Materialize", "Материализация: собрать тело и выпрямиться", "Появление",
        "Вертикальное появление из низкой позы: поднять кисти, собрать силуэт, спокойно опустить руки.",
        [("CastSummon", 25, 42, 24), ("CastSummon", 42, 50, 12),
         ("CastSummon", 50, 66, 24)],
        "В отличие от TeleportIn, руки идут вверх из исходного призыва. "
        "Подъём запястий завершается открытием ладоней и спокойной осанкой.",
        {"appear": 17, "rise": 29, "complete_shape": 38, "settle": 62}, [
            ("c_head.x", "X", [(16, 7), (28, 2), (38, -7), (53, -2), (68, 0)]),
            ("c_arm_fk.l", "Z", [(16, -12), (29, -9), (40, 6), (55, 2), (68, 0)]),
            ("c_arm_fk.r", "Z", [(16, 10), (29, 7), (40, -5), (55, -2), (68, 0)]),
            ("c_hand_fk.l", "X", [(17, 7), (28, 4), (40, -10), (56, -3), (68, 0)]),
            ("c_hand_fk.r", "X", [(17, -6), (30, -3), (42, 9), (58, 3), (68, 0)]),
        ], calibration="Появление из low25, без падения/полёта и без нового landing source. "
        "Для MageFallingIdle это отдельный переход Materialize, а не замена циклу падения."),

    _study("HeavyStagger", "Тяжёлое попадание: осесть и прийти в себя", "Реакции на удар",
        "Сильный удар с низкой исходной позой: осесть, отпустить руки, собраться обратно.",
        [("NutRain", 26, 38, 16), ("NutRain", 38, 38, 6),
         ("NutRain", 38, 26, 18), ("NutRain", 26, 1, 28)],
        "Native crouch даёт массу тела; руки теряют напряжение, голова задерживается "
        "внизу и только потом вновь ищет цель. Это stagger, не смерть и не отлёт.",
        {"impact": 15, "slump": 26, "dazed": 38, "refocus": 58, "settle": 76}, [
            ("c_head.x", "X", [(14, -6), (23, 10), (36, 9), (51, 4), (63, -3), (77, 0)]),
            ("c_head.x", "Y", [(22, 0), (36, -7), (49, 4), (65, 0)]),
            ("c_shoulder.l", "X", [(14, -3), (25, 4), (40, 3), (58, 1), (77, 0)]),
            ("c_shoulder.r", "X", [(15, -3), (28, 4), (43, 3), (61, 1), (77, 0)]),
            ("c_arm_fk.l", "X", [(16, -8), (29, 14), (43, 10), (60, 3), (77, 0)]),
            ("c_arm_fk.r", "X", [(17, -7), (31, 16), (46, 11), (63, 3), (77, 0)]),
            ("c_hand_fk.l", "Z", [(21, 8), (37, 10), (58, 3), (77, 0)]),
            ("c_hand_fk.r", "Z", [(23, -7), (40, -9), (61, -3), (77, 0)]),
        ], calibration="Проверить, что обратный участок Rain26→1 читается как восстановление. "
        "Не заявлять импульс отбрасывания: траектория root остаётся исходной."),

    _study("ShieldRaise", "Защитная печать: выставить ладонь", "Защита",
        "Поднять защиту перед собой и удержать: ведущая ладонь спереди, вторая рука поддерживает.",
        [("Firenut", 1, 18, 24), ("Firenut", 18, 18, 16), ("Firenut", 18, 1, 24)],
        "Не доходить до броска source27. Левый передний край исходной подготовки "
        "становится удержанной ладонью, правая кисть остаётся поддерживающей.",
        {"raise": 27, "ward_active": 35, "hold": 43, "lower": 55}, [
            ("c_head.x", "X", [(19, -2), (31, -5), (48, -4), (64, 0)]),
            ("c_arm_fk.l", "X", [(19, 5), (31, 16), (48, 16), (63, 4), (72, 0)]),
            ("c_arm_fk.r", "Z", [(20, 3), (33, 12), (49, 12), (65, 3), (72, 0)]),
            ("c_forearm_fk.l", "Z", [(22, 3), (33, 8), (48, 8), (65, 2), (72, 0)]),
            ("c_hand_fk.l", "X", [(23, -4), (33, -11), (49, -11), (65, -3), (72, 0)]),
            ("c_hand_fk.r", "X", [(24, 2), (35, 7), (49, 7), (66, 2), (72, 0)]),
        ], calibration="Левая ладонь должна читаться в силуэте как барьер; малый наклон "
        "кисти не гарантирует точную ориентацию ладони. При необходимости вручную "
        "поправить одну удержанную FK-позу, сохранив источник плеч/локтей."),

    _study("ShieldBreak", "Защитная печать: разбили щит", "Защита",
        "Удержанная защита ломается: ладонь срывается, руки раскрываются, голова догоняет удар.",
        [("Firenut", 1, 18, 18), ("Firenut", 18, 18, 6), ("Firenut", 18, 1, 24)],
        "Сначала тот же узнаваемый barrier жест. Затем отдельный короткий срыв "
        "ведущей кисти и более поздний отклик головы, без выпуска снаряда.",
        {"ward_ready": 25, "break": 33, "hand_snap": 37, "recoil": 42, "recover": 55}, [
            ("c_arm_fk.l", "X", [(18, 5), (25, 16), (31, 16), (38, -12), (46, -6), (57, 0)]),
            ("c_arm_fk.r", "Z", [(19, 3), (26, 11), (32, 11), (40, -14), (48, -5), (57, 0)]),
            ("c_hand_fk.l", "X", [(19, -4), (26, -10), (32, -10), (37, 12), (47, 3), (57, 0)]),
            ("c_hand_fk.r", "Z", [(23, -2), (31, -5), (41, 9), (50, 2), (57, 0)]),
            ("c_head.x", "X", [(21, -3), (31, -3), (40, -10), (48, 3), (57, 0)]),
            ("c_shoulder.l", "X", [(25, 1), (34, 0), (38, -4), (46, -2), (57, 0)]),
        ]),
]


def validate_recipes():
    """Small static contract check; no Blender execution or scene assertions."""
    errors = []
    if len(RECIPES) != 12 or len({r["name"] for r in RECIPES}) != 12:
        errors.append("Expected twelve uniquely named single-purpose studies")
    for recipe in RECIPES:
        name, frames = recipe["name"], recipe["frames"]
        if name.startswith("Combo"):
            errors.append(f"{name}: combinations are outside this set")
        if recipe["segments"][-1]["end"] + recipe["neutral_out_frames"] != frames:
            errors.append(f"{name}: final neutral duration mismatch")
        for segment in recipe["segments"]:
            source = ARTIST_ACTIONS[segment["source"]]
            if not (source["frame_start"] <= min(segment["source_start"], segment["source_end"])
                    <= max(segment["source_start"], segment["source_end"]) <= source["frame_end"]):
                errors.append(f"{name}: source range outside native performance")
        for accent in recipe["accents"]:
            bone, axis, keys = accent["bone"], accent["axis"], accent["keys"]
            if bone not in LIMITS_DEGREES or axis not in ("X", "Y", "Z", "WORLD_X", "WORLD_Y"):
                errors.append(f"{name}: unknown control or local axis {bone}/{axis}")
                continue
            if "forearm" in bone and axis != "Z":
                errors.append(f"{name}: forearm offsets must use its unlocked Z hinge")
            if any(abs(value) > LIMITS_DEGREES[bone] for _, value in keys):
                errors.append(f"{name}: {bone} angle exceeds its bound")
            if any(a[0] >= b[0] for a, b in zip(keys, keys[1:])):
                errors.append(f"{name}: {bone} keys are not strictly increasing")
            if keys[0] != (1, 0.) or keys[1] != (9, 0.) or keys[-1] != (frames, 0.):
                errors.append(f"{name}: {bone} does not preserve common neutral endpoints")
            if any(not 1 <= frame <= frames for frame, _ in keys):
                errors.append(f"{name}: {bone} key outside clip")
        if any(not 1 <= frame <= frames for frame in recipe["event_frames"].values()):
            errors.append(f"{name}: event outside clip")
    return errors


if __name__ == "__main__":
    issues = validate_recipes()
    if issues:
        raise ValueError("\n".join(issues))
    print("12 native-source gesture studies; recipe contract valid")
