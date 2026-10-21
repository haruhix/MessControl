"""Enable player contact on the existing Hub ball's physics sphere."""
import json
import shutil
from pathlib import Path
import unreal as u

def run():
    assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring'
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name() == '/Game/Maps/Hub.Hub'
    balls = [a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
             if isinstance(a, u.MCHubBasketball)]
    assert len(balls) == 1
    ball = balls[0]
    root = Path(u.Paths.project_dir())
    out = root / 'Saved/HubBasketballCollision_20261021'
    backup = out / 'Before/Content/Maps/Hub.umap'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(root / 'Content/Maps/Hub.umap', backup)
    with u.ScopedEditorTransaction('Enable player collision on Hub basketball'):
        ball.modify()
        ball.body.modify()
        ball.body.set_collision_response_to_channel(u.CollisionChannel.ECC_PAWN, u.CollisionResponseType.ECR_BLOCK)
        ball.body.set_editor_property('can_character_step_up_on', u.CanBeCharacterBase.ECB_NO)
    assert u.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/Hub')
    report = {
        'ball': ball.get_path_name(), 'pawn_response': str(ball.body.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)),
        'can_step_up': str(ball.body.get_editor_property('can_character_step_up_on')), 'saved': True,
    }
    (out / 'authored.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('BASKETBALL_PLAYER_COLLISION_AUTHORED', json.dumps(report))

if __name__ == '__main__':
    run()
