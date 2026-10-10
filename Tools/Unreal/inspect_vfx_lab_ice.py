"""Read the six real ice/physics stations in active L_VFXLab PIE worlds.

Run with Unreal's Python console after the shared gameplay batch has started.
This script never starts/stops PIE, drives inputs, resets stations, or loads maps.
Every Unreal world/actor reference is scoped to main() and its immediate helpers.
"""
import json
import re
from pathlib import Path
import unreal as u


EXPECTED_KINDS = (
    "FreezeThawAndRescue", "NovaProtection", "CrystalAndIcicles",
    "CentralCrystal", "IceBlocks", "PhysicsReaction",
)
EXPECTED_SECONDS = {
    "FreezeThawAndRescue": "about 11-13 s with 40 damage; thaw uses measured freeze amount",
    "NovaProtection": "about 6-8 s with 40 damage",
    "CrystalAndIcicles": "about 22.7 s; includes landing, get-up and three impacts",
    "CentralCrystal": "normal damage/cadence; actual CycleSeconds is extended for 1440 HP",
    "IceBlocks": "about 9-15 s with 40 damage; physics contact may take longer",
    "PhysicsReaction": "about 9.3 s; observes three real get-ups",
}


def _prop(obj, name, errors):
    """Read the actual reflected value; unavailable values stay null, never zero/pass."""
    if obj is None:
        return None
    snake = re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
    aliases = [name, snake]
    if name.startswith("b") and len(name) > 1 and name[1].isupper():
        aliases.append(snake[2:])
    last_error = None
    for alias in aliases:
        try:
            return obj.get_editor_property(alias)
        except Exception as exc:
            last_error = exc
        try:
            return getattr(obj, alias)
        except Exception:
            pass
    label = obj.get_path_name() if hasattr(obj, "get_path_name") else type(obj).__name__
    errors.append(f"{label}.{name}: {last_error}")
    return None


def _call(obj, name, errors, *args):
    if obj is None:
        return None
    try:
        return getattr(obj, name)(*args)
    except Exception as exc:
        errors.append(f"{obj.get_path_name()}.{name}(): {exc}")
        return None


def _path(obj):
    return obj.get_path_name() if obj is not None else None


def _vec(value):
    return [round(float(value.x), 2), round(float(value.y), 2), round(float(value.z), 2)] if value is not None else None


def _kind(value):
    text = str(value)
    normalized = "".join(c for c in text.lower() if c.isalnum())
    for name in EXPECTED_KINDS:
        if name.lower() in normalized:
            return name
    try:
        index = int(value)
        if 0 <= index < len(EXPECTED_KINDS):
            return EXPECTED_KINDS[index]
    except (TypeError, ValueError):
        pass
    return text


def _owned_by(actor, station):
    # Possession changes Pawn.Owner to its Controller; the controller retains station ownership.
    owner = actor.get_owner()
    seen = set()
    for _ in range(6):
        if owner is None:
            return False
        if owner == station:
            return True
        path = owner.get_path_name()
        if path in seen:
            return False
        seen.add(path)
        owner = owner.get_owner()
    return False


def _hero(actor, floor_top):
    errors = []
    status = _prop(actor, "Status", errors)
    state = _prop(status, "State", errors)
    physics = _prop(actor, "ToothPhysics", errors)
    controller = _call(actor, "get_controller", errors)
    capsules = actor.get_components_by_class(u.CapsuleComponent)
    capsule = capsules[0] if capsules else None
    meshes = actor.get_components_by_class(u.SkeletalMeshComponent)
    mesh = next((component for component in meshes if component.get_name() == "CharacterMesh0"), None)
    body_state = _call(physics, "get_body_state", errors)
    half = _call(capsule, "get_scaled_capsule_half_height", errors)
    position = actor.get_actor_location()
    feet_z = float(position.z) - float(half) if half is not None else None
    health = _prop(state, "Health", errors) if state is not None else None
    max_health = _prop(state, "MaxHealth", errors) if state is not None else None
    inventory = _prop(actor, "Inventory", errors)
    owned_inventory = [component for component in actor.get_components_by_class(u.ActorComponent)
                       if component.get_class().get_name() == "MCInventoryComponent"]
    inventory_owner = inventory.get_owner() if inventory is not None else None
    return {
        "name": actor.get_name(), "path": actor.get_path_name(),
        "authority": actor.has_authority(), "owner": _path(actor.get_owner()),
        "controller": _path(controller), "controller_class": _path(controller.get_class()) if controller else None,
        "location": _vec(position), "velocity": _vec(actor.get_velocity()),
        "feet_z": round(feet_z, 2) if feet_z is not None else None,
        "feet_above_flat_floor": round(feet_z-floor_top, 2) if feet_z is not None and floor_top is not None else None,
        "health": health, "max_health": max_health,
        "alive": health > 0 if health is not None else None,
        "body_state": str(body_state) if body_state is not None else None,
        "ice_leg_health": _prop(actor, "IceLegHealth", errors),
        "selected_tool": str(_prop(inventory, "Selected", errors)),
        "inventory_member": _path(inventory), "inventory_member_owner": _path(inventory_owner),
        "inventory_member_owned_by_subject": inventory_owner == actor if inventory is not None else None,
        "actual_owned_inventory": [{"path": _path(component), "owner": _path(component.get_owner()),
                                    "selected_tool": str(_prop(component, "Selected", errors))}
                                   for component in owned_inventory],
        "capsule": _path(capsule), "skeletal_mesh": _path(mesh),
        "capsule_collision": str(_call(capsule, "get_collision_enabled", errors)),
        "skeletal_physics": _call(mesh, "is_any_simulating_physics", errors),
        "errors": errors,
    }


def _winter(actor):
    errors = []
    row = {"name": actor.get_name(), "path": actor.get_path_name(), "location": _vec(actor.get_actor_location())}
    for name in ("Stage", "bFailed", "CandyHealth", "MaxCandyHealth", "bNovaWarning", "NovaImpactAt",
                 "StartedAt", "FreezeSeconds", "ThawSeconds", "CandyAnchor", "SafeAnchor"):
        value = _prop(actor, name, errors)
        row[name] = _vec(value) if name.endswith("Anchor") else str(value) if name == "Stage" else value
    row["floor"] = _path(_prop(actor, "Tongue", errors))
    row["player_ice_material"] = _path(_prop(actor, "PlayerIceMaterial", errors))
    row["players"] = []
    for player in _prop(actor, "Players", errors) or ():
        hero = _prop(player, "Hero", errors)
        row["players"].append({"hero": _path(hero), "amount": _prop(player, "Amount", errors), "safe": _prop(player, "bSafe", errors)})
    row["zone_crystals"] = []
    for crystal in _prop(actor, "ZoneCrystals", errors) or ():
        item = {name: _prop(crystal, name, errors) for name in ("Id", "ZoneIndex", "Health", "MaxHealth", "SpawnedAt", "ImpactAt", "bLanded")}
        item["Anchor"] = _vec(_prop(crystal, "Anchor", errors))
        row["zone_crystals"].append(item)
    row["icicles"] = []
    for strike in _prop(actor, "Strikes", errors) or ():
        row["icicles"].append({"id": _prop(strike, "Id", errors), "impact_at": _prop(strike, "ImpactAt", errors),
                               "impacted": _prop(strike, "bImpacted", errors), "anchor": _vec(_prop(strike, "Anchor", errors))})
    row["coating_components"] = len(actor.get_components_by_class(u.SkeletalMeshComponent))
    row["errors"] = errors
    return row


def _block(actor):
    errors = []
    body = _prop(actor, "Body", errors)
    return {"name": actor.get_name(), "location": _vec(actor.get_actor_location()),
            "static_mesh": _path(_call(body, "get_static_mesh", errors)),
            "health": _prop(actor, "Health", errors), "max_health": _prop(actor, "MaxHealth", errors),
            "simulating": _call(body, "is_simulating_physics", errors),
            "collision": str(_call(body, "get_collision_enabled", errors)), "errors": errors}


def main():
    classes = {name: u.load_class(None, "/Script/MessControl."+name)
               for name in ("MCVFXLabIceStation", "MCToothCharacter", "MCIceEvent", "MCIceBlock")}
    assert all(classes.values()), "Native ice lab classes are not loaded; await the shared compile"
    worlds = list(u.EditorLevelLibrary.get_pie_worlds(True))
    reports = []
    for world in worlds:
        if "L_VFXLab" not in world.get_path_name():
            continue
        stations = list(u.GameplayStatics.get_all_actors_of_class(world, classes["MCVFXLabIceStation"]))
        if not stations:
            continue
        heroes = list(u.GameplayStatics.get_all_actors_of_class(world, classes["MCToothCharacter"]))
        winters = list(u.GameplayStatics.get_all_actors_of_class(world, classes["MCIceEvent"]))
        blocks = list(u.GameplayStatics.get_all_actors_of_class(world, classes["MCIceBlock"]))
        rows = []
        for station in stations:
            errors = []
            kind = _kind(_prop(station, "Kind", errors))
            values = {name: _prop(station, name, errors) for name in (
                "bRunning", "CycleSeconds", "Cycles", "Passed", "Failed", "PassedCycles", "FailedCycles", "Status")}
            floor = _prop(station, "Floor", errors)
            floor_top = None
            bounds = None
            if floor is not None:
                origin, extent = floor.get_actor_bounds(False)
                bounds = {"origin": _vec(origin), "extent": _vec(extent)}
                floor_top = float(origin.z+extent.z)
            passed = values["PassedCycles"]
            failed = values["FailedCycles"]
            rows.append({"name": station.get_name(), "path": station.get_path_name(), "kind": kind,
                         "authority": station.has_authority(), "counters": values,
                         "has_actual_completed_pass": isinstance(passed, int) and passed > 0,
                         "clean_completed_pass": isinstance(passed, int) and passed > 0 and failed == 0,
                         "expected_duration": EXPECTED_SECONDS.get(kind),
                         "floor": _path(floor), "floor_bounds": bounds,
                         "subjects": [_hero(a, floor_top) for a in heroes if _owned_by(a, station)],
                         "winter": [_winter(a) for a in winters if _owned_by(a, station)],
                         "ice_blocks": [_block(a) for a in blocks if _owned_by(a, station)],
                         "errors": errors})
        present = {row["kind"] for row in rows}
        missing = sorted(set(EXPECTED_KINDS)-present)
        authority = any(row["authority"] for row in rows)
        # Observations Passed and started Cycles are deliberately excluded from this verdict.
        passed_kinds = sorted({row["kind"] for row in rows if row["authority"] and row["has_actual_completed_pass"]})
        clean_kinds = sorted({row["kind"] for row in rows if row["authority"] and row["clean_completed_pass"]})
        reports.append({"world": world.get_path_name(), "authority": authority,
                        "game_seconds": u.GameplayStatics.get_time_seconds(world), "station_count": len(rows),
                        "missing_kinds": missing, "passed_kinds": passed_kinds,
                        "all_six_actual_complete": authority and not missing and set(EXPECTED_KINDS).issubset(passed_kinds),
                        "all_six_clean": authority and not missing and set(EXPECTED_KINDS).issubset(clean_kinds),
                        "stations": rows})
    result = {"worlds": reports,
              "read_only": True,
              "notes": ["No PIE/editor/gameplay action was performed by this inspector.",
                        "Passed is an observation count; Cycles counts started cycles; only PassedCycles proves completion.",
                        "Native Step, KnockdownCount and RecoveryCount are not reflected; Status and GetBodyState are read instead.",
                        "IceBlock Shape/bBroken lack public script access; actual Body mesh, health and collision are read instead.",
                        "This snapshot proves observed gameplay counters/state, not rendered visual quality."]}
    output = Path(u.Paths.project_saved_dir()) / "Codex" / "vfx_lab_ice_latest.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    summary = [{"world": r["world"], "authority": r["authority"], "game_seconds": r["game_seconds"],
                "all_six_actual_complete": r["all_six_actual_complete"], "all_six_clean": r["all_six_clean"],
                "missing_kinds": r["missing_kinds"],
                "incomplete_or_failed": [{"kind": s["kind"], "counters": s["counters"], "errors": s["errors"]}
                                         for s in r["stations"] if not s["clean_completed_pass"]]}
               for r in reports]
    print("VFX_LAB_ICE_DIAGNOSTIC "+json.dumps({"output": str(output), "worlds": summary}, ensure_ascii=False))
    return result


main()
