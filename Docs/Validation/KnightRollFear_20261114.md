# Knight roll fear — 2026-11-14

Nearby living players use the existing Pain face while a live knight is
executing Roll. Proximity is radial in 3D, independent of the selected target:
full strength within 500 cm of the ball centre, smoothly fading to zero at
650 cm. Recovery, defeat, transformation, other attacks and mage rolls do not
trigger it. Swallowed/captured/dead players are excluded. Multiple rolling
knights contribute their maximum proximity weight.

The expression and pupil paths derive this cosmetic weight from replicated
boss role, state, attack and location on every peer. It is combined with the
existing Pain presentation priority, so speech, emote eyes, hook/rebirth and
social overlays cannot conceal it. Existing tool-face priority is retained.
Normal face smoothing restores the ongoing expression when the threat leaves.
Health, DamageAt, damage/push rules, movement, emote state and held props are
unchanged. No new replicated state, runtime assets or first-use loads are added.

The existing VFX Composer action `Boss.NutKnight.Roll` retains its real knight
scenario; its owner/description now identify the contextual facial reaction.
This change reuses the prepared player mesh and Pain morph/pupil resources.

Source review and scoped `git diff --check` passed. Two focused native regressions
are authored in `MCKnightRollFearTests.cpp`: `NearbyPlayersAndLifecycle` checks
the real Pain morph, nearby bystanders, radius, lifecycle and absence of damage;
`PainOverridesEmoteAndSpeechThenRestores` checks the real emote/speech morph
priority and restoration. No build, test execution, solo PIE, runtime motion
inspection or visual acceptance is claimed. Unreal MCP tools are not exposed
and no running editor was found. Project policy requires a separate explicit
build request before new C++ can be checked in PIE. Earlier gameplay fixes in
this checkout remain preserved and have the same deferred native validation.
