# Task rewards and perks

Individual score awards still use `AMCGameMode::AwardTask` / `AwardTaskToPlayerState`. Completed objectives use `NotifyObjectiveCompleted` with a stable ID: successful legacy events and day-plan steps enqueue one shared reward. Food rain, failure and the Complete sentinel do not create rewards. Duplicate completion IDs are rejected until the next run; individual food, ice and cleaning score awards do not enqueue chests. The F3 developer panel's **RewardChest** action queues a reward directly for authoring review.

## Safe drops

Only authored `AMCRewardDropZone` boxes can receive a falling reward. `bEnabled` controls validity; `bAllowRewardDrops` selects zones included in the director's startup cache. A separate zone validates the original placed chest while excluding random drops. Set `AllowedLandingSurface` to the actual tongue actor; an unset reference accepts only a native `AMCTongue`.

Map authoring creates four drop zones and sets `CandidatesPerZone=8`: at most 32 candidate positions per retry. Each candidate must keep the complete chest and lid-sweep envelope inside the box with an 80 cm inward margin. Center, corner and edge ground traces must hit the same allowed visible surface, with an upward normal and bounded deviation from the center tangent plane. A smooth tongue slope is valid; the chest aligns with its support normal. A volume overlap rejects occupied landing space, and a box sweep rejects an obstructed fall path. Valid candidates are ranked by the number of living players nearby; seeded random ties distribute equally suitable locations.

Default limits are two active chests and an eight-second reward cooldown. A missing/invalid table, capped players, occupied zones or cooldown leave the reward queued. Placement retries once per second; touchdown clearance is checked on the quarter-second approach timer. A newly occupied touchdown retries briefly, then aborts and requeues the reward. The fall is a server-timestamped kinematic presentation with a landing marker, not a simulated rigid body. Actor iteration occurs during placement retries; actor ticks run only while the chest is falling or opening.

## Choosing a perk

A living player presses **E** near a landed chest with unobstructed visibility. The server reserves the chest for that player, stops held gameplay input and starts five seconds of lockpicking. Death, loss of the pawn/controller or leaving the interaction radius releases the chest. The server rolls Positive or Negative from the eligible polarities, then rolls three distinct row IDs with weighted sampling without replacement. A usable row needs a finite positive weight, available stacks and a usable effect class. At least three eligible rows of the selected polarity are required; failure leaves the chest closed.

After five seconds the lid opens around its actual rear hinge (local −X, rotation around Y). Three large HUD cards appear for the opener, with a consistent polarity, table-authored names and optional icons/descriptions. Click a card or press **1 / 2 / 3**. Exactly one selection is granted; the other two disappear. The current rows remain empty placeholders. The modal HUD restores gameplay input after selection or cancellation. Deprecated world-pickup references and the old selection-policy property remain for saved-package compatibility; neither spawns pickups nor enables collecting all three.

The authority validates the reserved living pawn, distance, matching perk table, open stage, selected row and unclaimed reward before calling `ServerGrantPerk`. The claim guard prevents reentrant awards. Stage, server timestamps, opener, IDs, polarity and claim state replicate; clients present the result and never decide loot or grant perks.

Dynamic chests disappear after selection. A map chest with `bPlacedReward=true` starts landed, uses its own `PerkTable` and `PlacedDropZone`, and remains exhausted after use. `ResetPlacedReward()` closes its HUD, releases the player, clears the claim/roll state, restores its authored landing pose and reseeds from the new `RunSeed`. `RestartShift` initializes that seed before resetting the director, placed chest and player perks. An unclaimed dynamically destroyed reward is requeued.

The editable five-second hero/brush animation is staged in `ArtSource/ChestLockpick/ChestLockpick_Approval.blend`. It uses the real chest lock and the end of the brush handle. It awaits the requested visual approval before being imported or played in the game. The runtime currently has the interaction, opening progress, lid and cards.

## Data and effect extension

`AMCPlayerState::Perks` owns replicated row IDs and stack counts, so pawn replacement does not erase a run's selections. `FMCPerkDefinition` supplies display name, description, polarity, weight, maximum stacks, icon and an optional `EffectClass`.

All 12 current rows are empty placeholders. They change no stats and grant no items or lives. An empty `EffectClass` uses the concrete no-op `UMCPerkEffect`. Future unique perks subclass it and implement server events `OnApplied`, `OnStacksChanged` and `OnRemoved`: these can grant an item, restore a life, bind gameplay events or manage their own actors. Clients receive the selected IDs and the UI change event. `ServerGrantPerks` validates an entire batch before applying any stacks; it is an authority API, not a client RPC. See [PerkEffects.md](PerkEffects.md) for the extension pattern.

## Authoring status

`Tools/Unreal/create_roguelike_foundation.py` seeds `DT_Perks` from `Tools/Unreal/roguelike_perks.json`, creates a reward Blueprint, adds the four permitted drop zones and director, and converts the two original decorative chest meshes into one functional placed chest. New tables/profiles are seeded only when absent so reruns preserve designer edits. The script creates the boss assets and navigation described in [BossAI.md](BossAI.md), but removes the former map boss. The normal game has no boss spawn; the explicit F3 tools create it for review.

Saved paths are `/Game/Gameplay/Roguelike/DT_Perks`, `/Game/Gameplay/Roguelike/BP_RewardChest` and `/Game/Maps/L_Mouth`. The authoring pass compiled both Blueprints, imported the Zombie, converted the decorative chest and built/saved navigation. `reset_perk_placeholders.py` explicitly replaces the prototype table with the 12 empty rows when requested; normal foundation authoring preserves existing tables.

Editor module compilation and the focused solo runtime pass succeeded. The pass checks absence of an ordinary-map boss, objective deduplication, safe chest fall, E/five-second opening HUD, three distinct cards of one polarity, the exact owning-controller choice, no duplicate grant/world pickups, and explicit F3 spawn/AI with navigation, attack and death. Current captures and limitations are in [the chest/boss review](../Artifacts/ChestBossReview_20261004/README.md). The earlier `Artifacts/RoguelikeReview_20261004` captures show the preceding world-pickup prototype. Multiplayer replication and Windows packaging require separate checks.
