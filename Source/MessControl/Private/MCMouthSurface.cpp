#include "MCMouthSurface.h"
#include "MCCoffeeWipe.h"
#include "MCTongue.h"
#include "MCDirtPlacement.h"
#include "MCGameState.h"
#include "MCGameMode.h"
#include "MCPlayerState.h"
#include "MCToothStatusComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "MCUlcerProgressWidget.h"
#include "MCGameplayUIFactory.h"
#include "Components/WidgetComponent.h"
#include "MCHazardWave.h"
#include "MCFirePatch.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
AMCMouthSurface* AMCMouthSurface::SpawnExperimentalHugeStain(UWorld* World,FVector Near)
{
    if(!World || World->GetNetMode()==NM_Client || Near.ContainsNaN()) return nullptr;
    for(TActorIterator<AMCTongue> It(World);It;++It) {
        const FBox Bounds=It->Surface->Bounds.GetBox();
        if(!Bounds.IsValid) continue;
        const FVector Center=Bounds.GetCenter();
        FHitResult Floor;
        // One continuous work area, clipped to the existing tongue surface.
        if(!It->SurfacePoint(Center,Floor)) continue;
        const FTransform Pose(FRotationMatrix::MakeFromZX(Floor.ImpactNormal,FVector::ForwardVector).ToQuat(),Floor.ImpactPoint+Floor.ImpactNormal*5);
        auto* Patch=World->SpawnActorDeferred<AMCMouthSurface>(StaticClass(),Pose);
        if(!Patch) continue;
        Patch->bRandomizeLiquidSize=false;
        Patch->LiquidHalfSize=FMath::Clamp(float(FMath::Min(Bounds.GetSize().X,Bounds.GetSize().Y))*.38f,600.f,1600.f);
        Patch->Tags.Add(TEXT("MCExperimentalHugeStain"));
        Patch->FinishSpawning(Pose); Patch->Status->ApplyCoffee(1); Patch->ForceNetUpdate();
        return Patch;
    }
    return nullptr;
}
AMCMouthSurface::AMCMouthSurface()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
    SetNetUpdateFrequency(12); SetMinNetUpdateFrequency(2);
    Area=CreateDefaultSubobject<UBoxComponent>(TEXT("CareArea")); SetRootComponent(Area);
    Area->SetBoxExtent(FVector(62,62,6)); Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Patch")); Visual->SetupAttachment(Area);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeScale3D(FVector(1.24,1.24,.065));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere"));
    if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    UlcerDecal=CreateDefaultSubobject<UDecalComponent>(TEXT("BlendedUlcer")); UlcerDecal->SetupAttachment(Area);
    UlcerDecal->SetRelativeRotation(FRotator(-90,0,0));
    UlcerDecal->DecalSize=FVector(18,82,82); UlcerDecal->SetFadeScreenSize(.002f);
    UlcerDecal->SetVisibility(false);
    TreatmentIndicator=CreateDefaultSubobject<UWidgetComponent>(TEXT("TreatmentProgress")); TreatmentIndicator->SetupAttachment(Area);
    TreatmentIndicator->SetWidgetSpace(EWidgetSpace::Screen); TreatmentIndicator->SetWidgetClass(UMCUlcerProgressWidget::StaticClass());
    TreatmentIndicator->SetDrawSize(FVector2D(84,84)); TreatmentIndicator->SetRelativeLocation(FVector(0,0,80));
    TreatmentIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision); TreatmentIndicator->SetVisibility(false);
    Liquid=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CoffeeLiquid")); Liquid->SetupAttachment(Area);
    Liquid->SetCollisionEnabled(ECollisionEnabled::NoCollision); Liquid->SetCastShadow(false);
    Liquid->SetCanEverAffectNavigation(false);
    LiquidMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Liquid/MI_CoffeePuddle.MI_CoffeePuddle")));
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("CareLabel")); Label->SetupAttachment(Area);
    Label->SetRelativeLocation(FVector(0,0,24)); Label->SetWorldSize(16); Label->SetHorizontalAlignment(EHTA_Center);
    Status=CreateDefaultSubobject<UMCToothStatusComponent>(TEXT("SurfaceStatus"));
}
void AMCMouthSurface::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer && TreatmentIndicator->GetWidgetClass()==UMCUlcerProgressWidget::StaticClass())
        TreatmentIndicator->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("UlcerProgress"),UMCUlcerProgressWidget::StaticClass()));
    if (HasAuthority())
    {
        const FVector P=GetActorLocation();
        LiquidSeed=1+int32(HashCombine(GetTypeHash(FMath::RoundToInt(P.X)),GetTypeHash(FMath::RoundToInt(P.Y)))%4093);
        if (bRandomizeLiquidSize)
        {
            if (LiquidSizeRange.Equals(FVector2D(28,220))) LiquidSizeRange=FVector2D(56,440);
            FRandomStream Appearance(LiquidSeed);
            const float Low=FMath::Clamp(float(LiquidSizeRange.X),20.f,520.f);
            const float High=FMath::Clamp(float(LiquidSizeRange.Y),Low,520.f);
            const float Group=Appearance.FRand();
            const float Weight=Group<.30f?Appearance.FRandRange(0,.15f):Group<.70f?Appearance.FRandRange(.25f,.48f):Appearance.FRandRange(.68f,1.f);
            LiquidHalfSize=FMath::Lerp(Low,High,Weight);
        }
        OnRep_LiquidSize();
        ResetLiquid();
    }
    if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Coffee.M_Coffee")))
    { Material=UMaterialInstanceDynamic::Create(Base,this); Visual->SetMaterial(0,Material); }
    if (HasAuthority() && !Tongue) for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
    {
        FHitResult Hit;
        if (It->SurfacePoint(GetActorLocation(),Hit) && FVector::Dist(Hit.ImpactPoint,GetActorLocation())<35)
        {
            Tongue=*It; TongueAnchor=It->GetActorTransform().InverseTransformPosition(GetActorLocation()); break;
        }
    }
}
bool AMCMouthSurface::IsClean() const
{
    return !bUlcer && (bPaintTile?!FMCCoffeeWipe::HasWetCells(WipeMask):Status->State.CoffeeLeft==0);
}
bool AMCMouthSurface::IsNumb() const
{
    const auto* GS=GetWorld()->GetGameState();
    return bUlcer && NumbUntil>(GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds());
}
bool AMCMouthSurface::ApplyAnesthetic(float Seconds)
{
    if(!HasAuthority() || !bUlcer || IsNumb() || !FMath::IsFinite(Seconds) || Seconds<=0) return false;
    const auto* GS=GetWorld()->GetGameState();
    NumbUntil=(GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())+FMath::Clamp(Seconds,1.f,60.f);
    bDisturbed=false; ContactCooldown=0; ForceNetUpdate(); return true;
}
void AMCMouthSurface::Disturb()
{
    if (!HasAuthority() || !bUlcer || IsNumb() || (bTutorialLesion && !bContactPainOnly)) return;
    if (ContactCooldown<=0)
    {
        if (!bTutorialLesion) if (auto* GS=GetWorld()->GetGameState<AMCGameState>()) GS->MouthHealth=FMath::Max(0.f,GS->MouthHealth-DisturbDamage);
        ContactCooldown=1;
        if (Tongue) {if(bContactPainOnly) Tongue->TriggerToothpickPain(GetActorLocation());else Tongue->TriggerPain(GetActorLocation());}
    }
}
bool AMCMouthSurface::IsBurning() const
{
    for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) if(It->IsBurning() && It->Lesion==this) return true;
    return false;
}
bool AMCMouthSurface::Treat(AMCToothCharacter* Worker,float Seconds,float Power)
{
    if(!HasAuthority() || !bUlcer || bTreatmentBlocked || IsHealed() || IsBurning() || !IsValid(Worker) || !Worker->CanWork() || !FMath::IsFinite(Seconds) || Seconds<=0) return false;
    if(LastTreatmentFrame==GFrameCounter) return true; // Shared lesion cannot heal faster from duplicate calls or teammates.
    LastTreatmentFrame=GFrameCounter;
    const auto* GS=GetWorld()->GetGameState(); const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    NumbUntil=Now+.15; bDisturbed=false; PulseClock=0;
    HealSeconds=FMath::Clamp(HealSeconds,6.f,8.f);
    Healing=FMath::Min(1.f,Healing+FMath::Min(Seconds,.2f)/HealSeconds*FMath::Clamp(Power,1.f,3.f));
    if(Healing>=.99999f) {
        Healing=1; NumbUntil=Now+1; SetLifeSpan(.4f); Worker->NotifyTaskFeedback(true,GetActorLocation());
        if(!bTutorialLesion) if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTask(Worker,EMCScoreTask::Ulcer);
    }
    ForceNetUpdate(); return true;
}
void AMCMouthSurface::OnRep_SurfaceAnchor()
{
    // Old triangle bindings describe the previous location even if the tongue
    // actor itself is unchanged. Rebuild them on peers as well as authority.
    if(auto* Previous=BoundTongue.Get();Previous && Previous!=Tongue) RemoveTickPrerequisiteActor(Previous);
    LiquidVertices.Reset();SurfaceBindings.Reset();BoundTongue.Reset();
    PreviousBrush.Reset();BrushCenters.Reset();BrushAt=-100;
}
void AMCMouthSurface::RecoverUnsupportedFloorStain(float Dt)
{
    // Paint canvases have their own clipped support masks. Moving a canvas
    // would move unrelated coverage, so this recovery owns ordinary blobs only.
    if(!HasAuthority() || bUlcer || bPaintTile || IsClean() || !FMath::IsFinite(Dt) || Dt<=0) return;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(State && (State->bDayOneComplete || State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost)) return;
    if(Tongue && !IsValid(Tongue)) {
        Tongue=nullptr;OnRep_SurfaceAnchor();ForceNetUpdate();
    }
    FloorAccessElapsed+=Dt;
    if(FloorAccessElapsed<1.f) return;
    const float Interval=FloorAccessElapsed;FloorAccessElapsed=0;
    // A temporary pain wave or yawn is not proof that a sound anchor is bad.
    if(IsValid(Tongue) && (Tongue->IsMotionActive() || Tongue->IsYawnActive())) {
        UnsupportedFloorSeconds=0;return;
    }
    const float Radius=LiquidHalfSize*1.415f;
    if(!FMath::IsFinite(Radius) || Radius<=0) return;
    const FVector OldPosition=GetActorLocation();
    TArray<AMCTongue*> Available;
    FHitResult Floor;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
        if(It->IsActorBeingDestroyed() || !It->Surface || !It->Surface->IsRegistered()
            || It->CurrentVertices().IsEmpty() || It->IsMotionActive() || It->IsYawnActive()) continue;
        Available.Add(*It);
        if(MCDirtPlacement::FindSurface(*It,OldPosition,Radius,Floor)
            && FVector::DistSquared(OldPosition,Floor.ImpactPoint+Floor.ImpactNormal*5)<FMath::Square(35.f)) {
            UnsupportedFloorSeconds=0;
            // A loaded stain can already be in a good place but have lost its
            // tissue reference. Rebind without moving or resetting its work.
            if(Tongue!=*It) {
                if(IsValid(Tongue)) RemoveTickPrerequisiteActor(Tongue);
                Tongue=*It;TongueAnchor=Tongue->GetActorTransform().InverseTransformPosition(OldPosition);
                OnRep_SurfaceAnchor();ForceNetUpdate();
            }
            return;
        }
    }
    if(Available.IsEmpty()) return; // Loading/streaming is not a completed cleanup task.
    UnsupportedFloorSeconds+=Interval;
    if(UnsupportedFloorSeconds<2.5f) return;
    UnsupportedFloorSeconds=0;
    FRandomStream Random(HashCombine(GetUniqueID(),++FloorRecoveryAttempts));
    AMCTongue* Destination=nullptr;
    for(AMCTongue* CandidateTongue:Available) {
        const FBox Bounds=MCDirtPlacement::WorkBounds(CandidateTongue,Radius+65.f);
        if(!Bounds.IsValid) continue;
        // Try a nearby supported return first, then a bounded seeded search.
        for(int32 Attempt=0;Attempt<66;++Attempt) {
            FVector Point=Bounds.GetCenter();
            if(Attempt==0 && !OldPosition.ContainsNaN()) Point=Bounds.GetClosestPointTo(OldPosition);
            else if(Attempt>1) Point=FVector(Random.FRandRange(Bounds.Min.X,Bounds.Max.X),
                Random.FRandRange(Bounds.Min.Y,Bounds.Max.Y),Bounds.GetCenter().Z);
            if(MCDirtPlacement::FindSurface(CandidateTongue,Point,Radius,Floor)) {Destination=CandidateTongue;break;}
        }
        if(Destination) break;
    }
    if(!Destination) {
        const double Now=GetWorld()->GetTimeSeconds();
        if(Now>=NextFloorRecoveryWarningAt) {
            NextFloorRecoveryWarningAt=Now+30;
            UE_LOG(LogTemp,Warning,TEXT("MC_DIRT_RECOVERY no supported footprint stain=%s batch=%d half=%.1f; cleanup remains outstanding"),
                *GetName(),Batch,LiquidHalfSize);
        }
        return;
    }
    if(IsValid(Tongue)) RemoveTickPrerequisiteActor(Tongue);
    Tongue=Destination;
    const FVector Position=Floor.ImpactPoint+Floor.ImpactNormal*5;
    TongueAnchor=Tongue->GetActorTransform().InverseTransformPosition(Position);
    SetActorLocationAndRotation(Position,FRotationMatrix::MakeFromZX(Floor.ImpactNormal,FVector::ForwardVector).Rotator());
    AddTickPrerequisiteActor(Tongue);
    // UV masks, fractional work, care counters, Batch and Owner deliberately
    // survive. Only world-space contact history and old triangle bindings expire.
    OnRep_SurfaceAnchor();
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_DIRT_RECOVERY relocated stain=%s batch=%d from=%s to=%s remaining=%.6f"),
        *GetName(),Batch,*OldPosition.ToString(),*Position.ToString(),RemainingLiquid());
}
void AMCMouthSurface::Tick(float Dt)
{
    Super::Tick(Dt);
    if (IsValid(Tongue))
    {
        AddTickPrerequisiteActor(Tongue);
        FHitResult Hit;
        if (Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(TongueAnchor),Hit))
        {
            // Keep the liquid's UV heading stable as the tongue normal changes.
            // A basis built from Z alone can spin around that normal on nearly flat tissue.
            const FRotator Rotation=bPaintTile?FRotator::ZeroRotator:FRotationMatrix::MakeFromZX(Hit.ImpactNormal,FVector::ForwardVector).Rotator();
            const FVector Anchor=Tongue->GetActorTransform().TransformPosition(TongueAnchor);
            const FVector Position=bPaintTile?FVector(Anchor.X,Anchor.Y,Hit.ImpactPoint.Z+5):Hit.ImpactPoint+Hit.ImpactNormal*5;
            SetActorLocationAndRotation(Position,Rotation);
        }
    }
    RecoverUnsupportedFloorStain(Dt);
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if (HasAuthority() && bUlcer && !IsHealed() && State && !State->bDayOneComplete && State->Phase!=EMCShiftPhase::Won && State->Phase!=EMCShiftPhase::Lost)
    {
        ContactCooldown=FMath::Max(0.f,ContactCooldown-Dt); bDisturbed=false;
        TSet<TWeakObjectPtr<AMCToothCharacter>> CurrentFeet;
        bool FreshStep=false;
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            if (!It->Status->IsAlive()) continue;
            if(bContactPainOnly && It->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) continue;
            if (!bContactPainOnly && It->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll)
            {
                const FVector P=It->ToothPhysics->PhysicalLocation();
                if (FVector::DistSquared2D(P,GetActorLocation())<FMath::Square(80.f) && FMath::Abs(P.Z-GetActorLocation().Z)<60) bDisturbed=true;
            }
            else if (It->GetCharacterMovement()->IsMovingOnGround())
            {
                const FVector Foot=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                if (FVector::DistSquared2D(Foot,GetActorLocation())<FMath::Square(80.f) && FMath::Abs(Foot.Z-GetActorLocation().Z)<22)
                {
                    bDisturbed=true; CurrentFeet.Add(*It); FreshStep|=!FootContacts.Contains(*It);
                }
            }
        }
        FootContacts=MoveTemp(CurrentFeet);
        if(!bContactPainOnly) for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        {
            if (It->IsDisposed() || It->Phase==EMCFoodPhase::Equipped) continue;
            const FBox Box=It->Body->Bounds.GetBox(); const FVector P=GetActorLocation();
            if (Box.Min.Z<P.Z+18 && Box.Max.Z>P.Z-10 && FVector::DistSquared2D(Box.GetClosestPointTo(P),P)<FMath::Square(62.f)) bDisturbed=true;
        }
        if(IsNumb() || bContactPainOnly) { if(IsNumb()) bDisturbed=false; PulseClock=0; }
        else if((PulseClock+=Dt)>=FMath::Max(1.f,PulseInterval))
        {
            PulseClock=0;
            int32 Active=0; for(TActorIterator<AMCHazardWave> It(GetWorld());It;++It) ++Active;
            if(Active<6) {
                const FTransform T(GetActorRotation(),GetActorLocation());
                auto* Wave=GetWorld()->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),T);
                if(Wave) { Wave->Source=this; Wave->MaxRadius=PulseRadius; Wave->Damage=bTutorialLesion?0:PulseDamage; Wave->FinishSpawning(T); }
            }
        }
        if (bDisturbed && (!bContactPainOnly || FreshStep)) Disturb();
        auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if(!IsNumb() && !bTutorialLesion) GS->MouthHealth=FMath::Max(0.f,GS->MouthHealth-DamagePerSecond*Dt);
    }
    UpdateLiquid(Dt);
    if(GetNetMode()!=NM_DedicatedServer) {
        TreatmentIndicator->SetVisibility(bUlcer && !bTreatmentBlocked);
        if(bUlcer) {
            TreatmentIndicator->InitWidget();
            if(auto* Widget=Cast<UMCUlcerProgressWidget>(TreatmentIndicator->GetUserWidgetObject())) {
                Widget->Source=this; Widget->InvalidateLayoutAndVolatility();
            }
        }
    }
    if (bUlcer && !UlcerMID)
        if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_UlcerBlend.M_UlcerBlend")))
        { UlcerMID=UMaterialInstanceDynamic::Create(Base,this); UlcerDecal->SetDecalMaterial(UlcerMID); }
    UlcerDecal->SetVisibility(bUlcer && UlcerMID!=nullptr);
    if (UlcerMID)
    {
        UlcerMID->SetScalarParameterValue(TEXT("Healing"),Healing);
        UlcerMID->SetScalarParameterValue(TEXT("Disturbed"),bDisturbed?1.f:0.f);
        UlcerMID->SetScalarParameterValue(TEXT("Seed"),LiquidSeed);
        // Treatment suppresses pain; the lesion keeps its authored tissue appearance.
        UlcerMID->SetScalarParameterValue(TEXT("Frozen"),0.f);
    }
    Visual->SetVisibility(bUlcer && !UlcerMID); Label->SetVisibility(bShowCareLabel && (bUlcer || !IsClean()));
    if (Material) Material->SetVectorParameterValue(TEXT("Tint"),bUlcer?FLinearColor(.6f,.01f,.035f):FLinearColor(.11f,.035f,.008f));
    if (bUlcer) { Visual->SetRelativeScale3D(FVector(1.24,1.24,.07f+FMath::Sin(GetWorld()->GetTimeSeconds()*5)*.015f)); Label->SetText(FText::FromString(FString::Printf(TEXT("ULCER %d%% | HOLD SPRAY"),FMath::RoundToInt(Healing*100)))); }
    else if (bShowCareLabel) Label->SetText(FText::FromString(FString::Printf(TEXT("BRUSH %d"),Status->State.CoffeeLeft)));
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager) Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCMouthSurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DOREPLIFETIME(AMCMouthSurface,GroundResponse);
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCMouthSurface,Tongue); DOREPLIFETIME(AMCMouthSurface,TongueAnchor);
    DOREPLIFETIME(AMCMouthSurface,WipeMask); DOREPLIFETIME(AMCMouthSurface,LiquidSeed);
    DOREPLIFETIME(AMCMouthSurface,LiquidDepositMask);
    DOREPLIFETIME(AMCMouthSurface,bPaintTile);
    DOREPLIFETIME(AMCMouthSurface,LiquidHalfSize);
    DOREPLIFETIME(AMCMouthSurface,LiquidMaterial);
    DOREPLIFETIME(AMCMouthSurface,LiquidBornAt);
    DOREPLIFETIME(AMCMouthSurface,BrushUV); DOREPLIFETIME(AMCMouthSurface,BrushDirection); DOREPLIFETIME(AMCMouthSurface,BrushAt);
    DOREPLIFETIME(AMCMouthSurface,bUlcer); DOREPLIFETIME(AMCMouthSurface,Healing); DOREPLIFETIME(AMCMouthSurface,HealSeconds);
    DOREPLIFETIME(AMCMouthSurface,bTreatmentBlocked); DOREPLIFETIME(AMCMouthSurface,bTutorialLesion);
    DOREPLIFETIME(AMCMouthSurface,bContactPainOnly);
    DOREPLIFETIME(AMCMouthSurface,NumbUntil);
    DOREPLIFETIME(AMCMouthSurface,PulseInterval); DOREPLIFETIME(AMCMouthSurface,PulseRadius); DOREPLIFETIME(AMCMouthSurface,PulseDamage);
    DOREPLIFETIME(AMCMouthSurface,DamagePerSecond); DOREPLIFETIME(AMCMouthSurface,DisturbDamage); DOREPLIFETIME(AMCMouthSurface,bDisturbed); DOREPLIFETIME(AMCMouthSurface,Batch);
}
