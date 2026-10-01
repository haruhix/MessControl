#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCVomitBurst.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/TextRenderComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCThroat::AMCThroat()
{
    SetNetUpdateFrequency(20);
    Tissue=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LivingTissue")); Tissue->SetupAttachment(Volume);
    Tissue->SetCollisionEnabled(ECollisionEnabled::NoCollision); Tissue->SetCastShadow(true);
    SculptedTissue=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SculptedTissue")); SculptedTissue->SetupAttachment(Volume);
    SculptedTissue->SetCollisionEnabled(ECollisionEnabled::NoCollision); SculptedTissue->SetCastShadow(true);
    SculptedTissue->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SculptedTissue->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Gameplay/Throat/SK_Throat.SK_Throat")));
    SculptedTissue->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    AuthoredMouth=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AuthoredMouth")); AuthoredMouth->SetupAttachment(Volume);
    AuthoredMouth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AuthoredMouth->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    AuthoredMouth->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Uvula=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Uvula")); Uvula->SetupAttachment(Volume);
    Uvula->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UvulaLanding=CreateDefaultSubobject<UBoxComponent>(TEXT("UvulaLanding")); UvulaLanding->SetupAttachment(Volume);
    UvulaLanding->SetBoxExtent(FVector(28,38,16)); UvulaLanding->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    UvulaLanding->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore); UvulaLanding->SetCanEverAffectNavigation(false);
    ClosedBarrier=CreateDefaultSubobject<UBoxComponent>(TEXT("ClosedThroat")); ClosedBarrier->SetupAttachment(Volume);
    ClosedBarrier->SetCollisionProfileName(TEXT("BlockAllDynamic")); ClosedBarrier->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    ZoneRing=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FoodZoneRing")); ZoneRing->SetupAttachment(Volume);
    ZoneRing->SetCollisionEnabled(ECollisionEnabled::NoCollision); ZoneRing->SetCastShadow(false);
    Label->SetWorldSize(21); Label->SetRelativeRotation(FRotator(0,180,0));
    TissueMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_LivingThroat.MI_LivingThroat"));
    RingMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_ThroatRing.MI_ThroatRing"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Uvula->SetStaticMesh(Sphere.Object);
}
void AMCThroat::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    GateSize.X=FMath::Max(100.f,GateSize.X); GateSize.Y=FMath::Max(100.f,GateSize.Y);
    ZoneRadius=FMath::Max(80.f,ZoneRadius); ZoneHeight=FMath::Max(40.f,ZoneHeight);
    PressSeconds=FMath::Max(.1f,PressSeconds); AnticipationSeconds=FMath::Max(.1f,AnticipationSeconds);
    SwallowSeconds=FMath::Max(.5f,SwallowSeconds); RecoverySeconds=FMath::Max(.2f,RecoverySeconds);
    VomitSeconds=FMath::Max(2.f,VomitSeconds);
    Tissue->SetMaterial(0,TissueMaterial);
    Uvula->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_MouthPalate.MI_MouthPalate")));
    SculptedTissue->SetRelativeLocation(GateCenter);
    SculptedTissue->SetRelativeScale3D(FVector(1,GateSize.X/660,GateSize.Y/500));
    SculptedTissue->SetMaterial(0,TissueMaterial);
    Tissue->SetVisibility(!SculptedTissue->GetSkeletalMeshAsset());
    ClosedBarrier->SetRelativeLocation(GateCenter+FVector(45,0,250));
    ClosedBarrier->SetBoxExtent(FVector(35,GateSize.X*1.55,GateSize.Y*1.3));
    UpdateTissue(0,0,true); BuildRing(); UpdatePresentation(0);
}
void AMCThroat::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Throat/SM_Uvula.SM_Uvula"))) Uvula->SetStaticMesh(Mesh);
    if (!TissueMaterial) TissueMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_LivingThroat.MI_LivingThroat"));
    if (!RingMaterial) RingMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_ThroatRing.MI_ThroatRing"));
    Tissue->SetMaterial(0,TissueMaterial);
    Uvula->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_MouthPalate.MI_MouthPalate")));
    if(!SculptedTissue->GetSkeletalMeshAsset()) SculptedTissue->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Gameplay/Throat/SK_Throat.SK_Throat")));
    SculptedTissue->SetRelativeLocation(GateCenter); SculptedTissue->SetRelativeScale3D(FVector(1,GateSize.X/660,GateSize.Y/500));
    SculptedTissue->SetMaterial(0,TissueMaterial); Tissue->SetVisibility(!SculptedTissue->GetSkeletalMeshAsset());
    if (RingMaterial) { RingMID=UMaterialInstanceDynamic::Create(RingMaterial,this); ZoneRing->SetMaterial(0,RingMID); }
    BuildRing(); UpdatePresentation(0);
}
void AMCThroat::RebuildAppearance() { OnConstruction(GetActorTransform()); }
void AMCThroat::EndPlay(const EEndPlayReason::Type Reason) { ResetSwallow(); Super::EndPlay(Reason); }
double AMCThroat::ServerNow() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}
bool AMCThroat::ContainsFood(const AMCFoodActor* Food) const
{
    if (!IsValid(Food) || Food->IsDisposed() || Food->Phase==EMCFoodPhase::Stuck
        || Food->Phase==EMCFoodPhase::Equipped || Food->Phase==EMCFoodPhase::Swallowing || Food->Phase==EMCFoodPhase::Absorbing || !Food->Holders.IsEmpty()) return false;
    const FVector P=GetActorTransform().InverseTransformPosition(Food->GetActorLocation())-ZoneCenter;
    return P.SizeSquared2D()<=FMath::Square(ZoneRadius) && P.Z>=-30 && P.Z<=ZoneHeight;
}
bool AMCThroat::ContainsPlayer(const AMCToothCharacter* Hero) const
{
    if(!IsValid(Hero) || !Hero->Status->IsAlive()) return false;
    const FVector P=GetActorTransform().InverseTransformPosition(Hero->GetActorLocation())-ZoneCenter;
    // Jumping vertically is not an escape: the full column below the button is dangerous.
    return P.SizeSquared2D()<=FMath::Square(ZoneRadius) && P.Z>=-80 && P.Z<=FMath::Max(ZoneHeight,UvulaTop.Z-ZoneCenter.Z+160);
}
bool AMCThroat::CanOrderJump(const AMCToothCharacter* Hero) const
{
    if(!IsValid(Hero) || !Hero->CanWork() || Hero->OrderJumpTarget || !Hero->GetCharacterMovement()->IsMovingOnGround()
        || ThroatPhase!=EMCThroatPhase::Collecting || !ContainsPlayer(Hero)) return false;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(ContainsFood(*It)) { FVector Velocity; return OrderVelocity(Hero,Velocity); }
    return false;
}
bool AMCThroat::LaunchToUvula(AMCToothCharacter* Hero)
{
    if(!HasAuthority() || !CanOrderJump(Hero)) return false;
    FVector Velocity; if(!OrderVelocity(Hero,Velocity)) return false;
    Hero->CancelGameplayInput(); Hero->DropFood();
    Hero->OrderJumpTarget=this; Hero->OrderJumpStartedAt=ServerNow(); Hero->bOrderJumpLaunched=false;
    Hero->GetCharacterMovement()->StopMovementImmediately();
    PreparingPlayers.AddUnique(Hero); Hero->ForceNetUpdate(); return true;
}
float AMCThroat::UvulaBodyClearance(FVector Center,float Radius,float HalfHeight) const
{
    // Circumscribed bands of the profile authored in refine_uvula_anatomy.py.
    // This also covers the palatal root, which has no gameplay collision mesh.
    const FVector2D Profile[]={{-1.6,380},{-1,180},{-.55,72},{-.20,37},{.16,29},{.40,32},{.60,40},{.78,46},{1,46}};
    const FTransform Shape=Uvula->GetComponentTransform();
    const FVector Scale=Shape.GetScale3D().GetAbs();
    const float Length=FMath::Max(1.f,100*Scale.Z);
    float Clearance=MAX_flt;
    const float Stem=FMath::Max(0.f,HalfHeight-Radius);
    for(int32 I=0;I<=8;++I) {
        const FVector P=Shape.InverseTransformPositionNoScale(Center+FVector(0,0,FMath::Lerp(-Stem,Stem,I/8.f)));
        const float T=FMath::Clamp(float(-P.Z/Length),-1.6f,1.f);
        float R=46;
        for(int32 J=1;J<UE_ARRAY_COUNT(Profile);++J) if(T<=Profile[J].X) { R=FMath::Max(Profile[J-1].Y,Profile[J].Y); break; }
        const float Rx=R*.82f*Scale.X+4,Ry=R*Scale.Y+4;
        const float Radial=(FVector2D(P.X/Rx,P.Y/Ry).Size()-1)*FMath::Min(Rx,Ry);
        const float Axial=FMath::Abs(float(P.Z+T*Length));
        const float Distance=Axial>0?FVector2D(FMath::Max(0.f,Radial),Axial).Size():Radial;
        // Four extra centimetres enclose the gaps between sampled capsule spheres.
        Clearance=FMath::Min(Clearance,Distance-Radius-4);
    }
    return Clearance;
}
bool AMCThroat::OrderVelocity(const AMCToothCharacter* Hero,FVector& Velocity) const
{
    const FVector Goal=UvulaLanding->GetComponentLocation()+FVector(0,0,16+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
    const FVector Start=Hero->GetActorLocation();
    const float Gravity=Hero->GetCharacterMovement()->GetGravityZ();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCOrderJump),false,Hero); Query.AddIgnoredActor(this);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(ContainsFood(*It)) Query.AddIgnoredActor(*It);
    // Keep the original arc when it fits. A lower palate needs a shorter arc,
    // still descending onto the pad, with the same full-body clearance checks.
    for(float Flight:{1.05f,.9f,.75f,.65f}) {
        const FVector Candidate=(Goal-Start)/Flight+FVector(0,0,FMath::Abs(Gravity)*Flight*.5f);
        if(Candidate.Z+Gravity*Flight>-30) continue;
        bool Clear=true; FVector Previous=Start;
        for(int32 I=1;I<=32;++I) {
            const float T=Flight*I/32; const FVector P=Start+Candidate*T+FVector(0,0,Gravity*T*T*.5f);
            if(UvulaBodyClearance(P,Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+6,
                Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4)<2) {Clear=false;break;}
            FHitResult Hit;
            if(GetWorld()->SweepSingleByChannel(Hit,Previous,P,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()*.85f,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.95f),Query)) {
                // The moving tongue can rise into the grounded capsule between
                // ticks. An upward takeoff may leave that initial floor contact.
                const bool LeavingFloor=I==1 && Hit.bStartPenetrating && Hit.GetComponent()==Hero->GetMovementBaseObject()
                    && Hit.ImpactNormal.Z>.65f && FVector::DotProduct(Candidate,Hit.ImpactNormal)>0;
                if(!LeavingFloor) {Clear=false;break;}
            }
            Previous=P;
        }
        if(Clear) {Velocity=Candidate;return true;}
    }
    return false;
}
void AMCThroat::UpdateOrderJumps()
{
    for(int32 I=PreparingPlayers.Num()-1;I>=0;--I) {
        auto* Hero=PreparingPlayers[I].Get();
        if(!Hero || Hero->OrderJumpTarget!=this) { PreparingPlayers.RemoveAtSwap(I); continue; }
        bool HasMeal=false;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(ContainsFood(*It)) { HasMeal=true; break; }
        if(!Hero->CanWork() || !Hero->GetCharacterMovement()->IsMovingOnGround()
            || !ContainsPlayer(Hero) || ThroatPhase!=EMCThroatPhase::Collecting || !HasMeal) {
            Hero->ClearOrderJump(); PreparingPlayers.RemoveAtSwap(I); continue;
        }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        const FRotator Facing=(UvulaLanding->GetComponentLocation()-Hero->GetActorLocation()).GetSafeNormal2D().Rotation();
        Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Facing,GetWorld()->GetDeltaSeconds(),14.f));
        if(ServerNow()-Hero->OrderJumpStartedAt<AMCToothCharacter::OrderPrepareSeconds) continue;
        FVector Velocity;
        // The scene can change during the crouch: recheck the arc at takeoff.
        if(OrderVelocity(Hero,Velocity)) {
            Hero->bOrderJumpLaunched=true;
            Hero->ClientOrderLaunch_Implementation(Velocity);
            if(!Hero->IsLocallyControlled()) Hero->ClientOrderLaunch(Velocity);
            Hero->ForceNetUpdate();
        } else Hero->ClearOrderJump();
        PreparingPlayers.RemoveAtSwap(I);
    }
}
void AMCThroat::NotifyUvulaLanding(AMCToothCharacter* Hero,const FHitResult& Hit,float DownSpeed)
{
    if (!HasAuthority() || !IsValid(Hero) || !Hero->CanWork() || Hit.GetComponent()!=UvulaLanding
        || Hit.ImpactNormal.Z<.55f || DownSpeed<30) return;
    LandedPlayers.AddUnique(Hero);
    Hero->GetCharacterMovement()->StopMovementImmediately();
}
void AMCThroat::SetPhase(EMCThroatPhase Phase,double At)
{
    ThroatPhase=Phase; PhaseStartedAt=At; ForceNetUpdate();
}
float AMCThroat::OpenAmount() const
{
    const float Age=FMath::Max(0.,ServerNow()-PhaseStartedAt);
    if (ThroatPhase==EMCThroatPhase::Swallowing) return FMath::SmoothStep(0.f,.35f,Age);
    if (ThroatPhase==EMCThroatPhase::Recovering) return 1-FMath::SmoothStep(0.f,RecoverySeconds*.75f,Age);
    if (ThroatPhase==EMCThroatPhase::Spasm) return .32f+.28f*FMath::Square(FMath::Sin(Age*17));
    // Recovery owns closing. Closing here would reopen the aperture at the
    // transition to Recovering, whose initial opening is one.
    if (ThroatPhase==EMCThroatPhase::Vomiting) return .94f;
    return 0;
}
void AMCThroat::CaptureMeal()
{
    Meal.Reset();
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (ContainsFood(*It) && It->BeginSwallow()) Meal.Add({*It,It->GetActorLocation(),It->GetActorQuat()});
    FoodInZone=Meal.Num(); ++SwallowCount; ++MealSequence;
    for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get()) Food->PauseFuse(this);
    SwallowedPlayers.Reset();
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(ContainsPlayer(*It) && !It->SwallowedBy) {
        SwallowedPlayers.Add({*It,It->GetActorLocation()}); It->SetThroatCapture(this);
    }
}
void AMCThroat::SpitOut(bool Reset)
{
    const FVector Forward=GetActorForwardVector();
    int32 I=0;
    for(const auto& Entry:SwallowedPlayers) if(auto* Hero=Entry.Hero.Get()) {
        const FVector Exit=Reset?Entry.Start:GetActorTransform().TransformPosition(VomitOrigin+FVector(-25,(I++%4-1.5f)*90,0));
        Hero->SetActorLocation(Exit,false,nullptr,ETeleportType::TeleportPhysics); Hero->SetThroatCapture(nullptr);
        if(!Reset) Hero->LaunchCharacter(-Forward*1050+FVector(0,0,470),true,true);
        if(!Hero->IsLocallyControlled()) Hero->ClientThroatExit(Exit,Reset?FVector::ZeroVector:-Forward*1050+FVector(0,0,470));
    }
    SwallowedPlayers.Reset();
    for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get(); IsValid(Food) && !Food->IsDisposed()) {
        Food->SetActorLocation(Reset?Piece.Start:GetActorTransform().TransformPosition(VomitOrigin+FVector(-45,(I++%5-2)*55,35)),false,nullptr,ETeleportType::TeleportPhysics);
        Food->CancelSwallow(); Food->ResumeFuse(this); if(!Reset) Food->Body->SetPhysicsLinearVelocity(-Forward*780+FVector(0,0,400));
    }
    Meal.Reset();
}
void AMCThroat::BeginVomit()
{
    if(auto* Burst=GetWorld()->SpawnActor<AMCVomitBurst>()) { Burst->Configure(this); ActiveVomit=Burst; }
    SpitOut(); ++VomitCount; SetPhase(EMCThroatPhase::Vomiting,ServerNow());
}
void AMCThroat::ResetSwallow()
{
    if (!HasAuthority()) return;
    if(ActiveVomit.IsValid()) ActiveVomit->Destroy(); ActiveVomit.Reset();
    SpitOut(true);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->ResumeFuse(this);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->OrderJumpTarget==this) It->ClearOrderJump();
    PreparingPlayers.Reset();
    Meal.Reset(); LandedPlayers.Reset(); PressTime=Weight=0; bPressConsumed=false;
    SetPhase(EMCThroatPhase::Collecting,ServerNow());
}
void AMCThroat::Tick(float Dt)
{
    // AMCFoodDisposal's legacy tick deletes immediately; this actor owns the full cycle.
    AActor::Tick(Dt);
    if (HasAuthority())
    {
        const double Now=ServerNow();
        UpdateOrderJumps();
        LandedPlayers.RemoveAll([this](const auto& Entry){auto* H=Entry.Get(); return !IsValid(H) || !H->CanWork() || H->GetMovementBaseObject()!=UvulaLanding;});
        Weight=FMath::Min(2.f,float(LandedPlayers.Num()));
        if (Weight<=0) { PressTime=0; bPressConsumed=false; }
        FoodInZone=0; for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (ContainsFood(*It)) ++FoodInZone;
        if (ThroatPhase==EMCThroatPhase::Collecting && Weight>0 && !bPressConsumed)
        {
            PressTime+=Dt*Weight;
            if (PressTime>=FMath::Max(.48f,PressSeconds))
            {
                bPressConsumed=true;
                if (FoodInZone>0) {
                    SetPhase(EMCThroatPhase::Anticipation,Now);
                    for(const auto& Entry:LandedPlayers) if(auto* Hero=Entry.Get()) {
                        const FVector Hop=-GetActorForwardVector()*330+FVector(0,0,280);
                        Hero->LaunchCharacter(Hop,true,true);
                        if(!Hero->IsLocallyControlled()) Hero->ClientUvulaHop(Hop);
                    }
                }
            }
        }
        if (ThroatPhase==EMCThroatPhase::Anticipation && Now-PhaseStartedAt>=AnticipationSeconds)
        {
            const double At=PhaseStartedAt+AnticipationSeconds; CaptureMeal(); SetPhase(EMCThroatPhase::Swallowing,At);
        }
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
            if(ThroatPhase==EMCThroatPhase::Anticipation && ContainsFood(*It)) It->PauseFuse(this);
            else if(It->Phase!=EMCFoodPhase::Swallowing) It->ResumeFuse(this);
        }
        if (ThroatPhase==EMCThroatPhase::Swallowing)
        {
            const float T=FMath::Clamp(float(Now-PhaseStartedAt)/SwallowSeconds,0.f,1.f);
            const FVector End=GetActorTransform().TransformPosition(GateCenter+FVector(1050,0,20));
            const float Alpha=FMath::SmoothStep(.12f,1.f,T);
            for (const auto& Piece:Meal) if (auto* Food=Piece.Food.Get(); IsValid(Food) && !Food->IsDisposed())
            {
                FVector P=FMath::Lerp(Piece.Start,End,Alpha); P.Z+=FMath::Sin(Alpha*PI)*80;
                Food->SetActorLocationAndRotation(P,FQuat(FVector::RightVector,Alpha*PI)*Piece.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
            }
            for(const auto& Entry:SwallowedPlayers) if(auto* Hero=Entry.Hero.Get()) {
                const float PlayerAlpha=FMath::SmoothStep(0.f,.72f,T);
                FVector P=FMath::Lerp(Entry.Start,End,PlayerAlpha); P.Z+=FMath::Sin(PlayerAlpha*PI)*100;
                Hero->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
            }
            bool Wrong=!SwallowedPlayers.IsEmpty();
            for(const auto& Piece:Meal) if(const auto* Food=Piece.Food.Get()) Wrong|=Food->IsWrongIngredient();
            if (Wrong && T>=.78f) { ++SpasmCount; SetPhase(EMCThroatPhase::Spasm,Now); }
            else if (T>=1) {
                for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get(); IsValid(Food) && !Food->IsDisposed()) { Food->Dispose(); ++FoodSwallowed; }
                Meal.Reset(); SetPhase(EMCThroatPhase::Recovering,PhaseStartedAt+SwallowSeconds);
            }
        }
        if(ThroatPhase==EMCThroatPhase::Spasm && Now-PhaseStartedAt>=SpasmSeconds) BeginVomit();
        if(ThroatPhase==EMCThroatPhase::Vomiting && Now-PhaseStartedAt>=FMath::Max(2.f,VomitSeconds)) SetPhase(EMCThroatPhase::Recovering,Now);
        if (ThroatPhase==EMCThroatPhase::Recovering && Now-PhaseStartedAt>=RecoverySeconds) SetPhase(EMCThroatPhase::Collecting,Now);
    }
    UpdatePresentation(Dt);
    RingElapsed+=Dt; if (RingElapsed>.2f) { RingElapsed=0; BuildRing(); }
}
void AMCThroat::UpdatePresentation(float Dt)
{
    const float Time=ServerNow(),Open=OpenAmount();
    if(AuthoredMouth->GetSkeletalMeshAsset()) {
        const float Breath=.015f+.012f*FMath::Sin(Time*1.35f);
        const float Aperture=FMath::Clamp(Open+Breath,0.f,1.f);
        AuthoredMouth->SetMorphTarget(TEXT("Open"),bReverseAuthoredOpen?1-Aperture:Aperture);
        const float Age=FMath::Max(0.f,float(Time-PhaseStartedAt));
        const float Gag=ThroatPhase==EMCThroatPhase::Spasm?FMath::SmoothStep(0.f,.25f,Age)*(.65f+.35f*FMath::Square(FMath::Sin(Age*12))):0;
        const float Expel=ThroatPhase==EMCThroatPhase::Vomiting?(1-FMath::SmoothStep(VomitSeconds-.4f,VomitSeconds,Age))*(.82f+.18f*FMath::Square(FMath::Sin(Age*14))):0;
        AuthoredMouth->SetMorphTarget(TEXT("vomit"),FMath::Max(Gag,Expel));
        SculptedTissue->SetVisibility(false); Tissue->SetVisibility(false);
    }
    const float Target=Weight>0?1.f:0.f;
    VisualWeight=Dt>0?FMath::Lerp(VisualWeight,Target,1-FMath::Exp(-8.f*Dt)):Target;
    const float Pull=VisualWeight*28;
    const float Length=UvulaLength+Pull;
    // The authored uvula is 100 cm long, top-pivoted. The sphere is only a missing-asset fallback.
    const bool Authored=Uvula->GetStaticMesh() && Uvula->GetStaticMesh()->GetName()==TEXT("SM_Uvula");
    Uvula->SetRelativeLocation(Authored?UvulaTop:UvulaTop-FVector(0,0,Length*.5f));
    Uvula->SetRelativeScale3D(FVector(.7f,.85f,Length/100.f));
    UvulaLanding->SetBoxExtent(FVector(28,38,16),false);
    const float Sway=FMath::Sin(Time*1.8f)*1.2f*(1-VisualWeight)+(ThroatPhase==EMCThroatPhase::Spasm?FMath::Sin(Time*28)*12:
        ThroatPhase==EMCThroatPhase::Vomiting?FMath::Sin(float(Time-PhaseStartedAt)*24)*8*FMath::Exp(-float(Time-PhaseStartedAt)):0);
    Uvula->SetRelativeRotation(FRotator(0,0,Sway));
    // The character braces against the front of the bulb. A pad on its central
    // axis let the visible stalk pass through the body despite a valid landing.
    UvulaLanding->SetRelativeLocation(UvulaTop+FRotator(0,0,Sway).RotateVector(FVector(-86,0,-Length*.81f))-FVector(0,0,16));
    // The back wall always contains pawns and brushes. Captured food follows a kinematic arc.
    ClosedBarrier->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    const FLinearColor Color=ThroatPhase==EMCThroatPhase::Collecting?FLinearColor(.05f,1.f,.31f):
        ThroatPhase==EMCThroatPhase::Anticipation || ThroatPhase==EMCThroatPhase::Spasm || ThroatPhase==EMCThroatPhase::Vomiting?FLinearColor(1.f,.035f,.01f)*(1+.45f*FMath::Sin(Time*14)):FLinearColor(1.f,.52f,.045f);
    if (RingMID) RingMID->SetVectorParameterValue(TEXT("ZoneColor"),Color);
    Label->SetRelativeLocation(ZoneCenter+FVector(-30,0,72));
    Label->SetTextRenderColor(Color.ToFColorSRGB());
    Label->SetText(FText::FromString(ThroatPhase==EMCThroatPhase::Collecting?
        FString::Printf(TEXT("%d FOOD  /  DROP INSIDE\n%s"),FoodInZone,FoodInZone>0?TEXT("SPACE IN CIRCLE: ORDER"):TEXT("BRING FOOD HERE")):
        ThroatPhase==EMCThroatPhase::Anticipation?FString::Printf(TEXT("RUN OUTSIDE!  %.1f"),FMath::Max(0.f,AnticipationSeconds-(Time-float(PhaseStartedAt)))):
        ThroatPhase==EMCThroatPhase::Spasm?TEXT("WRONG INGREDIENT!  BLEURGH!"):
        ThroatPhase==EMCThroatPhase::Vomiting?TEXT("BLEURGH!"):
        ThroatPhase==EMCThroatPhase::Swallowing?TEXT("GULP!"):TEXT("CLOSING...")));
    GeometryElapsed+=Dt;
    if(SculptedTissue->GetSkeletalMeshAsset())
    {
        SculptedTissue->SetMorphTarget(TEXT("SwallowOpen"),Open);
        SculptedTissue->SetMorphTarget(TEXT("Breath"),.5f+.5f*FMath::Sin(Time*1.35f));
        const float GulpAge=FMath::Clamp(float(ServerNow()-PhaseStartedAt)/SwallowSeconds,0.f,1.f);
        SculptedTissue->SetMorphTarget(TEXT("Peristalsis"),ThroatPhase==EMCThroatPhase::Swallowing?FMath::Sin(GulpAge*PI):ThroatPhase==EMCThroatPhase::Spasm?.5f+.5f*FMath::Sin(Time*22):ThroatPhase==EMCThroatPhase::Vomiting?.65f+.35f*FMath::Sin(float(Time-PhaseStartedAt)*18):0.f);
    }
    else if (GetNetMode()!=NM_DedicatedServer && (Dt<=0 || GeometryElapsed>=1.f/30)) { GeometryElapsed=0; UpdateTissue(Open,Time); }
}
void AMCThroat::UpdateTissue(float Open,float Time,bool Rebuild)
{
    constexpr int32 Around=96,Rows=20;
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<int32> Tri; TArray<FProcMeshTangent> Tangents; TArray<FLinearColor> C;
    for (int32 R=0;R<=Rows;++R) for (int32 I=0;I<=Around;++I)
    {
        const float T=float(R)/Rows,A=2*PI*I/Around;
        const float Breathe=FMath::Sin(Time*1.35f)*3*FMath::Sin(T*PI);
        const float Y=FMath::Lerp(1.f,FMath::Lerp(.28f,.68f,Open),T)*GateSize.X*FMath::Cos(A);
        const float Z=FMath::Lerp(1.f,Open*.74f,T)*GateSize.Y*FMath::Sin(A);
        const float Fold=FMath::Sin(A*9+.5f)*FMath::Sin(T*PI)*8;
        const float X=T*(180+Open*120)-FMath::Sin(T*3*PI)*20+Fold+Breathe;
        V.Add(GateCenter+FVector(X,Y,Z)); UV.Add(FVector2D(float(I)/Around,T)); C.Add(FLinearColor::White);
        if (R<Rows && I<Around) { const int32 B=R*(Around+1)+I; Tri.Append({B,B+Around+1,B+1,B+1,B+Around+1,B+Around+2}); }
    }
    UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V,Tri,UV,N,Tangents);
    if (Rebuild || !Tissue->GetProcMeshSection(0)) Tissue->CreateMeshSection_LinearColor(0,V,Tri,N,UV,C,Tangents,false);
    else Tissue->UpdateMeshSection_LinearColor(0,V,N,UV,C,Tangents);
}
void AMCThroat::BuildRing()
{
    AMCTongue* Tongue=nullptr; if(GetWorld()) for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    auto Point=[&](FVector Local) {
        FHitResult Hit; if(Tongue && Tongue->SurfacePoint(GetActorTransform().TransformPosition(Local),Hit))
            return GetActorTransform().InverseTransformPosition(Hit.ImpactPoint+Hit.ImpactNormal*2.5f);
        return Local;
    };
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<int32> Tri; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;
    constexpr int32 S=64;
    for (int32 I=0;I<=S;++I) for(int32 Edge=0;Edge<2;++Edge)
    {
        const float A=2*PI*I/S,Radius=ZoneRadius+(Edge?5:-5);
        V.Add(Point(ZoneCenter+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,5))); N.Add(FVector::UpVector);
        UV.Add(FVector2D(float(I)/S,Edge)); C.Add(FLinearColor::White); T.Add(FProcMeshTangent(1,0,0));
        if(I<S && !Edge) { const int32 B=I*2; Tri.Append({B,B+1,B+2,B+1,B+3,B+2}); }
    }
    if(ZoneRing->GetProcMeshSection(0) && ZoneRing->GetProcMeshSection(0)->ProcVertexBuffer.Num()==V.Num()) ZoneRing->UpdateMeshSection_LinearColor(0,V,N,UV,C,T);
    else ZoneRing->CreateMeshSection_LinearColor(0,V,Tri,N,UV,C,T,false);
    ZoneRing->SetMaterial(0,RingMID?RingMID.Get():RingMaterial.Get());
    V.Reset(); N.Reset(); UV.Reset(); Tri.Reset(); C.Reset(); T.Reset();
    V.Add(Point(ZoneCenter)); N.Add(FVector::UpVector); UV.Add(FVector2D(.5,.5)); C.Add(FLinearColor(1,1,1,.025f)); T.Add(FProcMeshTangent(1,0,0));
    for(int32 I=0;I<=S;++I) {
        const float A=2*PI*I/S; V.Add(Point(ZoneCenter+FVector(FMath::Cos(A),FMath::Sin(A),0)*(ZoneRadius-6)));
        N.Add(FVector::UpVector); UV.Add(FVector2D(.5+.5*FMath::Cos(A),.5+.5*FMath::Sin(A))); C.Add(FLinearColor(1,1,1,.09f)); T.Add(FProcMeshTangent(1,0,0));
        if(I<S) Tri.Append({0,I+1,I+2});
    }
    if(ZoneRing->GetProcMeshSection(1) && ZoneRing->GetProcMeshSection(1)->ProcVertexBuffer.Num()==V.Num()) ZoneRing->UpdateMeshSection_LinearColor(1,V,N,UV,C,T);
    else ZoneRing->CreateMeshSection_LinearColor(1,V,Tri,N,UV,C,T,false);
    ZoneRing->SetMaterial(1,RingMID?RingMID.Get():RingMaterial.Get());
}
void AMCThroat::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCThroat,ThroatPhase); DOREPLIFETIME(AMCThroat,PhaseStartedAt); DOREPLIFETIME(AMCThroat,Weight);
    DOREPLIFETIME(AMCThroat,FoodInZone); DOREPLIFETIME(AMCThroat,SwallowCount); DOREPLIFETIME(AMCThroat,FoodSwallowed);
    DOREPLIFETIME(AMCThroat,SpasmCount);
    DOREPLIFETIME(AMCThroat,VomitCount); DOREPLIFETIME(AMCThroat,MealSequence);
}
