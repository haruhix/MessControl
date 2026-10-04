# Task rewards and perks

Individual score awards still use `AMCGameMode::AwardTask` / `AwardTaskToPlayerState`. Completed objectives use `NotifyObjectiveCompleted` with a stable ID: successful legacy events and day-plan steps enqueue one shared reward. Food rain, failure and the Complete sentinel do not create rewards. Duplicate completion IDs are rejected until the next run; individual food, ice and cleaning score awards do not enqueue chests. The F3 developer panel's **RewardChest** action queues a reward directly for authoring review.

## Safe drops

Only authored `AMCRewardDropZone` boxes can receive a falling reward. `bEnabled` controls validity; `bAllowRewardDrops` selects zones included in the director's startup cache. A separate zone for the original placed chest can therefore validate its pickups while excluding random drops. Set `AllowedLandingSurface` to the actual tongue actor; an unset reference accepts only a native `AMCTongue`.

The proposed map authoring creates four drop zones and sets `CandidatesPerZone=8`: at most 32 candidate positions per retry. Each candidate must keep the complete chest/pickup footprint inside the box with an 80 cm inward margin. Center, corner and edge ground traces must hit the same allowed visible surface, with an upward normal and bounded deviation from the center tangent plane. A smooth tongue slope is valid; the chest aligns with its support normal. A volume overlap rejects occupied landing space, and a box sweep rejects an obstructed fall path. The footprint encloses the three pickups and every chest yaw. Valid candidates are ranked by the number of living players nearby; seeded random ties distribute equally suitable locations. The chest faces inward, and its three pickup positions receive their own allowed-floor and clearance checks before opening.

Default limits are two active chests and an eight-second reward cooldown. A missing/invalid table, capped players, occupied zones or cooldown leave the reward queued. Placement retries once per second; touchdown clearance is checked on the quarter-second approach timer. A newly occupied touchdown retries briefly, then aborts and requeues the reward. The fall is a server-timestamped kinematic presentation with a landing marker, not a simulated rigid body. Actor iteration occurs during placement retries; actor ticks run only while the chest is falling or opening.

## Choosing a perk

A landed chest opens when a living current player pawn approaches with unobstructed visibility and enough eligible table rows. The server rolls Positive or Negative once from the eligible polarities, then rolls three distinct row IDs with weighted sampling without replacement. A usable row needs a finite positive weight, available stacks and a usable effect class. At least three eligible rows of the selected polarity are required; failure leaves the chest closed. Cached choices do not reroll on a failed placement/open attempt.

`EMCRewardSelectionPolicy::ChooseOne` is the default. The chest is shared: the first valid player to collect a choice receives that perk, and the other two disappear. `CollectAll` remains an editable alternative. Pickups display their replicated name and effect description in the world. Collection arms 0.8 seconds after the lid finishes opening. A player already overlapping an option must leave and enter again; simply revealing the options never awards a random first choice.

The authority validates a living current pawn, overlap, distance, visibility, matching perk table and unclaimed row before calling `ServerGrantPerk`. The claim guard prevents reentrant awards. Stage, server timestamps, IDs, polarity and claim state replicate; clients present the result and never decide loot or grant perks.

Dynamic chests disappear after collection. A map chest with `bPlacedReward=true` starts landed, uses its own `PerkTable` and `PlacedDropZone`, and remains exhausted after use. `ResetPlacedReward()` destroys its old options, clears the claim/roll state, restores its authored landing pose and reseeds from the new `RunSeed`. `RestartShift` initializes that seed before resetting the director, placed chest and player perks. An unopened dynamically destroyed reward is requeued; a partially collected `CollectAll` reward is not duplicated by replacement.

## Data and effect extension

`AMCPlayerState::Perks` owns replicated row IDs and stack counts, so pawn replacement does not erase a run's selections. `FMCPerkDefinition` supplies display name, description, polarity, weight, maximum stacks, icon and an optional `EffectClass`.

All 12 current rows are empty placeholders. They change no stats and grant no items or lives. An empty `EffectClass` uses the concrete no-op `UMCPerkEffect`. Future unique perks subclass it and implement server events `OnApplied`, `OnStacksChanged` and `OnRemoved`: these can grant an item, restore a life, bind gameplay events or manage their own actors. Clients receive the selected IDs and the UI change event. `ServerGrantPerks` validates an entire batch before applying any stacks; it is an authority API, not a client RPC. See [PerkEffects.md](PerkEffects.md) for the extension pattern.

## Authoring status

`Tools/Unreal/create_roguelike_foundation.py` is the intended editor authoring entry point. It seeds `DT_Perks` from `Tools/Unreal/roguelike_perks.json`, creates a reward Blueprint, adds the four permitted drop zones and director, and converts the two original decorative chest meshes into one functional placed chest. New tables/profiles are seeded only when absent so reruns preserve designer edits. The script also authors the dormant boss foundation described in [BossAI.md](BossAI.md); task rewards do not automatically activate enemies.

Saved paths are `/Game/Gameplay/Roguelike/DT_Perks`, `/Game/Gameplay/Roguelike/BP_RewardChest` and `/Game/Maps/L_Mouth`. The authoring pass compiled both Blueprints, imported the Zombie, converted the decorative chest and built/saved navigation. `reset_perk_placeholders.py` explicitly replaces the prototype table with the 12 empty rows when requested; normal foundation authoring preserves existing tables.

Native Editor modules compile. The focused solo editor-runtime demonstration passed objective completion/deduplication, safe fall, three distinct same-polarity choices, the exact selected row, single grant and removal of the other choices. It also passed boss damage, target selection, actual nav chase, telegraph/impact states and stopped behavior after death. Review captures are in `Artifacts/RoguelikeReview_20261004`. Multiplayer replication and Windows packaging were not exercised.
