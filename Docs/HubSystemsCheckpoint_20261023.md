# Hub systems checkpoint — 23 October 2026

Saved at the user's request before continuing the remaining validation. This is a work-in-progress checkpoint, not a release-ready Windows build.

## Changes

- Fishing is activated with E at the pier. Entering the zone no longer equips the rod; leaving cleans up casting/retrieval without losing an already accepted catch.
- Food uses the approved A-style filling spoil timer, horizontal HP bar and spoiled icon. Ordinary enemies use world HP indicators; existing boss bars remain separate.
- Character cosmetics have Face and Headwear slots, fitted glasses/cap assets, a DataTable catalog and reproducible Blender/Unreal authoring tools. The cap preserves the original mesh proportions, placement and backward brim; the erroneous X1.9 stretch and extra rotation were removed. The body and rig were not reshaped.
- Wardrobe fitting uses an independent, non-replicated display actor. Only the opening client's view hides real heroes, tools and nameplates; other clients retain their normal view. Camera/visibility are restored on close. Items can be tried at zero coins; buying and applying owned items are separate actions. Bag/belt colour uses the palette.
- Wardrobe and shop have shared-theme editable product cards, real item thumbnails, prices, rarity and selection/purchase states.
- The grill exchanges one fish for one persistent roll receipt. Physical reels and the enlarged coloured CRT-style hologram show the same rolling symbols/result. Personal shop purchases queue boosters for the next arena run.
- Calcium remains implemented for later use but is disabled by the economy rule. Existing saved charges/queued entries are retained, and suspended calcium does not block other boosters.
- Map selection stays inside the hub. Selection locks and restores movement; the saved selection stage is included.
- Preview actors, widgets and native-only visual resources are prepared before readiness. The known late `MM_Standart_SSS` shader work is requested before releasing the Editor loading screen. AGENTS.md now requires new runtime visuals to have preload and cook coverage.
- Basketball no longer hard-loads stateless Niagara during native CDO construction, which crashed the packaged EXE before its first window. It resolves the default during BeginPlay before Hub readiness. A narrow AlwaysCook rule includes all four confetti assets.
- Hub profile saving now checks the boolean result of `IFileManager::Move` correctly. Previously a successful rename was treated as failure and retried every two seconds; a failed rename could incorrectly update the in-memory cache.

## Validation completed

- Current Editor build: **Build10 PASS, 61.36 s**, including the one-line profile-save correction. Original local build configuration remains byte-identical.
- Previous hub integration: fishing real-E/cast/exit 13 checks; shop → Ready → Day 1 → arena booster handoff 8 checks; grill physical/hologram glyph calibration and native motion reviewed. These are bounded solo checks, not a complete campaign.
- Current wardrobe **Solo06: 78 checks PASS**, including zero-coin fitting, rejected purchases, real purchases, owned Apply, palette and restoration. **VideoSolo03: 47 checks PASS**; current native cap/wardrobe video was decoded and visually reviewed.
- **WardrobeTwoClients03: 8 groups PASS** using two actual network PIE worlds: private preview visibility, simultaneous fitting and independent close. No four-player, Steam/WAN or network purchase claim.
- Calcium suspension/preservation: **2 native tests PASS**. Basketball phased-load/real throw/confetti/cleanup: **14 solo checks PASS**.
- Fresh-GameInstance Editor startup ident: 15 native frames, changing reveal and approximately three seconds of real motion. Direct Hub PIE intentionally skips the MainMenu intro; returning to MainMenu in the same GameInstance does not replay it. Packaged intro motion was not captured before Firewall interrupted observation.
- Natural first-food Editor run: the known SSS material's 56 shader jobs completed before Ready; no late marker was observed in that bounded run.
- **Windows Development PackageStage04: build/cook/stage PASS**, 136.42 s; 0 errors and 29 known warnings. All four confetti packages are present, with no other cooked destination-set changes from Stage02. No archive/ZIP or Steam480 package was produced.
- Actual Stage04 EXE with a new isolated E: user directory reached MainMenu and Play → Hub. Hub Ready: 1.61 s, 695 assets, 748 resources, streaming 323/323. The user then manually reached the arena; root observed and captured real first-food gameplay. Mouth Ready: 2.15 s, 777 assets, 819 resources, streaming 348/348. No missing cached shader maps or late Editor shader markers were found in that log snapshot.

Local evidence is preserved under `E:/CodexWork/HubSystems_20261023` and `E:/CodexWork/HubWardrobePolish_20261023`. Current cap video: `NativeVideo03/Wardrobe.mp4`; earlier stretched-cap videos are historical and superseded. Capture timing is disclosed in the evidence; sampled/game-clock clips are not performance benchmarks.

## Continue from here

1. Package the **Build10** source and perform actual hub profile save/reload in a new GameInstance with the same isolated user directory and a visibly changed bag colour. Stage04 predates the boolean-save fix; do not distribute it as the final corrected build.
2. Finish actual packaged first-use wardrobe/shop and basketball checks. Editor and cooked coverage checks do not replace those interactions.
3. Improve remaining packaged PSO coverage. During the 199.298 s observed after Mouth Ready there were **15 new graphics PSO misses**, including two immediately after the first broccoli appeared; there were no new compute PSOs. These are pipeline preparation misses, not Editor shader compilation. The log does not identify each material/vertex factory or establish each miss's stall duration. A new user directory does not establish a cold GPU-driver cache.
4. Keep the separate idle Editor D3D12/SM5 GPU PageFault unresolved. Later successful checks did not reproduce it, but do not establish its cause or a fix.
5. Continue visual approval/polish as needed. Full death/travel/respawn cleanup, four players, Steam/WAN, a complete campaign and a final release/package certification are not claimed by these checks.

No additional build, playthrough or UI operation is started by saving this checkpoint.
