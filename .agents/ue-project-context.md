# MessControl Unreal project context

Verified from the checkout on 2026-10-10. Current source and saved Unreal packages remain authoritative; project indexes are derived evidence.

- Project root: `E:/DEVGAME/MessControl`; project: `MessControl.uproject`.
- Engine association: 5.8; local editor: `E:/UE/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe` (5.8.1).
- Runtime module: `MessControl`; editor module: `MessControlEditor`. Both targets use BuildSettingsVersion.V7 and Unreal5_8 include order.
- Framework: `AMCGameMode`, `AMCGameState`, `AMCPlayerController`, `AMCPlayerState`, `AMCToothCharacter`; engine `UGameInstance`. Default GameMode is `/Game/Blueprints/BP_MouthGameMode`; default map `/Game/Maps/L_MainMenu`; authored gameplay arena `/Game/Maps/L_Mouth`.
- Multiplayer is authoritative server gameplay with replicated actors, state and RPCs. Playtest AI uses `AMCPlaytestBotController`; some living-player checks require its nonspectator PlayerState.
- Enabled relevant plugins: Niagara, EnhancedInput, PhysicsControl, ProceduralMeshComponent, Metasound, OnlineSubsystemSteam and SteamSockets. PythonScriptPlugin, EditorScriptingUtilities, SequencerScripting, MessControlProjectIndex and the Epic MCP toolsets are editor-only.
- Rendering config: automatic exposure and motion blur disabled; AA method 2; dynamic GI method 0; reflection method 2; virtual shadows disabled; Substrate enabled. Saved arena light and postprocess settings can additionally override the project defaults.
- Editor authoring helpers: `Tools/Unreal`; live connection `unreal_epic`. Prefer the live MCP; `Tools/Unreal/remote_python.py` is the existing loopback editor-Python fallback. Saved asset index lives in `Saved/ProjectIndex` and can be stale after native edits.
- VFX lab map: `/Game/Tests/L_VFXLab`; imported sample packs remain unmodified. Lab actors are opt-in fixtures and do not belong in the main gameplay map.
- Validation preference: implement and integrate promptly, then batch requested gameplay/network/regression checks. Do not run a separate Windows build/cook/package without a user request. The user has authorized editor LiveCoding when useful.
- Preserve unrelated dirty source/assets and unsaved editor work. No commit or branch is assumed.

Unknown/unverified: packaged target platforms beyond the current Windows editor, final shipping/cook settings, and complete plugin usage. Consult source/configuration instead of treating absence here as evidence.
