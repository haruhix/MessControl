#include "SMCComposerSceneAudioPanel.h"

#include "MCComposerSceneAudioSession.h"
#include "MCDataAssets.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Sound/SoundBase.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MCComposerSceneAudioPanel"

void SMCComposerSceneAudioPanel::Construct(const FArguments& Args)
{
    Session=Args._Session;Scene=Args._Scene;Section=Args._Section;CanPreview=Args._CanPreview;IsActive=Args._IsActive;
    OnSceneSelected=Args._OnSceneSelected;OnChanged=Args._OnChanged;OnPlay=Args._OnPlay;OnStop=Args._OnStop;
    Rebuild();
}

void SMCComposerSceneAudioPanel::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
    if(Session && (SeenRevision!=Session->GetRevision() || SeenScene!=Scene.Get() || SeenSource.Get()!=Session->GetSource()))
    {
        // Preserve an in-progress numeric entry. Its captured revision still
        // rejects a stale commit; refresh the controls after focus leaves.
        if(SeenScene==Scene.Get()&&SeenSource.Get()==Session->GetSource()&&HasFocusedDescendants())return;
        LocalStatus.Reset();Rebuild();
    }
}

SMCComposerSceneAudioPanel::FEditScope SMCComposerSceneAudioPanel::CaptureScope() const
{
    FEditScope Scope;Scope.Source=Session?Session->GetSource():nullptr;Scope.Scene=Scene.Get();
    Scope.Revision=Session?Session->GetRevision():0;return Scope;
}

bool SMCComposerSceneAudioPanel::IsScopeCurrent(const FEditScope& Scope) const
{
    return IsActive.Get() && Session && Scope.Source.Get()==Session->GetSource() && Scope.Scene==Scene.Get() && Scope.Revision==Session->GetRevision();
}

void SMCComposerSceneAudioPanel::Rebuild()
{
    const auto Rows=SNew(SVerticalBox);
    if(!Session)Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("NoSession","Черновик сценового звука недоступен."))];
    else
    {
        SeenRevision=Session->GetRevision();SeenScene=Scene.Get();SeenSource=Session->GetSource();
        if(Section!=EMCComposerSceneAudioPanelSection::Layers)Rows->AddSlot().AutoHeight()[BuildHeader()];
        if(Section!=EMCComposerSceneAudioPanelSection::Header)Rows->AddSlot().AutoHeight().Padding(0,8)[BuildLayers()];
        Rows->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).AutoWrapText(true)
            .Text_Lambda([this]{return FText::FromString(LocalStatus.IsEmpty()?Session->GetStatus():LocalStatus);})];
    }
    ChildSlot[SNew(SBorder).Padding(8)[Rows]];
}

TSharedRef<SWidget> SMCComposerSceneAudioPanel::BuildHeader()
{
    const auto Rows=SNew(SVerticalBox);const FEditScope Scope=CaptureScope();
    const TWeakPtr<SMCComposerSceneAudioPanel> Weak=SharedThis(this);
    Rows->AddSlot().AutoHeight().Padding(0,0,0,6)[SNew(STextBlock).Text(LOCTEXT("Palette","Источник сценового звука"))];
    Rows->AddSlot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(UMCSoundPalette::StaticClass()).AllowClear(false)
        .ObjectPath_Lambda([Weak]{const auto Panel=Weak.Pin();return Panel&&Panel->Session?GetPathNameSafe(Panel->Session->GetSource()):FString();})
        .OnObjectChanged_Lambda([Weak,Scope](const FAssetData& Asset){if(const auto Panel=Weak.Pin())Panel->PickPalette(Asset,Scope);})];
    Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).Text(LOCTEXT("OpenPalette","Открыть палитру-источник"))
        .IsEnabled(Session->GetSource()!=nullptr)
        .OnClicked_Lambda([Weak,Scope]
        {
            if(const auto P=Weak.Pin();P&&P->IsScopeCurrent(Scope))if(auto* Source=P->Session->GetSource();Source&&GEditor)
                if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Source);
            return FReply::Handled();
        })];
    Rows->AddSlot().AutoHeight().Padding(0,8)[SNew(SComboButton)
        .IsEnabled(Session->GetSource()!=nullptr)
        .OnGetMenuContent_Lambda([Weak]() -> TSharedRef<SWidget>
        {
            const auto Items=SNew(SVerticalBox);const auto Panel=Weak.Pin();
            if(Panel&&Panel->Session)
            {
                const FEditScope MenuScope=Panel->CaptureScope();
                for(FName Name:Panel->Session->GetSceneNames())Items->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromName(Name))
                    .OnClicked_Lambda([Weak,MenuScope,Name]
                    {
                        if(const auto P=Weak.Pin();P&&P->IsScopeCurrent(MenuScope))
                        {P->OnStop.ExecuteIfBound();P->OnSceneSelected.ExecuteIfBound(Name);P->LocalStatus.Reset();FSlateApplication::Get().DismissAllMenus();P->Changed();}
                        return FReply::Handled();
                    })];
            }
            return SNew(SBox).MaxDesiredHeight(360)[SNew(SScrollBox)+SScrollBox::Slot()[Items]];
        })
        .ButtonContent()[SNew(STextBlock).Text_Lambda([Weak]
        {const auto P=Weak.Pin();return P&&!P->Scene.Get().IsNone()?FText::FromName(P->Scene.Get()):LOCTEXT("ChooseScene","Выберите сцену");})]];
    Rows->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("SceneScope",
        "Музыка и атмосфера сцены. Событийные SFX сохраняются отдельно. Пустой звук означает тишину."))];
    return Rows;
}

TSharedRef<SWidget> SMCComposerSceneAudioPanel::BuildLayers()
{
    const auto Rows=SNew(SVerticalBox);const FEditScope Scope=CaptureScope();const TWeakPtr<SMCComposerSceneAudioPanel> Weak=SharedThis(this);
    const bool bHasScene=Session->GetSource() && !Scope.Scene.IsNone() && Session->GetScene(Scope.Scene);
    Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::Format(LOCTEXT("SceneTitle","Сцена: {0}"),FText::FromName(Scope.Scene)))];
    if(bHasScene)
    {
        Rows->AddSlot().AutoHeight().Padding(0,8)[BuildLayer(LOCTEXT("Music","Музыка"),true)];
        Rows->AddSlot().AutoHeight().Padding(0,8)[BuildLayer(LOCTEXT("Ambience","Атмосфера"),false)];
    }
    else Rows->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text(LOCTEXT("NoScene","Выберите существующую сцену в палитре."))];
    Rows->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("SourcesNote",
        "Sound Wave, Sound Cue и MetaSound выбираются как реальные SoundBase-ресурсы. Их вложенные ресурсы и готовность загрузки проверяются отдельно."))];
    Rows->AddSlot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).Text(LOCTEXT("Play","Слушать сцену"))
            .IsEnabled_Lambda([Weak,bHasScene]{const auto P=Weak.Pin();return P&&bHasScene&&P->CanPreview.Get();})
            .OnClicked_Lambda([Weak,Scope]{if(const auto P=Weak.Pin();P&&P->IsScopeCurrent(Scope))P->OnPlay.ExecuteIfBound();return FReply::Handled();})]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("Stop","Стоп"))
            .OnClicked_Lambda([Weak]{if(const auto P=Weak.Pin())P->OnStop.ExecuteIfBound();return FReply::Handled();})]];
    Rows->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).Text(LOCTEXT("Apply","Применить сцены в игре"))
            .IsEnabled(Session->HasChanges()).ToolTipText(LOCTEXT("ApplyHint","Сохранить изменения Scenes в выбранную палитру. Events и событийные SFX не изменяются."))
            .OnClicked_Lambda([Weak,Scope]{if(const auto P=Weak.Pin())P->Apply(Scope);return FReply::Handled();})]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("Revert","Перечитать источник"))
            .IsEnabled(Session->GetSource()!=nullptr)
            .OnClicked_Lambda([Weak,Scope]{if(const auto P=Weak.Pin())P->Revert(Scope);return FReply::Handled();})]];
    return Rows;
}

TSharedRef<SWidget> SMCComposerSceneAudioPanel::BuildLayer(const FText& Label,bool bMusic)
{
    const FEditScope Scope=CaptureScope();const auto* Settings=Session->GetScene(Scope.Scene);
    const FMCSoundSceneLayer Layer=bMusic?Settings->Music:Settings->Ambience;
    const TWeakPtr<SMCComposerSceneAudioPanel> Weak=SharedThis(this);
    const auto Rows=SNew(SVerticalBox);
    Rows->AddSlot().AutoHeight().Padding(0,0,0,5)[SNew(STextBlock).Text(Label)];
    Rows->AddSlot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(USoundBase::StaticClass()).AllowClear(true)
        .ObjectPath(Layer.Sound.ToSoftObjectPath().ToString())
        .OnObjectChanged_Lambda([Weak,Scope,bMusic](const FAssetData& Asset)
        {if(const auto P=Weak.Pin())P->Edit(Scope,[bMusic,Path=Asset.GetSoftObjectPath()](FMCSoundScene& Value){(bMusic?Value.Music:Value.Ambience).Sound=TSoftObjectPtr<USoundBase>(Path);});})];
    Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).Text(LOCTEXT("OpenSound","Открыть исходный звук"))
        .IsEnabled(!Layer.Sound.IsNull())
        .OnClicked_Lambda([Weak,Scope,bMusic]
        {
            if(const auto P=Weak.Pin();P&&P->IsScopeCurrent(Scope))if(const auto* Current=P->Session->GetScene(Scope.Scene))
                if(auto* Sound=(bMusic?Current->Music.Sound:Current->Ambience.Sound).LoadSynchronous();Sound&&GEditor)
                    if(auto* Editors=GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())Editors->OpenEditorForAsset(Sound);
            return FReply::Handled();
        })];
    auto Number=[&](const FText& Title,float Value,float Maximum,TFunction<void(FMCSoundSceneLayer&,float)> Set)
    {
        Rows->AddSlot().AutoHeight().Padding(0,3)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(Title)]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(100)[SNew(SNumericEntryBox<float>).Value(Value).MinValue(0.f).MaxValue(Maximum).MinSliderValue(0.f).MaxSliderValue(Maximum)
                .OnValueCommitted_Lambda([Weak,Scope,bMusic,Set](float Number,ETextCommit::Type)
                {if(const auto P=Weak.Pin())P->Edit(Scope,[bMusic,Set,Number](FMCSoundScene& V){Set(bMusic?V.Music:V.Ambience,Number);});})]]];
    };
    Number(LOCTEXT("Volume","Громкость"),Layer.Volume,2.f,[](FMCSoundSceneLayer& L,float V){L.Volume=V;});
    Number(LOCTEXT("FadeIn","Появление, с"),Layer.FadeInSeconds,10.f,[](FMCSoundSceneLayer& L,float V){L.FadeInSeconds=V;});
    Number(LOCTEXT("FadeOut","Затухание, с"),Layer.FadeOutSeconds,10.f,[](FMCSoundSceneLayer& L,float V){L.FadeOutSeconds=V;});
    Rows->AddSlot().AutoHeight().Padding(0,5)[SNew(SCheckBox).IsChecked(Layer.bRepeatOnFinish?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
        .ToolTipText(LOCTEXT("RepeatHint","Повторять конечный источник после завершения. Бесшовный цикл задаётся в самом звуке; выключение этой опции не отменяет цикл источника."))
        .OnCheckStateChanged_Lambda([Weak,Scope,bMusic](ECheckBoxState State)
        {if(const auto P=Weak.Pin())P->Edit(Scope,[bMusic,State](FMCSoundScene& V){(bMusic?V.Music:V.Ambience).bRepeatOnFinish=State==ECheckBoxState::Checked;});})
        [SNew(STextBlock).Text(LOCTEXT("Repeat","Повторять после завершения"))]];
    return SNew(SBorder).Padding(8)[Rows];
}

void SMCComposerSceneAudioPanel::PickPalette(const FAssetData& Asset,const FEditScope& Scope)
{
    if(!IsScopeCurrent(Scope))return;
    auto* Source=Cast<UMCSoundPalette>(Asset.GetAsset());if(!Source||Source==Session->GetSource())return;
    OnStop.ExecuteIfBound();FString Error;
    if(!Session->Open(Source,Error)){LocalStatus=Error;Rebuild();return;}
    const auto Names=Session->GetSceneNames();if(!Names.Contains(Scene.Get()))OnSceneSelected.ExecuteIfBound(Names.IsEmpty()?NAME_None:Names[0]);
    LocalStatus.Reset();Changed();
}

void SMCComposerSceneAudioPanel::Edit(const FEditScope& Scope,TFunction<void(FMCSoundScene&)> Mutation)
{
    if(!IsScopeCurrent(Scope))return;const auto* Existing=Session->GetScene(Scope.Scene);if(!Existing)return;
    FMCSoundScene Value=*Existing;Mutation(Value);OnStop.ExecuteIfBound();FString Error;
    if(!Session->SetScene(Scope.Scene,Value,Error)){LocalStatus=Error;Rebuild();return;}
    LocalStatus.Reset();Changed();
}

void SMCComposerSceneAudioPanel::Apply(const FEditScope& Scope)
{
    if(!IsScopeCurrent(Scope))return;OnStop.ExecuteIfBound();FString Error;
    if(!Session->Apply(Error)){LocalStatus=Error;Rebuild();return;}
    LocalStatus.Reset();Changed();
}

void SMCComposerSceneAudioPanel::Revert(const FEditScope& Scope)
{
    if(!IsScopeCurrent(Scope))return;
    if(Session->HasChanges() && FMessageDialog::Open(EAppMsgType::YesNo,
        LOCTEXT("DiscardSceneDraft","Отбросить несохранённые изменения сцен и перечитать выбранную палитру?"))!=EAppReturnType::Yes)return;
    // The confirmation is modal: its old scope must still match before discarding anything.
    if(!IsScopeCurrent(Scope))return;OnStop.ExecuteIfBound();FString Error;
    if(!Session->Revert(Error)){LocalStatus=Error;Rebuild();return;}
    const auto Names=Session->GetSceneNames();if(!Names.Contains(Scene.Get()))OnSceneSelected.ExecuteIfBound(Names.IsEmpty()?NAME_None:Names[0]);
    LocalStatus.Reset();Changed();
}

void SMCComposerSceneAudioPanel::Changed()
{Rebuild();OnChanged.ExecuteIfBound();}

#undef LOCTEXT_NAMESPACE
