"""Start the already enabled Epic MCP listener in the current editor without restart."""
import json
import unreal

settings_class = unreal.load_class(None, "/Script/ModelContextProtocolEngine.ModelContextProtocolSettings")
if settings_class is None:
    raise RuntimeError("ModelContextProtocolEngine settings are unavailable; enable the Epic plugin first")
settings = unreal.get_default_object(settings_class)
before = {}
for name, value in (("bAutoStartServer", True), ("ServerPortNumber", 8000), ("ServerUrlPath", "/mcp")):
    try:
        before[name] = settings.get_editor_property(name)
        settings.set_editor_property(name, value)
    except Exception as error:
        # Older prebuilt plugin binaries may not expose settings added in source.
        before[name] = str(error)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "ModelContextProtocol.StartServer 8000")
unreal.log("MC_NATIVE_MCP_START " + json.dumps({"previous_settings": before, "endpoint": "http://127.0.0.1:8000/mcp"}))
