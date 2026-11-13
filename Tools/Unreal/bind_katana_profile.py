"""Assign the imported katana clips after rebuilding the editor module."""
import unreal as u


ANIMATION_ROOT = "/Game/Gameplay/CharacterCurrent/Animations/Katana/"
FIELDS = {
    "katana_carry": "Carry",
    "katana_diagonal_right": "DiagonalRight",
    "katana_diagonal_left": "DiagonalLeft",
    "katana_horizontal": "Horizontal",
    "katana_overhead": "Overhead",
    "katana_charged": "Charged",
}


def main():
    lib = u.EditorAssetLibrary
    profile = lib.load_asset("/Game/Data/DA_ToothAnimation")
    hero = lib.load_asset("/Game/Gameplay/CharacterCurrent/SK_CharacterGameplay")
    if profile is None or hero is None:
        raise RuntimeError("Missing character or animation profile")
    skeleton = hero.get_editor_property("skeleton")
    clips = {}
    for field, name in FIELDS.items():
        profile.get_editor_property(field)
        clip = lib.load_asset(ANIMATION_ROOT + "A_Katana_" + name)
        if not isinstance(clip, u.AnimSequence) or clip.get_editor_property("skeleton") != skeleton:
            raise RuntimeError(f"Missing or incompatible katana clip: {name}")
        clips[field] = clip
    for field, clip in clips.items():
        profile.set_editor_property(field, clip)
        assert profile.get_editor_property(field) == clip
    if not lib.save_loaded_asset(profile, only_if_is_dirty=False):
        raise RuntimeError("Could not save the character animation profile")
    u.log("MC_KATANA_PROFILE_BOUND")


if __name__ == "__main__":
    main()
