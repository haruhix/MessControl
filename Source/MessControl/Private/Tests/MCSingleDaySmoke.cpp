#if !UE_BUILD_SHIPPING
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCSingleDayDirector.h"
#include "MCTutorialDirector.h"
#include "MCToothpick.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCPlayerState.h"
#include "MCProgressionComponent.h"
#include "MCPerkComponent.h"
#include "MCPerkChoiceWidget.h"
#include "MCNutRainEvent.h"
#include "MCNutEnemy.h"
#include "MCBossCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

/** Scripted authority smoke on the saved arena. Placement/care/delivery and final boss damage use
 *  gameplay APIs; obstacle extraction, breaking, spraying and nut damage use normal character inputs.
 *  The event runner, director interval, boss spawn and victory transition advance naturally. */
void MCTickSingleDayValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCToothCharacter> Pawn;
        TWeakObjectPtr<ACameraActor> Camera;
        EMCTutorialStage Lesson=EMCTutorialStage::Finished;
        double StartedWall=0,LessonAt=0,PerkAt=0,EnemiesAt=0,DirectorAt=0,BossAt=0,WonAt=0;
        int64 TrainingExperience=0;
        int32 TrainingPoints=0,Checks=0,EnemyHitBaseline=0;
        float EnemyHealthBaseline=0;
        uint32 Shots=0;
        bool Failed=false,Finished=false,Initialized=false,FirstChoice=false,SawRain=false,SawAttack=false,SawEnemies=false,SawDirector=false,BossDefeated=false;
        FString Report;
    };
    static FRun R;
    if(R.World!=World) {R=FRun();R.World=World;R.StartedWall=FPlatformTime::Seconds();}
    if(R.Finished || World->GetNetMode()==NM_Client) return;
    auto Check=[&](bool Passed,const TCHAR* Message) {
        ++R.Checks;R.Failed|=!Passed;
        const FString Line=FString::Printf(TEXT("MC_SINGLE_DAY_CHECK %s %s\n"),Passed?TEXT("PASS"):TEXT("FAIL"),Message);
        R.Report+=Line;UE_LOG(LogTemp,Display,TEXT("%s"),*Line.TrimEnd());
    };
    auto Finish=[&]() {
        R.Finished=true;
        const FString Result=FString::Printf(TEXT("MC_SINGLE_DAY_TEST_%s checks=%d scope=scripted_saved_arena_training_first_choice_nut_combat_natural_director_boss_spawn_authority_boss_damage_victory"),R.Failed?TEXT("FAIL"):TEXT("PASS"),R.Checks);
        UE_LOG(LogTemp,Display,TEXT("%s"),*Result);
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("SingleDaySmoke")),true);
        FFileHelper::SaveStringToFile(R.Report+Result+TEXT("\n"),*(FPaths::ProjectSavedDir()/TEXT("SingleDaySmoke/Validation.txt")));
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    };
    if(FPlatformTime::Seconds()-R.StartedWall>180) {Check(false,TEXT("Saved arena smoke timed out"));Finish();return;}
    auto* GS=World->GetGameState<AMCGameState>();auto* PC=World->GetFirstPlayerController();
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* PS=Hero?Hero->GetPlayerState<AMCPlayerState>():nullptr;
    auto* Mode=World->GetAuthGameMode<AMCGameMode>();
    if(!GS || !Hero || !PS || !Mode || !GS->Progression || !GS->SingleDayDirector || FPlatformTime::Seconds()-R.StartedWall<3) return;
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(World);It;++It) {Tongue=*It;break;}
    if(!Tongue) return;
    const double Now=GS->GetServerWorldTimeSeconds();
    auto Place=[&](FVector Near) {
        FHitResult Floor;if(!Tongue->SurfacePoint(Near,Floor)) return false;
        Hero->GetCharacterMovement()->StopMovementImmediately();Hero->GetCharacterMovement()->DisableMovement();
        Hero->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        Hero->ToothPhysics->TryRecover();return true;
    };
    auto Shot=[&](int32 Index,const TCHAR* Name,FVector Aim,bool bMoveCamera=true) {
        if((R.Shots&(1u<<Index))!=0) return;R.Shots|=1u<<Index;
        if(!FParse::Param(FCommandLine::Get(),TEXT("MCSingleDayCapture")) || World->GetNetMode()==NM_DedicatedServer) return;
        if(bMoveCamera) {
            if(!R.Camera.IsValid()) R.Camera=World->SpawnActor<ACameraActor>();
            const FVector Offset(-400,520,410);R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());
            R.Camera->GetCameraComponent()->SetFieldOfView(55);PC->SetViewTarget(R.Camera.Get());
        }
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("SingleDaySmoke");IFileManager::Get().MakeDirectory(*Directory,true);
        FScreenshotRequest::RequestScreenshot(Directory/(FString(Name)+TEXT(".png")),true,false);
    };
    if(!R.Initialized) {
        R.Initialized=true;R.Pawn=Hero;R.TrainingExperience=GS->Progression->TotalExperience;R.TrainingPoints=PS->Points;
        Check(GS->bSingleDayLoop && GS->bTutorialActive,TEXT("Saved arena starts in the new training sequence"));
        Check(GS->Progression->TeamLevel==1,TEXT("The team starts at level one"));
        if(R.Failed) {Finish();return;}
    }
    if(GS->Phase==EMCShiftPhase::Lost) {Check(false,TEXT("The ordered arena sequence entered a losing state"));Finish();return;}
    if(GS->bTutorialActive) {
        auto* Director=Mode->TutorialDirector.Get();if(!Director) return;
        for(const auto& Player:Director->Players) Director->SetLoaded(Player.PlayerState);
        if(GS->Progression->TotalExperience!=R.TrainingExperience || PS->Points!=R.TrainingPoints) {Check(false,TEXT("Teaching actions awarded XP before training completion"));Finish();return;}
        if(Director->Stage!=R.Lesson) {
            Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);
            R.Lesson=Director->Stage;R.LessonAt=Now;
            UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY_LESSON %d"),int32(R.Lesson));
        }
        const double Age=Now-R.LessonAt;
        if(R.Lesson==EMCTutorialStage::Loading || R.Lesson==EMCTutorialStage::Intro) return;
        if(R.Lesson==EMCTutorialStage::ToothpickPull || R.Lesson==EMCTutorialStage::ToothpickBreak || R.Lesson==EMCTutorialStage::ToothpickHeal) {
            AMCToothpick* Pick=nullptr;for(TActorIterator<AMCToothpick> It(World);It;++It) if(It->Batch==AMCTutorialDirector::TutorialFoodBatch) {Pick=*It;break;}
            if(!Pick) return;
            Place(Pick->GetActorLocation()+FVector(-100,0,0));
            if(R.Lesson==EMCTutorialStage::ToothpickPull) {
                if(Age>.3) Shot(0,TEXT("01_Toothpick_Pull"),Pick->GetActorLocation()+FVector(0,0,80));
                if(Age>.8) Hero->SetHandleInputHeld(true);
            } else if(R.Lesson==EMCTutorialStage::ToothpickBreak) {
                Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
                if(Age>.3) Shot(1,TEXT("02_Toothpick_Pickaxe"),Pick->GetActorLocation()+FVector(0,0,55));
                if(Age>.8) {Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);Hero->SetPrimaryInputHeld(true);}
            } else {
                if(Age>.8) {Hero->Inventory->ServerSelect(EMCToolSlot::Spray);Hero->SetPrimaryInputHeld(true);}
                if(Pick->Ulcer && Pick->Ulcer->Healing>.3f) Shot(2,TEXT("03_Toothpick_Spray"),Pick->GetActorLocation()+FVector(0,0,55));
            }
            return;
        }
        if(Age<1) return;
        const auto Learners=Director->Players;
        for(const auto& Player:Learners) {
            auto* Worker=Player.PlayerState?Cast<AMCToothCharacter>(Player.PlayerState->GetPawn()):nullptr;
            auto* Target=Player.GoalTarget.Get();if(!Worker || !Target || Player.StageRequired==0 || Player.StageProgress>=Player.StageRequired) continue;
            if(R.Lesson==EMCTutorialStage::BrushTooth || R.Lesson==EMCTutorialStage::CoffeeCleanup) {
                Worker->Inventory->ServerSelect(EMCToolSlot::Brush);
                if(auto* Status=Target->FindComponentByClass<UMCToothStatusComponent>()) while(Status->NeedsCare(true)) Status->CareContact(true,Worker);
            } else if(R.Lesson==EMCTutorialStage::FoodCut) {
                Worker->Inventory->ServerSelect(EMCToolSlot::Knife);
                if(auto* Food=Cast<AMCFoodActor>(Target);Food && Worker->Inventory->CanBreak(Food) && Food->HitFood(100,Worker->GetActorForwardVector())) Director->NotifyAction(Worker,EMCTutorialAction::FoodCut,Food);
            } else if(R.Lesson==EMCTutorialStage::FreshSort) {
                auto* Food=Cast<AMCFoodActor>(Target);if(!Food || Food->Phase==EMCFoodPhase::Swallowing || Food->IsDisposed()) continue;
                for(TActorIterator<AMCThroat> It(World);It;++It) {
                    Food->SetStackCarrier(Worker);Food->SetStackCarrier(nullptr);
                    Food->SetActorLocation(It->GetActorTransform().TransformPosition(It->ZoneCenter+FVector(0,0,60)),false,nullptr,ETeleportType::TeleportPhysics);
                    Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);It->AcceptDelivery(Food);break;
                }
            }
        }
        return;
    }
    if(GS->SingleDayDirector->Stage==EMCSingleDayStage::FirstPerk) {
        Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);
        if(R.PerkAt==0) {
            R.PerkAt=Now;Check(R.Pawn.Get()==Hero,TEXT("Tutorial handoff preserves the same pawn"));
            Check(GS->Progression->TeamLevel==2 && PS->LevelUpOffer.PerkIDs.Num()==3 && PS->PendingLevelChoices==1,TEXT("Training grants one common level and three personal choices"));
        }
        if(Now-R.PerkAt>.7) {
            TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UMCPerkChoiceWidget::StaticClass(),false);
            if(Widgets.IsEmpty()) return;
            Shot(3,TEXT("04_First_Personal_Perk"),FVector::ZeroVector,false);
        }
        if(Now-R.PerkAt>2) {
            for(TActorIterator<AMCPlayerState> It(World);It;++It) if(It->HasPendingLevelChoices() && It->LevelUpOffer.IsValid())
                Check(It->TryChooseLevelUpPerk(It->LevelUpOffer.OfferId,0),TEXT("A personal choice claims its offered perk"));
            R.FirstChoice=true;
        }
        return;
    }
    auto* Nuts=GS->SingleDayDirector->NutEvent.Get();
    if(GS->SingleDayDirector->Stage==EMCSingleDayStage::Nuts && Nuts) {
        if(!R.SawRain && Nuts->Stage==EMCNutRainStage::Rainfall && Nuts->NutsSpawned>3) {
            R.SawRain=true;Check(R.FirstChoice && R.Pawn.Get()==Hero && !PS->Perks->ActivePerks.IsEmpty(),TEXT("Nuts begin after the first choice with the pawn and perk preserved"));
            Shot(4,TEXT("05_Nut_Rain"),Tongue->Surface->Bounds.Origin+FVector(0,0,100));
        }
        if(Nuts->Stage!=EMCNutRainStage::Enemies || Nuts->Enemies.IsEmpty()) return;
        if(!R.SawEnemies) {
            R.SawEnemies=true;R.EnemiesAt=Now;R.EnemyHealthBaseline=Hero->Status->State.Health;R.EnemyHitBaseline=Hero->ConfirmedHitCount;
            Check(R.SawRain && Nuts->EnemiesLeft>0,TEXT("Remaining nuts awaken after rainfall"));
        }
        if(Now-R.EnemiesAt>.3) Shot(5,TEXT("06_Nut_Enemies"),Nuts->Enemies[0]->GetActorLocation());
        AMCNutEnemy* Enemy=nullptr;for(AMCNutEnemy* Candidate:Nuts->Enemies) if(IsValid(Candidate) && Candidate->CanReceiveToolHit()) {Enemy=Candidate;break;}
        if(!Enemy) return;
        Place(Enemy->GetActorLocation()+FVector(-100,0,0));
        if(Hero->Status->State.Health<R.EnemyHealthBaseline) R.SawAttack=true;
        if(!R.SawAttack && Now-R.EnemiesAt<5) return;
        if(!R.SawAttack) {Check(false,TEXT("Awakened enemies did not perform a visible damaging attack"));Finish();return;}
        Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);Hero->SetPrimaryInputHeld(true);
        return;
    }
    if(R.SawEnemies && GS->SingleDayDirector->Stage==EMCSingleDayStage::Director) {
        Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);
        if(!R.SawDirector) {
            R.SawDirector=true;R.DirectorAt=Now;
            Check(R.SawAttack,TEXT("Nut enemies actually attacked the player"));
            Check(Hero->ConfirmedHitCount>R.EnemyHitBaseline,TEXT("Normal tool swings defeated the nut enemies"));
            Check(GS->Day==1 && R.Pawn.Get()==Hero && !PS->Perks->ActivePerks.IsEmpty(),TEXT("The director interval retains the same day, pawn and perk"));
        }
        // Keep the scripted participant on tissue while the real timed scheduler runs.
        Place(Tongue->Surface->Bounds.Origin+FVector(-350,250,0));
        for(TActorIterator<AMCPlayerState> It(World);It;++It) if(It->HasPendingLevelChoices() && It->LevelUpOffer.IsValid())
            Check(It->TryChooseLevelUpPerk(It->LevelUpOffer.OfferId,0),TEXT("A later earned personal choice resolves during the director interval"));
        return;
    }
    if(GS->SingleDayDirector->Stage==EMCSingleDayStage::Boss) {
        auto* Boss=GS->SingleDayDirector->FinalBoss.Get();if(!IsValid(Boss)) return;
        Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);
        if(R.BossAt==0) {
            R.BossAt=Now;
            Check(R.SawDirector && Now-R.DirectorAt>=29,TEXT("The real thirty-second director interval precedes the final boss"));
            Check(Boss->IsBossAlive() && Boss->Runtime.State!=EMCBossState::Dormant,TEXT("The event runner spawns and activates a living final boss"));
            Check(Boss->GetClass()!=AMCBossCharacter::StaticClass(),TEXT("The final boss uses its saved authored Blueprint class"));
            Check(GS->Phase==EMCShiftPhase::Working && GS->Day==1 && GS->DirectorState.NextTitle.IsEmpty(),
                TEXT("The final encounter keeps day one active without offering another next event"));
        }
        if(Now-R.BossAt>1.5) Shot(6,TEXT("07_Final_Boss"),Boss->GetActorLocation()+FVector(0,0,100));
        if(Now-R.BossAt>=3 && !R.BossDefeated) {
            // Deliberate authority damage verifies encounter completion, not a full player-input boss fight.
            const float Applied=Boss->ReceiveBossDamage(Boss->Runtime.MaxHealth+1,Hero);
            R.BossDefeated=true;
            Check(Applied>0 && !Boss->IsBossAlive(),TEXT("Scripted authority weapon damage defeats the final boss"));
        }
        return;
    }
    if(GS->SingleDayDirector->Stage==EMCSingleDayStage::Complete) {
        if(R.WonAt==0) {
            R.WonAt=Now;
            Check(R.BossDefeated && GS->Phase==EMCShiftPhase::Won,TEXT("Final boss death naturally completes the run with victory"));
            Check(GS->Day==1 && GS->RunSettings.DaysToSurvive==1 && GS->TasksLeft==0
                && GS->DirectorState.NextTitle.IsEmpty() && GS->DirectorState.Instruction.IsEmpty()
                && GS->DirectorState.FinishedFood==GS->DirectorState.SpawnedFood,
                TEXT("Victory finishes the sole day and clears completed encounter instructions"));
            Check(R.Pawn.Get()==Hero && !PS->Perks->ActivePerks.IsEmpty(),TEXT("Victory preserves the pawn and earned personal perks"));
        }
        if(Now-R.WonAt>.3) Shot(7,TEXT("08_Single_Day_Victory"),FVector::ZeroVector,false);
        if(Now-R.WonAt>=1) {Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Finish();}
        return;
    }
}
#endif
