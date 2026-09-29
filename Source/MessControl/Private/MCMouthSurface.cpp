#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCToothStatusComponent.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
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
    if (HasAuthority())
    {
        const FVector P=GetActorLocation();
        LiquidSeed=1+int32(HashCombine(GetTypeHash(FMath::RoundToInt(P.X)),GetTypeHash(FMath::RoundToInt(P.Y)))%4093);
        if (bRandomizeLiquidSize)
        {
            FRandomStream Appearance(LiquidSeed);
            const float Low=FMath::Clamp(float(LiquidSizeRange.X),20.f,260.f);
            const float High=FMath::Clamp(float(LiquidSizeRange.Y),Low,260.f);
            const float Group=Appearance.FRand();
            const float Weight=Group<.30f?Appearance.FRandRange(0,.15f):Group<.70f?Appearance.FRandRange(.25f,.48f):Appearance.FRandRange(.68f,1.f);
            LiquidHalfSize=FMath::Lerp(Low,High,Weight);
        }
        OnRep_LiquidSize();
        ResetLiquid();
    }
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
        {
            // Keep the liquid's UV heading stable as the tongue normal changes.
            // A basis built from Z alone can spin around that normal on nearly flat tissue.
            const FRotator Rotation=FRotationMatrix::MakeFromZX(Hit.ImpactNormal,FVector::ForwardVector).Rotator();
            SetActorLocationAndRotation(Hit.ImpactPoint+Hit.ImpactNormal*5,Rotation);
        }
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
    UpdateLiquid(Dt);
    if (bUlcer && !UlcerMID)
        if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_UlcerBlend.M_UlcerBlend")))
        { UlcerMID=UMaterialInstanceDynamic::Create(Base,this); UlcerDecal->SetDecalMaterial(UlcerMID); }
    UlcerDecal->SetVisibility(bUlcer && UlcerMID!=nullptr);
    if (UlcerMID)
    {
        UlcerMID->SetScalarParameterValue(TEXT("Healing"),Healing);
        UlcerMID->SetScalarParameterValue(TEXT("Disturbed"),bDisturbed?1.f:0.f);
        UlcerMID->SetScalarParameterValue(TEXT("Seed"),LiquidSeed);
    }
    Visual->SetVisibility(bUlcer && !UlcerMID); Label->SetVisibility(bShowCareLabel && (bUlcer || !IsClean()));
    if (Material) Material->SetVectorParameterValue(TEXT("Tint"),bUlcer?FLinearColor(.6f,.01f,.035f):FLinearColor(.11f,.035f,.008f));
    if (bUlcer) { Visual->SetRelativeScale3D(FVector(1.24,1.24,.07f+FMath::Sin(GetWorld()->GetTimeSeconds()*5)*.015f)); Label->SetText(FText::FromString(FString::Printf(TEXT("ULCER %d%% | %s"),FMath::RoundToInt(Healing*100),bDisturbed?TEXT("DISTURBED!"):TEXT("KEEP CLEAR")))); }
    else if (bShowCareLabel) Label->SetText(FText::FromString(FString::Printf(TEXT("BRUSH %d"),Status->State.CoffeeLeft)));
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager) Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCMouthSurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DOREPLIFETIME(AMCMouthSurface,GroundResponse);
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCMouthSurface,Tongue); DOREPLIFETIME(AMCMouthSurface,TongueAnchor);
    DOREPLIFETIME(AMCMouthSurface,WipeMask); DOREPLIFETIME(AMCMouthSurface,LiquidSeed);
    DOREPLIFETIME(AMCMouthSurface,LiquidHalfSize);
    DOREPLIFETIME(AMCMouthSurface,LiquidMaterial);
    DOREPLIFETIME(AMCMouthSurface,BrushUV); DOREPLIFETIME(AMCMouthSurface,BrushDirection); DOREPLIFETIME(AMCMouthSurface,BrushAt);
    DOREPLIFETIME(AMCMouthSurface,bUlcer); DOREPLIFETIME(AMCMouthSurface,Healing); DOREPLIFETIME(AMCMouthSurface,HealSeconds);
    DOREPLIFETIME(AMCMouthSurface,DamagePerSecond); DOREPLIFETIME(AMCMouthSurface,DisturbDamage); DOREPLIFETIME(AMCMouthSurface,bDisturbed); DOREPLIFETIME(AMCMouthSurface,Batch);
}
