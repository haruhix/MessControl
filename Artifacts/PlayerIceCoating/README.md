Player ice coating
==================

The existing winter event's replicated `Players.Amount` drives `IceAmount` on a local skeletal shell. The shell shares the player's final skeletal pose, including physics, and has no collision or animation tick. At zero freeze, player removal/death, or event cleanup, its component is destroyed.

`/Game/Gameplay/Cold/M_PlayerIceCoating` is derived from the artist's `/Game/Art/Materials/ice/M_Ice`. Its original Substrate surface graph is retained; the additions control growing patches and a small shell expansion. `IceThickness` defaults to 1.2 world centimetres. The player's base material slots remain assigned to their existing materials.

The native `AMCIceEvent.PlayerIceMaterial` default references the saved coating material. The server remains responsible for freezing/thawing and the existing gameplay outcomes; each rendering peer shows the replicated meter locally.

MaterialBuild.json records shader authoring checks. RuntimeVerification.json records the targeted two-peer editor preview when completed. This change does not include a packaged build or a performance benchmark.
