#include "MCHubGrillPresentationComponent.h"
#include "MCHubGrillStation.h"
#include "MCHubEconomyComponent.h"
#include "MCPlayerState.h"
#include "MCGameplayUIFactory.h"
#include "MCUIVisualStyle.h"
#include "MCToothCharacter.h"
#include "MCFishingRodComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "MCHubGrillHologram"

namespace
{
float ReelFrontV(float PhysicalAngle)
{
    // SM_Roulette_1 has 22 flat facets and a padded atlas seam, not a full
    // 0..1 cylindrical UV. Intersect the visible +Y ray with the actual facet.
    constexpr float V[]={.00560379028f,.05053710938f,.09552001953f,.14038085938f,
        .18542480469f,.23034667969f,.275390625f,.3203125f,.365234375f,.41015625f,
        .455078125f,.5f,.544921875f,.58984375f,.634765625f,.6796875f,.724609375f,
        .76953125f,.814453125f,.859375f,.904296875f,.94970703125f,.99462890625f};
    const float Wrapped=FMath::Fmod(FMath::Fmod(PhysicalAngle,360.f)+360.f,360.f);
    const float Facet=(1.f-Wrapped/360.f)*22.f;
    const int32 I=FMath::Clamp(FMath::FloorToInt(Facet),0,21);
    const float F=Facet-I,Step=UE_TWO_PI/22.f;
    const float A=FMath::Sin(F*Step),B=FMath::Sin((1.f-F)*Step);
    return FMath::Lerp(V[I],V[I+1],A/FMath::Max(A+B,UE_SMALL_NUMBER));
}
}

UMCHubGrillHologramWidget::UMCHubGrillHologramWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    ReelMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/M_GrillHologramReel.M_GrillHologramReel")));
}
void UMCHubGrillHologramWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {ResolveControls();return;}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HologramDesignSize"));
    Size->SetWidthOverride(390);Size->SetHeightOverride(190);WidgetTree->RootWidget=Size;
    HologramPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HologramPanel"));
    HologramPanel->SetPadding(FMargin(13,8));Size->SetContent(HologramPanel);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("HologramContent"));HologramPanel->SetContent(Column);
    HologramTitle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HologramTitle"));
    HologramTitle->SetText(LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА"));HologramTitle->SetJustification(ETextJustify::Center);
    Column->AddChildToVerticalBox(HologramTitle)->SetPadding(FMargin(0,0,0,5));
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HologramReels"));
    Column->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Center);
    for(int32 I=0;I<3;++I) {
        auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("HologramReelFrame%d"),I)));
        Frame->SetPadding(FMargin(3));auto* ReelSlot=Row->AddChildToHorizontalBox(Frame);ReelSlot->SetPadding(FMargin(4,0));
        auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(102);Box->SetHeightOverride(102);Frame->SetContent(Box);
        auto* Reel=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("HologramReel%d"),I)));Box->SetContent(Reel);
    }
    HologramReward=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HologramReward"));
    HologramReward->SetJustification(ETextJustify::Center);HologramReward->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(HologramReward)->SetPadding(FMargin(0,5,0,0));
    ResolveControls();ApplyTheme();
}
void UMCHubGrillHologramWidget::ResolveControls()
{
    if(!WidgetTree) return;
    HologramPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HologramPanel")));
    HologramTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HologramTitle")));
    HologramReward=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HologramReward")));
    Reels.Reset();for(int32 I=0;I<3;++I) Reels.Add(Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HologramReel%d"),I)))));
}
void UMCHubGrillHologramWidget::PrepareMaterials()
{
    ResolveControls();if(ReelMaterials.Num()==3) return;
    auto* Material=ReelMaterial.LoadSynchronous();if(!Material) return;
    ReelMaterials.Reset();for(UImage* Reel:Reels) {
        auto* MID=UMaterialInstanceDynamic::Create(Material,this);ReelMaterials.Add(MID);
        MID->SetScalarParameterValue(TEXT("ScanlineStrength"),ScanlineStrength);
        MID->SetScalarParameterValue(TEXT("ColorSeparation"),ColorSeparation);
        if(Reel) Reel->SetBrushFromMaterial(MID);
    }
}
void UMCHubGrillHologramWidget::SetStation(AMCHubGrillStation* Station)
{Source=Station;bPreview=false;BuildPresentationTree();PrepareMaterials();TickPresentation(0);}
void UMCHubGrillHologramWidget::ApplyTheme()
{
    ResolveControls();Super::ApplyTheme();const auto* ResolvedTheme=GetResolvedTheme();if(!ResolvedTheme || !WidgetTree) return;
    const auto& P=ResolvedTheme->Palette;MCUIVisualStyle::Panel(HologramPanel,ResolvedTheme,P.Backdrop.CopyWithNewOpacity(.77f));
    for(int32 I=0;I<3;++I) if(auto* Frame=Cast<UBorder>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HologramReelFrame%d"),I)))))
        MCUIVisualStyle::Panel(Frame,ResolvedTheme,P.Marine.CopyWithNewOpacity(.85f));
    for(auto* Label:{HologramTitle.Get(),HologramReward.Get()}) if(Label) {
        Label->SetFont(ResolvedTheme->MakeGameplayFont(18,true));Label->SetColorAndOpacity(P.Cream);
    }
}
void UMCHubGrillHologramWidget::TickPresentation(float DeltaSeconds)
{
    PrepareMaterials();const auto* Grill=Source.Get();if(!Grill) return;
    const auto Angles=Grill->GetReelDisplayAngles();
    for(int32 I=0;I<3 && I<ReelMaterials.Num();++I) if(ReelMaterials[I]) {
        // The physical head's aperture is angled above +Y. Its measured
        // per-reel alignment is included in the physical stop; remove that
        // alignment to sample the same fixed window centre in the atlas.
        const float Alignment=Grill->ReelAngleOffsets.IsValidIndex(I)?Grill->ReelAngleOffsets[I]:0.f;
        const float V=ReelFrontV((Angles.IsValidIndex(I)?Angles[I]:0.f)-Alignment);
        ReelMaterials[I]->SetScalarParameterValue(TEXT("VCenter"),V);
    }
    if(HologramTitle) HologramTitle->SetText(Grill->Stage==EMCHubGrillStage::Rolling?Localized(TEXT("UI.Grill.Rolling"),LOCTEXT("Rolling","КРУТИМ…")):
        Grill->Stage==EMCHubGrillStage::Closing?Localized(TEXT("UI.Grill.Accepted"),LOCTEXT("Cooking","РЫБА ПРИНЯТА")):
        Grill->CurrentReward.RollID.IsValid()?Localized(TEXT("UI.Grill.Combination"),LOCTEXT("Combination","ВАША КОМБИНАЦИЯ")):
            Localized(TEXT("UI.Grill.Idle"),LOCTEXT("Idle","1 РЫБА · 1 ПОПЫТКА")));
    if(HologramReward) {
        const bool Finished=Grill->CurrentReward.RollID.IsValid() && Grill->Stage!=EMCHubGrillStage::Closing && Grill->Stage!=EMCHubGrillStage::Rolling;
        if(!Finished) HologramReward->SetText(FText::GetEmpty());
        else if(Grill->CurrentReward.Kind==EMCGrillRewardKind::Coins)
            HologramReward->SetText(Grill->CurrentReward.Coins>0?
                FText::Format(Localized(TEXT("UI.Grill.Coins"),LOCTEXT("Coins","+{0} монет")),FText::AsNumber(Grill->CurrentReward.Coins)):
                Localized(TEXT("UI.Grill.NoPrize"),LOCTEXT("NoPrize","Без выигрыша · попробуй ещё")));
        else {
            FText Name=FText::FromName(Grill->CurrentReward.CatalogItem);
            const auto* PS=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
            if(PS && PS->Economy) if(const auto* Item=PS->Economy->FindCatalogItem(Grill->CurrentReward.CatalogItem)) Name=Localized(Item->DisplayNameKey,Item->DisplayName);
            HologramReward->SetText(FText::Format(Localized(TEXT("UI.Grill.Item"),LOCTEXT("Item","{0} ×{1}")),Name,FText::AsNumber(Grill->CurrentReward.Quantity)));
        }
        if(const auto* ResolvedTheme=GetResolvedTheme()) HologramReward->SetColorAndOpacity(!Finished?ResolvedTheme->Palette.Cream:
            Grill->CurrentReward.Kind==EMCGrillRewardKind::Coins && Grill->CurrentReward.Coins==0?ResolvedTheme->Palette.Warning:ResolvedTheme->Palette.Mint);
    }
}
void UMCHubGrillHologramWidget::ApplyEditorPreview(float Time)
{
    bPreview=true;BuildPresentationTree();Super::ApplyEditorPreview(Time);PrepareMaterials();
    for(int32 I=0;I<ReelMaterials.Num();++I) if(ReelMaterials[I]) ReelMaterials[I]->SetScalarParameterValue(TEXT("VCenter"),FMath::Frac(.06f+Time*.7f+I*.11f));
    if(HologramTitle) HologramTitle->SetText(LOCTEXT("Preview","КРУТИМ…"));
    if(HologramReward) HologramReward->SetText(LOCTEXT("PreviewReward","+25 монет"));
}
UMCHubGrillPresentationComponent::UMCHubGrillPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=1.f/30.f;
    WidgetClass=UMCHubGrillHologramWidget::StaticClass();
}
void UMCHubGrillPresentationComponent::BeginPlay()
{
    Super::BeginPlay();Station=Cast<AMCHubGrillStation>(GetOwner());
    if(!Station.IsValid() || GetNetMode()==NM_DedicatedServer) {SetComponentTickEnabled(false);return;}
    Display=NewObject<UWidgetComponent>(GetOwner(),TEXT("GrillHologramDisplay"));
    Display->SetupAttachment(GetOwner()->GetRootComponent());Display->SetRelativeLocation(HologramLocalOffset);
    Display->SetWidgetSpace(EWidgetSpace::Screen);Display->SetDrawSize(FVector2D(390,190));Display->SetPivot(FVector2D(.5,1));
    Display->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("GrillHologram"),WidgetClass?WidgetClass.Get():UMCHubGrillHologramWidget::StaticClass()));
    Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);Display->SetGenerateOverlapEvents(false);Display->SetCanEverAffectNavigation(false);
    Display->SetCastShadow(false);Display->RegisterComponent();Display->InitWidget();
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) W->SetStation(Station.Get());
    // Create all material variants now, before the initial Hub readiness snapshot.
    Display->SetVisibility(false);Display->SetHiddenInGame(true);
    const auto* Fishing=GetDefault<UMCFishingRodComponent>();
    IntakeFish=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("GrillIntakeFish"));
    IntakeFish->SetupAttachment(GetOwner()->GetRootComponent());IntakeFish->SetStaticMesh(Fishing->FishMesh);
    IntakeFish->SetMaterial(0,Fishing->FishMaterial);IntakeFish->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    IntakeFish->SetGenerateOverlapEvents(false);IntakeFish->SetCanEverAffectNavigation(false);
    IntakeFish->SetRelativeScale3D(FVector(.3));IntakeFish->SetVisibility(false);IntakeFish->RegisterComponent();
}
void UMCHubGrillPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Display) {Display->DestroyComponent();Display=nullptr;}
    if(IntakeFish) {IntakeFish->DestroyComponent();IntakeFish=nullptr;}
    Super::EndPlay(Reason);
}
void UMCHubGrillPresentationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);auto* Grill=Station.Get();if(!Grill || !Display) return;
    if(IntakeFish) {
        const bool Taking=Grill->Stage==EMCHubGrillStage::Closing && Grill->CurrentReward.RollID.IsValid();
        IntakeFish->SetVisibility(Taking);
        if(Taking) {
            if(IntakeRoll!=Grill->CurrentReward.RollID) {
                IntakeRoll=Grill->CurrentReward.RollID;
                const auto* OwnerPawn=Grill->ActivePlayer?Grill->ActivePlayer->GetPawn():nullptr;
                IntakeStart=OwnerPawn?OwnerPawn->GetActorLocation()+FVector(0,0,25):Grill->GetActorTransform().TransformPosition(FVector(0,150,130));
            }
            const auto* GS=GetWorld()->GetGameState();const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
            const float A=float(FMath::Clamp((Now-Grill->StageStartedAt)/FMath::Max(.05,Grill->StageEndsAt-Grill->StageStartedAt),0.,1.));
            const FVector End=Grill->GetActorTransform().TransformPosition(FVector(HologramLocalOffset.X,HologramLocalOffset.Y,145));
            IntakeFish->SetWorldLocation(FMath::Lerp(IntakeStart,End,FMath::SmoothStep(0.f,1.f,A))+FVector(0,0,FMath::Sin(A*PI)*75));
            IntakeFish->SetWorldRotation(FRotator(A*160,Now*120,0));
        }
    }
    auto* PC=UGameplayStatics::GetPlayerController(this,0);APawn* Pawn=PC?PC->GetPawn().Get():nullptr;
    const bool Show=PC && PC->IsLocalController() && Pawn && FVector::DistSquared(Pawn->GetActorLocation(),Grill->GetActorLocation())<FMath::Square(FMath::Clamp(VisibleDistance,300.f,5000.f));
    VisibilityAlpha=FMath::FInterpTo(VisibilityAlpha,Show?1.f:0.f,Dt,12.f);
    const bool Visible=VisibilityAlpha>.01f;
    Display->SetVisibility(Visible);Display->SetHiddenInGame(!Visible);Display->SetComponentTickEnabled(Visible);
    if(!Visible) return;
    Display->SetOwnerPlayer(PC->GetLocalPlayer());
    if(auto* W=Cast<UMCHubGrillHologramWidget>(Display->GetUserWidgetObject())) {
        W->SetOwningPlayer(PC);W->SetRenderOpacity(VisibilityAlpha*.95f);
        const float Distance=FVector::Distance(Pawn->GetActorLocation(),Grill->GetActorLocation());
        const float Scale=FMath::Clamp(800.f/FMath::Max(Distance,1.f),.55f,1.f);
        W->SetRenderScale(FVector2D(Scale*(.96f+.04f*VisibilityAlpha)));W->TickPresentation(Dt);
    }
    Grill->SetLocalPromptVisible(PC,!PC->IsMoveInputIgnored());
}
#undef LOCTEXT_NAMESPACE
