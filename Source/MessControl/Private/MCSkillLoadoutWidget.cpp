#include "MCSkillLoadoutWidget.h"
#include "MCPerkComponent.h"
#include "MCGameplayUITheme.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Engine/Texture2D.h"
namespace {
UTextBlock* SkillLabel(UWidgetTree* Tree,const UMCGameplayUITheme* ResolvedStyle,int32 Size) {
    auto* Label=Tree->ConstructWidget<UTextBlock>();Label->SetFont(ResolvedStyle->MakeGameplayFont(Size));
    Label->SetColorAndOpacity(ResolvedStyle->Palette.Surface);Label->SetAutoWrapText(true);return Label;
}
}
void UMCSkillLoadoutWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    const auto* ResolvedStyle=GetResolvedTheme();auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Root;
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Row);
    for(int32 Index=0;Index<4;++Index) {
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(112);Size->SetHeightOverride(120);
        auto* SlotState=Row->AddChildToHorizontalBox(Size);SlotState->SetPadding(FMargin(3,0));
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrush(FSlateRoundedBoxBrush(ResolvedStyle->Palette.Marine,12.f,ResolvedStyle->Palette.Mint,1.f));Border->SetPadding(FMargin(8,5));Size->SetContent(Border);
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Column);
        auto* Key=SkillLabel(WidgetTree,ResolvedStyle,15);Key->SetText(FText::AsNumber(Index+1));Key->SetColorAndOpacity(ResolvedStyle->Palette.Mint);Column->AddChildToVerticalBox(Key);
        auto* Name=SkillLabel(WidgetTree,ResolvedStyle,13);Name->SetWrapTextAt(94);Name->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        auto* NameSize=WidgetTree->ConstructWidget<USizeBox>();NameSize->SetHeightOverride(42);NameSize->SetClipping(EWidgetClipping::ClipToBounds);NameSize->SetContent(Name);Column->AddChildToVerticalBox(NameSize);Titles.Add(Name);
        auto* Icon=WidgetTree->ConstructWidget<UImage>();
        auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(24);IconSize->SetHeightOverride(24);IconSize->SetContent(Icon);
        Column->AddChildToVerticalBox(IconSize)->SetHorizontalAlignment(HAlign_Center);Icons.Add(Icon);
        auto* Status=SkillLabel(WidgetTree,ResolvedStyle,12);Status->SetColorAndOpacity(ResolvedStyle->Palette.Mint);Column->AddChildToVerticalBox(Status);StatusLabels.Add(Status);
    }
    auto* PassiveSize=WidgetTree->ConstructWidget<USizeBox>();PassiveSize->SetWidthOverride(472);PassiveSize->SetMaxDesiredHeight(140);
    Root->AddChildToVerticalBox(PassiveSize)->SetPadding(FMargin(3,6,3,0));
    PassiveScroll=WidgetTree->ConstructWidget<UScrollBox>();PassiveSize->SetContent(PassiveScroll);
    Passives=WidgetTree->ConstructWidget<UWrapBox>();Passives->SetInnerSlotPadding(FVector2D(5));Passives->SetExplicitWrapSize(true);Passives->SetWrapSize(460);PassiveScroll->AddChild(Passives);
}
void UMCSkillLoadoutWidget::Refresh(const UMCPerkComponent* Perks)
{
    BuildPresentationTree();if(!Perks || Titles.Num()!=4) return;
    const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
    const bool English=Culture.StartsWith(TEXT("en"));
    auto NameFor=[&](FName ID) {FMCPerkDefinition Row;if(!Perks->GetPerkDefinition(ID,Row)) return FText::FromName(ID);
        return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Row.DisplayNameKey,Row.DisplayName,Culture);};
    for(int32 Index=0;Index<4;++Index) {
        const auto* SlotState=Perks->ActiveSkillSlots.IsValidIndex(Index)?&Perks->ActiveSkillSlots[Index]:nullptr;
        const bool Filled=SlotState && !SlotState->PerkID.IsNone();Titles[Index]->SetText(Filled?NameFor(SlotState->PerkID):Localized(TEXT("UI.Skill.Empty"),English?FText::FromString(TEXT("Empty")):FText::FromString(TEXT("Пусто"))));
        FMCPerkDefinition Definition;UTexture2D* Texture=Filled && Perks->GetPerkDefinition(SlotState->PerkID,Definition)?Definition.Icon.Get():nullptr;
        Icons[Index]->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(Texture) Icons[Index]->SetBrushFromTexture(Texture);
        FText Status;
        if(Filled) {const float Cooldown=Perks->GetSkillCooldownRemaining(Index);
            if(Cooldown>0) Status=FText::Format(Localized(TEXT("UI.Skill.Cooldown"),English?FText::FromString(TEXT("{0} s")):FText::FromString(TEXT("{0} с"))),FText::AsNumber(FMath::CeilToInt(Cooldown)));
            else if(Perks->GetPerkKind(SlotState->PerkID)==EMCPerkKind::Consumable) Status=FText::Format(Localized(TEXT("UI.Skill.Charges"),English?FText::FromString(TEXT("Charges: {0}")):FText::FromString(TEXT("Заряды: {0}"))),FText::AsNumber(SlotState->Charges));
            else Status=Localized(TEXT("UI.Skill.Ready"),English?FText::FromString(TEXT("Ready")):FText::FromString(TEXT("Готово")));
        }
        StatusLabels[Index]->SetText(Status);
    }
    FString Signature=Culture;for(FName ID:Perks->PassivePerkIDs) Signature+=TEXT("|")+ID.ToString();
    if(Signature!=PassiveSignature) {
        PassiveSignature=Signature;Passives->ClearChildren();const auto* ResolvedStyle=GetResolvedTheme();
        for(FName ID:Perks->PassivePerkIDs) {
            auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrush(FSlateRoundedBoxBrush(ResolvedStyle->Palette.Marine,10.f,ResolvedStyle->Palette.Mint,.7f));Border->SetPadding(FMargin(8,4));
            auto* Label=SkillLabel(WidgetTree,ResolvedStyle,13);Label->SetText(NameFor(ID));Border->SetContent(Label);Passives->AddChildToWrapBox(Border);
        }
    }
    PassiveScroll->SetVisibility(Perks->PassivePerkIDs.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
}
