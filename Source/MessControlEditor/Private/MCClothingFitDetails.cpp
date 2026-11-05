#include "MCClothingFitDetails.h"
#include "SMCClothingFitViewport.h"
#include "MCCosmeticCatalog.h"
#include "MCDataAssets.h"

#include "AssetRegistry/AssetData.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MCClothingFitEditor"
namespace MCClothingFitDetailsPrivate
{
    class FCustomization final : public IDetailCustomization
    {
    public:
        static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FCustomization>(); }
        virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
        {
            TArray<TSharedPtr<FStructOnScope>> Scopes;Builder.GetStructsBeingCustomized(Scopes);
            if(Scopes.Num()!=1 || !Scopes[0] || Scopes[0]->GetStruct()!=FMCCosmeticCatalogRow::StaticStruct()) return;
            // Normal row selection recreates this root customization with the selected live scope.
            // Existing property rows/transactions remain owned by the native DataTable Row Editor.
            auto View=SNew(SMCClothingFitViewport).RowScope(Scopes[0]);
            const TWeakPtr<SMCClothingFitViewport> WeakView=View;
            auto& Category=Builder.EditCategory(TEXT("Fitting Preview"),LOCTEXT("Category","Fitting Preview"),ECategoryPriority::Important);
            Category.AddCustomRow(LOCTEXT("Search","Clothing body rig fitting preview mesh icon price"))
                .WholeRowContent()
                [SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
                    [SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("PreviewNote","Reference-pose fit on the actual character rig. Edit mesh, attachment and materials in the row below; unsaved changes update this preview."))]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                    [SNew(SObjectPropertyEntryBox).AllowedClass(UMCPlayerAppearance::StaticClass()).AllowClear(true)
                        .ObjectPath_Lambda([WeakView](){const auto Pinned=WeakView.Pin();return Pinned?Pinned->GetAppearancePath():FString();})
                        .OnObjectChanged_Lambda([WeakView](const FAssetData& Data){if(const auto Pinned=WeakView.Pin()) Pinned->SetAppearance(Cast<UMCPlayerAppearance>(Data.GetAsset()));})]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                    [SNew(SWrapBox).UseAllottedSize(true)
                        +SWrapBox::Slot()[SNew(SButton).Text(LOCTEXT("Frame","Frame"))
                            .OnClicked_Lambda([WeakView](){if(const auto Pinned=WeakView.Pin()) Pinned->Focus();return FReply::Handled();})]
                        +SWrapBox::Slot()[SNew(SButton).Text(LOCTEXT("Front","Front"))
                            .OnClicked_Lambda([WeakView](){if(const auto Pinned=WeakView.Pin()) Pinned->SetViewPreset(0);return FReply::Handled();})]
                        +SWrapBox::Slot()[SNew(SButton).Text(LOCTEXT("Side","Side"))
                            .OnClicked_Lambda([WeakView](){if(const auto Pinned=WeakView.Pin()) Pinned->SetViewPreset(1);return FReply::Handled();})]
                        +SWrapBox::Slot()[SNew(SButton).Text(LOCTEXT("Back","Back"))
                            .OnClicked_Lambda([WeakView](){if(const auto Pinned=WeakView.Pin()) Pinned->SetViewPreset(2);return FReply::Handled();})]]
                    +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(360).MinDesiredWidth(240)[View]]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                    [SNew(STextBlock).AutoWrapText(true).Text_Lambda([WeakView](){const auto Pinned=WeakView.Pin();return Pinned?Pinned->GetDiagnosticsText():FText::GetEmpty();})]
                ];
        }
    };
    bool bRegistered=false;
}
void MCClothingFitDetails::Register()
{
    if(MCClothingFitDetailsPrivate::bRegistered || IsRunningCommandlet()) return;
    auto& Properties=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    Properties.RegisterCustomClassLayout(FMCCosmeticCatalogRow::StaticStruct()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(&MCClothingFitDetailsPrivate::FCustomization::MakeInstance));
    Properties.NotifyCustomizationModuleChanged();MCClothingFitDetailsPrivate::bRegistered=true;
}
void MCClothingFitDetails::Unregister()
{
    if(!MCClothingFitDetailsPrivate::bRegistered) return;
    if(FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor"))) {
        auto& Properties=FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        Properties.UnregisterCustomClassLayout(FMCCosmeticCatalogRow::StaticStruct()->GetFName());
        Properties.NotifyCustomizationModuleChanged();
    }
    MCClothingFitDetailsPrivate::bRegistered=false;
}
#undef LOCTEXT_NAMESPACE
