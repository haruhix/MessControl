"""Author only the two task-owned VFX practice Blueprints through Epic MCP.

Run --schema first for read-only API availability. --apply probes exact pins on
its own newly created Blueprint, then creates/compiles/saves
only BP_PracticeStation and BP_PracticeBootstrap; it never loads/saves/spawns a
map, starts PIE, edits C++, or changes production Niagara assets. Root separately
authors L_VFXPractice, places the native lab + real tooth witnesses, assigns each
PracticeFX asset and Bootstrap.Lab, and performs the solo visual check.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PROJECT / "Tools" / "Unreal"))
from native_mcp_client import NativeMCP

BASE = "/Game/Tests/VFXPractice"
BP = "editor_toolset.toolsets.blueprint.BlueprintTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
OBJ = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
EDITOR = "EditorToolset.EditorAppToolset"
RECIPE_VERSION = "VFXPracticeBP_v1"
RECIPES = Path(__file__).with_name("recipes")
DEFAULT_OUT = Path("E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030/BlueprintAuthoring")
CONTEXT_BP = {"refPath": "/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter"}

# These exact common node IDs/pins were read from the connected UE5.8.1 Editor.
# The script repeats discovery before any graph write, including --apply.
PIN_CONTRACT = {
    "Components|Activation|SetAutoActivate": ["self", "bNewAutoActivate"],
    "Components|Activation|Deactivate": ["self"],
    "Components|Activation|Activate": ["self", "bReset"],
    "Niagara|SetNiagaraVariable(Bool)": ["self", "InVariableName", "InValue"],
    "Niagara|SetNiagaraVariable(Float)": ["self", "InVariableName", "InValue"],
    "Niagara|SetNiagaraVariable(Vector3)": ["self", "InVariableName", "InValue"],
    "Niagara|GetNiagaraSystemAsset": ["self"],
    "Niagara|ReinitializeSystem": ["self"],
    "Transformation|GetWorldLocation": ["self"],
    "Transformation|SetRelativeLocation": ["self", "NewLocation", "bSweep", "bTeleport"],
    "Class|SceneComponent|GetRelativeLocation": ["self"],
    "Math|Vector|MakeVector": ["X", "Y", "Z"],
    "Math|Vector|Normalize": ["A", "Tolerance"],
    "Math|Trig|Sin(Radians)": ["A"],
    "WorldSubsystems|GetMCWorldPreloadSubsystem": [],
    "Loading|BeginPreparation": ["self", "LocalController"],
    "Loading|IsReady": ["self"],
    "Loading|HasFailed": ["self"],
    "VFXLab|SetRunning": ["self", "bEnabled"],
    "Game|GetPlayerController": ["PlayerIndex"],
    "Actor|Tick|SetActorTickEnabled": ["self", "bEnabled"],
    "Utilities|IsValid": ["InputObject"],
    "Development|PrintString": ["InString", "Duration", "Key"],
    "AddEvent|EventTick": [],
    "AddEvent|EventBeginPlay": [],
}
STATION_VARS = {
    "bRunning": ("bool", False, False),
    "Status": ("string", "Preparing resources", False),
    "Cycles": ("int", 0, False),
    "bTrail": ("bool", False, True),
    "bAutoRepeat": ("bool", False, True),
    "RepeatSeconds": ("float", 4.0, True),
    "TrailSpeed": ("float", 220.0, True),
    "TrailAmplitude": ("float", 260.0, True),
    "TrailWidth": ("float", 84.0, True),
    "TrailLifetime": ("float", 2.4, True),
    "TrailOpacity": ("float", 0.48, True),
    "TrailSpawnSpacing": ("float", 18.0, True),
    "SurfaceNormal": ("Vector", {"x": 0.0, "y": 0.0, "z": 1.0}, True),
    "TrailDirection": ("Vector", {"x": 1.0, "y": 0.0, "z": 0.0}, True),
    "MotionPhase": ("float", 0.0, False),
    "CycleAge": ("float", 0.0, False),
    "EmitterOrigin": ("Vector", {"x": 0.0, "y": 0.0, "z": 0.0}, False),
    "PreviousWorld": ("Vector", {"x": 0.0, "y": 0.0, "z": 0.0}, False),
    "RecipeVersion": ("string", RECIPE_VERSION, False),
}
BOOTSTRAP_VARS = {
    "bReleased": ("bool", False, False),
    "bFailed": ("bool", False, False),
    "Status": ("string", "Preparing practice resources", False),
    "RecipeVersion": ("string", RECIPE_VERSION, False),
}


def ref(path):
    return {"refPath": path}


def decoded(value):
    if isinstance(value, str):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value
    return value


class Author:
    def __init__(self, output, resume_created=False):
        self.client = NativeMCP()
        self.output = output
        self.created = []
        self.evidence = {}
        self.node_cache = {}
        self.resume_created = resume_created
        ownership = output / "created_assets.json"
        self.owned = json.loads(ownership.read_text(encoding="utf-8")) if resume_created and ownership.exists() else []

    def write_json(self, name, data):
        (self.output / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")

    def call(self, toolset, tool_name, **args):
        result = self.client.tool("call_tool", {
            "toolset_name": toolset, "tool_name": tool_name, "arguments": args})
        if result.get("isError"):
            raise RuntimeError(f"{toolset}.{tool_name}: {result}")
        blocks = [item["text"] for item in result.get("content", []) if item.get("type") == "text"]
        if not blocks:
            raise RuntimeError(f"No response for {toolset}.{tool_name}")
        payload = decoded(blocks[0])
        if isinstance(payload, dict) and payload.get("error"):
            raise RuntimeError(f"{toolset}.{tool_name}: {payload}")
        return payload.get("returnValue") if isinstance(payload, dict) else payload

    def schema(self, graph=None):
        # Read docs before using graph DSL. Context graph is read, never written.
        docs = self.call(BP, "get_graph_dsl_docs")
        (self.output / "graph_dsl_reference.txt").write_text(docs, encoding="utf-8")
        probe_pins = graph is not None
        if graph is None:
            graph = self.call(BP, "get_graph", blueprint=CONTEXT_BP, graph_name="EventGraph")
        pins = {}
        for node, required in PIN_CONTRACT.items():
            choices = self.call(BP, "find_node_types", graph=graph,
                                type_id_filter=node, context_pins=[])
            if node not in choices:
                raise RuntimeError(f"Required node unavailable: {node}")
            if not probe_pins:
                pins[node] = {"available": True, "pin_probe": False}
                continue
            info = self.call(BP, "get_node_type_pins", graph=graph, type_id=node)
            names = {pin["name"] for pin in info["input_pins"]}
            if set(required) - names:
                raise RuntimeError(f"Node pin contract changed: {node}: {names}")
            pins[node] = info
        # Same-title overload must resolve to the catalogue, not an ice/boss station.
        if probe_pins:
            lab_target = next(p for p in pins["VFXLab|SetRunning"]["input_pins"] if p["name"] == "self")
            if lab_target["type_id"] != "MCVFXLab Object Reference":
                raise RuntimeError(f"Unexpected SetRunning overload: {lab_target}")
        self.evidence["common_pins"] = pins
        self.write_json("schema.json", self.evidence)
        return graph

    def property_schema(self, instance):
        return decoded(self.call(OBJ, "list_properties", instance=instance))

    @staticmethod
    def property_name(schema, name):
        if name in schema:
            return name
        # ObjectTools may present bools with their UI prefix removed.
        candidates = [key for key in schema if key.casefold() == name.casefold()
                      or key.casefold() == name.removeprefix("b").casefold()]
        if len(candidates) != 1:
            raise RuntimeError(f"Property {name!r} not found unambiguously: {candidates}")
        return candidates[0]

    def set_properties(self, instance, values):
        schema = self.property_schema(instance)
        exact = {self.property_name(schema, key): value for key, value in values.items()}
        before = decoded(self.call(OBJ, "get_properties", instance=instance, properties=list(exact)))
        if not self.call(OBJ, "set_properties", instance=instance,
                         values=json.dumps(exact, ensure_ascii=False)):
            raise RuntimeError(f"Property write failed: {instance}: {exact}")
        after = decoded(self.call(OBJ, "get_properties", instance=instance, properties=list(exact)))
        self.evidence.setdefault("properties", []).append({
            "instance": instance, "before": before, "requested": exact, "after": after})

    def create(self, name):
        path = BASE + "/" + name
        if self.call(ASSET, "exists", path=path):
            bp = ref(path + "." + name)
            if self.resume_created and bp in self.owned:
                if self.call(BP, "get_parent", blueprint=bp) != ref("/Script/Engine.Actor"):
                    raise RuntimeError("Owned asset parent changed: " + path)
                self.created.append(bp)
                return bp
            raise RuntimeError(f"Refusing to overwrite existing asset: {path}. Inspect it separately.")
        bp = self.call(BP, "create", folder_path=BASE, asset_name=name,
                       asset_type=ref("/Script/Engine.Actor"))
        self.created.append(bp)
        self.write_json("created_assets.json", self.created)
        return bp

    def variables(self, bp, declarations, objects=None):
        existing = set(self.call(BP, "list_variables", blueprint=bp))
        for name, (kind, _default, editable) in declarations.items():
            if name not in existing:
                self.call(BP, "add_variable", blueprint=bp, name=name, type_name=kind)
            self.call(BP, "set_variable_instance_editable", blueprint=bp,
                      variable_name=name, instance_editable=editable)
        for name, (object_class, editable) in (objects or {}).items():
            if name not in existing:
                self.call(BP, "add_object_variable", blueprint=bp, name=name, object_class=ref(object_class))
            self.call(BP, "set_variable_instance_editable", blueprint=bp,
                      variable_name=name, instance_editable=editable)

    def var_node(self, graph, name, access):
        key = (graph["refPath"], name, access)
        if key in self.node_cache:
            return self.node_cache[key]
        suffixes = {access + name, access + name.removeprefix("b")}
        found = self.call(BP, "find_node_types", graph=graph,
                          type_id_filter=name.removeprefix("b"), context_pins=[])
        candidates = [node for node in found if node.startswith("Variables|")
                      and node.rsplit("|", 1)[-1] in suffixes]
        if len(candidates) != 1:
            raise RuntimeError(f"Cannot resolve {access} {name}: {candidates}")
        self.call(BP, "get_node_type_pins", graph=graph, type_id=candidates[0])
        self.node_cache[key] = candidates[0]
        return candidates[0]

    def render(self, graph, recipe):
        code = (RECIPES / recipe).read_text(encoding="utf-8")
        code = re.sub(r"@get:([A-Za-z0-9_]+)@",
                      lambda match: f"({self.var_node(graph, match[1], 'Get')})", code)
        def local_call(match):
            node = "CallFunction|" + match[1]
            self.call(BP, "get_node_type_pins", graph=graph, type_id=node)
            return f"({node})"
        code = re.sub(r"@call:([A-Za-z0-9_]+)@", local_call, code)
        code = re.sub(r"@set:([A-Za-z0-9_]+):(.*?)@",
                      lambda match: f"({self.var_node(graph, match[1], 'Set')} {match[2]})", code)
        if "@" in code:
            raise RuntimeError(f"Unresolved recipe tokens in {recipe}")
        (self.output / recipe).write_text(code, encoding="utf-8")
        return code

    def graph_write(self, bp, graph, recipe):
        code = self.render(graph, recipe)
        self.call(BP, "write_graph_dsl", graph=graph, code=code)
        self.call(BP, "compile_blueprint", blueprint=bp, warnings_as_errors=True)
        authored = self.call(BP, "read_graph_dsl", graph=graph)
        (self.output / (recipe + ".readback")).write_text(authored, encoding="utf-8")

    def apply(self):
        if self.call(EDITOR, "IsPIERunning"):
            raise RuntimeError("PIE is running; no Blueprint authoring performed")
        # Check both paths before creating either, preserving a clean failure path.
        for name in ("BP_PracticeStation", "BP_PracticeBootstrap"):
            if self.call(ASSET, "exists", path=BASE + "/" + name):
                if not self.resume_created or ref(BASE + "/" + name + "." + name) not in self.owned:
                    raise RuntimeError(f"Task asset already exists; inspect before editing: {name}")
        station = self.create("BP_PracticeStation")
        self.call(BP, "compile_blueprint", blueprint=station, warnings_as_errors=False)
        station_event = self.call(BP, "get_graph", blueprint=station, graph_name="EventGraph")
        self.schema(station_event)
        self.variables(station, STATION_VARS, {"Floor": ("/Script/Engine.Actor", True)})
        self.call(BP, "compile_blueprint", blueprint=station, warnings_as_errors=False)
        cdo = self.call(BP, "get_default_object", blueprint=station)
        components = self.call(ACTOR, "get_components", actor=cdo,
                               component_type=ref("/Script/Niagara.NiagaraComponent"))
        matches = [item for item in components if item["refPath"].rsplit(".", 1)[-1].startswith("PracticeFX")]
        if len(matches) > 1:
            raise RuntimeError("More than one PracticeFX component")
        fx = matches[0] if matches else self.call(ACTOR, "add_component", owner=station,
                                                 component_type=ref("/Script/Niagara.NiagaraComponent"), name="PracticeFX")
        # A fixed parent prevents emitter motion from moving the actor/labels/witnesses.
        self.call(BP, "compile_blueprint", blueprint=station, warnings_as_errors=False)
        cdo = self.call(BP, "get_default_object", blueprint=station)
        parent = self.call(ACTOR, "get_parent_component", component=fx)
        if not parent:
            raise RuntimeError("PracticeFX must be a child of a fixed scene root")
        self.set_properties(fx, {"bAutoActivate": False})
        funcs = {}
        for name in ("ConfigureFX", "Play", "SetRunning", "Reset"):
            funcs[name] = self.call(BP, "add_function_graph", blueprint=station, graph_name=name)
        self.call(BP, "compile_blueprint", blueprint=station, warnings_as_errors=False)
        running_pins = self.call(BP, "get_node_type_pins", graph=station_event, type_id="CallFunction|SetRunning")
        if not any(pin["name"] == "bEnabled" for pin in running_pins["input_pins"]):
            self.call(BP, "add_function_param", graph=funcs["SetRunning"], param_name="bEnabled",
                      param_type="bool", input_param=True)
        self.call(BP, "compile_blueprint", blueprint=station, warnings_as_errors=False)
        self.set_properties(cdo, {name: item[1] for name, item in STATION_VARS.items()})
        for name in ("ConfigureFX", "Play", "SetRunning", "Reset"):
            self.graph_write(station, funcs[name], "Station_" + name + ".dsl")
        event = self.call(BP, "get_graph", blueprint=station, graph_name="EventGraph")
        self.graph_write(station, event, "Station_EventGraph.dsl")

        bootstrap = self.create("BP_PracticeBootstrap")
        # This native subsystem is callable from BP but is not a BlueprintType
        # member-variable type in this build. Fetch its singleton directly per event.
        if "Preparation" in self.call(BP, "list_variables", blueprint=bootstrap):
            self.call(BP, "remove_variable", blueprint=bootstrap, name="Preparation")
        self.variables(bootstrap, BOOTSTRAP_VARS, {"Lab": ("/Script/MessControl.MCVFXLab", True)})
        self.call(BP, "compile_blueprint", blueprint=bootstrap, warnings_as_errors=False)
        boot_cdo = self.call(BP, "get_default_object", blueprint=bootstrap)
        self.set_properties(boot_cdo, {name: item[1] for name, item in BOOTSTRAP_VARS.items()})
        boot_event = self.call(BP, "get_graph", blueprint=bootstrap, graph_name="EventGraph")
        self.graph_write(bootstrap, boot_event, "Bootstrap_EventGraph.dsl")

        # read_graph_dsl internally probes pins and can dirty compile status.
        # Final compilation must follow every readback, not precede it.
        for final_bp in (station, bootstrap):
            self.call(BP, "compile_blueprint", blueprint=final_bp, warnings_as_errors=True)
        # AssetTools operates only on the two explicit Blueprint packages here.
        self.call(ASSET, "save_assets", asset_paths=[BASE + "/BP_PracticeStation",
                                                    BASE + "/BP_PracticeBootstrap"])
        self.evidence.update({"created": self.created, "station_fx": fx, "fixed_parent": parent,
                              "map_authored": False, "pie_performed": False, "build_performed": False})
        self.write_json("apply_result.json", self.evidence)
        print(json.dumps({"saved_blueprints": self.created, "evidence": str(self.output)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--schema", action="store_true", help="Read API docs/pins only")
    mode.add_argument("--apply", action="store_true", help="Create only the two new practice Blueprints")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--resume-created", action="store_true", help="Resume only assets recorded in this output's ownership file")
    args = parser.parse_args()
    output = args.out.resolve()
    if output.drive.lower() != "e:":
        parser.error("All authoring evidence/output must be on E:")
    output.mkdir(parents=True, exist_ok=True)
    author = Author(output, args.resume_created)
    try:
        author.schema()
        if args.apply:
            author.apply()
        else:
            print(json.dumps({"read_only_schema": str(output / "schema.json")}, indent=2))
    except Exception as error:
        author.write_json("failure.json", {"error": str(error), "created": author.created,
                                           "source_build_started": False})
        raise


if __name__ == "__main__":
    main()
