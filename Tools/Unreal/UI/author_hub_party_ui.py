"""Scoped append-only Hub bar / Tab party migration and RU/EN localization.

Run through the project's editor Python bridge after the new Editor build:
    run('plan', output='E:/CodexWork/HubPartyLoading_20261024/PartyPlan01')
    run('apply', output='E:/CodexWork/HubPartyLoading_20261024/PartyAuthor01')

No build, map edit, PIE, online request, microphone operation or SaveAll.
The actual Theme roles are resolved; no assumed WBP path replaces an artist role.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
TEXTS = '/Game/Gameplay/UI/Data/DT_UI_Localization'
COPY = [
    ('hub.party.host_pending', 'Подключение…', 'Connecting…'),
    ('hub.party.summary', 'Хост: {Host}  ·  Игроки: {Count} / 4', 'Host: {Host}  ·  Players: {Count} / 4'),
    ('hub.party.invite', 'Пригласить друзей', 'Invite friends'),
    ('hub.privacy.public', 'Открытая', 'Public'),
    ('hub.privacy.friends', 'Только друзья', 'Friends only'),
    ('hub.privacy.invite', 'По приглашению', 'Invite only'),
    ('hub.party.close', 'Вернуться в хаб', 'Back to Hub'),
    ('hub.party.privacy_title', 'Доступ в комнату', 'Room access'),
    ('hub.party.privacy_value', 'Доступ: {Mode}', 'Access: {Mode}'),
    ('hub.party.local_title', 'Локальный хаб', 'Local Hub'),
    ('hub.party.updating', 'Меняем доступ…', 'Updating access…'),
    ('hub.party.local_notice', 'Приглашения доступны в Steam. К локальному хабу можно подключиться по IP.', 'Invitations are available in Steam. Join a local Hub using its IP address.'),
    ('hub.party.privacy_unknown', 'Не удалось подтвердить доступ в комнату. Хост может повторно выбрать режим.', 'Room access could not be confirmed. The host can choose a mode again.'),
    ('hub.party.full', 'Команда в сборе · свободных мест нет', 'Team is full · no free slots'),
    ('hub.party.public_notice', 'Открытая комната · игроки могут найти ваш хаб', 'Public room · players can find your Hub'),
    ('hub.party.friends_notice', 'Друзья могут подключаться сами или по приглашению', 'Friends can join directly or by invitation'),
    ('hub.party.invite_notice', 'Вход в хаб только по приглашению', 'Join this Hub by invitation only'),
    ('hub.party.footer', 'TAB / ESC — закрыть · Голос отключается только для вас', 'TAB / ESC — close · Voice mute affects only you'),
    ('hub.bottom.coins', 'Монеты', 'Coins'),
    ('hub.bottom.fish', 'Рыба для гриля', 'Fish for the grill'),
    ('hub.bottom.party', 'Команда {Count}/4 · {Key}', 'Team {Count}/4 · {Key}'),
    ('hub.privacy.status.updating', 'Меняем доступ к комнате…', 'Updating room access…'),
    ('hub.privacy.status.updated', 'Доступ к комнате изменён.', 'Room access updated.'),
    ('hub.privacy.status.restoring', 'Steam не подтвердил изменение. Восстанавливаем доступ…', 'Steam could not confirm the change. Restoring room access…'),
    ('hub.privacy.status.restored', 'Не удалось изменить доступ. Предыдущие настройки восстановлены.', 'Room access could not be changed. Previous settings restored.'),
    ('hub.privacy.status.unconfirmed', 'Steam не подтвердил доступ к комнате. Повтори выбор после восстановления соединения.', 'Steam could not confirm room access. Choose again once the connection is restored.'),
]


def dirty():
    return sorted(p.get_path_name() for p in
                  list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                  list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def role(role_name, native):
    cls = u.MCGameplayUIFactory.resolve_class(role_name, native.static_class())
    assert cls and cls.get_path_name().startswith('/Game/'), 'Keep the current authored ' + role_name + ' role'
    package = cls.get_path_name().split('.', 1)[0]
    asset = u.load_asset(package)
    assert isinstance(asset, u.WidgetBlueprint), package
    return package, asset, cls


def find(asset, name):
    return u.find_object(None, asset.get_path_name() + ':WidgetTree.' + name)


def inventory(asset):
    prefix = asset.get_path_name() + ':WidgetTree.'
    rows = []
    for widget in u.ObjectIterator(u.Widget):
        if widget.get_path_name().startswith(prefix):
            parent = widget.get_parent()
            rows.append(dict(name=widget.get_name(), native_class=widget.get_class().get_path_name(),
                             parent=parent.get_name() if parent else None))
    return sorted(rows, key=lambda row: row['name'])


def ancestor(widget, kind):
    parent = widget.get_parent() if widget else None
    while parent:
        if isinstance(parent, kind):
            return parent
        parent = parent.get_parent()
    return None


def validate_score_layout(asset, cls):
    frame, players = find(asset, 'ScoreFrame'), find(asset, 'ScorePlayers')
    assert isinstance(frame, u.Border) and isinstance(players, u.VerticalBox), 'Unknown Scoreboard tree; inspect before migration'
    size, scale = ancestor(frame, u.SizeBox), ancestor(frame, u.ScaleBox)
    scroll = ancestor(players, u.ScrollBox)
    assert size and scale and scroll and isinstance(frame.get_content(), u.VerticalBox), 'Unknown fixed-height layout; preserve the artist tree'
    assert ancestor(size, u.ScaleBox), 'The expanded party must fit as a complete panel'
    defaults = u.get_default_object(cls)
    assert defaults.get_editor_property('use_native_layout'), 'Artist opted out of native reflow; review before adding the party branch'
    return dict(frame=frame.get_name(), size=size.get_name(), scale=scale.get_name(),
                scroll=scroll.get_name(), initial_height=float(size.get_editor_property('height_override')),
                policy='Native adaptive presenter recognizes actual ancestor wrappers, including legacy auto-named v10 SizeBox/ScrollBox; whole party scales to fit.')


def backup(package, out):
    path = ROOT / ('Content/' + package.removeprefix('/Game/') + '.uasset')
    data = path.read_bytes()
    target = out / 'BeforePackages' / path.relative_to(ROOT / 'Content')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
    return dict(package=package, file=str(target), sha256=digest)


def run(action='plan', *, output='E:/CodexWork/HubPartyLoading_20261024/PartyAuthor01'):
    assert action in ('plan', 'apply')
    assert Path(u.Paths.project_dir()).resolve() == ROOT
    assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active Play'
    assert not dirty(), 'Preserve unsaved packages: ' + str(dirty())
    out = Path(output).resolve()
    assert out.drive.upper() == 'E:' and (not out.exists() or not any(out.iterdir())), 'Use a fresh E: output directory'
    out.mkdir(parents=True, exist_ok=True)
    hub_package, hub, hub_class = role('Hub', u.MCHubWidget)
    score_package, score, score_class = role('Scoreboard', u.MCScoreboardWidget)
    layout = validate_score_layout(score, score_class)
    table = u.load_asset(TEXTS)
    assert isinstance(table, u.DataTable)
    rows = json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    indexed = {row['Name'].casefold(): row for row in rows}
    assert len(indexed) == len(rows), 'Duplicate localization keys'
    additions = [dict(Name=key, RU=ru, EN=en, SourceText=ru, Context='Hub party / social bottom bar')
                 for key, ru, en in COPY if key.casefold() not in indexed]
    report = dict(action=action, status='planned', roles=dict(Hub=hub_package, Scoreboard=score_package),
                  layout=layout, localization_added=[row['Name'] for row in additions],
                  backups=[], saved_assets=[], previews=[], build=False, pie=False, network=False)
    (out / 'Plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if action == 'plan':
        return report
    for package in (hub_package, score_package, TEXTS):
        report['backups'].append(backup(package, out))
    before = {asset.get_path_name(): inventory(asset) for asset in (hub, score)}
    assert not dirty(), 'Do not save unrelated editor changes'
    if additions:
        table.modify()
        assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows + additions, ensure_ascii=False))
        actual = {row['Name'].casefold(): row for row in json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
        for row in rows:
            assert actual[row['Name'].casefold()] == row, 'Existing translation changed: ' + row['Name']
        assert u.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False)
        report['saved_assets'].append(TEXTS)
    assert u.MCHubFrontEndEditorLibrary.add_hub_guidance_presentation(hub)
    assert u.MCHubFrontEndEditorLibrary.add_scoreboard_hub_party_presentation(score)
    for package, asset in ((hub_package, hub), (score_package, score)):
        after = {row['name']: row for row in inventory(asset)}
        for row in before[asset.get_path_name()]:
            actual = after.get(row['name'])
            viewport_move = (package == hub_package and actual is not None and actual['native_class'] == row['native_class']
                             and actual['parent'] == 'HubViewportCanvas' and
                             ((row['name'] in ('HubFooterBackdrop', 'HubBottomBar') and row['parent'] == 'HubCanvas') or
                              (row['native_class'] == '/Script/UMG.ScaleBox' and row['parent'] is None)))
            assert actual == row or viewport_move, 'Existing widget reparented/replaced: ' + row['name']
        u.BlueprintEditorLibrary.compile_blueprint(asset)
        assert 'ERROR' not in str(asset.get_editor_property('status')).upper(), package
        assert u.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), package
        report['saved_assets'].append(package)
    assert find(hub, 'HubBottomBar') and find(hub, 'HubCoinIcon') and find(hub, 'HubFishIcon')
    assert find(score, 'HubPartyControls') and find(score, 'HubPartyInviteOnly')
    assert not dirty(), 'Unexpected dirty packages preserved; inspect before further operations'
    for native_role, native, state, culture, width, height in (
        ('Hub', u.MCHubWidget, 'Gathering', 'ru', 1280, 720),
        ('Hub', u.MCHubWidget, 'Gathering', 'en', 1920, 1080),
        ('Scoreboard', u.MCScoreboardWidget, 'HubHost', 'ru', 1280, 720),
        ('Scoreboard', u.MCScoreboardWidget, 'HubHost', 'en', 1920, 1080),
        ('Scoreboard', u.MCScoreboardWidget, 'HubGuest', 'en', 1280, 720),
        ('Scoreboard', u.MCScoreboardWidget, 'HubLocal', 'ru', 1280, 720),
    ):
        cls = u.MCGameplayUIFactory.resolve_class(native_role, native.static_class())
        image = out / f'{native_role}_{state}_{culture}_{width}x{height}.png'
        assert u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls, str(image), u.IntPoint(width, height), state, culture, .5)
        report['previews'].append(str(image))
    report['status'] = 'authored; visual review / gameplay / Steam validation remain separate'
    (out / 'Authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return report
