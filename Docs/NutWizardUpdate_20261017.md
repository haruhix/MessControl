# Nut Wizard artist animation update — 2026-10-17

Follow-up: `Docs/WizardBehaviorFix_20261017.md` supersedes the rain pose hold,
walking arm presentation and teleport range/attack order described by the
original validation snapshot below.

Source: `C:/Users/vasil/Downloads/Telegram Desktop/Nut_Wizard (2).blend`.
SHA256: `1cc9457fc636f90cb7db87d68503e0bf9a949e4b644c0bb33bdd61fd9d11fcaa`.
The supplied file is read in a separate factory-started Blender process and is
never saved. The file's actual action names differ from the requested spelling;
the semantic mapping below records the actions that were present.

| Gameplay | Source action | Full length at 30 fps |
|---|---|---:|
| Idle | NUT_W_Nut_Idle | 1.5 s |
| Damage | NUT_W_Nut_damage | 0.466667 s |
| Death | NUT_W_death | 3.3 s |
| Fire nut | NUT_W_Firenut | 2.3 s |
| Revive dropped nuts | NUT_W_CastSummon | 2.3 s |
| Nut rain | NUT_W_NutRain | 3.266667 s |
| Teleport | NUT_W_Nut_teleport | 3.3 s |

Exports and their report live in `ArtSource/NutWizardUpdate_20261017`.
The seven imported sequences and a derived mesh are saved under
`/Game/Gameplay/CoreLoop/NutWizard/ArtistUpdate_20261017`. They share the existing
wizard skeleton. The old mesh, skeleton and animation packages are preserved.
The derived mesh reuses the original geometry and materials and adds the stock
dual-quaternion mesh deformer to match Blender's Preserve Volume setting.

`DA_NutRain` retains the exact existing
`NutWizard/Animations/MCStyleV6/A_NutWizard_Walk` binding and independent close-range
shove. Each new sequence carries `MCAuthoredWizard`; casts also carry
`MCSpellGesture`. These markers suppress procedural pose overlays that would
otherwise overwrite the artist's arms, body and death motion.

The transition duration is 0.25 s. Casts advance through the full source, then
hold the final frame for the blend to idle. The native release fraction remains
0.55. Rain holds its release pose throughout the existing four-second gameplay
channel, then plays all remaining source frames. Damage does not restart a
committed cast. An interrupted damage reaction waits until the cast ends;
uninterrupted reactions play once in full before fading.

Three accepted tool hits spanning at least 0.6 s inside a two-second window
queue evasive teleport. The queue survives until the current animation and
existing special/teleport cooldowns permit it, including within the real
encounter. Mage rotation follows the shortest yaw arc at 180 degrees/s, with no
instant turn at cast start or teleport arrival.

Teleport uses the existing warning and replicated native relocation clock.
Three counter-rotating purple portal bands and sixteen rising sparks at each
endpoint accompany the existing arcane burst. The model contracts/disappears
and returns over 0.25 s around relocation. See `Docs/VFXReferences.md` for the
reviewed original tutorial and reproduction notes. No new texture or external
effect package is required.

Death plays its complete 3.3 s source before a 0.25 s fade. The event delays
victory cleanup until the final boss has finished this performance, preventing
the director from deleting it on the lethal hit.

## Validation

Seven import checks passed: matching skeleton/bind transforms, full clip
durations, finite dense poses and constant root transforms. All 145 protected
old wizard, knight and V10 arena assets retain their original SHA256 hashes;
the supplied Blender file is unchanged. The normal Editor build passed.

Both focused native tests passed:

- `MessControl.CoreLoop.NutBoss.Animation.WizardArtistFullClips`
- `MessControl.CoreLoop.NutBoss.WizardPressureWaitsForFullCastAndTurnsSmoothly`

The animation test uses an intentionally renderless transient mesh and reports
the expected render-data warnings. The combat fixture uses the engine Cube and
reports its existing CPU-access warning. The first death-gate assertion exposed
that the test event was never enabled to tick; the fixture now ticks the event
normally and observes both the delayed victory and subsequent completion.

All 12 solo PIE checks passed: new idle, preserved moving walk, bounded facing,
complete fire-nut/summon/rain performances, two actual revived nuts, actual rain
food, three accepted Pickaxe hits, relocation and visible blink, and full death
playback. The largest observed yaw step was 6 degrees at the fixture's native
30 Hz boss updates. Fire-nut and summon completed in 2.60 s; rain completed its
full source plus the existing channel in 7.60 s.

The first summon-only fixture did not initialize the event's falling-food data;
the final fixture starts the real event via F3 before holding its stage for the
bounded animation sequence. Only the normal AI readiness clocks and observer
placement are controlled. The final lethal hit uses the real Pickaxe. Full
casts, summon results, rain, damage, teleport and death are native gameplay.

`SoloCheck.json`, `Bindings.json`, `FinalAssetAudit.json`, build logs and the
Automation report are in `Artifacts/NutWizardUpdate_20261017`. The 26.3 s
`preview.mp4` repeats 395 genuine 15 Hz captures at 30 fps using their recorded
game-clock spacing; it has no synthesized/interpolated motion. Its complete
decode passed. Idle/walk/cast/summon/rain, both teleport endpoints and death
stills were opened for review under the saved mouth lighting. The arms remain
dark under this lighting; no material or lighting redesign is claimed.
No multiplayer, packaged build or GPU-cost measurement is part of this request.

Reproduction tools:

- `Tools/Blender/export_nut_wizard_update.py`
- `Tools/Unreal/import_nut_wizard_update.py`
- `Tools/Unreal/bind_nut_wizard_update.py`
- `Tools/Unreal/check_nut_wizard_update_solo.py`

Temporary compiler settings used for the live update were restored byte for
byte from `Artifacts/NutWizardUpdate_20261017/CompilerSettings`. A normal Editor
build integrates the final changes without depending on live patches.
