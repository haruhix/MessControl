#include "SMCEventSFXPanel.h"
#include "SMCSFXGallery.h"
#include "AssetRegistry/AssetData.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyCustomizationHelpers.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MCEventSFXPanel"

FText SMCEventSFXPanel::ModeLabel(EMCEventSFXMode Mode)const
{
    switch(Mode){
        case EMCEventSFXMode::UseDefault:return LOCTEXT("Default","Родной звук");
        case EMCEventSFXMode::Add:return LOCTEXT("Add","Родной звук + слои");
        case EMCEventSFXMode::Replace:return LOCTEXT("Replace","Только добавленные слои");
        default:return LOCTEXT("Disabled","Звук события выключен");
    }
}
TSharedRef<SWidget> SMCEventSFXPanel::ModeMenu()
{
    auto Rows=SNew(SVerticalBox);
    for(const auto Mode:{EMCEventSFXMode::UseDefault,EMCEventSFXMode::Add,EMCEventSFXMode::Replace,EMCEventSFXMode::Disabled})
        Rows->AddSlot().AutoHeight()[SNew(SButton).Text(ModeLabel(Mode))
            .IsEnabled(Context.bCanEdit&&(Context.bCanSuppressOriginal||Mode==EMCEventSFXMode::UseDefault||Mode==EMCEventSFXMode::Add))
            .ToolTipText(Context.OriginalReason)
            .OnClicked_Lambda([Weak=TWeakPtr<SMCEventSFXPanel>(SharedThis(this)),Mode]{if(auto W=Weak.Pin()){
                FString Error;if(W->Context.SetMode&&!W->Context.SetMode(W->Context.Scope,Mode,Error)){if(W->Context.ShowError)W->Context.ShowError(Error);}
                else if(W->Context.Refresh)W->Context.Refresh();}FSlateApplication::Get().DismissAllMenus();return FReply::Handled();})];
    return Rows;
}
void SMCEventSFXPanel::Construct(const FArguments& Args)
{
    Context=Args._Context;auto Rows=SNew(SVerticalBox);
    Rows->AddSlot().AutoHeight().Padding(0,0,0,7)[SNew(STextBlock).Text(Context.EventLabel).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),15)).AutoWrapText(true)];
    Rows->AddSlot().AutoHeight()[SNew(SComboButton).OnGetMenuContent(this,&SMCEventSFXPanel::ModeMenu).ButtonContent()[SNew(STextBlock).Text(ModeLabel(Context.Slot.Mode)).AutoWrapText(true)]];
    if(!Context.OriginalReason.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,5)[SNew(STextBlock).Text(Context.OriginalReason).AutoWrapText(true)];
    for(const auto& Source:Context.Originals)Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).IsEnabled(!Source.Asset.IsNull()||!Source.SourceOwner.IsEmpty())
        .ToolTipText(Source.Detail).OnClicked_Lambda([Open=Context.OpenSource,Source]{if(Open)Open(Source);return FReply::Handled();})
        [SNew(STextBlock).Text(Source.Label).AutoWrapText(true)]];
    if(!Context.bCanAudition&&!Context.AuditionReason.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text(Context.AuditionReason).AutoWrapText(true)];
    if(!Context.bGroupEnabled||Context.bGroupMuted)Rows->AddSlot().AutoHeight().Padding(0,5)[SNew(STextBlock)
        .Text(LOCTEXT("ParentMuted","Группа выключена или её звук заглушён. Настройки отдельных звуков сохранены.")).AutoWrapText(true)];
    for(const auto& Layer:Context.Slot.Layers)Rows->AddSlot().AutoHeight().Padding(0,3)[SNew(SButton)
        .OnClicked_Lambda([Select=Context.SelectLayer,ID=Layer.ID]{if(Select)Select(ID);return FReply::Handled();})
        [SNew(STextBlock).Text(FText::FromString(Layer.Name.IsNone()?Layer.Sound.ToSoftObjectPath().GetAssetName():Layer.Name.ToString())).AutoWrapText(true)]];
    const auto* Selected=Context.Slot.Layers.FindByPredicate([&](const auto& L){return L.ID==Context.SelectedLayer;});
    if(Selected)Rows->AddSlot().AutoHeight().Padding(0,9)[LayerDetails(*Selected)];
    else Rows->AddSlot().AutoHeight().Padding(0,9)[SNew(STextBlock).Text(LOCTEXT("ChooseLayer","Выберите звуковой слой или добавьте звук из галереи.")).AutoWrapText(true)];
    ChildSlot[Rows];
}
void SMCEventSFXPanel::Edit(FMCEventSFXLayer Layer,TFunction<void(FMCEventSFXLayer&)> Change)
{
    if(!Context.bCanEdit||!Context.SetLayer)return;
    const FGuid ID=Layer.ID;FString Error;
    // Asset pickers can outlive a detail refresh/Undo. Change just the requested
    // field on the current layer instead of restoring an old whole-layer copy.
    if(Context.ReadLayer&&!Context.ReadLayer(Context.Scope,ID,Layer,Error)){if(Context.ShowError)Context.ShowError(Error);return;}
    Change(Layer);
    if(!Context.SetLayer(Context.Scope,ID,Layer,Error)){if(Context.ShowError)Context.ShowError(Error);}
    else if(Context.Refresh)Context.Refresh();
}
TSharedRef<SWidget> SMCEventSFXPanel::LayerDetails(const FMCEventSFXLayer& Layer)
{
    auto Rows=SNew(SVerticalBox);const TWeakPtr<SMCEventSFXPanel> Weak=SharedThis(this);
    Rows->AddSlot().AutoHeight()[SNew(SObjectPropertyEntryBox).AllowedClass(USoundBase::StaticClass()).ObjectPath(Layer.Sound.ToSoftObjectPath().ToString()).IsEnabled(Context.bCanEdit)
        .OnObjectChanged_Lambda([Weak,Layer](const FAssetData& Asset){if(auto W=Weak.Pin()){
            const FText Unavailable=SMCSFXGallery::GetAssetUnavailableReason(Asset);
            if(!Unavailable.IsEmpty()){if(W->Context.ShowError)W->Context.ShowError(Unavailable.ToString());return;}
            W->Edit(Layer,[&](auto& L){L.Sound=Asset.GetSoftObjectPath();});}})];
    Rows->AddSlot().AutoHeight().Padding(0,6)[SNew(SCheckBox).IsEnabled(Context.bCanEdit).IsChecked(Layer.bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
        .OnCheckStateChanged_Lambda([Weak,Layer](ECheckBoxState State){if(auto W=Weak.Pin())W->Edit(Layer,[&](auto& L){L.bEnabled=State==ECheckBoxState::Checked;});})
        [SNew(STextBlock).Text(LOCTEXT("LayerEnabled","Этот звук включён"))]];
    auto Number=[&](FText Label,float Value,float Min,float Max,TFunction<void(FMCEventSFXLayer&,float)> Set){
        Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(.55)[SNew(STextBlock).Text(Label).AutoWrapText(true)]
            +SHorizontalBox::Slot().FillWidth(.45)[SNew(SSpinBox<float>).IsEnabled(Context.bCanEdit).Value(Value).MinValue(Min).MaxValue(Max)
                .OnValueCommitted_Lambda([Weak,Layer,Set](float V,ETextCommit::Type){if(auto W=Weak.Pin())W->Edit(Layer,[&](auto& L){Set(L,V);});})]];};
    Number(LOCTEXT("Gain","Громкость"),Layer.Volume,0,4,[](auto& L,float V){L.Volume=V;});
    Number(LOCTEXT("Pitch","Высота тона"),Layer.Pitch,.125f,4,[](auto& L,float V){L.Pitch=V;});
    Number(LOCTEXT("Delay","Задержка, с"),Layer.DelaySeconds,0,60,[](auto& L,float V){L.DelaySeconds=V;});
    Rows->AddSlot().AutoHeight().Padding(0,6)[SNew(SCheckBox).IsEnabled(Context.bCanEdit).IsChecked(Layer.bRepeatOnFinish?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
        .ToolTipText(LOCTEXT("RepeatHint","Повторяет конечный звук до окончания события или лимита. Встроенную петлю задаёт сам исходник."))
        .OnCheckStateChanged_Lambda([Weak,Layer](ECheckBoxState State){if(auto W=Weak.Pin())W->Edit(Layer,[&](auto& L){L.bRepeatOnFinish=State==ECheckBoxState::Checked;});})
        [SNew(STextBlock).Text(LOCTEXT("Repeat","Повторять после окончания"))]];
    Rows->AddSlot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).IsEnabled(Context.bCanEdit&&bool(Context.DuplicateLayer)).Text(LOCTEXT("Duplicate","Дубликат"))
            .OnClicked_Lambda([Weak,ID=Layer.ID]{if(auto W=Weak.Pin()){FString Error;if(W->Context.DuplicateLayer&&!W->Context.DuplicateLayer(W->Context.Scope,ID,Error)){if(W->Context.ShowError)W->Context.ShowError(Error);}else if(W->Context.Refresh)W->Context.Refresh();}return FReply::Handled();})]
        +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).IsEnabled(Context.bCanEdit&&bool(Context.RemoveLayer)).Text(LOCTEXT("Remove","Удалить звук"))
            .OnClicked_Lambda([Weak,ID=Layer.ID]{if(auto W=Weak.Pin()){FString Error;if(W->Context.RemoveLayer&&!W->Context.RemoveLayer(W->Context.Scope,ID,Error)){if(W->Context.ShowError)W->Context.ShowError(Error);}else if(W->Context.Refresh)W->Context.Refresh();}return FReply::Handled();})]];
    return Rows;
}
#undef LOCTEXT_NAMESPACE
