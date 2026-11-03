#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "MCVFXLabBossStation.h"
#include "MCVFXPresentationComponent.h"
#include "MCNutBoss.h"
#include "MCNutSpellProjectile.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCPlayerState.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutMageFireballPreviewContract,
    "MessControl.VFX.NutMageFireball.PreviewProviderUsesNativeEventOwners",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutMageFireballPreviewContract::RunTest(const FString&)
{
    const FName ID(TEXT("Boss.NutMage.Fireball"));const auto* Entry=MCPerkPreviewAdapters::Find(ID);
    const auto* Boss=GetDefault<AMCNutBoss>();const auto* Shot=GetDefault<AMCNutSpellProjectile>();
    if(!TestNotNull(TEXT("Actual Mage fireball provider"),Entry)
        || !TestNotNull(TEXT("Boss fireball presentation"),Boss->FireballPresentation.Get())
        || !TestNotNull(TEXT("Projectile fireball presentation"),Shot->FireballPresentation.Get()))return false;
    TestTrue(TEXT("Context requires no dummy perk row"),Entry->Kind==EMCPerkPreviewAdapterKind::Context && Entry->SourceRowID.IsNone());
    TestTrue(TEXT("Both native owners share one declared source"),Boss->FireballPresentation->SourceID==ID && Shot->FireballPresentation->SourceID==ID);
    TestTrue(TEXT("Provider and native event sets exactly agree"),Entry->Stages==Boss->FireballPresentation->Events && Entry->Stages==Shot->FireballPresentation->Events
        && Entry->Stages.Num()==4 && Entry->Stages.Contains(TEXT("Charge")) && Entry->Stages.Contains(TEXT("Release"))
        && Entry->Stages.Contains(TEXT("Flight")) && Entry->Stages.Contains(TEXT("Impact")));
    TestTrue(TEXT("Target, real wall and actual dodge/ground-miss scenarios are present"),Entry->Scenarios.Num()==3
        && Entry->Scenarios.Contains(EMCPerkPreviewScenario::Target) && Entry->Scenarios.Contains(EMCPerkPreviewScenario::NearWall) && Entry->Scenarios.Contains(EMCPerkPreviewScenario::Timeout));
    TestTrue(TEXT("Procedural native fallback is not flattened into an invented NS layer"),Entry->DefaultSystems.IsEmpty());
    auto Adapter=Entry->Create();FMCPerkPreviewContext Missing;Missing.PerkID=ID;FString Error;int32 Events=0;Missing.RecordEvent=[&](FName){++Events;};
    TestFalse(TEXT("Missing world cannot fabricate native cast"),Adapter->Prepare(Missing,Error));
    TestFalse(TEXT("Refusal has a concrete reason"),Error.IsEmpty());TestEqual(TEXT("No native event was fabricated"),Events,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutMageBorrowedSubjectLifecycle,
    "MessControl.VFX.NutMageFireball.LabBorrowsOneSubjectAndPreservesIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutMageBorrowedSubjectLifecycle::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run the isolated borrowed-subject test outside PIE."));return false;}
    FMCPerkPreviewRunner Runner;
    if(!TestTrue(TEXT("Private native preview world initializes"),Runner.Initialize())){AddError(Runner.GetStatus());return false;}
    if(!TestTrue(TEXT("Actual Mage fixture prepares"),Runner.StartAction(TEXT("Boss.NutMage.Fireball"),EMCPerkPreviewScenario::Target,nullptr)))
    {AddError(Runner.GetStatus());return false;}
    auto* World=Runner.GetWorld();auto* Hero=Runner.GetHero();auto* Controller=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    auto* Identity=Hero?Hero->GetPlayerState<AMCPlayerState>():nullptr;AMCVFXLabBossStation* Station=nullptr;
    int32 Pawns=0,Controllers=0,Players=0;
    for(TActorIterator<AMCToothCharacter> It(World);It;++It)if(IsValid(*It))++Pawns;
    for(TActorIterator<APlayerController> It(World);It;++It)if(IsValid(*It))++Controllers;
    for(TActorIterator<AMCPlayerState> It(World);It;++It)if(IsValid(*It))++Players;
    for(TActorIterator<AMCVFXLabBossStation> It(World);It;++It)if(IsValid(*It))Station=*It;
    if(!TestNotNull(TEXT("One actual Runner Hero"),Hero) || !TestNotNull(TEXT("Borrowing lab station"),Station)
        || !TestNotNull(TEXT("Existing controller"),Controller) || !TestNotNull(TEXT("Existing PlayerState"),Identity))return false;
    TestEqual(TEXT("Exactly one tooth Pawn, no hidden duplicate"),Pawns,1);TestEqual(TEXT("Exactly one participating player controller"),Controllers,1);TestEqual(TEXT("Exactly one PlayerState"),Players,1);
    TestTrue(TEXT("Station subject is the exact Runner Hero"),Station->GetComparisonSubject()==Hero);
    TestTrue(TEXT("No possession identity replacement"),Controller->GetPawn()==Hero && Controller->GetPlayerState<AMCPlayerState>()==Identity);
    const FVector Before=Hero->GetActorLocation();const bool ControllerTick=Controller->IsActorTickEnabled();
    auto State=Hero->Status->State;State.Health=77.f;Hero->Status->Restore(State);
    FTimerHandle OwnedTimer;World->GetTimerManager().SetTimer(OwnedTimer,FTimerDelegate::CreateWeakLambda(Controller,[]{}),100.f,false);
    FActorSpawnParameters Spawn;Spawn.Owner=Hero;Spawn.ObjectFlags=RF_Transient;
    auto* BorrowedTool=World->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    Station->Reset();
    TestTrue(TEXT("Station reset retains exact borrowed Hero"),Station->GetComparisonSubject()==Hero && Runner.GetHero()==Hero);
    TestTrue(TEXT("Reset retains controller and PlayerState"),Hero->GetController()==Controller && Hero->GetPlayerState<AMCPlayerState>()==Identity);
    TestTrue(TEXT("Borrowed preparation does not teleport physical body/hands"),Hero->GetActorLocation().Equals(Before,.01f));
    TestEqual(TEXT("Borrowed health is not reinitialized"),Hero->Status->State.Health,77.f);
    TestEqual(TEXT("Borrowed controller ticking is not changed"),Controller->IsActorTickEnabled(),ControllerTick);
    TestTrue(TEXT("Borrowed controller timers are not cleared"),World->GetTimerManager().IsTimerActive(OwnedTimer));
    TestTrue(TEXT("Owned tools of borrowed Hero are not claimed for cleanup"),IsValid(BorrowedTool) && !BorrowedTool->IsActorBeingDestroyed());
    Station->Destroy();
    TestTrue(TEXT("Station destruction leaves borrowed Pawn/identity alive"),IsValid(Hero) && !Hero->IsActorBeingDestroyed()
        && IsValid(Controller) && IsValid(Identity) && Controller->GetPawn()==Hero);
    TestTrue(TEXT("Station destruction still preserves borrowed-owned tools"),IsValid(BorrowedTool) && !BorrowedTool->IsActorBeingDestroyed());
    TestTrue(TEXT("Station destruction still preserves borrowed timers"),World->GetTimerManager().IsTimerActive(OwnedTimer));
    return true;
}
#endif
