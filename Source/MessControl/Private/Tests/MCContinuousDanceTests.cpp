#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCExpressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace MCContinuousDanceTestsPrivate
{
/** Real skeletal clips, component ticks and native movement/care input.
 * No network session is started; replicated-state sampling is checked locally. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    UMCExpressionComponent* Expression=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box);Box->SetBoxExtent(FVector(2000,2000,20));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
        Floor->SetActorLocation(FVector(0,0,-20));
        const float Half=GetDefault<AMCToothCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,Half+4),FRotator::ZeroRotator);
        World->SpawnActor<APlayerController>()->Possess(Hero);
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Expression=Hero->Expression;
        // Per-test edits never mutate the saved/shared DA_Emotes library.
        if(Expression->Library) Expression->Library=DuplicateObject<UMCEmoteLibrary>(Expression->Library,Hero);
        Step(.5f);
    }
    ~FFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnownDancesContinue,
    "MessControl.Animation.Dances.AllFiveContinueUntilMovement",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnownDancesContinue::RunTest(const FString&)
{
    MCContinuousDanceTestsPrivate::FFixture F;
    if(!TestNotNull(TEXT("The saved library loads through the real expression component"),F.Expression->Library.Get())) return false;
    for(const FName Id:{FName(TEXT("dance1")),FName(TEXT("dance2")),FName(TEXT("dance3")),FName(TEXT("dance4")),FName(TEXT("dance5"))}) {
        auto* Entry=F.Expression->Library->Entries.FindByPredicate([&](const FMCEmoteEntry& Value){return Value.Id==Id;});
        if(!TestTrue(*FString::Printf(TEXT("Saved %s has an authored body clip"),*Id.ToString()),Entry && Entry->Animation && !Entry->bFaceOnly)) return false;
        const float ClipLength=Entry->Animation->GetPlayLength();
        F.Expression->ServerPlayEmote(Id);F.Step(.3f);
        TestTrue(*FString::Printf(TEXT("%s enters the real body layer"),*Id.ToString()),F.Expression->State.Id==Id && F.Expression->BodyAlpha()>.99f);
        F.Step(FMath::Max(11.f,ClipLength*2.f+.5f));
        TestTrue(*FString::Printf(TEXT("%s survives the old duration and complete clip cycles"),*Id.ToString()),F.Expression->BodyAlpha()>.99f && F.Expression->State.StoppedAt<0);
        const uint16 Serial=F.Expression->State.Serial;
        F.Hero->AddMovementInput(FVector::ForwardVector);F.Step(1.f/60.f);
        TestTrue(TEXT("Ordinary movement records authoritative cancellation"),F.Expression->State.StoppedAt>=0);
        TestEqual(TEXT("Movement cancels the same request rather than starting another"),F.Expression->State.Serial,Serial);
        F.Step(.5f);
        TestEqual(TEXT("The interrupted body returns fully to locomotion"),F.Expression->BodyAlpha(),0.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDanceBlockedMovementAndActions,
    "MessControl.Animation.Dances.BlockedInputDamageAndWorkCancel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDanceBlockedMovementAndActions::RunTest(const FString&)
{
    MCContinuousDanceTestsPrivate::FFixture F;
    if(!TestNotNull(TEXT("Fixture library exists"),F.Expression->Library.Get())) return false;
    auto* Wall=F.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(10,250,200));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
    Wall->SetActorLocation(FVector(F.Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+10,0,150));
    F.Expression->ServerPlayEmote(TEXT("dance1"));F.Step(.3f);
    if(!TestTrue(TEXT("The blocked-input check starts from a real active dance"),F.Expression->BodyAlpha()>.99f)) return false;
    const FVector Before=F.Hero->GetActorLocation();
    F.Hero->AddMovementInput(FVector::ForwardVector);F.Step(1.f/60.f);
    TestTrue(TEXT("A wall keeps the player below the old velocity cancellation threshold"),F.Hero->GetVelocity().Size2D()<25.f);
    TestTrue(TEXT("Cancellation does not need the player to move through the wall"),FVector::Dist2D(Before,F.Hero->GetActorLocation())<2.f);
    TestTrue(TEXT("The ordinary blocked movement intent still cancels the dance"),F.Expression->State.StoppedAt>=0);
    F.Step(.5f);Wall->Destroy();
    F.Expression->ServerPlayEmote(TEXT("dance2"));F.Step(.3f);
    const float Health=F.Hero->Status->State.Health;
    F.Hero->Status->Damage(10);F.Step(.1f);
    TestTrue(TEXT("Real damage continues to affect the dancing player"),F.Hero->Status->State.Health<Health);
    TestTrue(TEXT("Damage retains the original emote interruption"),F.Expression->State.StoppedAt>=0);
    F.Step(1.f);
    F.Expression->ServerPlayEmote(TEXT("dance3"));F.Step(.3f);
    if(!TestTrue(TEXT("The next dance can start after prior cancellation"),F.Expression->BodyAlpha()>.99f)) return false;
    F.Hero->SetPrimaryInputHeld(true);F.Step(.1f);
    TestTrue(TEXT("The ordinary primary action retains control during a dance"),F.Hero->IsPrimaryHeld());
    TestTrue(TEXT("Work retains the original emote interruption"),F.Expression->State.StoppedAt>=0);
    F.Hero->SetPrimaryInputHeld(false);F.Step(.3f);
    TestEqual(TEXT("Normal work cancellation blends the dance out"),F.Expression->BodyAlpha(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDanceSamplingAndTimedGesture,
    "MessControl.Animation.Dances.LoopPhaseAndTimedGesturesStayDistinct",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDanceSamplingAndTimedGesture::RunTest(const FString&)
{
    MCContinuousDanceTestsPrivate::FFixture F;
    if(!TestNotNull(TEXT("Fixture library exists"),F.Expression->Library.Get())) return false;
    auto* Dance=F.Expression->Library->Entries.FindByPredicate([](const FMCEmoteEntry& E){return E.Id==TEXT("dance1");});
    if(!TestTrue(TEXT("The legacy non-loop-flag dance has its real clip"),Dance && Dance->Animation)) return false;
    Dance->bLooping=false; // Native ID compatibility must also wrap this saved legacy entry.
    F.Expression->ServerPlayEmote(Dance->Id);F.Step(.4f);
    const auto& Ref=F.Hero->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    TArray<FTransform> First=Ref.GetRefBonePose(),NextCycle=First;
    F.Expression->BuildBodyPose(First,Ref);
    // A replicated late-join timestamp one clip earlier must select the same
    // body phase. This checks payload sampling, without claiming a network run.
    F.Expression->State.StartedAt-=Dance->Animation->GetPlayLength();
    F.Expression->BuildBodyPose(NextCycle,Ref);
    double LargestPositionError=0,LargestAngleError=0;
    for(int32 I=0;I<First.Num();++I) {
        LargestPositionError=FMath::Max(LargestPositionError,FVector::Dist(First[I].GetLocation(),NextCycle[I].GetLocation()));
        LargestAngleError=FMath::Max(LargestAngleError,First[I].GetRotation().AngularDistance(NextCycle[I].GetRotation()));
    }
    TestTrue(TEXT("Replicated timestamp sampling repeats the actual clip's pose each cycle"),LargestPositionError<.05 && LargestAngleError<.005);
    F.Hero->AddMovementInput(FVector::ForwardVector);F.Step(.5f);
    auto* Greeting=F.Expression->Library->Entries.FindByPredicate([](const FMCEmoteEntry& E){return E.Id==TEXT("hello");});
    if(!TestTrue(TEXT("A real non-dance body greeting exists"),Greeting && Greeting->Animation)) return false;
    Greeting->bLooping=true;Greeting->bLoopUntilMovement=false;Greeting->Duration=.6f;
    TestFalse(TEXT("A looping body clip is not automatically an endless dance"),Greeting->LoopsUntilMovement());
    F.Expression->ServerPlayEmote(Greeting->Id);F.Step(.3f);
    TestTrue(TEXT("The ordinary timed gesture still plays"),F.Expression->BodyAlpha()>.99f);
    F.Step(1.f);TestEqual(TEXT("The timed gesture still ends by its authored duration"),F.Expression->BodyAlpha(),0.f);
    FMCEmoteEntry Other=*Dance;Other.Id=TEXT("dance1_preview");Other.bLoopUntilMovement=false;
    TestFalse(TEXT("Dance compatibility matches complete IDs, not substrings/prefixes"),Other.LoopsUntilMovement());
    Other.bLoopUntilMovement=true;
    TestTrue(TEXT("New authored continuous dances can opt in through the existing data library"),Other.LoopsUntilMovement());
    Other.bFaceOnly=true;
    TestFalse(TEXT("Face-only expressions remain timed despite the body-loop flag"),Other.LoopsUntilMovement());
    return true;
}
#endif
