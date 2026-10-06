# Boss foundation

`AMCBossCharacter` is a replicated character with a server-owned health/phase/attack snapshot. `AMCBossAIController` makes decisions on a timer, acquires living player-controlled tooth characters, checks visibility, follows real navigation paths, telegraphs a locked attack direction, strikes once, and recovers. It remembers a lost player's last visible location briefly; it cannot attack through occluders or replace failed navigation with direct movement.

`/Game/Gameplay/Boss/BP_ZombieBoss` is a dev-test asset; the normal map contains no boss and no automatic encounter trigger. F3 → **Босс — создать Zombie для теста** creates one dormant Zombie on an unobstructed Boss NavMesh point near the host, tagged `MC_DevBoss`. F3 → **включить AI и бой** explicitly enables combat. Separate buttons stop/reset it and remove only this tagged dev actor. The deprecated `bStartAwake` property is ignored; receiving damage does not wake a dormant boss. Task rewards do not spawn or activate it.

Its profile is `/Game/Gameplay/Boss/DA_ZombieBoss`. The profile references the artist Zombie, three used texture maps and its independent 85-bone skeleton. The artist's unused Bag material was excluded. The player skeleton has a different reference pose and is untouched. Seven in-place bone clips were authored directly on the Zombie skeleton: Idle/breathing, Shamble, PunchLeft, PunchRight, Kick, Hurt and Death. Native presentation plays these sequences when `AnimationClass` is empty; a later boss AnimBP can replace it through the same profile.

For another boss, create a Blueprint child of `AMCBossCharacter`, assign a `UMCBossProfile` and configure its capsule/mesh. Encounter spawning is deliberately unwired for now. Project SupportedAgents retain Default `35/144` and add Boss `60/220`, matching the Zombie capsule. The map retains saved navigation for both so F3 can test real movement. The F3 spawn query uses the boss capsule dimensions even before its movement component's owner-collision update. It rejects blocked spawn capsules and reports when no safe nearby point exists.

Each profile attack has a unique `AttackId`, phase gate, selection weight, range, vertical reach, cone, damage and timings. The default `ExecuteAttack` deals one frontal impact to eligible players through their authoritative tooth-status component. Override this BlueprintNativeEvent for a projectile, hazard or multi-step skill. The override owns its damage; keep authority checks and do not call the parent unless its frontal hit is intended too. Use `OnBossTelegraph`, `OnBossAttackImpact`, health/phase/state/death events for montage, audio and VFX only. Replicated server timestamps let presentation resume at the correct age instead of restarting on every update. Impact is a reliable cosmetic multicast; gameplay damage remains server-only.

Use `ReceiveBossDamage` or the engine `ApplyDamage` route to damage a combat-enabled test boss. Attack/phase configuration is sanitized and copied at initialization; edits do not mutate an attack midway through its windup. A phase unlocks when health crosses its sorted threshold and never regresses. Deactivate, death, unpossess and EndPlay stop movement and clear timers. Death cancels a pending strike and disables the collision capsule. `ResetForRun` clears attack timers/cooldowns, restores health, phase and walking collision, and returns even a killed boss to Dormant without activating it. Handle the Dormant state event to reset Blueprint presentation.

F3 provides individual animation buttons. These reset health, stop the AI, disable movement, replicate the selected clip and start time, and suppress received/dealt damage. Walk previews show an in-place gait; the dedicated AI button tests actual navigation. Death preview holds its final pose without killing the test actor, so another button can immediately play a different clip. Cosmetic single-node playback follows server timestamps and never fires combat notifies. One full attack clip spans Telegraph → Attacking → Recovering without restarting on those transitions; damage remains the single server timer impact.

| Clip | Duration | Authoritative impact |
|---|---:|---:|
| Idle | 2.4 s, loop | — |
| Shamble | 1.6 s, loop | — |
| PunchLeft | 1.4 s | 0.70 s |
| PunchRight | 1.5 s | 0.76 s |
| Kick | 1.8 s | 0.90 s |
| Hurt | 0.667 s | — |
| Death | 2.4 s | — |

`Tools/Blender/build_zombie_animations.py` exports the owned clips and `ArtSource/ZombieBossAnimations/ZombieBossAnimations.blend`: all seven actions plus a consecutive NLA review timeline and impact markers. It preserves the original ZombieBoss blend and the artist source. `Tools/Unreal/import_zombie_animations.py` imports sequences against `SK_ZombieBoss_Skeleton`, verifies their durations and configures the profile's three hand/kick attacks. It creates no level actor. Attack VFX, production AnimBP/blending, encounter pacing, bespoke abilities and boss loot remain future encounter work.

The combined chest/HUD solo Editor-runtime pass succeeded: no normal-map boss, explicit F3 dormant spawn with damage/movement disabled, separate F3 AI activation, authoritative damage, living-player targeting, actual navigation, telegraph/impact and stopped behavior after death. The clips were rendered in Blender and imported against the own skeleton; the previous profile's accidental Pitch90 was migrated to explicit Yaw−90 and visually checked. Review captures are in `Artifacts/ChestBossReview_20261004`. Multiplayer validation and game packaging are outside this pass.

## Third boss phase

2026-10-07: phase 3 now uses the corrected Auto-Rig Pro mesh and its stable
104-bone skeleton from `carries_weights_fixed.blend`. `walk` and
`hit_atack_01` replace the previous clips; all eight old animation packages
were removed. A neutral source pose supplies idle. See [BossPhase3ARP.md](BossPhase3ARP.md)
for current assets, F3 controls and the repeatable animation-only import path.
Melee eligibility now tests the player's capsule against reach/height/cone;
direct torso contact has no angular blind spot, including coincident XY with
different heights. Visibility still rejects occluded targets. The phase-3 hit
starts at source frame 37, impacts 0.2 s into its clip, and has no extra cooldown.
The phase-3 mesh is now 300 cm tall (+25%) with a matching 1.25 torso hitbox
scale. `StartDelaySeconds=0.50` holds idle before each combat strike; replicated
`AttackStartedAt` marks the future clip start. Total time to damage is 0.70 s,
while the complete 1.567 s clip retains its authored speed. Isolated F3 clip
previews continue to play immediately.
Its profile sets `AnimationBlendSeconds=0.30`: native `UMCBossAnimInstance`
crossfades timestamped full-body sequences. Attacks enter at full weight and
finish their entire clip before blending out to locomotion; death can cancel
the presentation lock. Repeated attacks also start without an incoming blend,
and server damage timing is unchanged. Profiles
with zero blend duration retain the original single-node presentation.
The following describes the superseded 2026-10-06 rig and its historical review.

Colleague commit `bfe90a7` supplies `/Game/FromBlender6/SK_Boss_stady3`, its one-root-bone skeleton and Guardian material. The existing Zombie remains phase 1; phase 2 is not connected. The third variant is `/Game/Gameplay/Boss/Phase3/BP_BossPhase3` with its own `DA_BossPhase3`, `SK_BossPhase3`, 22-bone skeleton and eight sequences. The derived mesh preserves the original topology/UVs, gains separate limb/face weights, and is uniformly normalized to 240 cm tall. Original colleague mesh/skeleton packages are retained. Its source actor remains an editor reference in L_Mouth, hidden and collision-free in gameplay.

F3 → **Босс · фаза 3** provides dormant spawn, eight isolated animation buttons, explicit AI activation, reset/stop and removal. Phase 1 and phase 3 can coexist; variant-specific actions select only their own tagged actor. Both retain `MC_DevBoss` for normal run cleanup. No encounter transition or automatic phase-3 spawn is installed. Health-threshold `Runtime.Phase` remains an internal profile mechanic, distinct from the model's third-phase designation.

| Third-phase clip | Duration | Impact |
|---|---:|---:|
| Idle | 2.4 s, loop | — |
| Walk | 1.8 s, loop | — |
| PunchLeft | 1.6 s | 0.85 s |
| PunchRight | 1.7 s | 0.95 s |
| Kick | 2.0 s | 1.12 s |
| Hurt | 0.8 s | — |
| Death | 3.0 s | — |
| Roar | 5.0 s | — |

Authoring: `Tools/Blender/build_boss_phase3_animations.py` and editable `ArtSource/BossPhase3/BossPhase3Animations.blend`. Package import: `Tools/Unreal/import_boss_phase3.py`. Native timestamp-driven presentation and server-owned combat are reused; root motion is disabled. The derived mesh uses Guardian `MI_Boss`; navigation retains the existing Boss capsule dimensions.

The legacy FBX animation importer strips Blender's Armature ancestor scale from the static root track. The import script removes only that nonmoving track, so playback inherits the own skeleton's reference root scale; all child tracks remain. Core package redirects preserve existing reward references after the colleague's chest mesh relocation to `/Game/FromBlender`.

`Tools/CaptureBossPhase3.ps1` runs the opt-in `-MCRoguelikePreview -MCBossPhase3Review` in Unreal, exercises the same owning-controller F3 actions, verifies independent phase-1 assets/state, all eight skeleton-compatible moving poses, playback timestamps, full-body camera and separate activation/stop/removal. It records real offscreen game frames and encodes the animation reel to `Artifacts/Approval/BossPhase3.mp4`; validation is written alongside it. This is a solo presentation/integration check, not a multiplayer latency test.

2026-10-06: Development Editor build and the complete solo review passed. Eight clips moved their evaluated bones and retained zero measured playback timing error; phase-1 defaults/state and observer health stayed unchanged. The reel contains 520 rendered frames over about 25 seconds. Saved-default inspection also confirmed both relocated reward meshes resolve through the package redirects.
