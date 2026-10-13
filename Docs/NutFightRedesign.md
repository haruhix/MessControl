# Nut encounter redesign

The opening cataclysm lasts 20–25 seconds, with one overhead nut every 1.5–3 seconds and no pauses between series. Each landing has a reachable warning footprint. Ground nuts remain available until players break or handle them, or the Wizard summons them. Neither nuts nor their fragments grant XP.

After the final landing, two large falling nuts become the Knight and Wizard. Both must die to complete the encounter. Ground debris does not delay their arrival; surviving summons do not delay victory.

| Boss | Ability | Result |
| --- | --- | --- |
| Wizard | Fire nut | A player-sized burning nut with flame ribbons, embers, damage and knockback. |
| Wizard | Nut rain | Actual persistent nuts and individual landing warnings, using the same food and event ownership as the opening rain. |
| Wizard | Summon | Every available whole ground nut becomes a killable small summon. Its fuse starts immediately; it pursues the nearest living player and explodes when the fuse expires. Killing it first cancels the explosion. |
| Knight | Enamel crusher | Three contacts, ending in a ground strike, then a two-second recovery window. |
| Knight | Ground shaker | A targeted jump and area impact, then a one-second recovery window. |
| Knight | Rolling thunder | A 12–20 second roll with bounded steering, anticipated arena edges and continuous travel-based visual rotation. |

The Knight guards while idle. A guarded frontal hit damages the shield and deals 20% normal damage to the Knight, including the hit that breaks it. Rear hits, lowered guard and subsequent hits after breaking deal full damage. Existing boss animation assets are reused; no new motion assets are imported.

Shield damage creates uneven, tapered fissures with side branches, dark interiors and muted shell edges. Geometry follows the imported shield-hand bone and saved curved surface. A faint target rim remains on the intact shield. Short hit feedback, shell debris and the authored unshielded model communicate the final break. The fracture material is separate from the arcane spell material.

To check locally: open `L_Mouth`, enter Play, resolve the first perk, then choose **F3 → Ореховый катаклизм — начать**. Dodge the rain, attack the Knight's front/rear and recovery windows, break or preserve nuts before Summon, and kill a summon before its timer ends. **Ореховый катаклизм — остановить тест** removes actors created by that fixture. F3 pauses the ordinary event sequence.

Default local summon tuning is 40 HP and a five-second fuse. Default shield durability is 500. The short shield video explicitly uses a temporary 160-durability PIE fixture so four ordinary base Pickaxe hits show the full damage progression; that fixture is not saved into game data.

Validation evidence and short videos live in `Artifacts/NutFightRedesign_20261013`. Checks cover the focused native cases, the approved Editor build and short solo PIE mechanics/VFX. Full-battle balance, multiplayer, cook and packaging remain outside this validation scope.
