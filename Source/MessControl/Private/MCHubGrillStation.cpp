#include "MCHubGrillStation.h"

#include "MCHubEconomyComponent.h"
#include "MCHubGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCGameplayUIFactory.h"
#include "MCHubInteractionPromptWidget.h"
#include "MCHubGrillPresentationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Templates/UnrealTemplate.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "MCHubGrill"

AMCHubGrillStation::AMCHubGrillStation()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    InteractionVolume=CreateDefaultSubobject<UBoxComponent>(TEXT("GrillInteractionVolume"));
    SetRootComponent(InteractionVolume); InteractionVolume->SetBoxExtent(FVector(140,120,150));
    InteractionVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    InteractionVolume->SetGenerateOverlapEvents(false); InteractionVolume->SetCanEverAffectNavigation(false);
    InteractionVolume->SetHiddenInGame(true);
    InteractionPrompt=CreateDefaultSubobject<UWidgetComponent>(TEXT("GrillInteractionPrompt"));
    InteractionPrompt->SetupAttachment(InteractionVolume); InteractionPrompt->SetRelativeLocation(FVector(0,0,220));
    InteractionPrompt->SetWidgetSpace(EWidgetSpace::Screen);
    InteractionPrompt->SetWidgetClass(UMCHubInteractionPromptWidget::StaticClass());
    InteractionPrompt->SetDrawSize(FVector2D(64,66)); InteractionPrompt->SetDrawAtDesiredSize(true);
    InteractionPrompt->SetPivot(FVector2D(.5,1)); InteractionPrompt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionPrompt->SetGenerateOverlapEvents(false); InteractionPrompt->SetCanEverAffectNavigation(false);
    InteractionPrompt->SetCastShadow(false); InteractionPrompt->SetVisibility(false); InteractionPrompt->SetHiddenInGame(true);
    InteractionPrompt->PrimaryComponentTick.bStartWithTickEnabled=false;
    ReelAngleOffsets.Init(0.f,3);
    Presentation=CreateDefaultSubobject<UMCHubGrillPresentationComponent>(TEXT("GrillPresentation"));
}
void AMCHubGrillStation::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode()!=NM_DedicatedServer && InteractionPrompt->GetWidgetClass()==UMCHubInteractionPromptWidget::StaticClass())
        InteractionPrompt->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("HubInteractionPrompt"),UMCHubInteractionPromptWidget::StaticClass()));
    if (IsValid(LidActor)) ClosedLidTransform=LidActor->GetActorTransform();
    InitialReelRotations.Reset();bAssemblyCaptured=IsValid(LidActor);
    for (AActor* Reel:ReelActors)
    {
        // The windows belong to the moving lid/head. Keep its complete closed
        // assembly, then spin each reel in that parent's local frame.
        const bool Attached=IsValid(Reel) && IsValid(LidActor)
            && Reel->AttachToActor(LidActor,FAttachmentTransformRules::KeepWorldTransform);
        bAssemblyCaptured&=Attached;
        InitialReelRotations.Add(Attached?Reel->GetRootComponent()->GetRelativeRotation().Quaternion():FQuat::Identity);
    }
    UpdateAssembly();
    SetActorTickEnabled(Stage!=EMCHubGrillStage::Idle);
}
void AMCHubGrillStation::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority()) if (auto* Economy=ActiveEconomy.Get())
        if (!Economy->CompleteGrillRoll(CurrentReward.RollID)) Economy->CancelGrillRoll(CurrentReward.RollID);
    Super::EndPlay(Reason);
}
double AMCHubGrillStation::ServerNow() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():(GetWorld()?GetWorld()->GetTimeSeconds():0);
}
bool AMCHubGrillStation::ContainsPawn(const APawn* Pawn) const
{
    if (!bEnabled || !InteractionVolume || !IsValid(Pawn) || Pawn->GetWorld()!=GetWorld()) return false;
    const FVector Local=InteractionVolume->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FVector Extent=InteractionVolume->GetUnscaledBoxExtent();
    return !Local.ContainsNaN() && FMath::Abs(Local.X)<=Extent.X && FMath::Abs(Local.Y)<=Extent.Y && FMath::Abs(Local.Z)<=Extent.Z;
}
bool AMCHubGrillStation::IsAssemblyConfigured(const UMCHubEconomyComponent* Economy,FText& Error) const
{
    if (!bAssemblyCaptured || !IsValid(LidActor) || ReelActors.Num()!=3 || InitialReelRotations.Num()!=3
        || LidHingeAxisLocal.ContainsNaN() || LidHingeAxisLocal.IsNearlyZero()
        || ReelSpinAxisLocal.ContainsNaN() || ReelSpinAxisLocal.IsNearlyZero() || !FMath::IsFinite(LidOpenDegrees))
    { Error=LOCTEXT("AssemblyMissing","Механизм гриля ещё не настроен."); return false; }
    TSet<const AActor*> Unique;
    if (!LidActor->GetRootComponent() || LidActor->GetRootComponent()->Mobility!=EComponentMobility::Movable)
    { Error=LOCTEXT("LidStatic","Крышка гриля должна быть настроена как Movable."); return false; }
    for (AActor* Reel:ReelActors)
    {
        if (!IsValid(Reel) || Reel==LidActor || Unique.Contains(Reel) || !Reel->GetRootComponent()
            || Reel->GetRootComponent()->Mobility!=EComponentMobility::Movable) { Error=LOCTEXT("ReelsMissing","Требуются три отдельных барабана с подвижностью Movable."); return false; }
        Unique.Add(Reel);
    }
    const TArray<FName> Symbols=Economy?Economy->GetGrillSymbols():TArray<FName>();
    if (Symbols.IsEmpty()) { Error=LOCTEXT("PoolMissing","Таблица результатов гриля ещё не подготовлена."); return false; }
    for (FName Symbol:Symbols)
    {
        const float* Angle=SymbolStopAngles.Find(Symbol);
        if (!Angle || !FMath::IsFinite(*Angle)) { Error=LOCTEXT("SymbolsMissing","Позиции символов рулетки ещё не настроены."); return false; }
    }
    return true;
}
bool AMCHubGrillStation::TryStartRoll(AMCToothCharacter* Hero,FText& Error)
{
    Error=FText::GetEmpty();
    const auto* Hub=GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
    if (!HasAuthority() || !Hub || Hub->Phase!=EMCHubPhase::Gathering || !IsValid(Hero) || !Hero->CanWork() || !ContainsPawn(Hero)) return false;
    if (IsBusy()) { Error=LOCTEXT("Busy","Дождись завершения текущей попытки."); return false; }
    auto* Player=Hero->GetPlayerState<AMCPlayerState>();
    auto* Economy=Player?Player->FindComponentByClass<UMCHubEconomyComponent>():nullptr;
    if (!Economy || !IsAssemblyConfigured(Economy,Error)) return false;
    TGuardValue<bool> Guard(bAcceptingRequest,true);
    FGuid Receipt; FMCGrillReward Reward;
    if (!Economy->BeginGrillRoll(Receipt,Reward,Error)) return false;
    RollStartAngles=GetReelDisplayAngles();
    ActiveRules=Economy->GetRules(); ActiveEconomy=Economy; ActivePlayer=Player; CurrentReward=MoveTemp(Reward);
    Hero->CancelGameplayInput(); SetStage(EMCHubGrillStage::Closing,ActiveRules.LidCloseSeconds);
    return true;
}
void AMCHubGrillStation::SetStage(EMCHubGrillStage NewStage,float Duration)
{
    Stage=NewStage; StageStartedAt=ServerNow(); StageEndsAt=StageStartedAt+FMath::Max(.05f,Duration);
    SetActorTickEnabled(NewStage!=EMCHubGrillStage::Idle);
    ForceNetUpdate(); OnRep_State();
}
void AMCHubGrillStation::AdvanceAuthority()
{
    if (!HasAuthority() || Stage==EMCHubGrillStage::Idle || ServerNow()<StageEndsAt) return;
    switch (Stage)
    {
    case EMCHubGrillStage::Closing: SetStage(EMCHubGrillStage::Rolling,ActiveRules.RollSeconds); break;
    case EMCHubGrillStage::Rolling:
        if (auto* Economy=ActiveEconomy.Get())
        {
            if (Economy->CompleteGrillRoll(CurrentReward.RollID) || Economy->LastGrillReward.RollID==CurrentReward.RollID)
                CurrentReward=Economy->LastGrillReward;
            else Economy->CancelGrillRoll(CurrentReward.RollID);
        }
        SetStage(EMCHubGrillStage::Revealing,ActiveRules.RevealSeconds); break;
    case EMCHubGrillStage::Revealing: SetStage(EMCHubGrillStage::Opening,ActiveRules.LidOpenSeconds); break;
    case EMCHubGrillStage::Opening:
        ActivePlayer=nullptr; ActiveEconomy.Reset(); SetStage(EMCHubGrillStage::Idle,.05f); break;
    default: break;
    }
}
TArray<float> AMCHubGrillStation::GetReelDisplayAngles() const
{
    TArray<float> Angles; Angles.Init(0.f,3);
    for (int32 I=0;I<3;++I)
    {
        const FName Symbol=CurrentReward.ReelSymbols.IsValidIndex(I)?CurrentReward.ReelSymbols[I]:NAME_None;
        const float* Stop=SymbolStopAngles.Find(Symbol.IsNone()?FName(TEXT("Cherry")):Symbol);
        const float Offset=ReelAngleOffsets.IsValidIndex(I)?ReelAngleOffsets[I]:0.f;
        const float Target=(Stop?*Stop:0.f)+Offset;
        const float Start=RollStartAngles.IsValidIndex(I)?FMath::Fmod(RollStartAngles[I]+360.f,360.f):0.f;
        if (Stage==EMCHubGrillStage::Rolling)
        {
            const double Duration=FMath::Max(.1,StageEndsAt-StageStartedAt);
            const double Delay=FMath::Min(.12*I,Duration*.2);
            const float Alpha=float(FMath::Clamp((ServerNow()-StageStartedAt-Delay)/(Duration-Delay),0.,1.));
            const float Ease=1.f-FMath::Pow(1.f-Alpha,3.f);
            const int32 Turns=5+I*2+int32(GetTypeHash(CurrentReward.RollID)%3);
            const float Travel=360.f*Turns+FMath::Fmod(Target-Start+720.f,360.f);
            Angles[I]=Start+Travel*Ease;
        }
        else Angles[I]=Stage==EMCHubGrillStage::Closing?Start:Target;
    }
    return Angles;
}
void AMCHubGrillStation::UpdateAssembly()
{
    if (!bAssemblyCaptured || GetNetMode()==NM_DedicatedServer) return;
    const float Alpha=float(FMath::Clamp((ServerNow()-StageStartedAt)/FMath::Max(.05,StageEndsAt-StageStartedAt),0.,1.));
    float Open=Stage==EMCHubGrillStage::Idle?1.f:0.f;
    if (Stage==EMCHubGrillStage::Closing) Open=1.f-FMath::SmoothStep(0.f,1.f,Alpha);
    if (Stage==EMCHubGrillStage::Opening) Open=FMath::SmoothStep(0.f,1.f,Alpha);
    if (IsValid(LidActor))
    {
        const FVector Pivot=GetActorTransform().TransformPosition(LidHingeLocal);
        const FVector Axis=GetActorTransform().TransformVectorNoScale(LidHingeAxisLocal).GetSafeNormal();
        const FQuat Rotation(Axis,FMath::DegreesToRadians(LidOpenDegrees*Open));
        const FVector Position=Pivot+Rotation.RotateVector(ClosedLidTransform.GetLocation()-Pivot);
        LidActor->SetActorLocationAndRotation(Position,Rotation*ClosedLidTransform.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    }
    const auto Angles=GetReelDisplayAngles();
    for (int32 I=0;I<ReelActors.Num() && I<3 && InitialReelRotations.IsValidIndex(I);++I)
        if (IsValid(ReelActors[I])) ReelActors[I]->SetActorRelativeRotation(InitialReelRotations[I]
            *FQuat(ReelSpinAxisLocal.GetSafeNormal(),FMath::DegreesToRadians(Angles[I])),false,nullptr,ETeleportType::TeleportPhysics);
}
void AMCHubGrillStation::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); AdvanceAuthority(); UpdateAssembly(); }
void AMCHubGrillStation::OnRep_State() { SetActorTickEnabled(Stage!=EMCHubGrillStage::Idle); UpdateAssembly(); OnGrillStateChanged.Broadcast(); }
void AMCHubGrillStation::SetLocalPromptVisible(APlayerController* Player,bool bVisible)
{
    if (!InteractionPrompt || !IsValid(Player) || !Player->IsLocalController() || Player->GetWorld()!=GetWorld() || !Player->GetLocalPlayer()) return;
    const bool Show=bVisible && !IsBusy() && ContainsPawn(Player->GetPawn());
    if (Show) { InteractionPrompt->SetOwnerPlayer(Player->GetLocalPlayer()); InteractionPrompt->InitWidget();
        if (auto* Widget=InteractionPrompt->GetUserWidgetObject()) Widget->SetOwningPlayer(Player); }
    if (auto* Widget=InteractionPrompt->GetUserWidgetObject()) Widget->SetVisibility(Show?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    InteractionPrompt->SetVisibility(Show); InteractionPrompt->SetHiddenInGame(!Show); InteractionPrompt->SetComponentTickEnabled(Show);
}
void AMCHubGrillStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCHubGrillStation,Stage); DOREPLIFETIME(AMCHubGrillStation,StageStartedAt); DOREPLIFETIME(AMCHubGrillStation,StageEndsAt);
    DOREPLIFETIME(AMCHubGrillStation,ActivePlayer); DOREPLIFETIME(AMCHubGrillStation,CurrentReward);
    DOREPLIFETIME(AMCHubGrillStation,RollStartAngles);
}

#undef LOCTEXT_NAMESPACE
