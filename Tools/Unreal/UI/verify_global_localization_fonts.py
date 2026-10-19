"""Five actual CJK UMG previews; synthetic copy exists only in /Engine/Transient.

Explicitly run in an idle editor after catalogue/font authoring. No saves, PIE,
builds, QA packages or global language changes. PNG success requires subsequent
visual inspection. MainMenu is covered by font identity here, not an actual PNG.
"""
import copy
import hashlib
import json
from pathlib import Path
import struct
import traceback

import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path('E:/CodexWork/MessControlLocalization_20261019/Previews/CJK')
SHARED='/Game/Gameplay/UI/Fonts/F_MCUI_Global.F_MCUI_Global'
PROFILES=(
    ('zh-Hans','ZH_HANS','选择增益','清洁口腔','等待其他玩家',('坚果骑士','冰霜','暴风雪')),
    ('zh-Hant','ZH_HANT','選擇增益','清潔口腔','等待其他玩家',('堅果騎士','冰霜','暴風雪')),
    ('ja','JA','パークを選択','口の清潔度','他のプレイヤーを待っています',('ナッツの騎士','氷の嵐','歯の健康')),
    ('ko','KO','특전 선택','입 청결도','다른 플레이어를 기다리는 중',('견과류 기사','얼음 폭풍','치아 건강')),
)


def require(ok,message):
    if not ok:
        raise RuntimeError(message)


def rows(table):
    return json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def culture():
    return {'language':u.InternationalizationLibrary.get_current_language(),
            'locale':u.InternationalizationLibrary.get_current_locale()}


def path(value):
    return value.get_path_name() if value is not None else None


def digest(values):
    return hashlib.sha256(json.dumps(values,ensure_ascii=False,sort_keys=True).encode('utf-8')).hexdigest()


def fixture_rows(baseline,cards):
    result=copy.deepcopy(baseline)
    by_key={row['Name']:row for row in result}
    selected={row['GameplayPerkID']:row for row in cards if row.get('GameplayPerkID') in ('X01','X02','X04')}
    require(len(selected)==3,'Risky preview requires existing X01/X02/X04 presentation rows')
    for requested,column,title,body,waiting,names in PROFILES:
        values={
            'UI.Reward.ChoosePerk':title,'UI.Reward.OtherChoicesDisappear':body,
            'UI.Reward.ChoiceHint':waiting,'UI.Reward.RiskyTitle':title,
            'UI.Reward.RiskySubtitle':body+' · '+waiting,'UI.Reward.Decline':waiting,
            'UI.Reward.Badge.Risky':title,'UI.Reward.Badge.Positive':title,
            'UI.Reward.Badge.Negative':title,'UI.Reward.Rarity.Standard':body,
            'UI.Reward.Rarity.Rare':body,'UI.Reward.Rarity.Legendary':body,
            'UI.Reward.Scope.Personal':title,'UI.Reward.Scope.Team':title,
            'UI.Reward.StackFormat':body+' {Before} → {After} / {Maximum}',
            'UI.HUD.ReserveTitle':names[2],'UI.HUD.Cleanliness':body,
            'UI.HUD.Task':title,'UI.HUD.UntilEvent':waiting,
            'UI.HUD.Tool.Brush':body,'UI.HUD.BrushAction':body,
            'UI.HUD.Preview.LongHint':waiting+' · '+body,
            'UI.HUD.Preview.TeamXP':title+' 2 · 35 / 75 XP',
            'UI.Stamina.Title':body,'UI.Alert.BossWarningPreview':waiting,
            'UI.Alert.LongPreview':waiting,'UI.Boss.Zombie.Name':names[0],
            'UI.Booster.FuryActive':body,'UI.Booster.GrenadeArmed':waiting,
        }
        for index,identity in enumerate(('X01','X02','X04')):
            card=selected[identity]
            require(card.get('DisplayNameKey') and card.get('DescriptionKey'),'Migrate card keys before CJK rendering')
            values[card['DisplayNameKey']]=names[index]
            # Existing numeric token metadata supplies real benefit/cost markup.
            # Copy is deliberately synthetic; it is not a translated perk rule.
            values[card['DescriptionKey']]=(body+' +30% · '+names[index]+' −25% · '+body+' +8% +15 −20%. ')*2
        for key,text in values.items():
            require(key in by_key,'Required localization key missing: '+key)
            require(column in by_key[key],'Migrate global language columns first: '+column)
            by_key[key][column]=text
    return result


def main():
    require(ROOT.drive.upper()=='E:','Project and outputs must be on E:')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Requires idle editor')
    OUT.mkdir(parents=True,exist_ok=True)
    require(not any(OUT.glob('*.png')),'Preserving existing CJK PNGs; move the previous run before retrying')
    report={'status':'running','renders':[],'synthetic_copy_not_shipping_translation':True,
            'saved_assets':False,'created_game_packages':False,'visual_acceptance':'pending actual PNG inspection',
            'limitations':['No packaged-game proof.','MainMenu font identity only; menu interaction remains a separate solo check.',
                           'No scroll-to-end or live language-switch video.','Droid common BMP CJK coverage is not every Unicode character.'],
            'cleanup_errors':[]}
    report_file=OUT/'Report.json'
    before_culture=culture()
    report['culture_before']=before_culture
    theme=u.MCGameplayUIFactory.get_master_theme()
    require(theme is not None,'Master theme missing')
    tables={'localization':u.MCUIDataLibrary.get_default_localization_table(),
            'cards':u.MCUIDataLibrary.get_default_card_table(),
            'boosters':u.MCUIDataLibrary.get_default_booster_table()}
    require(all(isinstance(value,u.DataTable) for value in tables.values()),'Saved UI tables missing')
    baseline={key:rows(table) for key,table in tables.items()}
    before_tables={key:digest(value) for key,value in baseline.items()}
    theme_before={key:theme.get_editor_property(key).export_text() for key in
                  ('body_font','heading_font','display_font','compact_heading_font','palette')}
    theme_refs={key:path(theme.get_editor_property(key)) for key in ('localization_table','card_table','booster_table')}
    report['font_identity']={}
    for label,size,bold in (('body',16,False),('heading',22,True),('display',32,True)):
        font=theme.make_font(size,bold)
        info={'font_object':path(font.get_editor_property('font_object')),
              'typeface':str(font.get_editor_property('typeface_font_name')),'size':font.get_editor_property('size')}
        report['font_identity'][label]=info
        require(info['font_object']==SHARED,'Run shared font authoring first: '+label)
    report['font_identity']['compact']=theme_before['compact_heading_font']
    report['main_menu_font_identity']='Native helper uses master Theme.MakeFont(Regular/Bold); same three shared font objects checked above, no menu PNG claimed.'
    classes={}
    for role,native in (('PerkChoice','MCPerkChoiceWidget'),('GameplayHUD','MCGameplayHUD'),
                        ('Booster','MCBoosterWidget'),('Stamina','MCStaminaWidget'),
                        ('BossHealth','MCBossHealthWidget'),('GameplayAlert','MCGameplayAlertWidget')):
        native_class=u.load_class(None,'/Script/MessControl.'+native)
        cls=u.MCGameplayUIFactory.resolve_class(u.Name(role),native_class)
        require(cls is not None,'Missing UI role: '+role)
        classes[role]=cls
    for role in ('PerkChoice','GameplayHUD'):
        require(classes[role].get_path_name().startswith('/Game/'),'Authored root WBP required: '+role)
    # Include explicit composed classes, not just factory mappings.
    all_classes={cls.get_path_name():cls for cls in classes.values()}
    for role,fields in (('PerkChoice',('card_widget_class',)),
                       ('GameplayHUD',('booster_widget_class','stamina_widget_class','alert_widget_class','encounter_preview_widget_class'))):
        defaults=u.get_default_object(classes[role])
        for field in fields:
            value=defaults.get_editor_property(field)
            if value is not None:
                all_classes[value.get_path_name()]=value
    choice=u.get_default_object(classes['PerkChoice'])
    require(not choice.get_editor_property('PreviewPerkIDs'),'Risky fixture expects empty authored PreviewPerkIDs; preserving a custom selection')
    actual_cards=choice.get_editor_property('card_table') or theme.get_editor_property('card_table')
    require(actual_cards==tables['cards'],'This bounded fixture requires the authored shared card table; preserving custom tables')
    table=u.new_object(u.DataTable)
    require(table.get_path_name().startswith('/Engine/Transient.'),'QA table must remain in transient package')
    row_struct=u.load_object(None,'/Script/MessControl.MCUILocalizationRow')
    # RowStruct is EditConst; pass its native import parameter instead of writing
    # a read-only property. FillFromJSON forces automated import with this arg.
    require(u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,
        json.dumps(fixture_rows(baseline['localization'],baseline['cards']),ensure_ascii=False),
        import_row_struct=row_struct),'Transient localization import failed')
    report['transient_table']=table.get_path_name()
    restored=[]
    failure=None
    try:
        for cls in all_classes.values():
            defaults=u.get_default_object(cls)
            original=defaults.get_editor_property('translation_table')
            original_theme=defaults.get_editor_property('theme')
            restored.append((defaults,original,original_theme))
            # Never sends editor property notifications or dirties a saved WBP.
            defaults.set_editor_property('translation_table',table,notify_mode=u.PropertyAccessChangeNotifyMode.NEVER)
        for requested,column,title,body,waiting,names in PROFILES:
            actual=str(u.MCUIDataLibrary.resolve_text(table,u.Name('UI.Reward.RiskyTitle'),u.Text(''),requested))
            require(actual==title,'Synthetic culture did not resolve: '+requested)
            output=OUT/('Choice_'+requested+'_1280x720.png')
            ok=u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(classes['PerkChoice'],str(output),u.IntPoint(1280,720),'Risky',requested,1.)
            require(ok and output.is_file(),'CJK choice render failed: '+requested)
            data=output.read_bytes()
            require(data[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',data[16:24])==(1280,720),'Unexpected PNG geometry')
            report['renders'].append({'role':'PerkChoice','culture':requested,'state':'Risky','path':str(output),
                                      'size':[1280,720],'sha256':hashlib.sha256(data).hexdigest(),'expected_heading':title,'expected_card_names':names})
            require(culture()==before_culture,'Renderer changed global culture')
            report_file.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        output=OUT/'HUD_zh-Hans_1280x720.png'
        ok=u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(classes['GameplayHUD'],str(output),u.IntPoint(1280,720),'PolishStress','zh-Hans',1.)
        require(ok and output.is_file(),'CJK HUD render failed')
        data=output.read_bytes()
        require(struct.unpack('>II',data[16:24])==(1280,720),'Unexpected HUD PNG geometry')
        report['renders'].append({'role':'GameplayHUD','culture':'zh-Hans','state':'PolishStress','path':str(output),
                                  'size':[1280,720],'sha256':hashlib.sha256(data).hexdigest(),
                                  'proof':'Ordinary HUD labels, alert and booster; encounter stack intentionally hidden by PolishStress.'})
    except Exception as error:
        failure=error
        report['error']=str(error)
        report['traceback']=traceback.format_exc()
    finally:
        for defaults,original,original_theme in reversed(restored):
            try:
                defaults.set_editor_property('translation_table',original,notify_mode=u.PropertyAccessChangeNotifyMode.NEVER)
                require(defaults.get_editor_property('translation_table')==original,'CDO TranslationTable restoration failed')
                require(defaults.get_editor_property('theme')==original_theme,'CDO Theme changed during render')
            except Exception as error:
                report['cleanup_errors'].append(str(error))
        report['culture_after']=culture()
        report['global_culture_unchanged']=report['culture_after']==before_culture
        report['saved_tables_unchanged']=all(digest(rows(tables[key]))==value for key,value in before_tables.items())
        report['theme_unchanged']=all(theme.get_editor_property(key).export_text()==value for key,value in theme_before.items()) and all(path(theme.get_editor_property(key))==value for key,value in theme_refs.items())
        report['cdo_restored_count']=len(restored)
        report['status']='rendered_pending_visual_review' if failure is None and not report['cleanup_errors'] and report['global_culture_unchanged'] and report['saved_tables_unchanged'] and report['theme_unchanged'] else 'failed'
        report_file.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    require(report['status']!='failed','CJK validation or cleanup failed; see '+str(report_file))
    if failure is not None:
        raise failure
    u.log('MC_GLOBAL_LOCALIZATION_FONT_PREVIEWS '+str(report_file))
    return report


if __name__=='__main__':
    main()
