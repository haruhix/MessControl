# Focused animation refinement

Intermediate critical-frame evidence: `intermediate_evidence/preburst_lateral_only.png`.
The first PreBurst action increased X/Y by 1.83 but used an absolute Z scale of 1.10, so the pose read as widening rather than balloon inflation. No intermediate exports were approved for Unreal import.

The existing character/animation workflow covers pose and transition validation; no new skill or public skill change is necessary. Repair: Z grows with the same 1.83 balloon factor plus 1.10 vertical stretch. Burst begins at exactly that terminal scale before collapse. Preview camera framing expands to contain that larger pose. Timings and fixed root remain unchanged.

Separate export correction: UE5.8 importer strips Blender's wrapper node only when it is named Armature. Temporarily use Armature for FBX exports, retain SK_CoffeeSlime_Rig in source and GLB. Actual engine skeleton acceptance remains a later root validation.

No Unreal package, source, Editor, build or Git actions by this asset author.

## Engine import unit correction

Root's actual UE import rejected a mismatch that Blender's own fresh import could not expose: the old FBX meter metadata gave the mesh reference root scale100 while imported clip root scale was1, reducing the intended34cm hop to .34cm. This is recorded as a failed engine contract, not a weakened validation gate.

Repair uses the existing export workflow's unit-normalization method: a disposable loaded scene scales mesh coordinates, armature rest coordinates and pose-location keys by100; sets scene unit length .01 and leaves object/root scales1. The `.blend` and GLB remain meters. Root's actual UE gate passed the candidate with reference root scale1, face +X58/Z62cm and Hop33.998cm; 17 owned packages saved. The exact tested FBX bytes and CentimeterExportAudit.json are promoted to canonical `exports`, with equal SHA256 recorded in CanonicalPromotion.json. No component-scale compensation is used.
