#if !UE_BUILD_SHIPPING
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCTaskActor.h"
#include "MCReactionVFX.h"
#include "MCGazeComponent.h"
#include "MCExpressionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Drives production gameplay APIs in L_Mouth and records the actual game target.
void MCTickFoodReworkValidation(UWorld* W)
{
    struct FRun {
        TWeakObjectPtr<UWorld> W;TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<AMCFoodActor> Food;
        TWeakObjectPtr<AMCThroat> Throat;TWeakObjectPtr<AMCFirePatch> Fire;
        TArray<TWeakObjectPtr<AMCFoodActor>> Foods;
        double At=0;int32 Stage=0,MaxStack=0,FallenBeforeMotion=0;bool Started=false,Failed=false,Impact=false,Stretched=false,Squashed=false,MotionStarted=false;
        FVector P,Start;float FreshStart=0,MinScaleZ=FLT_MAX,MaxScaleZ=0;double RestImpact=-100,NextDiagnostic=0;float YawnDrag=0,HandGap=FLT_MAX,StackTilt=0;
        bool FoodImpactParticles=false,RedFlash=false,YawnEyesFocused=false;
    };static FRun R;if(R.W!=W) {R=FRun();R.W=W;}
    FString Case;if(!FParse::Value(FCommandLine::Get(),TEXT("MCFoodRework="),Case)) return;
    auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=W->GetGameState<AMCGameState>();AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(W);It;++It) {Tongue=*It;break;}
    if(!H || !GS || !Tongue || W->GetTimeSeconds()<4) return;
#if WITH_EDITOR
    if(!R.Started && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto Check=[&](bool OK,const TCHAR* Text){R.Failed|=!OK;UE_LOG(LogTemp,Display,TEXT("MC_FOOD_REWORK_CHECK %s %s %s"),*Case,OK?TEXT("PASS"):TEXT("FAIL"),Text);};
    auto Finish=[&](){UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s FOOD_REWORK_%s"),R.Failed?TEXT("FAIL"):TEXT("PASS"),*Case);FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);};
    auto Camera=[&](FVector Focus,FVector Offset){if(!R.Camera.IsValid()) R.Camera=W->SpawnActor<ACameraActor>();R.Camera->SetActorLocation(Focus+Offset);R.Camera->SetActorRotation((-Offset).Rotation());R.Camera->GetCameraComponent()->SetFieldOfView(50);PC->SetViewTarget(R.Camera.Get());};
    auto Spawn=[&](FName Name,FVector P,bool Fragment=false){
        const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Table?Table->FindRow<FMCFoodRow>(Name,TEXT("Food rework capture")):nullptr;
        if(!Row) return static_cast<AMCFoodActor*>(nullptr);
        const FTransform T(P);auto* F=W->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(F) {FRandomStream Random(41);F->ConfigureItem(Name,*Row,Random,Fragment);F->FinishSpawning(T);R.Foods.Add(F);}return F;
    };
    if(!R.Started) {
        R.Started=true;R.At=W->GetTimeSeconds();if(auto* Mode=W->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
        GS->bDevManualEvents=true;GS->bPhysicalBrushes=false;GS->Phase=EMCShiftPhase::Working;GS->PhaseEndsAt=0;GS->bDayOneComplete=false;
        GS->DayPlan=nullptr;GS->CurrentEvent=NewObject<UMCDayEvent>(GS);
        GS->CurrentEvent->Title=FText::FromString(Case==TEXT("FoodDamage")?TEXT("ПОЖАР И ЯЗВА"):Case==TEXT("FoodYawn")?TEXT("ЗЕВАНИЕ"):Case==TEXT("FoodThroat")?TEXT("ДОСТАВЬ ЕДУ В ГЛОТКУ"):Case==TEXT("FoodStars")?TEXT("ЗАВЕРШЕНИЕ ЗАДАЧ"):TEXT("УБОРКА ЕДЫ"));
        Tongue->bAutomaticYawns=false;Tongue->ResetPain();Tongue->SetActorTickEnabled(true);
        for(TActorIterator<AMCFoodActor> It(W);It;++It) It->Destroy();
        for(TActorIterator<AMCTaskActor> It(W);It;++It) It->Destroy();
        for(TActorIterator<AMCMouthSurface> It(W);It;++It) It->Destroy();
        for(TActorIterator<AMCThroat> It(W);It;++It) {It->ResetSwallow();It->SetActorTickEnabled(false);}
        H->CancelGameplayInput();H->Inventory->ServerSelect(EMCToolSlot::Brush);
        FHitResult Floor;FVector P=Tongue->Surface->Bounds.Origin;
        if(!Tongue->SurfacePoint(P,Floor)) {Check(false,TEXT("tongue fixture has a real floor"));Finish();return;}
        R.P=Floor.ImpactPoint;H->SetActorLocation(R.P+FVector(-90,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
        H->SetActorRotation(FRotator::ZeroRotator);H->GetCharacterMovement()->StopMovementImmediately();R.Start=H->GetActorLocation();
        Camera(R.P+FVector(0,0,95),FVector(-460,-620,330));
        if(Case==TEXT("FoodReaction")) {R.Food=Spawn(TEXT("Egg"),R.P+FVector(70,0,450));if(R.Food.IsValid()) R.Food->Health=500;Camera(R.P+FVector(40,0,180),FVector(-460,-620,330));}
        if(Case==TEXT("FoodCollect") || Case==TEXT("FoodBalance")) {
            for(int32 I=0;I<4;++I) {auto* F=Spawn(TEXT("Egg"),R.P+FVector(20+(I/2)*25,(I%2?1:-1)*45,55),true);if(F) F->Phase=EMCFoodPhase::Free;}
        }
        if(Case==TEXT("FoodThroat")) {
            for(TActorIterator<AMCThroat> It(W);It;++It) {R.Throat=*It;It->SetActorTickEnabled(true);break;}
            if(!R.Throat.IsValid()) {Check(false,TEXT("authored throat exists"));Finish();return;}
            R.P=R.Throat->GetActorTransform().TransformPosition(R.Throat->ZoneCenter);
            FHitResult F;if(Tongue->SurfacePoint(R.P,F)) R.P=F.ImpactPoint;
            H->SetActorLocation(R.P+FVector(-200,-100,70),false,nullptr,ETeleportType::TeleportPhysics);
            Camera((R.Throat->VacuumInlet()+H->GetActorLocation())*.5f+FVector(0,0,17),R.Throat->GetActorTransform().TransformVectorNoScale(FVector(-1300,-500,150)));
            R.Camera->GetCameraComponent()->SetFieldOfView(58);
        }
        if(Case==TEXT("FoodYawn")) {
            for(TActorIterator<AMCThroat> It(W);It;++It) {R.Throat=*It;It->SetActorTickEnabled(true);break;}
            if(!R.Throat.IsValid()) {Check(false,TEXT("authored mouth exists for the yawn"));Finish();return;}
            const FTransform Mouth=R.Throat->GetActorTransform();
            const FVector Probe=Mouth.TransformPosition(R.Throat->ZoneCenter)-R.Throat->GetActorForwardVector()*420-R.Throat->GetActorRightVector()*80;
            FHitResult NearMouth;if(!Tongue->SurfacePoint(Probe,NearMouth)) {Check(false,TEXT("walkable tongue in front of the mouth"));Finish();return;}
            R.P=NearMouth.ImpactPoint;H->SetActorLocation(R.P+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
            R.Start=H->GetActorLocation();H->bBrushing=true;H->CareTarget=Tongue;
            const FVector Inlet=R.Throat->VacuumInlet();
            Camera((Inlet+H->GetActorLocation())*.5f+FVector(0,0,17),Mouth.TransformVectorNoScale(FVector(-1300,-500,150)));
            R.Camera->GetCameraComponent()->SetFieldOfView(58);
            R.Food=Spawn(TEXT("Egg"),R.P+R.Throat->GetActorForwardVector()*45+R.Throat->GetActorRightVector()*40+FVector(0,0,50),true);
        }
        if(Case==TEXT("FoodSpoil")) {R.Food=Spawn(TEXT("Egg"),R.P+FVector(45,0,60));if(R.Food.IsValid()) {R.Food->Phase=EMCFoodPhase::Free;R.Food->SpoilAt=W->GetTimeSeconds()+180;R.FreshStart=W->GetTimeSeconds();}}
        if(Case==TEXT("FoodDamage")) {H->Inventory->ServerSelect(EMCToolSlot::Spray);R.Food=Spawn(TEXT("SpicyPepper"),R.P+FVector(80,0,450));H->SetActorLocation(R.P+FVector(-220,-180,70));Camera(R.P+FVector(60,0,120),FVector(-620,-800,470));}

        if(Case==TEXT("FoodStars")) {auto* Task=W->SpawnActor<AMCTaskActor>(R.P+FVector(40,0,30),FRotator::ZeroRotator);auto* Event=NewObject<UMCDayEvent>();Event->Kind=EMCTaskKind::Coffee;Event->WorkSeconds=1;Task->Initialize(Event);}
        UE_LOG(LogTemp,Display,TEXT("MC_FOOD_REWORK_START %s"),*Case);
    }
    const double T=W->GetTimeSeconds()-R.At;
    // Keep the review footage clear; gameplay HUD still shows stack controls.
    for(TActorIterator<AMCFoodActor> It(W);It;++It) It->Label->SetVisibility(false);
    if(R.Throat.IsValid()) R.Throat->Label->SetVisibility(false);
    R.MaxStack=FMath::Max(R.MaxStack,H->FoodCollection->Pieces.Num());
    if(Case==TEXT("FoodReaction") && R.Food.IsValid()) {
        const auto* F=R.Food.Get();const FVector Base=F->bFragment?F->FoodData.FragmentScale:F->FoodData.Scale;
        R.MinScaleZ=FMath::Min(R.MinScaleZ,float(F->Visual->GetRelativeScale3D().Z));R.MaxScaleZ=FMath::Max(R.MaxScaleZ,float(F->Visual->GetRelativeScale3D().Z));
        R.Stretched|=F->Visual->GetRelativeScale3D().Z>Base.Z*1.1;R.Squashed|=F->Visual->GetRelativeScale3D().Z<Base.Z*.95;
        R.Impact|=F->ImpactAt>R.At;
        R.RedFlash|=F->Visual->GetOverlayMaterial()!=nullptr;
        for(TActorIterator<AMCReactionVFX> It(W);It;++It) R.FoodImpactParticles|=It->Effect==EMCReactionEffect::Impact;
        if(T>2.5 && R.Stage==0) {R.RestImpact=R.Food->ImpactAt;++R.Stage;}
        if(T>4 && R.Stage==1) {Check(R.Food->ImpactAt==R.RestImpact,TEXT("resting food never generates incidental impact flashes"));R.Food->HitFood(10,FVector::RightVector);++R.Stage;}
        if(T>6 && R.Stage==2) {R.Food->HitFood(10,-FVector::RightVector);++R.Stage;}
        if(T>8) {
            UE_LOG(LogTemp,Display,TEXT("MC_FOOD_REWORK_REACTION stretch=%d squash=%d impact=%d scaleZ=%.3f..%.3f base=%.3f"),R.Stretched,R.Squashed,R.Impact,R.MinScaleZ,R.MaxScaleZ,Base.Z);
            Check(R.Stretched && R.Squashed && R.Impact && R.RedFlash,TEXT("fall stretch, light bounce and red hit flash observed"));
            Check(!R.FoodImpactParticles && R.MinScaleZ>=Base.Z*.85,TEXT("landing and damage use a small deformation without impact particles"));Finish();
        }
    } else if(Case==TEXT("FoodCollect") || Case==TEXT("FoodBalance")) {
        if(T>1.5 && R.Stage==0) {H->ServerSetPrimary(true);H->ServerSetPrimary(false);++R.Stage;Check(H->FoodCollection->bCollecting,TEXT("one click toggles collection and release keeps it active"));}
        if(T>(Case==TEXT("FoodBalance")?3:5) && R.Stage==1) {Check(R.MaxStack>=3,TEXT("three or more real food pieces form a stack"));++R.Stage;}
        if(Case==TEXT("FoodCollect") && T>7 && R.Stage==2) {H->ServerSetPrimary(true);H->ServerSetPrimary(false);Check(!H->FoodCollection->bCollecting && H->FoodCollection->Pieces.IsEmpty(),TEXT("second click releases the stack"));++R.Stage;}
        if(Case==TEXT("FoodBalance") && T>3.2 && T<3.8) {
            if(!R.MotionStarted) {R.MotionStarted=true;R.FallenBeforeMotion=H->FoodCollection->FallenPieces;Check(H->FoodCollection->Pieces.Num()>=3,TEXT("movement begins while three physical pieces are still supported"));}
            H->AddMovementInput(FVector::RightVector,1);
        }
        if(Case==TEXT("FoodBalance") && T>3.8 && T<4.4) H->AddMovementInput(-FVector::RightVector,1);
        if(Case==TEXT("FoodBalance") && !H->FoodCollection->Pieces.IsEmpty())
            R.StackTilt=FMath::Max(R.StackTilt,float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(H->FoodCollection->Pieces.Last()->GetActorUpVector().Z,-1.,1.)))));
        if(Case==TEXT("FoodBalance") && T>5.2 && R.Stage==2) {
            Check(R.MotionStarted && H->FoodCollection->Pieces.Num()>=3 && H->FoodCollection->FallenPieces==R.FallenBeforeMotion,TEXT("ordinary movement retains the complete hand stack"));
            Check(R.StackTilt>.5f && R.StackTilt<=12.1f,TEXT("the rendered stack sways within a bounded angle"));
            H->Status->Damage(1,FVector::RightVector);++R.Stage;
            Check(H->FoodCollection->Pieces.IsEmpty() && !H->FoodCollection->bCollecting,TEXT("a received hit spills the complete load"));
        }
        if(T>9) {if(Case==TEXT("FoodBalance")) Check(R.Stage==3 && H->FoodCollection->FallenPieces>R.FallenBeforeMotion,TEXT("released pieces resume physical falling after impact"));Finish();}
    } else if(Case==TEXT("FoodThroat")) {
        if(T>1.5 && R.Stage==0) {R.Food=Spawn(TEXT("Egg"),R.P+FVector(-80,0,75),true);++R.Stage;}
        if(T>5 && R.Stage==1) {Check(R.Throat->FoodSwallowed>0 && !H->SwallowedBy,TEXT("ingredient swallowed automatically; player remains outside intake"));Spawn(TEXT("Carrot"),R.P+FVector(30,0,70),true);++R.Stage;}
        if(T>9) {Check(R.Throat->FoodSwallowed>=2 && R.Throat->UvulaLanding->GetCollisionEnabled()==ECollisionEnabled::NoCollision,TEXT("repeat intake works and decorative uvula has no trigger"));Finish();}
    } else if(Case==TEXT("FoodYawn")) {
        // A continuous gameplay take: establish the mouth and airflow, then
        // show both planted hands and the face before returning to the mouth.
        if(T>4.1 && T<7.1) {
            const FVector Eyes=(H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("eye_l")))+H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("eye_r"))))*.5f;
            const FVector Hands=(H->YawnHandPoint(0)+H->YawnHandPoint(1))*.5f;
            Camera((Eyes+Hands)*.5f+FVector(0,0,12),-H->YawnPullDirection*360+FVector::CrossProduct(FVector::UpVector,H->YawnPullDirection)*-280+FVector(0,0,170));
        } else if(R.Throat.IsValid()) {
            Camera((R.Throat->VacuumInlet()+R.Start)*.5f+FVector(0,0,17),R.Throat->GetActorTransform().TransformVectorNoScale(FVector(-1300,-500,150)));
            R.Camera->GetCameraComponent()->SetFieldOfView(58);
        }
        if(T>1 && R.Stage==0) {H->ServerSetPrimary(true);H->ServerSetPrimary(false);++R.Stage;}
        if(T>2.5 && R.Stage==1) {
            R.Start=H->GetActorLocation();Tongue->StartYawn(4);
            Check(!H->CanWork() && !H->bBrushing && !H->bHandling && !H->FoodCollection->bCollecting && H->YawnTongue==Tongue,TEXT("yawn cancels work and automatically grips tongue"));
            int32 MouthFlows=0,HeroFlows=0;for(TActorIterator<AMCReactionVFX> It(W);It;++It) if(It->Effect==EMCReactionEffect::Yawn) {MouthFlows+=It->GetAttachParentActor()==R.Throat.Get();HeroFlows+=Cast<AMCToothCharacter>(It->GetAttachParentActor())!=nullptr;}
            Check(MouthFlows==1 && HeroFlows==0,TEXT("one cartoon vacuum originates at the mouth with no player ray emitters"));++R.Stage;
        }
        if(T>3 && T<6) {
            H->AddMovementInput(FVector::RightVector,1);
            R.YawnDrag=FMath::Max(R.YawnDrag,float(FVector::Dist2D(H->GetActorLocation(),R.Start)));
            const FVector Hand=H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("hand_l")));
            R.HandGap=FMath::Min(R.HandGap,float(FVector::Dist(Hand,H->YawnHandPoint(0))));
            R.YawnEyesFocused|=H->YawnPoseAlpha()>.9f && FMath::Abs(H->Gaze->LeftAngles.Y)<15 && FMath::Abs(H->Gaze->RightAngles.Y)<15;
            if(T>R.NextDiagnostic) {
                R.NextDiagnostic=T+.75;
                const auto* Mesh=H->GetMesh();const auto* Asset=Mesh->GetSkeletalMeshAsset();
                UE_LOG(LogTemp,Display,TEXT("MC_YAWN_FACE t=%.2f mesh=%s correctiveExists=%d effort=%.3f corrective=%.3f eyesBlink=%.3f left=%s right=%s blink=%.3f emotion=%d alpha=%.3f gazePoint=%s head=%s"),
                    T,*GetNameSafe(Asset),Asset && Asset->FindMorphTarget(TEXT("BlinkCancel_Mouth_Effort"))!=nullptr,
                    Mesh->GetMorphTarget(TEXT("Mouth_Effort")),Mesh->GetMorphTarget(TEXT("BlinkCancel_Mouth_Effort")),Mesh->GetMorphTarget(TEXT("Eyes_Blink")),
                    *H->Gaze->LeftAngles.ToString(),*H->Gaze->RightAngles.ToString(),H->Gaze->Blink,int32(H->Expression->CurrentEmotion),H->YawnPoseAlpha(),
                    *H->Gaze->TargetPoint().ToString(),*Mesh->GetSocketLocation(H->RigBone(TEXT("gaze_head"))).ToString());
            }
        }
        if(T>7 && R.Stage==2) {Check(!H->IsYawning() && H->CanWork(),TEXT("yawn releases automatically and work becomes available"));++R.Stage;}
        if(T>8) {Check(R.YawnDrag>45 && R.HandGap<28,TEXT("visible suction drag and grounded skeletal hand contact"));Check(R.YawnEyesFocused,TEXT("yawn gaze stays away from the downward pitch limit"));UE_LOG(LogTemp,Display,TEXT("MC_YAWN_POSE drag=%.1f handGap=%.1f"),R.YawnDrag,R.HandGap);Finish();}
    } else if(Case==TEXT("FoodSpoil") && R.Food.IsValid()) {
        // Advance the freshness clock explicitly, leaving normal gameplay speed intact.
        const float FreshAge=FMath::Min(181.f,float(T)*24);R.Food->SpoilAt=W->GetTimeSeconds()+180-FreshAge;
        if(T>4 && R.Stage==0) {Check(!R.Food->bSpoiled,TEXT("food is still fresh before 180 seconds"));++R.Stage;}
        if(T>8.5) {int32 Ulcers=0;for(TActorIterator<AMCMouthSurface> It(W);It;++It) Ulcers+=It->bUlcer;Check(R.Food->bSpoiled && !R.Food->IsDisposed() && Ulcers==0,TEXT("at 180 seconds food spoils without absorption or ulcers"));Finish();}
    } else if(Case==TEXT("FoodDamage")) {
        if(T>2 && R.Stage==0) {
            Check(R.Food.IsValid() && R.Food->IsDisposed() && R.Food->FireTrail.Num()>=6 && R.Food->BurnLesion,TEXT("landing immediately ignites a persistent fire road and one lesion"));
            Check(R.Food.IsValid() && !R.Food->IsHazardResolved(),TEXT("ignition cannot complete the food objective"));
            H->Inventory->ServerSelect(EMCToolSlot::Spray);++R.Stage;
        }
        if(R.Stage==3) {H->ServerSetPrimary(false);if(T>R.FreshStart+2.5) {GS->TasksLeft=0;Finish();}return;}
        if(T>2.2 && R.Food.IsValid()) {
            AMCFirePatch* Next=nullptr;float Distance=FLT_MAX;int32 Left=0;
            for(auto Fire:R.Food->FireTrail) if(IsValid(Fire) && Fire->IsBurning()) {++Left;const float D=FVector::DistSquared2D(H->GetActorLocation(),Fire->GetActorLocation());if(D<Distance) {Distance=D;Next=Fire;}}
            GS->TasksTotal=8;GS->TasksLeft=Left+(R.Food->IsHazardResolved()?0:1);
            const FVector Aim=Next?Next->GetActorLocation():IsValid(R.Food->BurnLesion)?R.Food->BurnLesion->GetActorLocation():R.P;
            const FVector Goal=Aim+FVector(0,-165,0),Delta=(Goal-H->GetActorLocation()).GetSafeNormal2D();
            if(FVector::Dist2D(Goal,H->GetActorLocation())>22) H->AddMovementInput(Delta,.55f);
            H->SetActorRotation((Aim-H->GetActorLocation()).GetSafeNormal2D().Rotation());
            if(H->Inventory->Selected!=EMCToolSlot::Spray) H->Inventory->ServerSelect(EMCToolSlot::Spray);
            H->ServerSetPrimary(true);
            if(T>R.NextDiagnostic) {
                R.NextDiagnostic=T+1;
                UE_LOG(LogTemp,Display,TEXT("MC_FIRE_REVIEW t=%.1f left=%d canwork=%d slot=%d primary=%d target=%s healing=%s pos=%s aim=%s"),T,Left,H->CanWork(),int32(H->Inventory->Selected),H->IsPrimaryHeld(),*GetNameSafe(H->Inventory->FireTarget),*GetNameSafe(H->Inventory->HealingTarget),*H->GetActorLocation().ToString(),*Aim.ToString());
            }
            if(Left==0 && R.Stage==1) {Check(!R.Food->IsHazardResolved(),TEXT("all fire is extinguished but lesion still needs treatment"));++R.Stage;}
            if(R.Stage==2 && R.Food->IsHazardResolved()) {Check(true,TEXT("held spray extinguishes the road and fully heals its ulcer"));R.FreshStart=T;H->ServerSetPrimary(false);++R.Stage;}
            if(R.Stage==3 && T>R.FreshStart+2.5) {GS->TasksLeft=0;Finish();}
        }
    } else if(Case==TEXT("FoodStars")) {
        if(T>1.5 && T<2.7) for(TActorIterator<AMCTaskActor> It(W);It;++It) It->ApplyWork(H,true,W->GetDeltaSeconds());
        if(T>3 && R.Stage==0) {Check(H->TaskSuccessAt>R.At,TEXT("real task completion emits happy feedback"));int32 Stars=0;for(TActorIterator<AMCReactionVFX> It(W);It;++It) Stars+=It->Effect==EMCReactionEffect::Stars;Check(Stars>0,TEXT("completion star particles are present"));++R.Stage;}
        if(T>5.5 && R.Stage==1) {auto* Task=W->SpawnActor<AMCTaskActor>(R.P+FVector(20,-60,30),FRotator::ZeroRotator);auto* Event=NewObject<UMCDayEvent>();Event->Kind=EMCTaskKind::Coffee;Event->WorkSeconds=1;Task->Initialize(Event);++R.Stage;}
        if(T>6 && T<7.3) for(TActorIterator<AMCTaskActor> It(W);It;++It) It->ApplyWork(H,true,W->GetDeltaSeconds());
        if(T>9.5) {Check(H->TaskSuccessAt>R.At+6,TEXT("second fully completed task also emits the shared celebration"));Finish();}
    }
    if(T>(Case==TEXT("FoodDamage")?28:16)) {Check(false,TEXT("scenario timed out"));Finish();}
}
#endif
