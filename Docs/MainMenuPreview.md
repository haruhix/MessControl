# Editing and previewing the main menu

Open **Tools → Main Menu Preview** (the **Wash n Smash** section) in Unreal Editor. The panel
uses the widget assigned to the shared theme's **MainMenu** role. At present
this is `/Game/UI/MenuShowcase/AnatomyV2/UI/WBP_WashNSmashMenu_AdaptiveV2`.
There is no separate copy of the menu layout to keep in sync.

1. Use **Open actual menu in Designer** to edit that WBP in its UMG Designer. Select a child in
   the hierarchy to change its anchors, position, size, spacing, images or fonts.
2. Choose the menu page or settings section and preview language in the preview
   controls. The standard Designer **Screen Size** control handles resolution
   and aspect ratio. **Preview**, **Play**, **Stop** and **Reset** here refer to
   the UI sample, not to starting the game.
3. For the composition with the real mouth scene, use **Open main-menu scene…** for
   `/Game/Maps/L_MainMenu` and enable its preview overlay in the panel. The
   overlay uses the latest authored widget tree and the actual menu camera.
   It follows the viewport's size and the project's UI DPI rule.
4. Edit the WBP while the overlay is visible. Changes and Undo refresh the
   transient preview; use **Refresh** to request a fresh copy explicitly.
   Save the WBP normally when the design is ready.
5. **Open shared theme** edits the existing parent palette. Colors listed in
   the widget's **Style Bindings** come from that theme and are reapplied by
   the presenter; adjust the binding or palette to change those colors.
   Font sizes on bound labels are also controlled by **Style Bindings** in
   Class Defaults. Localized copy is controlled by **Text Bindings** and
   `/Game/Gameplay/UI/Data/DT_UI_Localization`.
6. **Open scene animation** opens the active map's Level Sequence for scrubbing
   and animation editing. The preview does not start a gameplay world.

Page/language choices affect only the editor sample. The preview's game
buttons do not launch travel, connect to Steam, quit or apply player settings.
Use the panel selectors to inspect another page. Stopping the preview, closing
its panel, switching maps or beginning PIE removes its overlay and restores
the viewport state. The normal game menu keeps its existing actions.

The mouth scene and widget remain WIP. This tool previews the actual authored
assets; it does not imply final animation/art approval or packaged-game and
multiplayer validation.

Validated on 2026-10-22: Development Editor build; the real unselected UMG
Designer opens with its authored visibility; all menu pages, five settings
sections and ten culture samples; automatic refresh of an unsaved layout edit
and its Undo, including with Designer open. Those checks preserved saved
package bytes, actor transforms, global culture and clean package state.
Solo PIE exercised actual menu callbacks, Play to Hub and return to the menu,
including automatic removal of the editor overlay before PIE.
