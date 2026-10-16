# DAY01 Human Painter release

Two separate 4096² sets: warm ivory MC_Enamel and wet coral MC_Gum. Independent editable SPP. Physical maps were baked/exported from this day's frozen OBJ/UV packet; no other-day UV bitmap was reused. Root accepted the shape, actual UV checker and both atlases, and independent geometry/UV identity gates passed before final bake.

Five texture parameters are listed in shader_binding.json. BaseColor is sRGB; roughness, AO and thickness are linear; tangent normal is DirectX with no extra green flip. Gamma probes exactly match RGB8 enamel242/223/196 and gum213/47/60. Native baked thickness is black-thin/white-thick normalized per texture set, not metric. Additional ThinTissueMask is an artist mask.

Anatomical neck/tip/margin masks are regenerated from the day's packet. Fine enamel, mucosal, vascular and wet-film fields use 3D triplanar coordinates and are baked into the final UV0. Broad reference lights, white highlights, pink bounce and shadows are not painted into albedo. Actual Painter front, close and oblique grazing views were inspected without visible facial UV seams. Full 4K pixel ranges, hashes and normal statistics are in texture_qa.json.

The staged MTL only had leading-dot values normalized for Painter's strict parser; frozen OBJ/UV/FBX were untouched. Native Unreal lighting, SSS, dynamic jaw/card clipping, character, animation and 1:1 reference acceptance remain the lead's final art gate.
