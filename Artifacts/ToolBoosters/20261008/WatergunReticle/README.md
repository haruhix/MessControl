# Watergun aiming reticle

Watergun now has a compact outlined cyan aiming marker with a centre dot and four short ticks. It follows the projected impact point in the owning player's viewport, with DPI-aware widget coordinates and per-frame camera updates. Care mode previews the water direction and active treatment target. Pressure mode uses the same object sweep, charge-dependent reach, and sweep radius as the actual charged shot. A shared trace helper keeps shot and preview collision behavior together.

While carried sideways, the marker previews the authored working muzzle instead of tracing from the lowered carry muzzle. During use and recoil it follows the actual posed muzzle. Pressure mode adds a small outer ring for charge/cooldown and dims the marker during cooldown. The marker is hidden for other tools, menus, spectators, unavailable hand/tool states, and projected points outside the visible viewport. Existing movement, shot direction, damage, charge timing, and cooldown mechanics are retained.

Validation:

- UE 5.8.1 native editor build succeeded, including the final carry-muzzle correction.
- Existing `MessControl.ToolBoosters.Mechanics` automation passed on the final build (`TestsFinal/index.json`), covering care reach/treatment, charged damage, charge cancellation, and cooldown.
- Live PIE checks passed (`Runtime.json`): care and pressure previews hit the controlled target; full charge reached 100%; rotating the camera preserved the world impact point and moved its screen projection; a shot began the existing four-second cooldown; tool switches and F3 menu state were checked.
- Full Slate screenshots include the native HUD marker. Care, ready pressure, full charge, orbit, cooldown, other-tool hiding, and menu hiding/restoration were visually inspected. The captured marker centre matches the projected impact point in the viewport.
- Relevant saved snapshots (`WBP_GameplayHUD`, `DA_Equipment`) refreshed successfully with no export errors and a current C++ build. This was a scoped refresh; other assets were not broadly re-exported.
- `git diff --check` passed. Existing user changes in editor configuration, food tables/director profile, and generated food collision packages were preserved.

Editor remains open; PIE is stopped and the original foreground performance settings are restored. No content packages or maps are dirty.
