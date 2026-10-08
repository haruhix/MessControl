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
#include "MCNutLandingShadow.h"
#include "MCNutEnemy.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCBossCharacter.h"
#include "MCRewardChest.h"
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

/** Scripted authority smoke on the saved arena. Placement/care/delivery use gameplay APIs;
 *  obstacle extraction, breaking, spraying and nut damage use normal character inputs.
 *  This scope ends during support after nuts, before the next key event. */
void MCTickSingleDayValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCToothCharacter> Pawn;
        TWeakObjectPtr<ACameraActor> Camera;
        EMCTutorialStage Lesson=EMCTutorialStage::Finished;
        double StartedWall=0,LessonAt=0,PerkAt=0,EnemiesAt=0,DirectorAt=0,BossAt=0,WonAt=0,NextPullDebugAt=0,SupportCheckAt=0,SupportFinishAt=0;
        int64 TrainingExperience=0;
        int32 TrainingPoints=0,Checks=0,EnemyHitBaseline=0,ImpactSerial=0;
        float EnemyHealthBaseline=0;
        uint32 Shots=0;
        bool Failed=false,Finished=false,Initialized=false,FirstChoice=false,SawRain=false,SawAttack=false,SawEnemies=false,SawDirector=false,BossDefeated=false,SawFalling=false,ImpactChecked=false,ExtractionChecked=false,HandleAttempted=false;
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
        const FString Result=FString::Printf(TEXT("MC_SINGLE_DAY_TEST_%s checks=%d scope=scripted_saved_arena_training_personal_choice_directed_nuts_real_tool_boss_combat_support_before_next_key_event"),R.Failed?TEXT("FAIL"):TEXT("PASS"),R.Checks);
        UE_LOG(LogTemp,Display,TEXT("%s"),*Result);
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("SingleDaySmoke")),true);
        FFileHelper::SaveStringToFile(R.Report+Result+TEXT("\n"),*(FPaths::ProjectSavedDir()/TEXT("SingleDaySmoke/Validation.txt")));
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    };
    if(FPlatformTime::Seconds()-R.StartedWall>420) {Check(false,TEXT("Saved arena smoke timed out"));Finish();return;}
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
            R.Lesson=Director->Stage;R.LessonAt=Now;R.HandleAttempted=false;
            UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY_LESSON %d"),int32(R.Lesson));
        }
        const double Age=Now-R.LessonAt;
        if(R.Lesson==EMCTutorialStage::Loading || R.Lesson==EMCTutorialStage::Intro) return;
        if(R.Lesson==EMCTutorialStage::ToothpickPull || R.Lesson==EMCTutorialStage::ToothpickBreak || R.Lesson==EMCTutorialStage::ToothpickHeal) {
            AMCToothpick* Pick=nullptr;for(TActorIterator<AMCToothpick> It(World);It;++It) if(It->Batch==AMCTutorialDirector::TutorialFoodBatch) {Pick=*It;break;}
            if(!Pick) return;
            // The obstacle now enters from above. Wait for its real impact
            // before positioning the fixture beside the interaction point.
            if (Pick->State==EMCToothpickState::Falling) {
                R.SawFalling=true;
                if(Age>.15) Shot(8,TEXT("00_Toothpick_Falling"),Pick->GetActorLocation()+FVector(0,0,80));
                return;
            }
            if(!R.ImpactChecked) {
                R.ImpactChecked=true;R.ImpactSerial=Tongue->Motion.Serial;
                Check(R.SawFalling && Pick->Ulcer && Pick->Ulcer->bContactPainOnly && R.ImpactSerial>0
                    && Tongue->Motion.Settings.Height>=65,TEXT("Visible toothpick descent ends in one large impact with a contact-only wound"));
            }
            Place(Pick->GetActorLocation()+FVector(-100,0,0));
            if(R.Lesson==EMCTutorialStage::ToothpickPull) {
                if(Now>=R.NextPullDebugAt) {
                    R.NextPullDebugAt=Now+1;
                    const FVector Contact=Pick->Body->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation());
                    FHitResult Block;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSingleDayPullContact),false,Hero);Query.AddIgnoredActor(Pick);
                    World->LineTraceSingleByChannel(Block,Hero->GetActorLocation(),Contact,ECC_Visibility,Query);
                    UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY_PULL can_pull=%d can_work=%d can_contact=%d handling=%d held_food=%s body_state=%d reward=%s move_ignored=%d progress=%.3f los_block=%s"),
                        Pick->CanPull(Hero),Hero->CanWork(),Hero->CanContact(Pick),Hero->bHandling,*GetNameSafe(Hero->HeldFood.Get()),
                        int32(Hero->ToothPhysics->GetBodyState()),*GetNameSafe(Hero->RewardInteraction.Get()),PC->IsMoveInputIgnored(),Pick->PullProgress,*GetNameSafe(Block.GetActor()));
                }
                if(Age>.3) Shot(0,TEXT("01_Toothpick_Pull"),Pick->GetActorLocation()+FVector(0,0,80));
                if(Age>1.3) Shot(9,TEXT("00b_Toothpick_Impact"),Pick->GetActorLocation()+FVector(0,0,80));
                if(Age>.8) {
                    // The real impact can interrupt a held action by knocking the player down.
                    // Once pulling is available again, model an ordinary E release and new press.
                    if(!Hero->bHandling && Pick->CanPull(Hero)) {
                        Hero->SetHandleInputHeld(false);
                        if(R.HandleAttempted) UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY_PULL_RETRY knockdowns=%d progress=%.3f"),Hero->ToothPhysics->KnockdownCount,Pick->PullProgress);
                    }
                    Hero->SetHandleInputHeld(true);R.HandleAttempted=true;
                }
            } else if(R.Lesson==EMCTutorialStage::ToothpickBreak) {
                Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
                if(!R.ExtractionChecked) {
                    R.ExtractionChecked=true;
                    Check(Tongue->Motion.Serial==R.ImpactSerial,TEXT("Toothpick extraction does not create another pain wave"));
                }
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
        }
        if(R.SawRain && Nuts->Stage==EMCNutRainStage::Rainfall && (R.Shots&(1u<<4))==0) {
            AMCNutLandingShadow* Warning=nullptr;float Strength=0;
            for(AMCNutLandingShadow* Candidate:Nuts->LandingShadows) if(IsValid(Candidate) && Candidate->IsWarningActive()) {
                const float Value=Candidate->GetShadowStrength();
                if(Value>Strength) {Warning=Candidate;Strength=Value;}
            }
            // Observe a real active warning, allowing its normal fade-in to begin before capture.
            if(Warning) {
                UE_LOG(LogTemp,Display,TEXT("MC_SINGLE_DAY_RAIN_CAPTURE shadow=%s strength=%.3f impact_in=%.3f"),*Warning->GetName(),Strength,Warning->SecondsToImpact());
                Shot(4,TEXT("05_Nut_Rain"),Warning->GetActorLocation()+FVector(0,0,100));
            }
        }
        if(Nuts->Stage!=EMCNutRainStage::Enemies || Nuts->Enemies.IsEmpty()) return;
        if(!R.SawEnemies) {
            R.SawEnemies=true;R.EnemiesAt=Now;R.EnemyHealthBaseline=Hero->Status->State.Health;R.EnemyHitBaseline=Hero->ConfirmedHitCount;
            Check(R.SawRain && Nuts->Bosses.Num()==2 && Nuts->CompletedSeries>=4 && Nuts->EnemiesLeft>2,TEXT("Both bosses and weak creeps arrive after directed rain"));
        }
        if(Now-R.EnemiesAt>.3) Shot(5,TEXT("06_Nut_Enemies"),Nuts->Enemies[0]->GetActorLocation());
        AMCNutEnemy* Enemy=nullptr;for(AMCNutEnemy* Candidate:Nuts->Enemies) if(IsValid(Candidate) && Candidate->CanReceiveToolHit()) {Enemy=Candidate;break;}
        if(!Enemy) return;
        Place(Enemy->GetActorLocation()+FVector(-160,0,0));
        if(Hero->Status->State.Health<R.EnemyHealthBaseline) R.SawAttack=true;
        if(!R.SawAttack && Now-R.EnemiesAt<8) return;
        if(!R.SawAttack) {Check(false,TEXT("Awakened enemies did not perform a visible damaging attack"));Finish();return;}
        // This input/completion fixture restores its stationary worker after observing
        // actual incoming damage. Full bot runs measure survival without this aid.
        if(R.SawAttack && Hero->Status->State.Health<Hero->Status->State.MaxHealth*.75f) {
            FMCToothStatus Restored=Hero->Status->State;Restored.Health=Restored.MaxHealth;Hero->Status->Restore(Restored);
        }
        for(TActorIterator<AMCNutSpellProjectile> It(World);It;++It) Shot(9,TEXT("07_Mage_Fireball"),It->GetActorLocation());
        for(const AMCNutBoss* Boss:Nuts->Bosses) if(IsValid(Boss)) {
            if(Boss->Attack==EMCNutBossAttack::NutRain) Shot(6,TEXT("08_Mage_Rain"),Boss->LockedTarget);
            if(Boss->Attack==EMCNutBossAttack::Charge && Boss->State==EMCNutBossState::Telegraph) Shot(10,TEXT("09_Tank_Charge"),(Boss->LockedStart+Boss->LockedTarget)*.5);
            if(Boss->Attack==EMCNutBossAttack::Jump && Boss->State==EMCNutBossState::Telegraph) Shot(11,TEXT("10_Tank_Jump"),Boss->LockedTarget);
        }
        Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);Hero->SetPrimaryInputHeld(true);
        return;
    }
    if(R.SawEnemies && GS->SingleDayDirector->Stage==EMCSingleDayStage::Director) {
        Hero->SetPrimaryInputHeld(false);Hero->SetHandleInputHeld(false);
        if(!R.SawDirector) {
            R.SawDirector=true;R.DirectorAt=Now;
            const double Deadline=GS->DirectorState.DayEndAt;
            const double SupportSeconds=Deadline>Now?FMath::Min(34.,FMath::Max(.1,Deadline-Now-1.)):34.;
            R.SupportCheckAt=Now+SupportSeconds;
            R.SupportFinishAt=Deadline>Now?FMath::Min(R.SupportCheckAt+1.,Deadline-.1):Now+35.;
            Check(R.SawAttack,TEXT("Nut enemies actually attacked the player"));
            Check(Hero->ConfirmedHitCount>R.EnemyHitBaseline,TEXT("Normal tool swings defeated the nut enemies"));
            Check(GS->Day==1 && R.Pawn.Get()==Hero && !PS->Perks->ActivePerks.IsEmpty(),TEXT("The director interval retains the same day, pawn and perk"));
        }
        Place(Tongue->Surface->Bounds.Origin+FVector(-350,250,0));
        for(TActorIterator<AMCPlayerState> It(World);It;++It) if(It->HasPendingLevelChoices() && It->LevelUpOffer.IsValid())
            Check(It->TryChooseLevelUpPerk(It->LevelUpOffer.OfferId,0),TEXT("Earned completion XP grants another personal choice"));
        if(Now>=R.SupportCheckAt) {
          if(R.WonAt==0) {
            R.WonAt=Now;
            Check(!GS->SingleDayDirector->bAuthoredFragmentComplete && GS->Phase==EMCShiftPhase::Working,
                TEXT("Completing nuts keeps the same run active with the next authored event pending"));
            Check(GS->DirectorState.DayEndAt>Now,
                TEXT("Support retains the authored interval before the next key event"));
            Check(!GS->SingleDayDirector->FinalBoss && GS->SingleDayDirector->Stage==EMCSingleDayStage::Director,
                TEXT("Support cannot start the obsolete final boss"));
            Check(GS->DirectorDecisionLog.Num()>5,TEXT("Persistent director history records the ordered encounter and support decisions"));
          }
            Shot(7,TEXT("11_Director_Support"),FVector::ZeroVector,false);
            if(Now>=R.SupportFinishAt) { Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Finish(); }
        }
        return;
    }
}
#endif
