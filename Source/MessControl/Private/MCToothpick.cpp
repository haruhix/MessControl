#include "MCToothpick.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCUlcerProgressWidget.h"
#include "MCReactionVFX.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCToothpick::AMCToothpick()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick=true; SetNetUpdateFrequency(15);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ToothpickRoot")));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Toothpick")); Body->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Body->SetStaticMesh(Mesh.Object); Body->SetRelativeScale3D(FVector(.09f,.09f,2.2f));
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore); Body->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Body->SetCanEverAffectNavigation(false);
    ActionIndicator=CreateDefaultSubobject<UWidgetComponent>(TEXT("ToothpickAction")); ActionIndicator->SetupAttachment(GetRootComponent());
    ActionIndicator->SetWidgetSpace(EWidgetSpace::Screen); ActionIndicator->SetWidgetClass(UMCUlcerProgressWidget::StaticClass());
    ActionIndicator->SetDrawSize(FVector2D(96,96)); ActionIndicator->SetRelativeLocation(FVector(0,0,230));
    ActionIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMCToothpick::BeginPlay()
{
    Super::BeginPlay();
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Coffee.M_Coffee")))
        if(auto* MID=Body->CreateDynamicMaterialInstance(0,Material)) MID->SetVectorParameterValue(TEXT("Color"),FLinearColor(.72f,.46f,.19f));
    RefreshAppearance();
}

bool AMCToothpick::Impale(AMCTongue* Surface,FVector Point,bool bTraining,int32 InBatch)
{
    if(!HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=GetWorld() || Point.ContainsNaN() || Ulcer) return false;
    FHitResult Hit; if(!Surface->InteriorSurfacePoint(Point,85,Hit)) return false;
    Tongue=Surface; SurfaceAnchor=Surface->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
    Batch=InBatch; State=EMCToothpickState::Impaled; PullProgress=0;
    const FTransform Pose(FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator(),Hit.ImpactPoint+Hit.ImpactNormal*5);
    Ulcer=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Ulcer) return false;
    Ulcer->bUlcer=true; Ulcer->bRandomizeLiquidSize=false; Ulcer->Batch=InBatch;
    Ulcer->bTreatmentBlocked=true; Ulcer->bTutorialLesion=bTraining;
    if(bTraining) { Ulcer->DamagePerSecond=0; Ulcer->DisturbDamage=0; Ulcer->PulseDamage=0; }
    Ulcer->FinishSpawning(Pose); SetActorTransform(Pose);
    if(bTraining && Tongue->IsMotionActive()) Tongue->ResetPain();
    Tongue->TriggerPain(Hit.ImpactPoint); AddTickPrerequisiteActor(Tongue);
    RefreshAppearance(); ForceNetUpdate(); return true;
}

bool AMCToothpick::CanPull(const AMCToothCharacter* Worker) const
{
    if(State!=EMCToothpickState::Impaled || !IsValid(Worker) || Worker->GetWorld()!=GetWorld() || !Worker->CanWork() || Worker->bInCoffee || Worker->HeldFood) return false;
    const FVector Point=GetActorLocation()+FVector(0,0,70), Offset=Point-Worker->GetActorLocation();
    return Offset.SizeSquared()<FMath::Square(180.f)
        && FVector::DotProduct(Offset.GetSafeNormal2D(),Worker->GetActorForwardVector())>.15f && Worker->CanContact(const_cast<AMCToothpick*>(this));
}

AMCToothpick* AMCToothpick::FindPullTarget(const AMCToothCharacter* Worker)
{
    if(!IsValid(Worker)) return nullptr;
    AMCToothpick* Best=nullptr; double Distance=TNumericLimits<double>::Max();
    for(TActorIterator<AMCToothpick> It(Worker->GetWorld());It;++It)
        if(It->CanPull(Worker)) { const double D=FVector::DistSquared(Worker->GetActorLocation(),It->GetActorLocation()); if(D<Distance) {Distance=D;Best=*It;} }
    return Best;
}

bool AMCToothpick::TryPull(AMCToothCharacter* Worker,float Seconds)
{
    if(!HasAuthority() || !CanPull(Worker) || !FMath::IsFinite(Seconds) || Seconds<=0) return false;
    if(LastPullFrame==GFrameCounter) return true;
    LastPullFrame=GFrameCounter;
    PullProgress=FMath::Min(1.f,PullProgress+FMath::Min(Seconds,.2f)/FMath::Max(.2f,PullSeconds));
    if(PullProgress>=1.f-KINDA_SMALL_NUMBER) {PullProgress=1;State=EMCToothpickState::Extracted;Worker->NotifyTaskFeedback(true,GetActorLocation());}
    RefreshAppearance(); ForceNetUpdate(); return true;
}

bool AMCToothpick::CanReceivePickaxeHit(const AMCToothCharacter* Worker) const
{
    if(State!=EMCToothpickState::Extracted || !IsValid(Worker) || Worker->GetWorld()!=GetWorld()
        || !Worker->CanWork() || Worker->bInCoffee || !Worker->Inventory || Worker->Inventory->Selected!=EMCToolSlot::Pickaxe) return false;
    const FVector Offset=Body->Bounds.GetBox().GetClosestPointTo(Worker->GetActorLocation())-Worker->GetActorLocation();
    // A worker can stand alongside the long shaft: the closest bounds point may have no forward component.
    // Use that point for reach, but face the visible shaft center when deciding whether a swing is aimed at it.
    const FVector Aim=Body->Bounds.Origin-Worker->GetActorLocation();
    return Offset.SizeSquared()<=FMath::Square(180.f)
        && FVector::DotProduct(Aim.GetSafeNormal2D(),Worker->GetActorForwardVector())>=.15f
        && Worker->CanContact(const_cast<AMCToothpick*>(this));
}

bool AMCToothpick::HitWithPickaxe(AMCToothCharacter* Worker,float Damage)
{
    if(!HasAuthority() || !FMath::IsFinite(Damage) || Damage<=0 || !CanReceivePickaxeHit(Worker)) return false;
    State=EMCToothpickState::Broken;
    if(IsValid(Ulcer)) {Ulcer->bTreatmentBlocked=false;Ulcer->ForceNetUpdate();}
    Worker->NotifyTaskFeedback(true,GetActorLocation());
    AMCReactionVFX::Spawn(GetWorld(),Body->Bounds.Origin,EMCReactionEffect::Impact,1,90);
    RefreshAppearance(); ForceNetUpdate(); return true;
}

void AMCToothpick::RefreshAppearance()
{
    const bool Visible=!IsBroken(); Body->SetVisibility(Visible); ActionIndicator->SetVisibility(Visible);
    Body->SetCollisionEnabled(Visible?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    Body->SetRelativeLocation(State==EMCToothpickState::Impaled?FVector(0,0,80+PullProgress*90):FVector(0,65,12));
    Body->SetRelativeRotation(State==EMCToothpickState::Impaled?FRotator(0,0,12):FRotator(90,0,0));
    ActionIndicator->SetRelativeLocation(State==EMCToothpickState::Impaled?FVector(0,0,230):FVector(0,65,90));
}

void AMCToothpick::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority() && IsValid(Ulcer) && Ulcer->IsHealed() && !bWoundHealed) {bWoundHealed=true;ForceNetUpdate();}
    if(Tongue) {
        FHitResult Hit;
        if(Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(SurfaceAnchor),Hit))
            SetActorLocationAndRotation(Hit.ImpactPoint+Hit.ImpactNormal*5,FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
    }
    RefreshAppearance();
    if(GetNetMode()!=NM_DedicatedServer && !IsBroken()) {
        ActionIndicator->InitWidget();
        if(auto* Widget=Cast<UMCUlcerProgressWidget>(ActionIndicator->GetUserWidgetObject())) {Widget->ToothpickSource=this;Widget->InvalidateLayoutAndVolatility();}
    }
}

void AMCToothpick::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCToothpick,State); DOREPLIFETIME(AMCToothpick,PullProgress);
    DOREPLIFETIME(AMCToothpick,Ulcer); DOREPLIFETIME(AMCToothpick,Batch); DOREPLIFETIME(AMCToothpick,Tongue); DOREPLIFETIME(AMCToothpick,SurfaceAnchor);
    DOREPLIFETIME(AMCToothpick,bWoundHealed);
}
