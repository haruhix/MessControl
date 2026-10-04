# Roguelike and boss foundation review

Open `/Game/Maps/L_Mouth` in Unreal and play. F3 exposes **Roguelike — сундук за задачу** and **Босс — активировать Zombie** for a quick review.

- `Chest.png`: a reward landed inside an authored tongue zone.
- `Choices.png`: three empty placeholder choices of one polarity. Enter one orb to choose it; the others disappear. They grant no stats, items or lives yet.
- `BossTelegraph.png`: the imported Zombie during the AI attack sequence. Animation and attack VFX are future work.
- `ZombieSourcePreview.png`: the derived artist model, with its independent skeleton.

Editor-runtime verification passed: one deduplicated objective reward, safe fall, three distinct choices, the exact selected row granted once, siblings disabled, boss damage/target/navigation chase/attack/death. This was a solo run; multiplayer and Windows packaging were not run.
