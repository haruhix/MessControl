# VFX flow studies — 2026-10-29

Status: **visual work in progress; no artistic approval or production heal integration**.
The user rejected Art03 globally at 1/10, then corrected the direction toward
Genshin/WuWa-like flow and timing adapted to this game's tooth/mouth palette.
Spray work remains deferred. The latest creative brief is surface restoration
(`enamel comes alive`); its arena-tooth versus player target is being clarified.

All generated media, intermediate reports and authoring scripts are on E: under
`E:/CodexWork/MessControl/VFXLab_20261029/`. Editable carrier masters are in
`ArtSource/VFXLab/`; saved experiments are under `/Game/Development/VFXLab/`.
No C++ build, cook/package, multiplayer or microphone test was run for this lab.

## Actual attempts and limits

| Attempt | Result from actual recorded frames |
| --- | --- |
| HealStudy01 | Rejected internally: broad translucent curtain covers face. |
| HealStudy02 | Narrower carrier, but reads as belt/orbit; dark carrier/straight edges. |
| HealStudy03 | Failed shader authoring exercise; excluded as visual evidence. |
| HealStudy04 | Rising side flow, face clear, dark carrier fixed; blunt head and overly long motes. Root 4/10. |
| HealStudy05 | Rounded head, tag-based lead/follower timing, short motes. Still resembles detached comma/leaf. Root 5/10. |
| MouthFlowStudy01 | Failed Python setup before recording: unexposed character getter; no visual claim. |
| MouthFlowStudy02 | Actual L_Mouth with native gameplay camera and a separate review camera. Palette readable; the isolated stroke does not strongly convey healing. Root 5/10. |
| MouthFlowStudy03 | Softer cross-section/radiance material. No obvious visual improvement at inspected native peaks. Normal-game food occludes later footage; only the first clear native-camera interval is useful. Root 5/10. |
| ExistingReferences01 | Failed preview API setup; excluded. |
| ExistingReferences02 | Four existing project systems previewed without editing sources. FreeMagicBuff has useful textured movement but its full fire stack obscures the tooth. SoftTofuHeart supplies a readable small positive accent but crowds the face at stock settings. PlayerBuffLooping/PickupSuccess are setup-inconclusive, not ranked for art. |

The mouth trials spawn **temporary visual carriers**, not the native healing perk.
They do not alter HP, inventory or cleaning state. L_Mouth's saved SHA256 stayed
`40be7a3a1cdee1cb0bb360a04911c081236441058da91d5ea8878949b5962fc0`.
Movement attachment, simultaneous players, gameplay-trigger integration, GPU
cost and packaged first launch remain unverified.

## Review method

Use a normal-speed GIF/video, actual native-resolution peak and onset/peak/tail
frames. Label any crop as magnification. GIFs select existing source frames at
15/20 fps; no interpolation or speed change. MP4 originals remain available.

Judge separately:

- Meaning: can a player identify healing/restoration from the effect?
- Gameplay readability: native camera scale, contrast, face/tool/danger overlap.
- Motion: coherent onset, one main gesture, subordinate followers, clean exit.
- Game fit: ivory enamel, coral mouth, mint equipment, soft shape and controlled highlights.
- Clutter: visibility when several events overlap; particle/transparency cost is a separate measured check.

The number is the root agent's subjective art assessment with observable reasons,
not a user rating or approval. Successful compilation/recording cannot raise it
by itself. The current root score remains 5/10; the surface-restoration concept
is not implemented or claimed as delivered.

## Assets and readiness

The rising strip has 260 vertices/384 triangles and UV0 along/across the flow.
Two carriers plus six motes form the current prototype. The motes were shortened
from 3–4 seconds to 0.42–0.68 seconds. New materials compile without reported
errors. The lab map has saved references; an additional per-map preload entry
names the 05/06 variants. Preview MIDs are created and warmed before capture.
Production L_Mouth's manifest is not changed because these effects are not yet
integrated there. A warm Editor capture does not prove cold packaged PSO readiness.

The original `NS_Free_Magic_Buff`, `NS_Player_Buff_Looping`, `NS_Pickup_Success`
and `NS_Sparkling_Heart` were referenced, not edited. The live Blender file
`C:/Users/user/Downloads/slide.blend` and its unsaved state were left alone;
carrier masters were created by a separate headless Blender process on E:.

## Tool failures retained honestly

An unsupported PNG texture exporter asserted during earlier analysis; it was not
retried. A later attempt to duplicate/load the mouth world asserted on editor
world cleanup. The failed map copy was not saved, and the original map's hash
was checked after subsequent successful transient previews. These are editor
authoring failures, not reproduced gameplay bugs. Logs/rejected attempts remain
in the task folder.

The material-setter boolean was unreliable in this local UE build, so instance
values were checked by readback. Connected material outputs were traversed to
avoid editing orphan graph nodes. These repairs and reference-viewing limits are
recorded in `Docs/VFXReferences.md` and the task's `ResearchHeal/` notes.

The local review board now supports GIF images while keeping MP4, versions and
human feedback separate. Human feedback storage is never replaced by a seed
update or by an agent's higher score.
