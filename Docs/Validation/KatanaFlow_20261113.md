# Katana input, charged cuts and cleave — solo validation

The katana starts an ordinary cut on press, buffers two further presses and chooses among four ordinary clips without consecutive repeats. A continued hold accepts one charged series after 0.38 seconds; releasing does not add another attack. The charged clip plays its complete authored preparation at 1.4×, preserving the guard-to-attack transition. Gameplay still requires real blade intersections.

Validation used Editor Live Coding and owned standalone PIE on `L_Mouth`. Fixtures and unrelated-food visibility changes were confined to transient PIE. No saved map/profile changes, multiplayer, cook or packaged build were performed.

| Check | Actual evidence |
|---|---|
| Input, queue and variants | Immediate press accepted; three short presses executed three cuts after input stopped. Twelve ordinary samples covered IDs 5/6/7/9 without adjacent repeats. Movement and brush interruption checks passed. |
| Hold and queued charge | Long hold produced exactly one ordinary cut followed by one automatic series. Buffered ordinary/charged commands retained FIFO order. |
| Contact and strength | Ordinary enemy hit after **0.1061 s** at the grounded 225 cm fixture; HP loss **80**. A separate 150 cm body intersected all three charged arcs and lost **92/92/92 HP**. Three Niagara ribbon segments were observed. |
| Food finishing | Grounded 100 cm tomato, with a deliberately strict 40 cm intake budget: ordinary cut retained physical nonready portions; charge finished all contacted, contained portions. Delivered mass **9.000001**, original **9**, within logged precision. |
| Footprint | A contacted 320 cm ingredient retained oversized physical remainder; food/enemy fixtures at 390 cm remained untouched. Saved-scale grape mass and distant island identity were preserved. |
| Soft correction | Body correction **6.66°** toward off-axis food. A real **30°** camera orbit cancelled correction, leaving body yaw **0°**. |
| Collateral | Two separate 30 cm enemy bodies at 110/180 cm were both hit by charged strokes 0 and 1. Two separate 50 cm tomatoes were both cut by charged stroke 0. Accepted contacts remained unique per actor/stroke. |

Core acceptance: `E:/CodexWork/MessControl/Validation/KatanaFlow/20261113T065354003773Z/Acceptance.json`. This explicitly references the unchanged raw report, whose only remaining failed assertion incorrectly required a charged grape hit after every random ordinary prestrike. In that run the ordinary overhead cut separated the nearby grapes; the charged sweeps found no further triangle intersection. The assertion is excluded with its reason, rather than forcing damage outside the blade. Actual charged grape contacts under the same native patch are retained in run `20261113T064852679416Z`.

Earlier failed diagnostic reports remain unchanged. They exposed phase-relative input scheduling that shortened intended holds, floating fixtures outside particular arcs, an over-conservative rotated AABB containment gate, and the skipped charged preparation. Input intervals now use actual prior input times; grounded fixtures and actual prepared vertices resolve those issues. The ordinary pool/movement/interruption evidence is retained in diagnostic run `20261113T062932447527Z`. The measured common charged arc is documented in `Arc_20261113T064549038009Z/ArcIntersection.json`.

Collateral acceptance: `E:/CodexWork/MessControl/Validation/KatanaFlow/Cleave_20261113T065834614933Z/Report.json`. Recipes: `Tools/Unreal/check_katana_flow_solo.py` and `Tools/Unreal/check_katana_cleave_solo.py`.

Composer `Tool.Katana` discovery, real `Target` input/automatic charge, cancellation during an accepted charged series, and clean reset passed. Switching actions and reopening Katana preserved canonical owner/profile digests, systems, stage modes and settings; no profile was modified. `ComposerAuditFinal.json` retains the evidence. The MCP source-opening operation completed, but this Editor Python API cannot independently enumerate active asset editors. This Live Coding session also retains a cached three-cut caption; current source describes four.

Composer observed prepared Katana resources with zero pending receipts. The initial cold food fixture reported late Editor compilation of `M_FoodCut`; this is not evidence of packaged PSO preparation. No packaged first-launch claim is made.

The primary agent inspected actual gameplay/front/side frames at 12 fps from the retained 30 fps recordings, plus collateral frames. The character/blade were clear, no obvious detached hands appeared, the complete charged preparation removed the guard jump, and actual finishing/HP contacts were visible. Artistic approval remains with the user.

Review media, kept outside Git:

- `E:/CodexWork/MessControl/Validation/KatanaFlow/20261113T065354003773Z/katana-flow-review.mp4`
- `E:/CodexWork/MessControl/Validation/KatanaFlow/Cleave_20261113T065834614933Z/katana-cleave.mp4`

Final editor inspection: no PIE, no dirty maps/content; Composer stopped with zero running actions or active Niagara components. The editor remains on `L_Mouth`.
