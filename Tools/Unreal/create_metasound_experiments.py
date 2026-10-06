"""Author six sample-free MetaSound Sources in the MessControl editor.

Run with UE 5.8: -run=pythonscript -script=<absolute path to this file>.
The graphs use public Blueprint/Python builder APIs. No WAVs are generated or
imported: the saved assets contain Noise, Sine, filters and AD envelopes.
Existing recognized graphs are retained while playback settings are refreshed,
so this script can be rerun safely.
"""

import json
import builtins
import re
from pathlib import Path

import unreal


DESTINATION = "/Game/Audio/Experiments"
LEGACY_SPECS = {
    "MS_Step": dict(duration=0.155, attack=0.002, cutoff=(550, 1700),
                    noise_gain=0.24, resonance=0.38, mode="Low Pass Filter",
                    tones=[(95, 110, 0.22, 0.130)]),
    "MS_KnifeSwing": dict(duration=0.205, attack=0.035, cutoff=(1800, 6500),
                          noise_gain=0.42, resonance=0.15, mode="Band Pass",
                          tones=[]),
    "MS_KnifeHit": dict(duration=0.245, attack=0.001, cutoff=(1200, 3600),
                        noise_gain=0.28, resonance=0.45, mode="Low Pass Filter",
                        tones=[(170, 185, 0.17, 0.200),
                               (640, 0, 0.055, 0.120)]),
    "MS_PickaxeSwing": dict(duration=0.225, attack=0.055, cutoff=(900, 3300),
                            noise_gain=0.42, resonance=0.15, mode="Band Pass",
                            tones=[]),
    "MS_PickaxeHit": dict(duration=0.310, attack=0.001, cutoff=(700, 2500),
                          noise_gain=0.24, resonance=0.35, mode="Low Pass Filter",
                          tones=[(135, 85, 0.17, 0.240),
                                 (780, 0, 0.070, 0.290),
                                 (1275, 0, 0.038, 0.260)]),
    "MS_Brush": dict(duration=0.220, attack=0.018, cutoff=(650, 2500),
                     noise_gain=0.43, resonance=0.28, mode="Band Pass",
                     tones=[], brush=True),
}
SPECS = {name: dict(spec, resonance=1.0) for name, spec in LEGACY_SPECS.items()}
SPECS["MS_KnifeHit"] = dict(
    design="organic_cut_v2", duration=0.205, attack=0.002,
    cutoff=(2400.0, 4400.0), noise_gain=0.40, resonance=1.45, mode="Band Pass",
    scratch_delay=0.014, scratch_decay=0.115,
    pop_hz=105.0, pop_sweep=720.0, pop_gain=0.24, pop_decay=0.050,
)
SPECS["MS_PickaxeHit"] = dict(
    design="hard_click_ring_v2", duration=0.315, attack=0.0005,
    cutoff=(4200.0, 6800.0), noise_gain=0.40, resonance=1.0,
    mode="High Pass Filter", click_decay=0.016, ring_delay=0.025,
    tones=[(1260.0, 0.15, 0.220), (2075.0, 0.10, 0.195), (3180.0, 0.05, 0.160)],
)
KNOWN_VERSIONS = {"SampleFree.v1", "SampleFree.v2"}


def require(result, operation):
    if result != unreal.MetaSoundBuilderResult.SUCCEEDED:
        raise RuntimeError("MetaSound operation failed: {} ({})".format(operation, result))


def struct_guid(value, field):
    # Builder handles intentionally expose no Python getter for their protected ID.
    # StructBase's public text serialization preserves the reflected GUID exactly.
    match = re.search(r"(?:^|[,(])" + field + r"=([0-9A-Fa-f]{32})", value.export_text())
    if not match:
        raise RuntimeError("Missing serialized GUID " + field)
    return match.group(1)


def create_playback_settings():
    """Keep nearby sources spatial and put a ceiling on simultaneous voices."""
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    attenuation = unreal.load_asset(DESTINATION + "/SA_Experiment")
    if attenuation is None:
        attenuation = asset_tools.create_asset("SA_Experiment", DESTINATION,
                                               unreal.SoundAttenuation,
                                               unreal.SoundAttenuationFactory())
        settings = attenuation.get_editor_property("attenuation")
        settings.set_editor_property("attenuate", True)
        settings.set_editor_property("spatialize", True)
        settings.set_editor_property("attenuation_shape_extents", unreal.Vector(150.0, 0.0, 0.0))
        settings.set_editor_property("falloff_distance", 1500.0)
        attenuation.set_editor_property("attenuation", settings)
        if not unreal.EditorAssetLibrary.save_loaded_asset(attenuation, only_if_is_dirty=False):
            raise RuntimeError("Unable to save experiment attenuation")
    concurrency = unreal.load_asset(DESTINATION + "/SC_Experiment")
    if concurrency is None:
        concurrency = asset_tools.create_asset("SC_Experiment", DESTINATION,
                                               unreal.SoundConcurrency,
                                               unreal.SoundConcurrencyFactory())
        settings = concurrency.get_editor_property("concurrency")
        settings.set_editor_property("max_count", 8)
        settings.set_editor_property("resolution_rule", unreal.MaxConcurrentResolutionRule.STOP_QUIETEST)
        settings.set_editor_property("voice_steal_release_time", 0.025)
        concurrency.set_editor_property("concurrency", settings)
        if not unreal.EditorAssetLibrary.save_loaded_asset(concurrency, only_if_is_dirty=False):
            raise RuntimeError("Unable to save experiment concurrency")
    return attenuation, concurrency


def configure_source(asset, attenuation, concurrency, version=None, spec=None):
    asset.set_editor_property("attenuation_settings", attenuation)
    asset.set_editor_property("concurrency_set", {concurrency})
    asset.set_editor_property("volume", 0.8)
    if version is not None:
        unreal.EditorAssetLibrary.set_metadata_tag(asset, "MC.AudioExperiment", version)
    if spec is not None:
        unreal.EditorAssetLibrary.set_metadata_tag(asset, "MC.AudioExperiment.Spec",
                                                 json.dumps(spec, sort_keys=True))
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Unable to save " + asset.get_path_name())


class Graph:
    def __init__(self, name, existing_asset=None):
        self.name = name
        self.system = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)
        self.editor = unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem)
        self.existing_asset = existing_asset
        if existing_asset is None:
            (self.builder, self.on_play, self.on_finished, self.audio_out,
             result) = self.system.create_source_builder(name + "_Authoring")
            require(result, "create source " + name)
        else:
            # Runtime BuildAndOverwriteMetaSound refuses serialized assets, and
            # BuildToAsset calls AssetTools.CreateAsset (which would prompt/delete
            # an existing UObject). Edit the existing document through its editor
            # builder instead, preserving both package and graph interface nodes.
            self.builder, result = self.editor.find_or_begin_building(existing_asset)
            require(result, "begin existing graph " + name)
            self.clear_existing_dsp()
        self.nodes = []
        self.edges = 0
        self.start = self.on_play
        intensity = self.graph_input("Intensity", 0.65)
        speed = self.graph_input("Speed", 0.5)
        self.intensity = self.clamp(intensity)
        self.speed = self.clamp(speed)
        random = self.node("RandomFloat", "", "UE")
        self.value(random, "Min", 0.93)
        self.value(random, "Max", 1.07)
        self.connect(self.on_play, self.input(random, "Next"))
        self.variation = self.output(random, "Value")
        self.start = self.output(random, "On Next")

    def node(self, name, variant="Audio", namespace="UE"):
        node, result = self.builder.add_node_by_class_name(
            unreal.MetasoundFrontendClassName(namespace=namespace, name=name, variant=variant))
        require(result, "add {}/{}/{}".format(namespace, name, variant))
        # Existing asset builders do not run BuildToAsset's InitNodeLocations.
        # Frontend nodes need locations to be visible when the editor syncs them.
        if self.existing_asset is not None:
            index = len(self.nodes)
            require(self.editor.set_node_location(
                self.builder, node, unreal.Vector2D(300.0 + (index % 8) * 320.0,
                                                   (index // 8) * 260.0)),
                    "position existing graph node")
        self.nodes.append(node)
        return node

    def input(self, node, name):
        handle, result = self.builder.find_node_input_by_name(node, name)
        require(result, "find input " + name)
        return handle

    def output(self, node, name):
        handle, result = self.builder.find_node_output_by_name(node, name)
        require(result, "find output " + name)
        return handle

    def connect(self, output, input_handle):
        require(self.builder.connect_nodes(output, input_handle), "connect")
        self.edges += 1

    def literal(self, value):
        if isinstance(value, bool):
            return self.system.create_bool_meta_sound_literal(value)[0]
        if isinstance(value, int):
            return self.system.create_int_meta_sound_literal(value)[0]
        return self.system.create_float_meta_sound_literal(float(value))[0]

    def value(self, node, name, value):
        require(self.builder.set_node_input_default(self.input(node, name), self.literal(value)),
                "default " + name)

    def graph_input(self, name, default):
        if self.existing_asset is not None:
            _, data_type, handle, result = self.builder.find_graph_input_node(name)
            require(result, "existing graph input " + name)
            if str(data_type) != "Float":
                raise RuntimeError("Expected Float input " + name)
            require(self.builder.set_graph_input_default(name, self.literal(default)),
                    "graph input default " + name)
            return handle
        handle, result = self.builder.add_graph_input_node(name, "Float", self.literal(default))
        require(result, "graph input " + name)
        return handle

    def clear_existing_dsp(self):
        """Remove computational nodes, keeping the source's input/output IDs."""
        document = self.existing_asset.get_editor_property("root_metasound_document")
        root_graph = document.get_editor_property("root_graph")
        pages = root_graph.get_editor_property("paged_graphs")
        if len(pages) != 1:
            raise RuntimeError("Refusing to rebuild experiment with multiple graph pages")
        old_nodes = list(pages[0].get_editor_property("nodes"))
        preserved = set()
        names, result = self.builder.get_graph_input_names()
        require(result, "graph input names")
        for name in names:
            handle, _, output, result = self.builder.find_graph_input_node(name)
            require(result, "preserve input " + str(name))
            preserved.add(struct_guid(handle, "NodeID"))
            if str(name) == "UE.Source.OnPlay":
                self.on_play = output
        names, result = self.builder.get_graph_output_names()
        require(result, "graph output names")
        self.audio_out = []
        for name in names:
            handle, data_type, input_handle, result = self.builder.find_graph_output_node(name)
            require(result, "preserve output " + str(name))
            preserved.add(struct_guid(handle, "NodeID"))
            if str(name) == "UE.Source.OneShot.OnFinished":
                self.on_finished = input_handle
            elif str(data_type) == "Audio":
                self.audio_out.append(input_handle)
        if not hasattr(self, "on_play") or not hasattr(self, "on_finished") or len(self.audio_out) != 1:
            raise RuntimeError("Unexpected source interface on " + self.name)
        for node in old_nodes:
            node_id = struct_guid(node, "ID")
            if node_id in preserved:
                continue
            handle = unreal.MetaSoundNodeHandle()
            if not handle.import_text("(NodeID=" + node_id + ")"):
                raise RuntimeError("Cannot reconstruct existing node handle")
            # Removing an input template can also remove a dependent template;
            # check presence before removing the snapshot's next node.
            if self.builder.contains_node(handle):
                require(self.builder.remove_node(handle), "remove old DSP node")
        # Remove the previous graph's connections into its retained output nodes.
        for input_handle in [self.on_finished] + list(self.audio_out):
            if self.builder.node_input_is_connected(input_handle):
                require(self.builder.disconnect_node_input(input_handle), "disconnect old output")

    def clamp(self, source):
        node = self.node("Clamp", "Float", "Clamp")
        self.value(node, "Min", 0.0)
        self.value(node, "Max", 1.0)
        self.connect(source, self.input(node, "In"))
        return self.output(node, "Value")

    def math(self, operation, left, right, variant="Float"):
        node = self.node(operation, variant)
        for pin, value in (("PrimaryOperand", left), ("AdditionalOperands", right)):
            if isinstance(value, (float, int)):
                self.value(node, pin, float(value))
            else:
                self.connect(value, self.input(node, pin))
        return self.output(node, "Out")

    def affine(self, source, minimum, maximum):
        return self.math("Add", self.math("Multiply", source, maximum - minimum), minimum)

    def envelope(self, attack, decay, audio=True, finish=False, trigger=None,
                 attack_curve=1.5, decay_curve=0.5):
        node = self.node("AD Envelope", "Audio" if audio else "Float", "AD Envelope")
        self.value(node, "Attack Time", attack)
        self.value(node, "Decay Time", decay)
        self.value(node, "Attack Curve", attack_curve)
        self.value(node, "Decay Curve", decay_curve)
        self.connect(self.start if trigger is None else trigger, self.input(node, "Trigger"))
        if finish:
            self.connect(self.output(node, "On Done"), self.on_finished)
        return self.output(node, "Out Envelope")

    def delay_trigger(self, delay):
        node = self.node("Trigger Delay", "")
        self.value(node, "Delay Time", delay)
        self.connect(self.start, self.input(node, "In"))
        return self.output(node, "Out")

    def filtered_noise(self, spec, cutoff):
        noise = self.node("Noise")
        self.value(noise, "Type", 1)
        self.value(noise, "Seed", -1)
        filter_node = self.node("State Variable Filter")
        self.connect(self.output(noise, "Audio"), self.input(filter_node, "In"))
        self.connect(cutoff, self.input(filter_node, "Cutoff Frequency"))
        self.value(filter_node, "Resonance", spec["resonance"])
        return self.output(filter_node, spec["mode"])

    def oscillator(self, frequency, bipolar=True):
        node = self.node("Sine")
        self.value(node, "Bi Polar", bipolar)
        self.connect(frequency, self.input(node, "Frequency"))
        return self.output(node, "Audio")

    def synthesize(self, spec):
        if spec.get("design") in ("organic_cut_v2", "hard_click_ring_v2"):
            return self.synthesize_hit_v2(spec)
        # A new random seed per instance and a small frequency variation avoid
        # identical repetitions. Intensity affects gain and filter; Speed affects
        # pitch, filter and (for brushing) the grain repetition rate.
        gain = self.affine(self.intensity, 0.15, 1.0)
        brightness = self.math("Add", self.math("Multiply", self.intensity, 0.7),
                               self.math("Multiply", self.speed, 0.3))
        cutoff = self.affine(brightness, *spec["cutoff"])
        cutoff = self.math("Multiply", cutoff, self.variation)
        noise = self.node("Noise")
        self.value(noise, "Type", 1)  # White noise enum in UE5.8 source.
        self.value(noise, "Seed", -1)
        filter_node = self.node("State Variable Filter")
        self.connect(self.output(noise, "Audio"), self.input(filter_node, "In"))
        self.connect(cutoff, self.input(filter_node, "Cutoff Frequency"))
        self.value(filter_node, "Resonance", spec["resonance"])
        texture = self.output(filter_node, spec["mode"])
        if spec.get("brush"):
            grain_rate = self.affine(self.speed, 35.0, 95.0)
            texture = self.math("Multiply", texture, self.oscillator(grain_rate, False), "Audio")
        noise_envelope = self.envelope(spec["attack"], spec["duration"] - spec["attack"],
                                       finish=True)
        mixed = self.math("Multiply", texture, noise_envelope, "Audio")
        mixed = self.math("Multiply", mixed, spec["noise_gain"], "Audio by Float")
        pitch_multiplier = self.math("Multiply", self.affine(self.speed, 0.94, 1.18),
                                      self.variation)
        for hz, sweep, tone_gain, decay in spec["tones"]:
            frequency = self.math("Multiply", pitch_multiplier, float(hz))
            if sweep:
                pitch_envelope = self.envelope(0.001, decay, audio=False)
                frequency = self.math("Add", frequency,
                                      self.math("Multiply", pitch_envelope, float(sweep)))
            tone = self.oscillator(frequency)
            tone_envelope = self.envelope(0.001, decay)
            tone = self.math("Multiply", tone, tone_envelope, "Audio")
            tone = self.math("Multiply", tone, tone_gain, "Audio by Float")
            mixed = self.math("Add", mixed, tone, "Audio")
        mixed = self.math("Multiply", mixed, gain, "Audio by Float")
        self.connect(mixed, self.audio_out[0])

    def synthesize_hit_v2(self, spec):
        gain = self.affine(self.intensity, 0.15, 1.0)
        brightness = self.math("Add", self.math("Multiply", self.intensity, 0.7),
                               self.math("Multiply", self.speed, 0.3))
        cutoff = self.math("Multiply", self.affine(brightness, *spec["cutoff"]), self.variation)
        pitch = self.math("Multiply", self.affine(self.speed, 0.94, 1.18), self.variation)
        # Completion is independent of the short impact: delayed scratch/ring
        # voices all finish before this silent deadline envelope reports done.
        self.envelope(0.001, spec["duration"] - 0.001, finish=True)
        if spec["design"] == "organic_cut_v2":
            pitch_env = self.envelope(0.001, spec["pop_decay"], audio=False,
                                      attack_curve=1.0, decay_curve=0.4)
            frequency = self.math("Add", self.math("Multiply", pitch, spec["pop_hz"]),
                                  self.math("Multiply", pitch_env, spec["pop_sweep"]))
            pop = self.oscillator(frequency)
            pop = self.math("Multiply", pop,
                            self.envelope(0.001, spec["pop_decay"], decay_curve=0.4), "Audio")
            pop = self.math("Multiply", pop, spec["pop_gain"], "Audio by Float")
            scratch_start = self.delay_trigger(spec["scratch_delay"])
            sweep_env = self.envelope(0.001, 0.075, audio=False, trigger=scratch_start)
            cutoff = self.math("Multiply", cutoff, self.affine(sweep_env, 0.7, 1.5))
            scratch = self.filtered_noise(spec, cutoff)
            scratch_env = self.envelope(spec["attack"], spec["scratch_decay"],
                                        trigger=scratch_start, decay_curve=0.65)
            scratch = self.math("Multiply", scratch, scratch_env, "Audio")
            scratch = self.math("Multiply", scratch, spec["noise_gain"], "Audio by Float")
            mixed = self.math("Add", pop, scratch, "Audio")
        else:
            click = self.filtered_noise(spec, cutoff)
            click_env = self.envelope(spec["attack"], spec["click_decay"], decay_curve=0.35)
            click = self.math("Multiply", click, click_env, "Audio")
            mixed = self.math("Multiply", click, spec["noise_gain"], "Audio by Float")
            ring_start = self.delay_trigger(spec["ring_delay"])
            for hz, tone_gain, decay in spec["tones"]:
                tone = self.oscillator(self.math("Multiply", pitch, hz))
                ring_env = self.envelope(0.001, decay, trigger=ring_start,
                                         attack_curve=1.0, decay_curve=1.5)
                tone = self.math("Multiply", tone, ring_env, "Audio")
                tone = self.math("Multiply", tone, tone_gain, "Audio by Float")
                mixed = self.math("Add", mixed, tone, "Audio")
        self.connect(self.math("Multiply", mixed, gain, "Audio by Float"), self.audio_out[0])

    def save(self, spec, attenuation, concurrency, version="SampleFree.v2"):
        # Use the editor subsystem so these become editable graph assets with
        # initialized node positions and registered class metadata.
        if self.existing_asset is None:
            _, result = self.editor.build_to_asset(self.builder, "MessControl Audio Experiments",
                                                   self.name, DESTINATION)
            require(result, "build asset " + self.name)
        path = DESTINATION + "/" + self.name
        asset = unreal.load_asset(path)
        if not isinstance(asset, unreal.MetaSoundSource):
            raise RuntimeError("Expected MetaSound Source at " + path)
        # Saving calls UMetaSoundSource::PreSave -> FAssetHelper::PreSaveAsset,
        # registering the changed graph with the frontend/editor in the same
        # UObject. BuildToAsset is reserved for newly authored packages.
        configure_source(asset, attenuation, concurrency, version=version, spec=spec)
        return dict(path=asset.get_path_name(), nodes=len(self.nodes), edges=self.edges,
                    duration=spec["duration"], inputs=["Intensity", "Speed"], waves=0,
                    version=version, spec=spec, rebuilt=self.existing_asset is not None)


def main():
    project_name = Path(unreal.Paths.get_project_file_path()).stem
    if project_name != "MessControl":
        raise RuntimeError("Refusing to author assets in project " + project_name)
    rebuild = bool(getattr(builtins, "mc_rebuild_audio_experiments", False)) or (
        "-MCRebuildAudioExperiments" in unreal.SystemLibrary.get_command_line())
    attenuation, concurrency = create_playback_settings()
    output = Path(unreal.Paths.project_saved_dir()) / "MetaSoundExperiments.json"
    previous = {}
    if output.exists():
        previous = {row["path"]: row for row in json.loads(output.read_text(encoding="utf-8"))}
    results = []
    for name, spec in SPECS.items():
        path = DESTINATION + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            asset = unreal.load_asset(path)
            version = unreal.EditorAssetLibrary.get_metadata_tag(asset, "MC.AudioExperiment")
            if not isinstance(asset, unreal.MetaSoundSource) or version not in KNOWN_VERSIONS:
                raise RuntimeError("Existing unrecognized asset at " + path)
            if rebuild:
                # Validate the full DSP first against a transient document, then
                # apply the same construction to the existing serialized asset.
                preview = Graph(name)
                preview.synthesize(spec)
                graph = Graph(name, existing_asset=asset)
                graph.synthesize(spec)
                results.append(graph.save(spec, attenuation, concurrency))
                unreal.log("Rebuilt procedural experiment in place " + path)
                continue
            configure_source(asset, attenuation, concurrency)
            row = dict(previous.get(asset.get_path_name(), {}))
            serialized_spec = unreal.EditorAssetLibrary.get_metadata_tag(asset, "MC.AudioExperiment.Spec")
            if serialized_spec:
                actual_spec = json.loads(serialized_spec)
            elif version == "SampleFree.v1":
                actual_spec = LEGACY_SPECS[name]
            else:
                raise RuntimeError("Missing saved v2 specification for " + path)
            row.update(path=asset.get_path_name(), retained=True, duration=actual_spec["duration"],
                       inputs=["Intensity", "Speed"], waves=0, version=version, spec=actual_spec)
            results.append(row)
            continue
        graph = Graph(name)
        graph.synthesize(spec)
        results.append(graph.save(spec, attenuation, concurrency))
        unreal.log("Created procedural experiment " + path)
    output.write_text(json.dumps(results, indent=2), encoding="utf-8")
    unreal.log("MetaSound experiments authored: " + str(output))


if __name__ == "__main__":
    main()
