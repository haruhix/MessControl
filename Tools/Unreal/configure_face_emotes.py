"""Assign the artist's eye morphs to the facial choices in the T menu."""
import unreal as u

lib = u.EditorAssetLibrary
mesh = lib.load_asset('/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay')
assert mesh, 'Import the current gameplay character first.'
morphs = {m.get_name() for m in mesh.get_editor_property('morph_targets')}
assert {'Eyes_Scary', 'Eyes_delight'} <= morphs, morphs
emotes = lib.load_asset('/Game/Data/DA_Emotes')
assert emotes
entries = list(emotes.get_editor_property('entries'))
for id, label, morph, mood in [
    ('artist_shock', 'Испуг', 'Eyes_Scary', u.MCEmotion.SURPRISE),
    ('love', 'Влюблённость', 'Eyes_delight', u.MCEmotion.HAPPY),
]:
    entry = next((e for e in entries if str(e.id) == id), None)
    if entry is None:
        entry = u.MCEmoteEntry()
        entries.append(entry)
    entry.id = id
    entry.label = label
    entry.animation = None
    entry.emotion = mood
    entry.set_editor_property('eye_morph', morph)
    entry.set_editor_property('face_only', True)
    entry.set_editor_property('hold_final_pose', False)
    entry.duration = 3
emotes.set_editor_property('entries', entries)
assert lib.save_loaded_asset(emotes)
u.log('MC_FACE_EMOTES_CONFIGURED: Eyes_Scary / Eyes_delight')
