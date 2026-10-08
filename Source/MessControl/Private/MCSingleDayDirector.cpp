#include "MCSingleDayDirector.h"
#include "MCNutRainEvent.h"
#include "MCNutRainProfile.h"
#include "MCGameDirector.h"
#include "MCGameDirectorProfile.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCProgressionComponent.h"
#include "MCBossCharacter.h"
#include "MCTongue.h"
#include "MCDayPlan.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCSingleDayDirector::AMCSingleDayDirector()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
}
void AMCSingleDayDirector::Initialize(UMCDayPlan* Plan, UMCSingleDayProfile* Profile, UMCGameDirectorProfile* InterludeProfile)
{
    if(!HasAuthority()) return;
    Mechanics=Plan?Plan:NewObject<UMCDayPlan>(this);
    Settings=Profile?DuplicateObject<UMCSingleDayProfile>(Profile,this):NewObject<UMCSingleDayProfile>(this);
    DirectorProfile=InterludeProfile;
    Stage=EMCSingleDayStage::Training; bStopped=false;
    if(auto* GS=GetWorld()->GetGameState<AMCGameState>()) {
        GS->RunSettings.DaysToSurvive=1; GS->Day=1;
        FRandomStream Roll(GS->RunSeed);
        VariantIndex=Settings->NutRainVariants.IsEmpty()?0:Roll.RandRange(0,Settings->NutRainVariants.Num()-1);
    }
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginFirstPerk()
{
    if(!HasAuthority() || Stage!=EMCSingleDayStage::Training || bStopped) return;
    Stage=EMCSingleDayStage::FirstPerk;
    Publish(TEXT("ПЕРВОЕ УСИЛЕНИЕ"),TEXT("Задания дают общий опыт. Каждый выбирает свой перк: 1 / 2 / 3."));
    ForceNetUpdate();
}
void AMCSingleDayDirector::Publish(const FString& Title,const FString& Instruction,int32 Left,int32 Total)
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
    GS->TasksLeft=Left; GS->TasksTotal=Total;
    auto& S=GS->DirectorState; S=FMCGameDirectorState(); S.bEnabled=true;
    S.CurrentTitle=Title; S.Instruction=Instruction;
    S.NextTitle=Stage==EMCSingleDayStage::FirstPerk?TEXT("ОРЕХОВЫЙ ДОЖДЬ"):Stage==EMCSingleDayStage::Nuts?TEXT("ДИРЕКТОР"):Stage==EMCSingleDayStage::Boss?TEXT(""):TEXT("ФИНАЛЬНЫЙ БОСС");
    S.SpawnedFood=Total; S.FinishedFood=FMath::Max(0,Total-Left);
    S.CompletionProgress=Total>0?1-float(Left)/Total:0;
    S.DayProgress=Stage==EMCSingleDayStage::Nuts?.25f:Stage==EMCSingleDayStage::Boss?.85f:.1f;
    GS->ForceNetUpdate();
}
void AMCSingleDayDirector::BeginNuts()
{
    Stage=EMCSingleDayStage::Nuts;
    NutEvent=GetWorld()->SpawnActor<AMCNutRainEvent>();
    if(!NutEvent) { Fail(TEXT("Не удалось создать ореховое событие.")); return; }
    NutEvent->SetOwner(this);
    UMCNutRainProfile* Variant=Settings->NutRainVariants.IsValidIndex(VariantIndex)?Settings->NutRainVariants[VariantIndex].LoadSynchronous():nullptr;
    NutEvent->Start(Mechanics,Variant);
    Publish(TEXT("ОРЕХОВЫЙ ДОЖДЬ"),TEXT("Продержитесь под дождём. Кирка разбивает твёрдые орехи; еду можно доставлять."));
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY NUTS variant=%d"),VariantIndex);
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginDirector()
{
    Stage=EMCSingleDayStage::Director;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS->Progression) GS->Progression->AddExperience(FMath::Max(0,Settings->NutEventExperience));
    const float Seconds=FMath::IsFinite(Settings->DirectorSeconds)?FMath::Clamp(Settings->DirectorSeconds,5.f,300.f):30.f;
    InterludeEndsAt=GS->GetServerWorldTimeSeconds()+Seconds;
    Interlude=GetWorld()->SpawnActor<AMCGameDirector>();
    if(Interlude) {
        Interlude->SetOwner(this);
        Interlude->InitializeRun(Mechanics,DirectorProfile);
        Interlude->BeginInterlude(Seconds);
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->GameDirector=Interlude;
    }
    GS->RunSettings.DaysToSurvive=1;
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY DIRECTOR seconds=%.1f"),Seconds);
    ForceNetUpdate();
}
void AMCSingleDayDirector::BeginBoss()
{
    if(Interlude) { Interlude->Stop(); Interlude->Destroy(); Interlude=nullptr; }
    if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->GameDirector=nullptr;
    Stage=EMCSingleDayStage::Boss;
    TSubclassOf<AMCBossCharacter> BossClass=Settings->FinalBossClass.LoadSynchronous();
    if(!BossClass) BossClass=LoadClass<AMCBossCharacter>(nullptr,TEXT("/Game/Gameplay/Boss/BP_ZombieBoss.BP_ZombieBoss_C"));
    if(!BossClass) BossClass=AMCBossCharacter::StaticClass();
    FVector Point(650,0,300); FHitResult Floor;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(It->SurfacePoint(FVector(650,0,0),Floor)) {Point=Floor.ImpactPoint+FVector(0,0,115);break;}
    FActorSpawnParameters Params; Params.Owner=this; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    FinalBoss=GetWorld()->SpawnActor<AMCBossCharacter>(BossClass,Point,FRotator(0,180,0),Params);
    if(FinalBoss) FinalBoss->ActivateBoss();
    else { Fail(TEXT("Не удалось создать финального босса.")); return; }
    Publish(TEXT("ФИНАЛЬНЫЙ БОСС"),TEXT("Победите босса. Ваши личные перки и общий уровень сохраняются."),1,1);
    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY BOSS")); ForceNetUpdate();
}
void AMCSingleDayDirector::Tick(float Dt)
{
    Super::Tick(Dt); if(!HasAuthority() || bStopped) return;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    if(GS->Phase==EMCShiftPhase::Lost) {Stop();return;}
    if(Stage==EMCSingleDayStage::FirstPerk && GS->Progression && !GS->Progression->HasPendingChoices()) BeginNuts();
    else if(Stage==EMCSingleDayStage::Nuts && NutEvent) {
        if(NutEvent->bFailed) { Fail(TEXT("Не удалось запустить ореховый дождь. Проверьте язык и коллизию орехов.")); return; }
        if(NutEvent->IsComplete()) BeginDirector();
        else if(NutEvent->Stage==EMCNutRainStage::Enemies)
            Publish(TEXT("ОРЕХИ ОЖИЛИ"),TEXT("Последние орехи атакуют! Кирка и нож наносят им урон."),NutEvent->EnemiesLeft,FMath::Max(GS->TasksTotal,NutEvent->EnemiesLeft));
        else Publish(TEXT("ОРЕХОВЫЙ ДОЖДЬ"),TEXT("Продержитесь до конца катаклизма. Следите за летящими орехами."));
    }
    else if(Stage==EMCSingleDayStage::Director && GS->GetServerWorldTimeSeconds()>=InterludeEndsAt) BeginBoss();
    else if(Stage==EMCSingleDayStage::Boss && IsValid(FinalBoss) && !FinalBoss->IsBossAlive()) {
        Stage=EMCSingleDayStage::Complete; bStopped=true;
        GS->Phase=EMCShiftPhase::Won; GS->PhaseEndsAt=0; GS->TasksLeft=0;
        GS->DirectorState.CurrentTitle=TEXT("БОСС ПОБЕЖДЁН"); GS->DirectorState.DayProgress=GS->DirectorState.CompletionProgress=1;
        GS->DirectorState.Instruction.Empty(); GS->DirectorState.NextTitle.Empty();
        GS->DirectorState.FinishedFood=GS->DirectorState.SpawnedFood;
        GS->ForceNetUpdate(); ForceNetUpdate(); UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY COMPLETE"));
    }
}
void AMCSingleDayDirector::Stop()
{
    bStopped=true;
    if(!HasAuthority()) return;
    if(IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();} NutEvent=nullptr;
    if(IsValid(Interlude)) {Interlude->Stop();Interlude->Destroy();} Interlude=nullptr;
    if(IsValid(FinalBoss)) FinalBoss->Destroy(); FinalBoss=nullptr;
}
void AMCSingleDayDirector::Fail(const FString& Reason)
{
    Publish(TEXT("СОБЫТИЕ НЕ ЗАПУСТИЛОСЬ"),Reason);
    Stop();
    if(auto* GS=GetWorld()->GetGameState<AMCGameState>()) { GS->Phase=EMCShiftPhase::Lost; GS->ForceNetUpdate(); }
    UE_LOG(LogTemp,Error,TEXT("MC_SINGLE_DAY FAILED: %s"),*Reason);
}
void AMCSingleDayDirector::EndPlay(const EEndPlayReason::Type Reason) {Stop();Super::EndPlay(Reason);}
void AMCSingleDayDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCSingleDayDirector,Stage); DOREPLIFETIME(AMCSingleDayDirector,NutEvent);
    DOREPLIFETIME(AMCSingleDayDirector,FinalBoss); DOREPLIFETIME(AMCSingleDayDirector,VariantIndex);
}
