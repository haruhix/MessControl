#if !UE_BUILD_SHIPPING
#include "MCFoodActor.h"
#include "MCFoodEntrySettings.h"
#include "MCDayDirector.h"
#include "MCGameState.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Opt-in standalone L_Mouth test. It drives the current day director and the
// real saved egg collider, with screenshots from the normal player camera.
void MCTickFoodEntryValidation(UWorld* World)
{
    struct FPiece
    {
        TWeakObjectPtr<AMCFoodActor> Food;
        FVector Start=FVector::ZeroVector;
        double At=0;
        float TravelX=0,Rise=0;
        bool Landed=false;
    };
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCDayDirector> Director;
        TWeakObjectPtr<AMCFoodActor> PushFood;
        TArray<FPiece> Pieces;
        FMCFoodRow Egg;
        FVector Floor=FVector::ZeroVector,PushStart=FVector::ZeroVector;
        double At=0,PushAt=0,LastEntryAt=0;
        float Health=0,MaxPushSpeed=0,PushDistance=0;
        int32 Stage=0,Knockdowns=0;
        bool Started=false,Failed=false,Captured=false,VisibleIncoming=false,LandedCaptured=false;
    };
    static FRun Run;
    if(Run.World.Get()!=World) { Run=FRun();Run.World=World; }
    auto* PC=World->GetFirstPlayerController();
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* State=World->GetGameState<AMCGameState>();
    AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It;break; }
    if(!Hero || !State || !Tongue || World->GetTimeSeconds()<4) return;
#if WITH_EDITOR
    if(!Run.Started && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    const double Now=World->GetTimeSeconds();
    const auto Check=[&](bool Pass,const TCHAR* Message)
    {
        Run.Failed|=!Pass;
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ENTRY_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Message);
    };
    const auto Finish=[&]()
    {
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s FOOD_ENTRY pieces=%d push_speed=%.1f push_distance=%.1f"),Run.Failed?TEXT("FAIL"):TEXT("PASS"),Run.Pieces.Num(),Run.MaxPushSpeed,Run.PushDistance);
        FPlatformMisc::RequestExitWithStatus(false,Run.Failed?1:0);
    };
    if(!Run.Started)
    {
        Run.Started=true;Run.At=Now;
        auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Egg=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Food entry validation")):nullptr;
        if(!Egg) { Check(false,TEXT("saved egg menu row exists"));Finish();return; }
        Run.Egg=*Egg;
        if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        for(TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Destroy();
        for(TActorIterator<AMCThroat> It(World);It;++It) { It->ResetSwallow();It->SetActorTickEnabled(false); }
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();
        FHitResult Floor;
        if(!Tongue->SurfacePoint(Tongue->Surface->Bounds.Origin,Floor))
        { Check(false,TEXT("real tongue floor exists"));Finish();return; }
        Run.Floor=Floor.ImpactPoint;
        Hero->CancelGameplayInput();
        Hero->SetActorLocation(Run.Floor+FVector(150,-200,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->bManualCameraOrbit=true;Hero->bMouthCameraInitialized=false;
        Hero->CameraOrbitYaw=180;Hero->CameraOrbitPitch=-8;Hero->CameraOrbitDistance=1400;
        Hero->UpdateMouthCamera(1);PC->SetViewTarget(Hero);
        auto* Director=World->SpawnActor<AMCDayDirector>();Run.Director=Director;
        auto* Plan=NewObject<UMCDayPlan>(Director);
        auto* Menu=NewObject<UDataTable>(Plan);
        Menu->RowStruct=FMCFoodRow::StaticStruct();Menu->AddRow(TEXT("Egg"),Run.Egg);
        Plan->Menu=Menu;Plan->BreakfastCount=3;Plan->Steps.Reset();
        FMCDayStepSettings Rain;Rain.Step=EMCDayStep::BreakfastRain;Rain.Seconds=6;
        Rain.Title=FText::FromString(TEXT("ЕДА · ПООДИНОЧКЕ ЧЕРЕЗ ВХОД"));Plan->Steps.Add(Rain);
        FMCDayStepSettings Cleanup;Cleanup.Step=EMCDayStep::BreakfastCleanup;Cleanup.Seconds=0;
        Cleanup.Title=FText::FromString(TEXT("УБРАТЬ ЕДУ"));Plan->Steps.Add(Cleanup);
        // Demand comes from this fixture. The production API must emit exactly
        // one piece per request, regardless of any future difficulty director.
        Director->Start(Plan,0,true);
        Director->SetActorTickEnabled(false);
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("FoodEntry")),true);
        IFileManager::Get().Delete(*(FPaths::ProjectSavedDir()/TEXT("FoodEntry/Incoming_PlayerCamera.png")));
        IFileManager::Get().Delete(*(FPaths::ProjectSavedDir()/TEXT("FoodEntry/Landed_PlayerCamera.png")));
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ENTRY_START tongue=%s"),*Tongue->Surface->Bounds.GetBox().ToString());
    }
    const double Age=Now-Run.At;
    if(Run.Stage==0)
    {
        if(Run.Pieces.Num()<3 && Age>=.75+Run.Pieces.Num()*2.)
        {
            const auto Count=[](UWorld* W) { int32 N=0;for(TActorIterator<AMCFoodActor> It(W);It;++It) if(!It->IsDisposed() && !It->IsActorBeingDestroyed()) ++N;return N; };
            const int32 Before=Count(World);
            auto* Food=Run.Director->SpawnMenuFoodEntry(2);
            Check(Food && Count(World)==Before+1,TEXT("one production entry request creates exactly one whole ingredient"));
            if(!Food) { Finish();return; }
            // Snapshot in the same call as spawning, before a physics frame can
            // advance it past the front boundary or merge separate emissions.
            FPiece Piece;Piece.Food=Food;Piece.Start=Food->GetActorLocation();Piece.At=Now;
            Run.Pieces.Add(Piece);Run.LastEntryAt=Now;
            Check(Piece.Start.X<Tongue->Surface->Bounds.GetBox().Min.X,TEXT("a menu entry starts outside the front mouth opening"));
            Check(Food->Body->GetPhysicsLinearVelocity().X>100,TEXT("a menu entry has visible horizontal flight"));
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ENTRY_PIECE index=%d time=%.3f position=%s velocity=%s extent=%s"),Run.Pieces.Num(),Age,*Piece.Start.ToCompactString(),*Food->Body->GetPhysicsLinearVelocity().ToCompactString(),*Food->Body->GetScaledBoxExtent().ToCompactString());
        }
        for(FPiece& Piece:Run.Pieces)
        {
            if(!Piece.Food.IsValid()) continue;
            const FVector Position=Piece.Food->GetActorLocation();
            Run.Failed|=Position.ContainsNaN();
            Piece.TravelX=FMath::Max(Piece.TravelX,float(Position.X-Piece.Start.X));
            Piece.Rise=FMath::Max(Piece.Rise,float(Position.Z-Piece.Start.Z));
            Piece.Landed|=Piece.Food->Phase==EMCFoodPhase::Free;
        }
        if(!Run.Captured && Run.Pieces.Num()>0 && Run.Pieces[0].Food.IsValid()
            && Now-Run.Pieces[0].At>.2 && Now-Run.Pieces[0].At<1.2)
        {
            FVector2D Screen;int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);
            auto* Food=Run.Pieces[0].Food.Get();
            const bool Projected=PC->ProjectWorldLocationToScreen(Food->Visual->Bounds.Origin,Screen);
            const bool InView=Projected && Width>0 && Height>0 && Screen.X>Width*.2 && Screen.X<Width*.8 && Screen.Y>Height*.15 && Screen.Y<Height*.7;
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ENTRY_VIEW age=%.3f projected=%d screen=%.1f,%.1f viewport=%d,%d camera=%s food=%s"),Now-Run.Pieces[0].At,Projected,Screen.X,Screen.Y,Width,Height,*Hero->Camera->GetComponentLocation().ToCompactString(),*Food->Visual->Bounds.Origin.ToCompactString());
            if(InView)
            {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("FoodEntry/Incoming_PlayerCamera.png"),true,false);
                Run.Captured=Run.VisibleIncoming=true;
            }
        }
        if(!Run.LandedCaptured && Run.Pieces.Num()>0 && Run.Pieces[0].Landed)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("FoodEntry/Landed_PlayerCamera.png"),true,false);
            Run.LandedCaptured=true;
        }
        if(Run.Pieces.Num()==3 && Now>Run.LastEntryAt+3.5)
        {
            Check(Run.Pieces.Num()==3,TEXT("three production entry requests produce three separate ingredients"));
            const FPiece& First=Run.Pieces[0];
            Check(First.TravelX>300 && First.Rise>20,TEXT("live Chaos motion follows a rising front-to-back arc"));
            Check(First.Landed,TEXT("the first live ingredient reaches the mouth floor"));
            Check(Run.VisibleIncoming,TEXT("the incoming ingredient is inside the player's normal front view"));
            Run.Director->SetActorTickEnabled(false);
            for(FPiece& Piece:Run.Pieces) if(Piece.Food.IsValid()) Piece.Food->Destroy();
            Hero->SetActorLocation(Run.Floor+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
            Hero->GetCharacterMovement()->StopMovementImmediately();
            Run.PushStart=Hero->GetActorLocation();Run.Health=Hero->Status->State.Health;
            Run.Knockdowns=Hero->ToothPhysics->KnockdownCount;
            // A level approach isolates the entry-contact policy from ground
            // impacts. The collider and contact event are the saved egg's real ones.
            FTransform Transform(FRotator::ZeroRotator,Run.PushStart-FVector(600,0,0));
            auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!Food) { Check(false,TEXT("entry contact fixture spawns"));Finish();return; }
            FRandomStream Random(41);Food->ConfigureItem(TEXT("Egg"),Run.Egg,Random);
            // Keep the bottom of the real egg above the floor. At capsule-center
            // height this tall egg contacted the tongue first and ceased entry.
            const double CenterZ=Run.Floor.Z+Food->Body->GetScaledBoxExtent().Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.8;
            FVector Position=Transform.GetLocation();Position.Z=CenterZ;Transform.SetLocation(Position);
            Food->FinishSpawning(Transform);
            Food->BeginMouthEntry(FVector(650,0,0),140);Food->Body->SetEnableGravity(false);
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ENTRY_CONTACT_START hero=%s food=%s extent=%s floor=%.1f movement=%d body_state=%d"),*Hero->GetActorLocation().ToCompactString(),*Food->GetActorLocation().ToCompactString(),*Food->Body->GetScaledBoxExtent().ToCompactString(),Run.Floor.Z,int32(Hero->GetCharacterMovement()->MovementMode),int32(Hero->ToothPhysics->GetBodyState()));
            Run.PushFood=Food;Run.PushAt=Now;Run.Stage=1;
        }
    }
    else if(Run.Stage==1)
    {
        Run.MaxPushSpeed=FMath::Max(Run.MaxPushSpeed,float(Hero->GetVelocity().Size2D()));
        Run.PushDistance=FMath::Max(Run.PushDistance,float(Hero->GetActorLocation().X-Run.PushStart.X));
        if(Now>Run.PushAt+2.5)
        {
            if(Run.PushFood.IsValid()) UE_LOG(LogTemp,Display,TEXT("MC_FOOD_ENTRY_CONTACT_END hero=%s food=%s velocity=%s phase=%d active=%d impacts=%d"),*Hero->GetActorLocation().ToCompactString(),*Run.PushFood->GetActorLocation().ToCompactString(),*Run.PushFood->Body->GetPhysicsLinearVelocity().ToCompactString(),int32(Run.PushFood->Phase),Run.PushFood->IsMouthEntryActive(),Run.PushFood->ConfirmedImpacts);
            Check(Run.PushFood.IsValid() && Run.PushFood->ConfirmedImpacts>0,TEXT("a flying ingredient makes a real player contact"));
            Check(Run.MaxPushSpeed>20 && Run.PushDistance>5,TEXT("entry contact gives the player a small forward push"));
            Check(Run.MaxPushSpeed<250,TEXT("entry push stays below a strong knockback"));
            Check(FMath::IsNearlyEqual(Hero->Status->State.Health,Run.Health),TEXT("ordinary entering food causes no health damage"));
            Check(Hero->ToothPhysics->KnockdownCount==Run.Knockdowns,TEXT("ordinary entering food does not knock the player down"));
            Run.Stage=2;Run.PushAt=Now;
        }
    }
    else if(Run.Stage==2 && Now>Run.PushAt+.3) { Finish();return; }
    if(Age>25) { Check(false,TEXT("entry observation completed before timeout"));Finish(); }
}
#endif
