# Coffee slime v2 design contract

Stage: root explicitly passed ROOT CONCEPT APPROVED, ROOT GRAYBOX APPROVED and corrected source-material review. Final DCC outputs/evidence are delivered; see README.md/work_state.md. The gate descriptions below record the original review requirements, not an outstanding approval request.

Binding image: `concept_v2.png`, generated from `concept_prompt.md` using built-in imagegen as a brand-new design. Broad low rounded coherent coffee volume, three moderate integrated lower lobes, two flat pale oval eyes with minimal slant, one small flush crema crescent. No cone/pile, separate feet, eye spheres, pupils/lids/brows, nose/mouth/teeth, cream cap/horn, floating droplets or copied Genshin decoration.

Root resolved the concept/provisional-size tension in favor of the image: source dimensions X depth1.58m / Y front width1.68m / Z height.90m; +X forward, +Z up, floor pivot. Do not stretch to v1's130cm height. Native collision remains radius72cm; later foam_tip anchor follows the new crown near90cm and face pivot follows the actual v2 eyes. Bone names remain root/gel_body/gel_top/face/foam_tip; no accessory should be invented to justify a bone name.

Graybox: one continuous rounded cap/drop body with smooth shoulders and subtle lower radial lobes. Eyes and crema are shallow surface-conforming color patches, not raised facial parts or a cream topping. Three simple materials: M_CoffeeSlimeV2_Body, M_CoffeeSlimeV2_Crema, M_CoffeeSlimeV2_Eyes. Warm medium caramel-coffee; modest specular/coat and moderate roughness. No v1 dark basecolor, micro-pores or noisy mottling. No emission for this stage.

Evidence: actual Blender3/4,front,side,back renders under the same neutral lighting and a labelled sheet. Compare broad crown/shoulders, lowest silhouette/lobes, eye flatness/spacing/slant, flush crescent area and warm color to the concept. Orthographic-like generated views are design hypotheses, not measured geometry; actual graybox views must establish consistency. Metrics are measurements, not visual approval.

Future only after gate: smooth game mesh under15k triangles if feasible; five actions Idle1.20s/Hop.90s/Hurt.28s/PreBurst.60s/Burst.18s at100Hz; fixed root, springy volume-preserving squash/stretch; source/GLB meters, FBX centimeter-coordinate derivation with object/reference/animated root scale1. Reuse no v1 human face or geometry. Validate actual engine units/hierarchy/poses/materials separately.

Root owns LeadReview.md and all Unreal/native/Git integration. This pipeline owns v2 source/evidence and author_coffee_slime_v2.py only; v1 remains untouched and rejected by the new art brief.
