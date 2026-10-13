# Perk choice pause — 13 October 2026

The server pauses the shared world when a player's perk panel opens and holds a separate pause delegate for each chooser. The last finished, cancelled, invalidated or disconnected chooser releases the pause. Chest opening runs before the choice panel; rerolls and contract steps keep the world paused. Controller checks use real time so UI cleanup still works during the pause.

Validation:

- The Editor target compiled and UnrealBuildTool confirmed that it is up to date. UBA stalled on compilation; the engine's exported build actions were executed directly, including linking and metadata generation.
- `MessControl.Rewards.ChoiceSharedPause` passed, with no warnings. It checks frozen gameplay time, duplicate requests, two chooser ownership, disconnect cleanup, request eligibility and chest opening versus card selection.
- Solo PIE exercised the real widgets using R, One and Escape through the native Slate tools. Gameplay time stayed at 0.4166668057 during reroll and two queued choices, then advanced after the last choice. A cursed contract stayed at 12.5248665102 through both choice steps and confirmation, then resumed after Escape.
- Play settings and performance settings were restored. PIE and the editor launched for verification were closed; no maps or content packages were saved.

Network sessions, cooking and packaged builds were not run. The two chooser test exercises server pause ownership in a transient world; it does not replace a replication test.

Evidence: `Tests/index.json`, `Tests/Automation.log`, `LiveStates.jsonl`, `PlayBefore.json`, `EditorBefore.json`.
