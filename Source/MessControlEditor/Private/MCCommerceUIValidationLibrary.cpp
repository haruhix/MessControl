#include "MCCommerceUIValidationLibrary.h"

#include "MCCosmeticItemWidget.h"
#include "MCHubEconomyComponent.h"
#include "MCHubShopWidget.h"
#include "MCHubWardrobeWidget.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCUIGridScrollBox.h"
#include "MCUnifiedShopWidget.h"
#include "MCUnifiedUIWidget.h"
#include "MCUnifiedWardrobeWidget.h"
#include "MCUniversalUIWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

namespace MCCommerceUIValidationPrivate
{
    using FObject=TSharedRef<FJsonObject>;
    FString Encode(const FObject& Value) {FString Output;FJsonSerializer::Serialize(Value,TJsonWriterFactory<>::Create(&Output));return Output;}
    FString ValidationError(const FString& Message) {auto Value=MakeShared<FJsonObject>();Value->SetBoolField(TEXT("valid"),false);Value->SetStringField(TEXT("error"),Message);return Encode(Value);}
    FString Path(const UObject* Object) {return Object?Object->GetPathName():FString();}
    TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values) {TArray<TSharedPtr<FJsonValue>> Result;for(double Value:Values) Result.Add(MakeShared<FJsonValueNumber>(Value));return Result;}
    TArray<TSharedPtr<FJsonValue>> Names(const TArray<FName>& Values) {TArray<TSharedPtr<FJsonValue>> Result;for(FName Value:Values) Result.Add(MakeShared<FJsonValueString>(Value.ToString()));return Result;}
    TArray<TSharedPtr<FJsonValue>> Loadout(const FMCCharacterCosmeticLoadout& Value) {
        TArray<TSharedPtr<FJsonValue>> Result;
        for(const auto& Entry:Value.Slots) {auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("slot"),Entry.Slot.ToString());Item->SetStringField(TEXT("itemId"),Entry.ItemId.ToString());Result.Add(MakeShared<FJsonValueObject>(Item));}return Result;
    }
    UTextBlock* Caption(UWidget* Widget) {
        if(auto* Text=Cast<UTextBlock>(Widget)) return Text;
        if(auto* Shape=Cast<UMCUniversalUIWidget>(Widget)) return Shape->WidgetTree?Shape->WidgetTree->FindWidget<UTextBlock>(TEXT("UniversalLabel")):nullptr;
        if(auto* Button=Cast<UButton>(Widget)) return Cast<UTextBlock>(Button->GetContent());
        return nullptr;
    }
    FObject Geometry(UWidget* Widget,const FGeometry& RootGeometry) {
        auto Value=MakeShared<FJsonObject>();Value->SetStringField(TEXT("path"),Path(Widget));Value->SetBoolField(TEXT("exists"),Widget!=nullptr);if(!Widget) return Value;
        const auto& Paint=Widget->GetPaintSpaceGeometry();const FVector2D LocalSize=Paint.GetLocalSize();
        const FVector2D AbsoluteMin=Paint.LocalToAbsolute(FVector2D::ZeroVector),AbsoluteMax=Paint.LocalToAbsolute(LocalSize);
        const FVector2D RootMin=RootGeometry.AbsoluteToLocal(AbsoluteMin),RootMax=RootGeometry.AbsoluteToLocal(AbsoluteMax);
        Value->SetArrayField(TEXT("paintAbsoluteRect"),Numbers({AbsoluteMin.X,AbsoluteMin.Y,AbsoluteMax.X-AbsoluteMin.X,AbsoluteMax.Y-AbsoluteMin.Y}));
        Value->SetArrayField(TEXT("rootLocalRect"),Numbers({RootMin.X,RootMin.Y,RootMax.X-RootMin.X,RootMax.Y-RootMin.Y}));
        Value->SetArrayField(TEXT("localSize"),Numbers({LocalSize.X,LocalSize.Y}));Value->SetBoolField(TEXT("visible"),Widget->IsVisible());Value->SetBoolField(TEXT("enabled"),Widget->GetIsEnabled());Value->SetNumberField(TEXT("renderOpacity"),Widget->GetRenderOpacity());
        if(const auto* Label=Caption(Widget)) {Value->SetStringField(TEXT("caption"),Label->GetText().ToString());Value->SetNumberField(TEXT("fontSize"),Label->GetFont().Size);}
        return Value;
    }
    FObject Scroll(UMCUIGridScrollBox* Widget,const FGeometry& RootGeometry) {
        auto Value=Geometry(Widget,RootGeometry);if(!Widget) return Value;
        Value->SetNumberField(TEXT("rowStride"),Widget->GetRowStride());Value->SetNumberField(TEXT("offset"),Widget->GetScrollOffset());Value->SetNumberField(TEXT("firstVisibleRow"),Widget->GetFirstVisibleRow());Value->SetNumberField(TEXT("endOffset"),Widget->GetScrollOffsetOfEnd());
        const float Height=Widget->GetPaintSpaceGeometry().GetLocalSize().Y,Stride=Widget->GetRowStride();
        Value->SetNumberField(TEXT("viewportHeight"),Height);Value->SetNumberField(TEXT("rowsInViewport"),Stride>0?Height/Stride:0);Value->SetBoolField(TEXT("offsetAligned"),Stride>0 && FMath::IsNearlyEqual(Widget->GetScrollOffset()/Stride,float(FMath::RoundToInt(Widget->GetScrollOffset()/Stride)),.001f));return Value;
    }
    template<typename T> T* ExactControllerWidget(AMCPlayerController* Controller,FName PropertyName) {
        const auto* Property=FindFProperty<FObjectPropertyBase>(Controller->GetClass(),PropertyName);
        if(!Property || !Property->PropertyClass->IsChildOf(T::StaticClass())) return nullptr;
        // Read-only reflection of the exact existing controller-owned field.
        return Cast<T>(Property->GetObjectPropertyValue_InContainer(Controller));
    }
    struct FContext {
        AMCPlayerController* Controller=nullptr;UMCUnifiedUIWidget* Root=nullptr;UMCHubWardrobeWidget* Wardrobe=nullptr;UMCHubShopWidget* Shop=nullptr;UMCHubEconomyComponent* Economy=nullptr;int32 VisibleRootCount=0;
    };
    bool Resolve(UWorld* World,FContext& Context,FString& Why) {
        if(!IsInGameThread() || !GEditor || !IsValid(World) || World->WorldType!=EWorldType::PIE || World->GetNetMode()!=NM_Standalone || World->bIsTearingDown || GEditor->PlayWorld!=World) {Why=TEXT("Expected the actual current standalone PIE world");return false;}
        Context.Controller=Cast<AMCPlayerController>(World->GetFirstPlayerController());
        if(World->GetNumPlayerControllers()!=1 || !Context.Controller || !Context.Controller->IsLocalController() || Context.Controller->GetWorld()!=World) {Why=TEXT("Expected exactly one actual local gameplay controller in this world");return false;}
        Context.Wardrobe=ExactControllerWidget<UMCHubWardrobeWidget>(Context.Controller,TEXT("WardrobeWidget"));Context.Shop=ExactControllerWidget<UMCHubShopWidget>(Context.Controller,TEXT("HubShopWidget"));
        if(const auto* State=Context.Controller->GetPlayerState<AMCPlayerState>()) Context.Economy=State->Economy;
        TArray<UUserWidget*> TopLevel;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,TopLevel,UMCUnifiedUIWidget::StaticClass(),true);
        for(UUserWidget* Candidate:TopLevel) if(auto* Root=Cast<UMCUnifiedUIWidget>(Candidate);Root && Root->GetWorld()==World && Root->GetOwningPlayer()==Context.Controller && Root->IsInViewport() && Root->IsVisible()) {Context.Root=Root;++Context.VisibleRootCount;}
        if(Context.VisibleRootCount!=1) Context.Root=nullptr;
        return true;
    }
    FObject SourceCard(UMCCosmeticItemWidget* Card,const FGeometry& RootGeometry) {
        auto Value=Geometry(Card,RootGeometry);if(!Card) return Value;
        Value->SetStringField(TEXT("itemId"),Card->GetItemId().ToString());Value->SetStringField(TEXT("slot"),Card->GetItemSlot().ToString());Value->SetStringField(TEXT("catalogId"),Card->GetCatalogId().ToString());
        if(Card->WidgetTree) {
            const TCHAR* NodeNames[]={TEXT("CosmeticName"),TEXT("CosmeticDescription"),TEXT("CosmeticPrice"),TEXT("CosmeticState")};const TCHAR* Keys[]={TEXT("name"),TEXT("description"),TEXT("price"),TEXT("state")};
            for(int32 Index=0;Index<4;++Index) if(const auto* Label=Card->WidgetTree->FindWidget<UTextBlock>(NodeNames[Index])) Value->SetStringField(Keys[Index],Label->GetText().ToString());
            if(const auto* Image=Card->WidgetTree->FindWidget<UImage>(TEXT("CosmeticThumbnail"))) Value->SetStringField(TEXT("icon"),Path(Image->GetBrush().GetResourceObject()));
        }return Value;
    }
    bool SourceOwned(const UUserWidget* Source,const FContext& Context,UWorld* World) {return Source && Source->GetWorld()==World && Source->GetOwningPlayer()==Context.Controller;}
    FObject View(UUserWidget* Widget,const FGeometry& RootGeometry,const TArray<FName>& Nodes,FName ScrollName) {
        auto Value=Geometry(Widget,RootGeometry);if(!Widget || !Widget->WidgetTree) return Value;
        auto Named=MakeShared<FJsonObject>();for(FName Name:Nodes) Named->SetObjectField(Name.ToString(),Geometry(Widget->WidgetTree->FindWidget(Name),RootGeometry));Value->SetObjectField(TEXT("nodes"),Named);
        Value->SetObjectField(TEXT("gridScroll"),Scroll(Widget->WidgetTree->FindWidget<UMCUIGridScrollBox>(ScrollName),RootGeometry));return Value;
    }
}

FString UMCCommerceUIValidationLibrary::InspectPIECommerce(UWorld* World)
{
    using namespace MCCommerceUIValidationPrivate;
    FContext Context;FString Why;if(!Resolve(World,Context,Why)) return ValidationError(Why);
    auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("valid"),true);Result->SetStringField(TEXT("scope"),TEXT("Read-only current controller-owned source widgets and unique visible top-level New root; no global card object enumeration or refresh/mutation."));
    Result->SetStringField(TEXT("world"),Path(World));Result->SetStringField(TEXT("controller"),Path(Context.Controller));Result->SetBoolField(TEXT("worldReady"),Context.Controller->IsWorldPresentationReady());
    Result->SetBoolField(TEXT("wardrobeOpen"),Context.Controller->IsWardrobeOpen());Result->SetBoolField(TEXT("shopOpen"),Context.Controller->IsHubShopOpen());Result->SetNumberField(TEXT("visibleNewRootCount"),Context.VisibleRootCount);
    Result->SetStringField(TEXT("root"),Path(Context.Root));Result->SetStringField(TEXT("activeScreen"),Context.Root?StaticEnum<EMCUnifiedUIScreen>()->GetNameStringByValue(int64(Context.Root->GetActiveScreen())):FString());
    const FGeometry RootGeometry=Context.Root?Context.Root->GetPaintSpaceGeometry():FGeometry();
    Result->SetArrayField(TEXT("rootLocalSize"),Numbers({RootGeometry.GetLocalSize().X,RootGeometry.GetLocalSize().Y}));Result->SetNumberField(TEXT("rootScale"),RootGeometry.GetAccumulatedLayoutTransform().GetScale());
    if(Context.Economy) {Result->SetStringField(TEXT("economy"),Path(Context.Economy));Result->SetNumberField(TEXT("coins"),Context.Economy->Coins);Result->SetArrayField(TEXT("ownedCosmetics"),Names(Context.Economy->OwnedCosmetics));Result->SetArrayField(TEXT("equippedLoadout"),Loadout(Context.Economy->EquippedLoadout));}
    auto Wardrobe=MakeShared<FJsonObject>();Wardrobe->SetStringField(TEXT("source"),Path(Context.Wardrobe));const bool WardrobeOwned=SourceOwned(Context.Wardrobe,Context,World);Wardrobe->SetBoolField(TEXT("sourceOwnedByCurrentController"),WardrobeOwned);
    TArray<TSharedPtr<FJsonValue>> WardrobeCards;
    if(WardrobeOwned) {
        Wardrobe->SetBoolField(TEXT("sourceInViewport"),Context.Wardrobe->IsInViewport());Wardrobe->SetBoolField(TEXT("sourceVisible"),Context.Wardrobe->IsVisible());Wardrobe->SetArrayField(TEXT("draftLoadout"),Loadout(Context.Wardrobe->GetDraftLoadout()));Wardrobe->SetBoolField(TEXT("economyMatchesPlayerState"),Context.Wardrobe->GetUnifiedEconomy()==Context.Economy);
        for(UMCCosmeticItemWidget* Card:Context.Wardrobe->GetUnifiedCosmeticTiles()) if(Card && Card->GetWorld()==World && Card->GetOwningPlayer()==Context.Controller) WardrobeCards.Add(MakeShared<FJsonValueObject>(SourceCard(Card,RootGeometry)));
    }
    Wardrobe->SetNumberField(TEXT("cardCount"),WardrobeCards.Num());Wardrobe->SetArrayField(TEXT("cards"),WardrobeCards);Result->SetObjectField(TEXT("wardrobe"),Wardrobe);
    auto Shop=MakeShared<FJsonObject>();Shop->SetStringField(TEXT("source"),Path(Context.Shop));const bool ShopOwned=SourceOwned(Context.Shop,Context,World);Shop->SetBoolField(TEXT("sourceOwnedByCurrentController"),ShopOwned);
    TArray<TSharedPtr<FJsonValue>> ShopCards;
    if(ShopOwned && Context.Shop->WidgetTree) {
        Shop->SetBoolField(TEXT("sourceInViewport"),Context.Shop->IsInViewport());Shop->SetBoolField(TEXT("sourceVisible"),Context.Shop->IsVisible());Shop->SetStringField(TEXT("selectedItem"),Context.Shop->GetSelectedPresentationItemID().ToString());
        if(auto* Grid=Context.Shop->WidgetTree->FindWidget<UUniformGridPanel>(TEXT("ShopCatalogGrid"))) for(UWidget* Child:Grid->GetAllChildren()) if(auto* Card=Cast<UMCCosmeticItemWidget>(Child)) ShopCards.Add(MakeShared<FJsonValueObject>(SourceCard(Card,RootGeometry)));
    }
    Shop->SetNumberField(TEXT("cardCount"),ShopCards.Num());Shop->SetArrayField(TEXT("cards"),ShopCards);Result->SetObjectField(TEXT("shop"),Shop);
    if(Context.Root && Context.Root->WidgetTree) {
        Result->SetObjectField(TEXT("newWardrobe"),View(Context.Root->WidgetTree->FindWidget<UUserWidget>(TEXT("UnifiedWardrobe")),RootGeometry,{TEXT("WardrobeV2LeftPanel"),TEXT("WardrobeV2GridViewport"),TEXT("WardrobeV2Title"),TEXT("WardrobeV2Back"),TEXT("WardrobeV2Primary"),TEXT("WardrobeV2DetailName"),TEXT("WardrobeV2Description")},TEXT("WardrobeV2GridScroll")));
        Result->SetObjectField(TEXT("newShop"),View(Context.Root->WidgetTree->FindWidget<UUserWidget>(TEXT("UnifiedShop")),RootGeometry,{TEXT("ShopV2Frame"),TEXT("ShopV2GridBounds"),TEXT("ShopV2Header"),TEXT("ShopV2Back"),TEXT("ShopV2Buy"),TEXT("ShopV2SelectedName"),TEXT("ShopV2SelectedDescription")},TEXT("ShopV2CatalogScroll")));
    }
    return Encode(Result);
}
FString UMCCommerceUIValidationLibrary::SendPIEGridWheel(UWorld* World,FName Target,float Delta)
{
    using namespace MCCommerceUIValidationPrivate;
    if(!FParse::Param(FCommandLine::Get(),TEXT("MCUIFoundationOwned")) || !FParse::Param(FCommandLine::Get(),TEXT("MCHubTransientProfile"))) return ValidationError(TEXT("Wheel probe requires both owned UI and transient profile command-line flags"));
    if(!FMath::IsFinite(Delta) || (Delta!=1.f && Delta!=-1.f)) return ValidationError(TEXT("Only one +1 or -1 native wheel notch is allowed"));
    FContext Context;FString Why;if(!Resolve(World,Context,Why)) return ValidationError(Why);
    if(!Context.Controller->IsWorldPresentationReady() || !Context.Root || !Context.Root->WidgetTree) return ValidationError(TEXT("Actual world readiness and one visible current New root are required"));
    UUserWidget* TargetView=nullptr;FName GridName;FName ScrollName;
    if(Target==TEXT("Wardrobe")) {
        if(!Context.Controller->IsWardrobeOpen() || !SourceOwned(Context.Wardrobe,Context,World) || !Context.Wardrobe->IsInViewport() || Context.Root->GetActiveScreen()!=EMCUnifiedUIScreen::Wardrobe) return ValidationError(TEXT("Actual current Wardrobe is not open on this New root"));
        auto* View=Context.Root->WidgetTree->FindWidget<UMCUnifiedWardrobeWidget>(TEXT("UnifiedWardrobe"));if(!View || !View->HasBoundBackend()) return ValidationError(TEXT("Current Wardrobe view is not bound"));TargetView=View;GridName=TEXT("WardrobeV2Products");ScrollName=TEXT("WardrobeV2GridScroll");
    } else if(Target==TEXT("Shop")) {
        if(!Context.Controller->IsHubShopOpen() || !SourceOwned(Context.Shop,Context,World) || !Context.Shop->IsInViewport() || Context.Root->GetActiveScreen()!=EMCUnifiedUIScreen::Boosters) return ValidationError(TEXT("Actual current Shop is not open on this New root"));
        auto* View=Context.Root->WidgetTree->FindWidget<UMCUnifiedShopWidget>(TEXT("UnifiedShop"));if(!View || !View->HasBoundBackend()) return ValidationError(TEXT("Current Shop view is not bound"));TargetView=View;GridName=TEXT("ShopV2CatalogGrid");ScrollName=TEXT("ShopV2CatalogScroll");
    } else return ValidationError(TEXT("Target must be exactly Wardrobe or Shop"));
    if(!TargetView->IsVisible() || TargetView->GetWorld()!=World || TargetView->GetOwningPlayer()!=Context.Controller || !TargetView->WidgetTree) return ValidationError(TEXT("Current target view is not visible/owned in the actual world"));
    auto* Grid=TargetView->WidgetTree->FindWidget<UUniformGridPanel>(GridName);auto* Scroller=TargetView->WidgetTree->FindWidget<UMCUIGridScrollBox>(ScrollName);
    const auto Slate=Scroller?Scroller->GetCachedWidget():TSharedPtr<SWidget>();
    if(!Grid || Grid->GetChildrenCount()<1 || !Scroller || !Slate.IsValid() || !Scroller->IsVisible() || !Scroller->GetIsEnabled() || !FMath::IsFinite(Scroller->GetRowStride()) || Scroller->GetRowStride()<=0.f) return ValidationError(TEXT("Actual snapped cell grid is not ready; no refresh or synthetic metrics are substituted"));
    const FGeometry Paint=Scroller->GetPaintSpaceGeometry();if(Paint.GetLocalSize().X<=1 || Paint.GetLocalSize().Y<=1) return ValidationError(TEXT("Actual scroller has no painted viewport"));
    const auto RootGeometry=Context.Root->GetPaintSpaceGeometry();auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("valid"),true);Result->SetStringField(TEXT("target"),Target.ToString());Result->SetNumberField(TEXT("delta"),Delta);Result->SetObjectField(TEXT("before"),Scroll(Scroller,RootGeometry));
    const FVector2D Center=Paint.LocalToAbsolute(Paint.GetLocalSize()*.5);const TSet<FKey> PressedButtons;
    const FPointerEvent Pointer(0u,Center,Center,PressedButtons,FKey(),Delta,FModifierKeysState());
    const FReply Reply=Slate->OnMouseWheel(Paint,Pointer);
    Result->SetBoolField(TEXT("handled"),Reply.IsEventHandled());Result->SetObjectField(TEXT("after"),Scroll(Scroller,RootGeometry));
    Result->SetStringField(TEXT("scope"),TEXT("Direct actual cached Slate OnMouseWheel handler plus existing OnUserScrolled row snap. Not hardware, hit-test routing or desktop input proof. Immediate desired target observed; capture the next rendered frame separately."));
    return Encode(Result);
}
