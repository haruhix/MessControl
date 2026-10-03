# Gameplay controls and configuration

The gameplay additions use the existing `BP_PlayerCharacter`, `BP_MouthGameMode`
and `L_Mouth`. Their native parents provide the new behavior without rebuilding
the Blueprint widget layout.

| Input | Behavior |
| --- | --- |
| Shift | Immediate dash on a fresh press; hold to sprint. Both spend stamina. |
| E near small food fragments | Toggle stack collection while keeping the selected tool. |
| Hold LMB | Continue tool swings or cleaning at the tool's normal cooldown. |
| Hold Tab | Names, points, ping, player colors and the host crown. |
| T | Radial categories for face emotes, body gestures and attention/help alarms. |
| T menu color buttons | Choose a player color shared by the body and UI icons. |
| Left/right or LMB/RMB while dead | Switch between living players until respawn. |

## Designer settings

`MCToothMovementComponent` exposes maximum stamina, sprint drain, recovery,
recovery delay and dash cost. Defaults are 100 maximum, 12/sec sprint drain,
22/sec recovery after a .65 second pause, and 22 per dash. Exhaustion clears at
15% of maximum to avoid rapid sprint toggling near zero. The movement response
acknowledges stamina together with the client's movement timestamp.

`MCGameplayHUD.StaminaWidgetClass` accepts a Blueprint subclass of
`MCStaminaWidget`. Its current value, maximum, normalized value and exhausted
state are available through the `OnStaminaChanged` event; the native bar is a
usable fallback.

`MCGameMode.ScoreRewards` configures separate completion rewards for coffee,
repair, delivered food, ulcers and ice. Defaults are 10, 20, 5, 25 and 10 points.
The server awards points on actual completion. `MCPlayerState` preserves them
through pawn death; restarting the shift resets them. `RespawnDelay` defaults to
five seconds. Alarms last four seconds and have a two second cooldown.

## Arena and cleaning

Camera sweeps cover the complete distance from the pawn to the desired eye.
Spectating uses the followed living player's camera and restores the new pawn's
camera after respawn.

Coffee/cola extents and heights follow the placed tongue bounds. Dirt and ice
choose bounded interior positions, with a margin from the arena rim. Vomit
checks the predicted first impact and recent landing positions before choosing
a trajectory, so separate distant targets do not all strike the same near edge.

Player and decorative tooth cleaning share the brush contact pose. Server-side
work waits for a short approach and validates reach and facing independently of
the rendered wrist pose. Fractional mask accumulation keeps cleaning progress
consistent across frame rates. Holding the brush keeps the acquired crown while
the player turns toward its next reachable stain. Coating generation samples the
resting tongue and permanent gum floor once per patch, leaving clearance for the
brush head instead of placing dirt behind the enlarged arena's gum collision.
The body's heading follows the stain center independently of the small brush
stroke; reach and work checks still use the actual bristle contact. Tongue
cleaning interpolates an unstroked center before adding the current stroke, so
the previous stroke does not accumulate into movement at higher frame rates.
An empty coating result is cached after generation instead of being rebuilt
every frame.
Ice keeps hard references to its required mesh and material assets for cooking;
its render proxy does not wait for asynchronous PSO precaching.

The tooth emergence animation is deferred as requested.

## Test maintenance

The duplicate `MessControl.Animation.Teeth3Layers` registration was removed.
Its unique work clips are checked by `MessControl.Physics.ArtistRigIntegration`,
and its face emote IDs by `MessControl.Animation.EmotesReactionsAndSpeech`.
Both consolidated tests passed. Existing gameplay and network checks cover the
new behavior; no additional Automation registrations were introduced for it.
