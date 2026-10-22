#include "MCMainMenuWidget.h"

#include "MCSteamSessionSubsystem.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

void UMCHubPlayStatusWidget::BuildPresentationTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Border=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubPlayStatusBorder"));
    Border->SetPadding(FMargin(18,12));Border->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,10.f));
    WidgetTree->RootWidget=Border;
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubPlayStatusMessage"));
    Label->SetAutoWrapText(true);Label->SetWrapTextAt(600.f);Label->SetJustification(ETextJustify::Center);
    Border->SetContent(Label);
    BindStyle(TEXT("HubPlayStatusBorder"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    BindStyle(TEXT("HubPlayStatusMessage"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,20,true);
    Border->SetVisibility(ESlateVisibility::Collapsed);SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMCHubPlayStatusWidget::ApplyEditorPreview(float Time)
{
    if(!IsDesignTime()) return;
    BuildPresentationTree();
    if(auto* Label=WidgetTree?WidgetTree->FindWidget<UTextBlock>(TEXT("HubPlayStatusMessage")):nullptr)
        Label->SetText(PreviewState==TEXT("Failed")
            ?Localized(TEXT("UI.Connection.Status.CreateFailed"),NSLOCTEXT("MCHubPlayStatus","CreateFailed","Не удалось создать комнату. Проверь подключение Steam и попробуй снова."))
            :Localized(TEXT("UI.Connection.Status.Creating"),NSLOCTEXT("MCHubPlayStatus","Creating","Создаём комнату Steam…")));
    if(auto* Border=WidgetTree?WidgetTree->FindWidget<UBorder>(TEXT("HubPlayStatusBorder")):nullptr) Border->SetVisibility(ESlateVisibility::HitTestInvisible);
    SetVisibility(ESlateVisibility::HitTestInvisible);ApplyTheme();
}

void UMCHubPlayStatusWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(IsDesignTime()) return;
    RefreshElapsed+=DeltaSeconds;if(RefreshElapsed<.1f) return;RefreshElapsed=0.f;
    auto* Instance=GetGameInstance();auto* Sessions=Instance?Instance->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;
    const bool Show=Sessions && (Sessions->IsBusy() || Sessions->HadFailure());
    // Keep this widget tickable while its inner status card is hidden. A
    // collapsed UserWidget would never see a later asynchronous host failure.
    auto* Border=WidgetTree?WidgetTree->FindWidget<UBorder>(TEXT("HubPlayStatusBorder")):nullptr;
    if(!Border) return;
    Border->SetVisibility(Show?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    if(!Show) return;
    FText Text=FText::FromString(Sessions->GetStatus());
    UDataTable* Table=GetResolvedTranslationTable();
    if(Table && Table->GetRowStruct()==FMCUILocalizationRow::StaticStruct())
        for(const auto& Entry:Table->GetRowMap())
        {
            if(!Entry.Key.ToString().StartsWith(TEXT("UI.Connection.Status."))) continue;
            const auto* Row=reinterpret_cast<const FMCUILocalizationRow*>(Entry.Value);
            const FString* Native=Row?FTextInspector::GetSourceString(Row->SourceText):nullptr;
            if(Row && (Native?*Native:Row->SourceText.ToString())==Sessions->GetStatus()) {
                Text=Localized(Entry.Key,Text);break;
            }
        }
    if(auto* Label=WidgetTree->FindWidget<UTextBlock>(TEXT("HubPlayStatusMessage")))
        if(!Label->GetText().EqualTo(Text)) Label->SetText(Text);
}
