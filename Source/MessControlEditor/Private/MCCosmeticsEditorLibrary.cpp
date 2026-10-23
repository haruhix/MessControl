#include "MCCosmeticsEditorLibrary.h"
#include "MCHubWardrobeWidget.h"
#include "MCCosmeticItemWidget.h"
#include "MCHubShopWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Components/PanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/Border.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

bool UMCCosmeticsEditorLibrary::AddWardrobeCosmeticsPresentation(UWidgetBlueprint* Blueprint,TSubclassOf<UMCCosmeticItemWidget> TileClass)
{
    if(!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !Blueprint || !Blueprint->WidgetTree
        || Blueprint->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_HubWardrobe.WBP_HubWardrobe")
        || Blueprint->ParentClass!=UMCHubWardrobeWidget::StaticClass() || Blueprint->GetPackage()->IsDirty() || !TileClass) return false;
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    // All mutations happen in a copy until the existing controls and new tree are verified.
    TStrongObjectPtr<UWidgetTree> Original(Blueprint->WidgetTree.Get());
    TStrongObjectPtr<UWidgetTree> Working(DuplicateObject<UWidgetTree>(Original.Get(),Blueprint,
        MakeUniqueObjectName(Blueprint,UWidgetTree::StaticClass(),TEXT("WardrobeCosmeticsWorking"))));
    if(!UMCHubWardrobeWidget::AddCosmeticsPresentation(Working.Get())) return false;
    bool bPreserved=true;
    Original->ForEachWidget([&](UWidget* Widget) {
        auto* Copy=Working->FindWidget(Widget->GetFName());if(!Copy || Copy->GetClass()!=Widget->GetClass()) bPreserved=false;
    });
    if(!bPreserved || !Working->FindWidget(TEXT("WardrobeColorPalette")) || !Working->FindWidget(TEXT("WardrobeHueStrip"))
        || !Working->FindWidget(TEXT("WardrobeDefaultSlot")) || !Working->FindWidget(TEXT("WardrobeEmptySlot5"))) return false;
    const auto PreviousGuids=Blueprint->WidgetVariableNameToGuidMap;
    Blueprint->Modify();Original->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    Working->Rename(TEXT("WidgetTree"),Blueprint,REN_DontCreateRedirectors|REN_NonTransactional);Blueprint->WidgetTree=Working.Get();
    Working->ClearFlags(RF_Transient);Working->SetFlags(RF_Transactional);
    Working->ForEachWidget([Blueprint](UWidget* Widget) {
        Widget->ClearFlags(RF_Transient);Widget->SetFlags(RF_Transactional);
        if(Widget->Slot) {Widget->Slot->ClearFlags(RF_Transient);Widget->Slot->SetFlags(RF_Transactional);}
        if(!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) Blueprint->OnVariableAdded(Widget->GetFName());
    });
    auto Rollback=[&] {
        Working->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        Original->Rename(TEXT("WidgetTree"),Blueprint,REN_DontCreateRedirectors|REN_NonTransactional);Blueprint->WidgetTree=Original.Get();
        Blueprint->WidgetVariableNameToGuidMap=PreviousGuids;FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    };
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) {Rollback();return false;}
    auto* Defaults=Cast<UMCHubWardrobeWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Defaults) {Rollback();return false;}
    Defaults->Modify();Defaults->bUseNativeLayout=false;Defaults->CosmeticTileClass=TileClass;
    if(!Defaults->TextBindings.Contains(TEXT("WardrobeFaceHeading"))) Defaults->TextBindings.Add(TEXT("WardrobeFaceHeading"),TEXT("UI.Wardrobe.Face"));
    if(!Defaults->TextBindings.Contains(TEXT("WardrobeHatHeading"))) Defaults->TextBindings.Add(TEXT("WardrobeHatHeading"),TEXT("UI.Wardrobe.Hat"));
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    Blueprint->MarkPackageDirty();
    if(!Assets->SaveLoadedAsset(Blueprint,false)) {Rollback();return false;}
    UE_LOG(LogTemp,Display,TEXT("MC_COSMETICS_WARDROBE_SAVED Preserved palette/skin slots/graphs; added Face/Hat and fixed-footer scroll body"));
    return true;
}

bool UMCCosmeticsEditorLibrary::RefreshHubProductCards(UWidgetBlueprint* Card,UWidgetBlueprint* Wardrobe,UWidgetBlueprint* Shop)
{
    if(!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !Card || !Wardrobe || !Shop) return false;
    if(Card->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_WardrobeCosmeticTile.WBP_WardrobeCosmeticTile")
        || Wardrobe->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_HubWardrobe.WBP_HubWardrobe")
        || Shop->GetPathName()!=TEXT("/Game/Gameplay/UI/WBP_HubShop.WBP_HubShop")
        || Card->ParentClass!=UMCCosmeticItemWidget::StaticClass() || Wardrobe->ParentClass!=UMCHubWardrobeWidget::StaticClass()
        || Shop->ParentClass!=UMCHubShopWidget::StaticClass()) return false;
    for(auto* Blueprint:{Card,Wardrobe,Shop}) if(Blueprint->GetPackage()->IsDirty() || !Blueprint->WidgetTree || !Blueprint->GeneratedClass) return false;
    if(!Wardrobe->WidgetTree->FindWidget(TEXT("WardrobeColorPalette")) || !Wardrobe->WidgetTree->FindWidget(TEXT("WardrobeHueStrip"))
        || !Wardrobe->WidgetTree->FindWidget(TEXT("WardrobeDefaultSlot")) || !Wardrobe->WidgetTree->FindWidget(TEXT("WardrobeEmptySlot5"))) return false;
    for(FName Name:{FName(TEXT("WardrobeFaceItems")),FName(TEXT("WardrobeHatItems"))})
        if(!Wardrobe->WidgetTree->FindWidget<UUniformGridPanel>(Name)) return false;
    if(!Shop->WidgetTree->FindWidget<UUniformGridPanel>(TEXT("ShopCatalogGrid")) || !Shop->WidgetTree->FindWidget<USizeBox>(TEXT("ShopCatalogHost"))) return false;
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();if(!Assets) return false;
    TStrongObjectPtr<UWidgetBlueprint> CardGuard(Card),WardrobeGuard(Wardrobe),ShopGuard(Shop);
    // Only the old two-label native seed is eligible for replacement. Once upgraded, artists own the tree.
    if(!Card->WidgetTree->FindWidget(TEXT("CosmeticCardSurface"))) {
        const TSet<FName> LegacyNames={TEXT("CosmeticCardSize"),TEXT("CosmeticButton"),TEXT("CosmeticCardContent"),TEXT("CosmeticName"),TEXT("CosmeticState")};
        bool bLegacyOnly=true;Card->WidgetTree->ForEachWidget([&](UWidget* Widget) {if(!LegacyNames.Contains(Widget->GetFName())) bLegacyOnly=false;});
        if(!Card->WidgetTree->FindWidget(TEXT("CosmeticCardSize")) || !Card->WidgetTree->FindWidget(TEXT("CosmeticButton"))
            || !Card->WidgetTree->FindWidget(TEXT("CosmeticName")) || !Card->WidgetTree->FindWidget(TEXT("CosmeticState"))
            || !bLegacyOnly || Card->Animations.Num()>0) return false;
        TStrongObjectPtr<UMCCosmeticItemWidget> Source(NewObject<UMCCosmeticItemWidget>(GetTransientPackage()));
        Source->SetDesignerFlags(EWidgetDesignFlags::Designing);Source->Initialize();Source->BuildPresentationTree();
        if(!Source->WidgetTree || !Source->WidgetTree->FindWidget(TEXT("CosmeticCardSurface"))) return false;
        Card->Modify();Card->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        Card->WidgetTree=DuplicateObject<UWidgetTree>(Source->WidgetTree,Card,TEXT("WidgetTree"));
        Card->WidgetTree->ClearFlags(RF_Transient);Card->WidgetTree->SetFlags(RF_Transactional);
        Card->WidgetTree->ForEachWidget([Card](UWidget* Widget) {
            Widget->ClearFlags(RF_Transient);Widget->SetFlags(RF_Transactional);
            if(Widget->Slot) {Widget->Slot->ClearFlags(RF_Transient);Widget->Slot->SetFlags(RF_Transactional);}
            if(!Card->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) Card->OnVariableAdded(Widget->GetFName());
        });
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Card);FKismetEditorUtilities::CompileBlueprint(Card);
        if(Card->Status==BS_Error || !Card->GeneratedClass || !Assets->SaveLoadedAsset(Card,false)) return false;
    }
    // Compact the known card controls in place; retain their variable names and artist graph bindings.
    Card->Modify();
    auto* CardSize=Card->WidgetTree->FindWidget<USizeBox>(TEXT("CosmeticCardSize"));
    auto* ThumbSize=Card->WidgetTree->FindWidget<USizeBox>(TEXT("CosmeticThumbnailSize"));
    auto* BuySize=Card->WidgetTree->FindWidget<USizeBox>(TEXT("CosmeticBuySize"));
    auto* Surface=Card->WidgetTree->FindWidget<UBorder>(TEXT("CosmeticCardSurface"));
    auto* ThumbFrame=Card->WidgetTree->FindWidget<UBorder>(TEXT("CosmeticThumbnailFrame"));
    if(!CardSize || !ThumbSize || !BuySize || !Surface || !ThumbFrame) return false;
    CardSize->Modify();CardSize->SetMinDesiredHeight(180);Surface->Modify();Surface->SetPadding(FMargin(8));
    ThumbSize->Modify();ThumbSize->SetHeightOverride(64);ThumbFrame->Modify();ThumbFrame->SetPadding(FMargin(4));
    BuySize->Modify();BuySize->SetHeightOverride(28);
    if(auto* ThumbRow=Cast<UVerticalBoxSlot>(ThumbSize->Slot)) {ThumbRow->Modify();ThumbRow->SetPadding(FMargin(0,0,0,4));}
    if(auto* BuyRow=Cast<UVerticalBoxSlot>(BuySize->Slot)) {BuyRow->Modify();BuyRow->SetPadding(FMargin(0,4,0,0));}
    for(FName Name:{FName(TEXT("CosmeticName")),FName(TEXT("CosmeticDescription")),FName(TEXT("CosmeticState"))}) {
        auto* Text=Card->WidgetTree->FindWidget<UTextBlock>(Name);if(!Text) return false;
        auto* TextSize=Cast<USizeBox>(Text->GetParent());if(!TextSize) return false;
        TextSize->Modify();
        if(Name==TEXT("CosmeticName")) TextSize->SetMinDesiredHeight(20);
        if(Name==TEXT("CosmeticState")) TextSize->SetMinDesiredHeight(16);
        if(auto* TextRow=Cast<UVerticalBoxSlot>(TextSize->Slot)) {TextRow->Modify();TextRow->SetPadding(FMargin(1,0,1,2));}
    }
    if(auto* Fit=Card->WidgetTree->FindWidget<UScaleBox>(TEXT("CosmeticThumbnailFit"))) {Fit->Modify();Fit->SetStretch(EStretch::ScaleToFit);}
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Card);FKismetEditorUtilities::CompileBlueprint(Card);
    if(Card->Status==BS_Error || !Card->GeneratedClass) return false;
    auto* CardDefaults=Cast<UMCCosmeticItemWidget>(Card->GeneratedClass->GetDefaultObject());if(!CardDefaults) return false;
    CardDefaults->Modify();
    // Full nonzero-alpha bounds measured from the real 512px renders, expanded by 8px on every side.
    CardDefaults->ThumbnailUVRegions.Add(TEXT("T_Face_SportGlasses"),FVector4(27./512.,203./512.,492./512.,367./512.));
    CardDefaults->ThumbnailUVRegions.Add(TEXT("T_Hat_WorkCap"),FVector4(37./512.,173./512.,475./512.,339./512.));
    CardDefaults->ThumbnailUVRegions.Add(TEXT("T_Hat_WorkCapRed"),FVector4(37./512.,173./512.,475./512.,339./512.));
    CastChecked<UWidgetBlueprintGeneratedClass>(Card->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    Card->MarkPackageDirty();if(!Assets->SaveLoadedAsset(Card,false)) return false;
    const FName WardrobeGrids[]={TEXT("WardrobeFaceItems"),TEXT("WardrobeHatItems")};
    Wardrobe->Modify();Shop->Modify();
    for(FName Name:WardrobeGrids) {
        auto* Grid=Wardrobe->WidgetTree->FindWidget<UUniformGridPanel>(Name);if(!Grid) return false;
        Grid->Modify();Grid->SetMinDesiredSlotWidth(184);Grid->SetMinDesiredSlotHeight(180);Grid->SetSlotPadding(FMargin(4));
    }
    auto* ShopGrid=Shop->WidgetTree->FindWidget<UUniformGridPanel>(TEXT("ShopCatalogGrid"));
    auto* Host=Shop->WidgetTree->FindWidget<USizeBox>(TEXT("ShopCatalogHost"));if(!ShopGrid || !Host) return false;
    ShopGrid->Modify();ShopGrid->SetMinDesiredSlotWidth(184);ShopGrid->SetMinDesiredSlotHeight(180);ShopGrid->SetSlotPadding(FMargin(4));
    Host->Modify();Host->SetHeightOverride(308);
    // Put actual accessories before the currently empty skin slots, preserving every widget and its layout.
    auto* Body=Wardrobe->WidgetTree->FindWidget<UVerticalBox>(TEXT("WardrobeBody"));
    auto* Cosmetics=Wardrobe->WidgetTree->FindWidget(TEXT("WardrobeCosmeticsPanel"));
    if(!Body || !Cosmetics || Cosmetics->GetParent()!=Body) return false;
    const auto BodyChildren=Body->GetAllChildren();
    struct FBodyLayout {UWidget* Child;FMargin RowPadding;FSlateChildSize RowSize;EHorizontalAlignment H;EVerticalAlignment V;};
    TArray<FBodyLayout> BodyLayouts;
    for(auto* Child:BodyChildren) {
        const auto* Row=Cast<UVerticalBoxSlot>(Child->Slot);if(!Row) return false;
        BodyLayouts.Add({Child,Row->GetPadding(),Row->GetSize(),Row->GetHorizontalAlignment(),Row->GetVerticalAlignment()});
    }
    BodyLayouts.StableSort([Cosmetics](const FBodyLayout& A,const FBodyLayout& B) {return A.Child==Cosmetics && B.Child!=Cosmetics;});
    Body->Modify();Body->ClearChildren();
    for(const auto& Layout:BodyLayouts) {
        auto* Row=Body->AddChildToVerticalBox(Layout.Child);Row->SetPadding(Layout.RowPadding);Row->SetSize(Layout.RowSize);Row->SetHorizontalAlignment(Layout.H);Row->SetVerticalAlignment(Layout.V);
    }
    for(auto* Blueprint:{Wardrobe,Shop}) {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) return false;
    }
    auto* WardrobeDefaults=Cast<UMCHubWardrobeWidget>(Wardrobe->GeneratedClass->GetDefaultObject());
    auto* ShopDefaults=Cast<UMCHubShopWidget>(Shop->GeneratedClass->GetDefaultObject());
    if(!WardrobeDefaults || !ShopDefaults) return false;
    WardrobeDefaults->Modify();WardrobeDefaults->CosmeticTileClass=Card->GeneratedClass;
    ShopDefaults->Modify();ShopDefaults->ProductCardClass=Card->GeneratedClass;
    for(auto* Blueprint:{Wardrobe,Shop}) {
        CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
        Blueprint->MarkPackageDirty();if(!Assets->SaveLoadedAsset(Blueprint,false)) return false;
    }
    UE_LOG(LogTemp,Display,TEXT("MC_HUB_PRODUCT_CARDS_SAVED Editable shared card; two-column grids; palette/footer/panel/graphs retained"));
    return true;
}
