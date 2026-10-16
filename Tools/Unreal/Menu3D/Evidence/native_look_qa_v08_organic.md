# Independent native look QA — Organic_r05

Scope: read-only review of saved source previews, native Unreal PNGs, runtime receipts and the isolated-lighting helper. No editor changes. This is an art HOLD, not a 1:1 approval.

## Evidence reviewed

- Approved `References/B_FullWidth_Approved.png`, especially the right opened Vampire panel.
- `PBRCalibration_v08b_05_CrossRow`: actual A and D captures.
- `PBRCalibration_v08b_06_Interior`: actual A and D captures and runtime MID readbacks.
- `IsolatedLighting_v08_01`: all three actual captures and complete receipt.
- `Interior/Organic_r05`: front/oblique interior-only and front composite source previews.
- `OrganicDetail_v08_01/03_detail_oblique_inspection.png`: actual temporary native inspection view with dynamic light shadows disabled; `02_detail_front_inspection.png` is black and invalid for art review.

## Findings

1. **Cross-row shadows are a real failure.** The global baseline casts large angular tooth silhouettes over the DAY03 cavity. Exclusive channels at unchanged light transforms reduce these in isolated candidate B. This supports fixing light ownership rather than painting around the artifacts in the albedo.
2. **Centered key-only candidate C is not accepted.** It adds a broad dark horizontal arc behind the upper crowns/fangs and suppresses cavity form. A darker result alone does not establish reference fidelity. The local renderer still gives hard own-jaw shadows.
3. **Interior form needs an art pass.** The broad side walls read mostly as smooth colored bands in the catalog. Tongue papillae are present at small scale, but the tongue silhouette reads as two bright hills with a deep central depression. That silhouette is already pronounced in the source front preview, so lighting alone will not resolve it. Compare a macro native frame before changing frozen geometry.
4. **Outer material hierarchy is still flatter than the reference.** The reference has stronger enamel highlight/body separation and a richer coral gum surface. The current Human and Predator crowns are uniformly pale at catalog size. Do not call the three families final merely because all PBR maps are bound.
5. **The Vampire lower row is readable in these settled open frames.** Separate paired central crowns and a central seam are visible; it is not the previous missing-lower-row defect. This does not replace normal-speed opening/closing review.
6. **Macro inspection confirms the tongue form failure.** Native `03_detail_oblique_inspection.png` shows actual papillae and wet material highlights, but the twin hills dominate the contour. This is a geometry repair target, not a request to amplify the normal map. Broad cheek-wall folds are visible as smooth vertical highlights; the rear/palate remains nearly featureless in this particular light/camera view. The black front capture cannot be used to claim missing geometry.

## Receipt limit

`IsolatedLighting_v08_01` captured three valid PNGs after at least one second settling. Its final status is `failed` solely because five protected pointer properties could not be restored with `set_editor_property`; all twelve runtime lights were destroyed, no runtime light actors remain, and original light/channel restoration is recorded. All baseline contact-shadow lengths are zero, so contact-shadow cross-talk does not explain this particular A/B difference.

## Remaining gates

- Macro native interior frame with explicit shadow/camera/UI overrides recorded, then review fold readability and tongue silhouette.
- Settled catalog frame after the chosen lighting repair.
- Normal-speed actual opening/closing frames/contact sheet, including partial poses; no flicker, orange triangular patches, lower-row disappearance or mask pop.
- No 1:1 claim until camera/layout and visible surface details pass comparison in the actual menu framing.

## Actual native VSM comparison follow-up

Read-only review: `NativeVSM_v08_01/A_SM6_Conventional.png`, `B_SM6_VSM.png`, `C_SM6_VSM_WarmBody.png`, their receipt and `review_native_vsm_and_pigment_v08.py`.

- Same-session SM6 B removes the large angular rear-cavity silhouettes seen in A while preserving actual shadow depth at gingival collars and crown seams. This is a useful lighting improvement, not a final look approval. No settled-frame page corruption/checkerboard or missing-shadow region is apparent in these captures.
- Lower Vampire central paired crowns and seam remain readable. Current r05 tongue hills and smooth dark wall bands remain art HOLD; VSM does not repair their geometry.
- C changes ordinary crown pigment subtly while retaining the fang appearance. Small fixed native body ROIs have medians B→C: upper `[227,196,185]`→`[226,189.5,177]`, lower `[220,184,171]`→`[219,177,162]`. Fang ROIs have mean absolute RGB changes only 0.31–0.38, similar to unchanged Human control 0.27. These are local ROI observations, not whole-material matching scores.
- Receipt records no error, original material restored and VSM restored to 0. Static frames do not verify disappearance of the former normal-speed orange gingival patches.

### Short motion validation to prepare, not yet run

Use a fresh output directory and the actual SM6 menu camera/material/mesh identities. After shader compilation settles, enable VSM only for the bounded review and record its actual value with samples. At game dilation 1.0 capture closed→DAY03 open→Back closed→open again, then DAY04/DAY03 transition. Review actual partial-pose frames for orange gum patches, hard shadow popping, lower-row loss and card-mask edges. Keep a slowed diagnostic separate from normal-speed evidence. Record real capture intervals: duplicated frames in a 30 fps export do not establish 30 fps scene observation.

The current `record_vampire_lower_review.py` only whitelists AA and VSM cache overrides. It also changes overrides before widget/preflight assertions, so a preflight failure can bypass restoration. Before adding VSM Enable, validate all preconditions before mutation and ensure an outer cleanup guard restores CVar/UI/dilation state on setup failure as well as callback completion. The current 13-action state validator is specific to its default schedule and must not be applied unchanged to a shorter schedule.

### Future menu-scoped policy, pending SM6 shipping decision

One menu-stage owner should acquire the VSM override after capability checks and release it on stage EndPlay/travel/error; nested widgets must not independently toggle it. VSM is process-global, not world-local. A native tagged `ECVF_SetByTemp` override plus matching `Unset` is preferable to shipping console commands, which leave a high-priority console setting behind even after writing the old numeric value. Honor stronger console overrides and verify readback. This is a source/API recommendation only: no native implementation, build or shipping renderer change was made. SM6 target/cook availability and the non-Nanite deforming-mesh shadow cost remain separate decisions.
