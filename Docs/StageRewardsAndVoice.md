# Arena arrival, knight pressure, stage boosters and voice

Implementation scope agreed on 2026-10-21. Source validation and actual runtime
validation must be reported separately. This document does not certify a build
or a multiplayer session.

## Arrival

`AMCGameMode.ArenaArrivalHeight` defaults to 420 cm of clearance below the
capsule. The initial spawn checks the tongue and the whole capsule corridor;
low ceilings shorten the drop. A replicated hold, also reapplied after client
restart, prevents the drop from finishing behind loading. The existing team
loading/first-perk barrier releases it. Tutorial readiness is independent of the
ordinary day director's Training stage. Tooth-reserve respawns retain their
existing 30 cm settling clearance.

## Knight pressure response

Knight profile fields control 10–12 successful weapon hits shared across the
team, a four-second gap reset, a 0.45-second warning, 340 cm radius, 6 damage,
750 push and four-second cooldown. The counter cannot be interrupted by more
weapon contacts. Jump/roll execution is not cancelled mid-motion. It reuses the
existing melee animation and impact cues. Breaking the shield clears pressure
and retains the guaranteed five-second stun; hits during that stun do not
accumulate a new counter. Ordinary damage is still accepted during the counter.

## Shared stage chest

Each completed opening/post-nut food portion waits for both food processing and
visible cleanup, then drops one chest onto a validated central tongue footprint.
Key events and the small mint use the same reward gate. The next portion/event
waits until this reward resolves. Completion IDs prevent duplicate rewards.
Occupied landing positions retry instead of silently losing the chest.

The first eligible nearby human opens the existing three-card interface.
Outstanding personal level choices retain priority. The choice uses the existing
pause lease and retained carried-food path. Disconnection releases the opener.
All-bot diagnostic sessions resolve a choice without inventing a human RPC;
bots cannot take the choice away from a participating human, including a dead
human waiting to recover.

The choice immediately activates one booster for connected living teammates:

- Mouthwash: one union of teammate radii; each target receives the effect once,
  without friendly push or damage to carried food.
- Brush fury: starts five seconds of fury immediately for each living teammate.
- Grenade: immediately arms each living teammate; their next knockdown triggers
  the existing explosion.

These shared choices do not add charges or alter ordinary personal booster
charges. The team-specific card copy explains the difference. Fury's timer starts
at selection, including during a quiet interval; it is not deferred until combat.

## Voice

`UMCVoiceChatSubsystem` uses Unreal's existing OSS Steam voice transport and
codec. Default input is **V, hold to talk**. Preferences include rebinding,
open microphone with threshold, gain, voice volume, self mute and persistent
per-player mute by online identity. Slate observes input before UI-only menus
without consuming it; text fields, key capture, focus loss and travel release
the held key. No custom audio packet RPC is introduced.

Voice has an independent sound-class root, so SFX volume does not change it.
The overhead indicator uses actual OSS talking state and microphone/playback
envelopes. Bots do not speak. The three envelope bars and optional microphone
brush inherit the shared UI theme.

The public engine OSS API cannot switch capture devices. Settings show the
Windows default microphone and open Windows sound settings; a device change
may require restarting the game. There is no pretend device-selection dropdown.
The explicit local microphone test is available outside online sessions. It
stops and releases on leaving the voice panel or travelling, and never sends
test audio into a room. Ordinary PIE/NULL cannot validate Steam voice transport.

Editable assets are authored by `Tools/Unreal/UI/author_voice_ui.py` after an
approved Editor build: `WBP_VoiceSettings`, `WBP_VoiceIndicator`, and an append-only
fifth tab in `WBP_MainMenu`, under `/Game/Gameplay/UI`. Existing four tabs and
artist material parameters are preserved. Factory roles are `VoiceSettings` and
`VoiceIndicator`. New copy belongs in `DT_UI_Localization` via the existing
catalog migration, with Russian and English entries.

## Validation performed on 2026-10-21

The approved MessControlEditor build succeeded. All 47 focused native checks
passed: spawn clearance/hold/tutorial release, knight pressure/shield stun and
animation timing, shared chest claims/effects and sequence continuation, and
voice privacy gates/preferences. These are local checks, including multiple
recipient objects in one authority world; they do not validate replication.

Short solo PIE checks passed 15 arrival/counter assertions, 16 chest assertions
and 68 voice UI assertions. The clean counter run observed 11 shared hits, a
100-to-94 HP change and 462 cm maximum displacement. A hit during the warning
did not restart the counter. The chest dropped, opened on approach, applied one
team grenade booster and allowed the native coffee sequence to continue.
The voice panel saved/restored preferences without starting microphone capture.
The two new WBP assets and fifth main-menu tab compiled and saved; authoring
checked existing tab/material preservation, theme roles and localization.

Four videos, contact sheets and the detailed receipts are in
`Artifacts/StageRewardsVoice_20261021/Preview.md`. All three latest solo Editor
processes exited normally. One earlier arrival check completed gameplay but
exited with 0xC0000005 after its log closed. No stack or dump was available;
the full repeat with automatic script completion exited normally. Its cause
is unproven, and no production crash fix is claimed.

No multiplayer, microphone capture, cook or packaging test was run. Real Steam
voice transmission, the overhead indicator with actual speech, peer muting and
travel require a separately authorized multiplayer check. Physical input rebinding
and the optional local microphone test also remain unverified.
