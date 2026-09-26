#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCToothStatusComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
AMCMouthSurface::AMCMouthSurface()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
    Area=CreateDefaultSubobject<UBoxComponent>(TEXT("CareArea")); SetRootComponent(Area);
    Area->SetBoxExtent(FVector(62,62,6)); Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Patch")); Visual->SetupAttachment(Area);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeScale3D(FVector(1.24,1.24,.065));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere"));
    if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("CareLabel")); Label->SetupAttachment(Area);
    Label->SetRelativeLocation(FVector(0,0,24)); Label->SetWorldSize(16); Label->SetHorizontalAlignment(EHTA_Center);
    Status=CreateDefaultSubobject<UMCToothStatusComponent>(TEXT("SurfaceStatus"));
}
void AMCMouthSurface::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Coffee.M_Coffee")))
    { Material=UMaterialInstanceDynamic::Create(Base,this); Visual->SetMaterial(0,Material); }
    if (HasAuthority()) for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
    {
        FHitResult Hit;
        if (It->SurfacePoint(GetActorLocation(),Hit) && FVector::Dist(Hit.ImpactPoint,GetActorLocation())<35)
        {
            Tongue=*It; TongueAnchor=It->GetActorTransform().InverseTransformPosition(GetActorLocation()); break;
        }
    }
}
bool AMCMouthSurface::IsClean() const { return !bUlcer && Status->State.CoffeeLeft==0; }
void AMCMouthSurface::Disturb()
{
    if (!HasAuthority() || !bUlcer) return;
    Healing=0;
    if (ContactCooldown<=0)
    {
        if (auto* GS=GetWorld()->GetGameState<AMCGameState>()) GS->MouthHealth=FMath::Max(0.f,GS->MouthHealth-DisturbDamage);
        ContactCooldown=1;
        if (Tongue) Tongue->TriggerPain(GetActorLocation());
    }
}
void AMCMouthSurface::Tick(float Dt)
{
    Super::Tick(Dt);
    if (Tongue)
    {
        AddTickPrerequisiteActor(Tongue);
        FHitResult Hit;
        if (Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(TongueAnchor),Hit))
            SetActorLocationAndRotation(Hit.ImpactPoint+Hit.ImpactNormal*5,FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
    }
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if (HasAuthority() && bUlcer && State && !State->bDayOneComplete && State->Phase!=EMCShiftPhase::Won && State->Phase!=EMCShiftPhase::Lost)
    {
        ContactCooldown=FMath::Max(0.f,ContactCooldown-Dt); bDisturbed=false;
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            if (!It->Status->IsAlive()) continue;
            if (It->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll)
            {
                const FVector P=It->ToothPhysics->PhysicalLocation();
                if (FVector::DistSquared2D(P,GetActorLocation())<FMath::Square(80.f) && FMath::Abs(P.Z-GetActorLocation().Z)<60) bDisturbed=true;
            }
            else if (It->GetCharacterMovement()->IsMovingOnGround())
            {
                const FVector Foot=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                if (FVector::DistSquared2D(Foot,GetActorLocation())<FMath::Square(80.f) && FMath::Abs(Foot.Z-GetActorLocation().Z)<22) bDisturbed=true;
            }
        }
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        {
            if (It->IsDisposed() || It->Phase==EMCFoodPhase::Equipped) continue;
            const FBox Box=It->Body->Bounds.GetBox(); const FVector P=GetActorLocation();
            if (Box.Min.Z<P.Z+18 && Box.Max.Z>P.Z-10 && FVector::DistSquared2D(Box.GetClosestPointTo(P),P)<FMath::Square(62.f)) bDisturbed=true;
        }
        if (bDisturbed) Disturb(); else Healing=FMath::Min(1.f,Healing+Dt/FMath::Max(1.f,HealSeconds));
        auto* GS=GetWorld()->GetGameState<AMCGameState>(); GS->MouthHealth=FMath::Max(0.f,GS->MouthHealth-DamagePerSecond*Dt);
        if (Healing>=1) { Destroy(); return; }
    }
    Visual->SetVisibility(bUlcer || !IsClean()); Label->SetVisibility(bUlcer || !IsClean());
    if (Material) Material->SetVectorParameterValue(TEXT("Tint"),bUlcer?FLinearColor(.6f,.01f,.035f):FLinearColor(.11f,.035f,.008f));
    if (bUlcer) { Visual->SetRelativeScale3D(FVector(1.24,1.24,.07f+FMath::Sin(GetWorld()->GetTimeSeconds()*5)*.015f)); Label->SetText(FText::FromString(FString::Printf(TEXT("ULCER %d%% | %s"),FMath::RoundToInt(Healing*100),bDisturbed?TEXT("DISTURBED!"):TEXT("KEEP CLEAR")))); }
    else { Visual->SetRelativeScale3D(FVector(1.24,1.24,.065)*FMath::Lerp(.35f,1.f,Status->CoffeeAmount())); Label->SetText(FText::FromString(FString::Printf(TEXT("BRUSH %d"),Status->State.CoffeeLeft))); }
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager) Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCMouthSurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCMouthSurface,Tongue); DOREPLIFETIME(AMCMouthSurface,TongueAnchor);
    DOREPLIFETIME(AMCMouthSurface,bUlcer); DOREPLIFETIME(AMCMouthSurface,Healing); DOREPLIFETIME(AMCMouthSurface,HealSeconds);
    DOREPLIFETIME(AMCMouthSurface,DamagePerSecond); DOREPLIFETIME(AMCMouthSurface,DisturbDamage); DOREPLIFETIME(AMCMouthSurface,bDisturbed); DOREPLIFETIME(AMCMouthSurface,Batch);
}
