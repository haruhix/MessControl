#include "MCWorldLabelLocalization.h"
#include "MCWorldLabelPresenterComponent.h"
#include "MCWorldLabelWidget.h"
#include "MCGameplayUITheme.h"
#include "MCUIDataLibrary.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"

UDataTable* MCWorldLabelLocalization::ResidentTable(const UTextRenderComponent* SourceLabel)
{
    if(!IsValid(SourceLabel) || !SourceLabel->GetOwner()) return nullptr;
    TInlineComponentArray<UMCWorldLabelPresenterComponent*> Presenters(SourceLabel->GetOwner());
    for(const auto* Presenter:Presenters) {
        if(!IsValid(Presenter) || Presenter->SourceText.Get()!=SourceLabel) continue;
        if(const auto* View=Cast<UMCWorldLabelWidget>(Presenter->GetUserWidgetObject()))
            return View->GetResolvedTranslationTable(); // Explicit view override, then its resident theme table; no load.
        const auto* ResidentTheme=Presenter->PresentationTheme.Get();if(!ResidentTheme) return nullptr;
        if(const auto* Role=ResidentTheme->WidgetClasses.Find(TEXT("WorldLabel"));Role && !Role->IsNull()) {
            UClass* ResidentClass=Role->Get();
            if(!ResidentClass || !ResidentClass->IsChildOf(UMCWorldLabelWidget::StaticClass())) return nullptr;
            // false means lookup only: never construct a CDO from a first grenade/caption.
            const auto* Defaults=Cast<UMCWorldLabelWidget>(ResidentClass->GetDefaultObject(false));
            if(!Defaults) return nullptr;
            if(IsValid(Defaults->TranslationTable.Get())) return Defaults->TranslationTable.Get();
        }
        return ResidentTheme->LocalizationTable.Get();
    }
    return nullptr; // Original native fallback is refreshed when the presenter/table becomes resident.
}
FText MCWorldLabelLocalization::Resolve(const UTextRenderComponent* SourceLabel,FName Key,const FText& Fallback)
{
    return UMCUIDataLibrary::ResolveText(ResidentTable(SourceLabel),Key,Fallback);
}
FText MCWorldLabelLocalization::Format(const UTextRenderComponent* SourceLabel,FName Key,const FText& Fallback,const FFormatNamedArguments& ActualArguments)
{
    return UMCUIDataLibrary::FormatTextForCulture(Resolve(SourceLabel,Key,Fallback),ActualArguments);
}

void MCWorldLabelLocalization::Apply(UTextRenderComponent* SourceLabel,const FText& ActualCopy)
{
    if(!IsValid(SourceLabel)) return;
    SourceLabel->SetText(ActualCopy);
    if(!SourceLabel->GetOwner()) return;
    TInlineComponentArray<UMCWorldLabelPresenterComponent*> Presenters(SourceLabel->GetOwner());
    for(auto* Presenter:Presenters) if(IsValid(Presenter) && Presenter->SourceText.Get()==SourceLabel)
        Presenter->RecordNativeSourceCopy(SourceLabel->Text);
}
