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
