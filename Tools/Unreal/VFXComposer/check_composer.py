"""Create/audit one Composer fixture AFTER the matching Editor build.

Run explicitly from Unreal Python; this module never builds, starts PIE, spawns
actors in the open level, opens asset editors, or saves source Niagara systems.
Re-running audits an existing fixture without overwriting any manual changes.
"""

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import traceback

import unreal as u


FIXTURE = "/Game/Tests/VFXPractice/Compositions/DA_ComposerSmoke_20261030"
FOAM = "/Game/Tests/VFXPractice/Effects/NS_Practice_FoamBasics"
ACCENT = "/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Impact"
SOURCES = (FOAM, ACCENT)
PROJECT = Path(u.Paths.project_dir()).resolve()
EVIDENCE = Path("E:/CodexWork/MessControl/Validation/VFXComposer_20261030/FixtureScript")
TEMP = Path("E:/CodexTemp/MessControl_VFXComposer")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def dirty_snapshot():
    return {
        "content": sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
        "maps": sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
    }


def source_disk_hashes():
    result = {}
    for package in SOURCES:
        require(package.startswith("/Game/"), "Only project /Game sources are allowed")
        stem = PROJECT / "Content" / package.removeprefix("/Game/")
        for suffix in (".uasset", ".uexp", ".ubulk"):
            filename = stem.with_suffix(suffix)
            if suffix == ".uasset":
                require(filename.is_file(), "Missing saved source package: " + str(filename))
            if filename.is_file():
                result[str(filename)] = hashlib.sha256(filename.read_bytes()).hexdigest()
    return result


def parameters(system):
    return sorted(
        ({"name": str(info.parameter_name), "type": str(info.type_name)}
         for info in u.NiagaraFunctionLibrary.get_all_user_parameters(system)),
        key=lambda item: (item["name"], item["type"]),
    )


def override(kind, name, value):
    result = kind()
    result.set_editor_property("name", name)
    result.set_editor_property("override", True)
    result.set_editor_property("value", value)
    return result


def layer(name, source, binding, position, start, emission, scale):
    result = u.MCVFXCompositionLayer()
    for key, value in {
        "name": name,
        "enabled": True,
        "solo": False,
        "system": source,
        "binding": binding,
        "transform": u.Transform(location=u.Vector(*position), scale=u.Vector(scale, scale, scale)),
        "start_seconds": start,
        "emission_seconds": emission,
    }.items():
        result.set_editor_property(key, value)
    return result


def add_supported_accent_overrides(accent_layer, metadata):
    """Use only known, actually present User names with matching reported types."""
    floats, colors, vectors = [], [], []
    applied = []
    float_values = {"User.Duration": .65, "User.Intensity": .7, "User.Radius": 65.0}
    for info in metadata:
        # UE5.8 GetAllUserParameters reports canonical short names (for example
        # "Color"), while NiagaraComponent overrides require "User.Color".
        name = info["name"] if info["name"].startswith("User.") else "User." + info["name"]
        kind = "".join(character for character in info["type"].lower() if character.isalnum())
        if name in float_values and kind in ("float", "float32", "niagarafloat"):
            floats.append(override(u.MCVFXFloatOverride, name, float_values[name]))
            applied.append({"name": name, "type": "float", "value": float_values[name]})
        elif name == "User.Color" and kind in ("linearcolor", "color", "float4", "niagaralinearcolor"):
            colors.append(override(u.MCVFXColorOverride, name, u.LinearColor(.6, .95, .9, .9)))
            applied.append({"name": name, "type": "LinearColor", "value": [.6, .95, .9, .9]})
        elif name == "User.Direction" and kind in ("vector", "vector3", "vector3f", "float3", "niagaravector"):
            vectors.append(override(u.MCVFXVectorOverride, name, u.Vector(0, 0, 1)))
            applied.append({"name": name, "type": "Vector3", "value": [0, 0, 1]})
    require(applied, "Accent exposed no recognized typed User inputs; inspect source metadata before changing this recipe")
    accent_layer.set_editor_property("floats", floats)
    accent_layer.set_editor_property("colors", colors)
    accent_layer.set_editor_property("vectors", vectors)
    return applied


def layer_readback(item):
    system = item.get_editor_property("system")
    transform = item.get_editor_property("transform")
    position = transform.translation
    scale = transform.scale3d
    result = {
        "name": str(item.get_editor_property("name")),
        "enabled": bool(item.get_editor_property("enabled")),
        "solo": bool(item.get_editor_property("solo")),
        "system": system.get_path_name() if hasattr(system, "get_path_name") else str(system),
        "binding": str(item.get_editor_property("binding")),
        "position": [position.x, position.y, position.z],
        "scale": [scale.x, scale.y, scale.z],
        "start_seconds": float(item.get_editor_property("start_seconds")),
        "emission_seconds": float(item.get_editor_property("emission_seconds")),
        "overrides": {},
    }
    for kind in ("floats", "colors", "vectors", "bools"):
        result["overrides"][kind] = [
            {"name": str(entry.get_editor_property("name")),
             "override": bool(entry.get_editor_property("override")),
             "value": str(entry.get_editor_property("value"))}
            for entry in item.get_editor_property(kind)
        ]
    return result


def main():
    require(PROJECT.drive.upper() == "E:", "This recipe must use the project on drive E:")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    TEMP.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = str(TEMP)
    os.environ["TMP"] = str(TEMP)
    report = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "project": str(PROJECT),
        "engine": str(u.SystemLibrary.get_engine_version()),
        "fixture": FIXTURE,
        "scope": "Create or read back one DA fixture; no source NS edits, gameplay actor spawning, PIE, builds or global saves",
        "passed": False,
        "created": False,
        "saved_fixture_only": False,
        "runtime_lifecycle": {
            "status": "not_run",
            "reason": "Python recipe does not spawn into the open level. Native tests and actual isolated Composer viewport review are separate evidence.",
        },
        "visual_acceptance": "not_run; no artistic approval",
        "deferred": ["actual Composer controls", "GPU/history seek accuracy", "runtime integration and per-map prewarm", "network", "packaged cold PSO", "overdraw/GPU profiling"],
    }
    before = dirty_snapshot()
    report["dirty_before"] = before
    report["source_hashes_before"] = source_disk_hashes()
    try:
        for class_name in ("MCVFXComposition", "MCVFXCompositionLayer", "MCVFXFloatOverride", "MCVFXColorOverride", "MCVFXVectorOverride"):
            require(hasattr(u, class_name), "Missing built class: " + class_name)
        systems = {path: u.load_asset(path) for path in SOURCES}
        for path, system in systems.items():
            require(isinstance(system, u.NiagaraSystem), "Expected NiagaraSystem: " + path)
        report["source_user_parameters_before"] = {path: parameters(system) for path, system in systems.items()}
        require(dirty_snapshot() == before, "Read-only source loading changed dirty package flags; do not save or revert source packages")

        fixture = u.load_asset(FIXTURE)
        if fixture:
            require(isinstance(fixture, u.MCVFXComposition), "Fixture path belongs to a different asset class; preserved")
            report["mode"] = "audit_existing_without_overwrite"
        else:
            report["mode"] = "create_new_fixture"
            binding = u.MCVFXCompositionBinding
            layers = [
                layer("Foam stationary", systems[FOAM], binding.COMPOSITION, (-65, 0, 35), 0.0, 1.2, .7),
                layer("Foam follows tooth", systems[FOAM], binding.SUBJECT, (0, 0, 50), .65, 1.2, .8),
                layer("Delayed contact accent", systems[ACCENT], binding.WORLD, (65, 0, 2), 1.5, .3, .35),
            ]
            report["typed_overrides_requested"] = add_supported_accent_overrides(layers[2], report["source_user_parameters_before"][ACCENT])
            factory = u.DataAssetFactory()
            factory.set_editor_property("data_asset_class", u.MCVFXComposition)
            folder, asset_name = FIXTURE.rsplit("/", 1)
            fixture = u.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, folder, u.MCVFXComposition, factory)
            require(isinstance(fixture, u.MCVFXComposition), "Could not create the owned fixture")
            report["created"] = True
            fixture.set_editor_property("duration_seconds", 3.0)
            fixture.set_editor_property("tail_seconds", 3.0)
            fixture.set_editor_property("layers", layers)
            require(u.EditorAssetLibrary.save_asset(FIXTURE, only_if_is_dirty=True), "Could not save the owned fixture")
            report["saved_fixture_only"] = True

        readback = list(fixture.get_editor_property("layers"))
        report["readback"] = {
            "duration_seconds": float(fixture.get_editor_property("duration_seconds")),
            "tail_seconds": float(fixture.get_editor_property("tail_seconds")),
            "layers": [layer_readback(item) for item in readback],
        }
        report["reflected_runtime_methods_present"] = {
            name: hasattr(u.MCVFXCompositionActor, name)
            for name in ("prepare_composition", "play_prepared", "pause_composition", "stop_composition", "get_playback_time", "is_playing", "is_running", "get_last_error")
        }
        require(all(report["reflected_runtime_methods_present"].values()), "Runtime reflection methods are incomplete")
        require(len(readback) == 3, "Fixture differs from the expected three-layer recipe; manual changes preserved")
        require(abs(report["readback"]["duration_seconds"] - 3.0) < .0001, "Fixture duration was changed; manual value preserved")
        require(abs(report["readback"]["tail_seconds"] - 3.0) < .0001, "Fixture tail was changed; manual value preserved")
        require(abs(float(readback[1].get_editor_property("start_seconds")) - .65) < .0001, "Delayed source readback differs; manual value preserved")
        require(abs(float(readback[2].get_editor_property("start_seconds")) - 1.5) < .0001, "Accent delay readback differs; manual value preserved")
        for index, expected_source in enumerate((FOAM, FOAM, ACCENT)):
            require(expected_source in report["readback"]["layers"][index]["system"], "Source reference differs; manual value preserved")
            require(report["readback"]["layers"][index]["enabled"], "Fixture layer was muted; manual value preserved")
        require(readback[0].get_editor_property("binding") == u.MCVFXCompositionBinding.COMPOSITION, "Stationary binding changed; manual value preserved")
        require(readback[1].get_editor_property("binding") == u.MCVFXCompositionBinding.SUBJECT, "Subject binding changed; manual value preserved")
        require(readback[2].get_editor_property("binding") == u.MCVFXCompositionBinding.WORLD, "World binding changed; manual value preserved")
        require(report["readback"]["layers"][0]["position"] != report["readback"]["layers"][1]["position"], "Two source instances need separate transforms")
        require(any(entry["override"] for kind in report["readback"]["layers"][2]["overrides"].values() for entry in kind), "Accent typed overrides were disabled; manual value preserved")
        report["source_user_parameters_after"] = {path: parameters(system) for path, system in systems.items()}
        require(report["source_user_parameters_after"] == report["source_user_parameters_before"], "Source User metadata changed")
    except Exception:
        report["error"] = traceback.format_exc()
    finally:
        report["dirty_after"] = dirty_snapshot()
        report["source_hashes_after"] = source_disk_hashes()
        report["source_disk_bytes_preserved"] = report["source_hashes_after"] == report["source_hashes_before"]
        report["dirty_flags_preserved"] = report["dirty_after"] == before
        report["passed"] = not report.get("error") and report["source_disk_bytes_preserved"] and report["dirty_flags_preserved"]
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%fZ")
        destination = EVIDENCE / ("Fixture_" + stamp + ".json")
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        u.log("MCVFXComposer fixture report: " + str(destination))
    require(report["passed"], "Composer fixture check failed; preserve current packages and inspect " + str(destination))
    return report


if __name__ == "__main__":
    main()
