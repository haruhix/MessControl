#include "MCNutLandingShadow.h"

#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

AMCNutLandingShadow::AMCNutLandingShadow()
{
    bReplicates=true; bAlwaysRelevant=true; SetNetUpdateFrequency(10);
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.05f;
    Shadow=CreateDefaultSubobject<UDecalComponent>(TEXT("LandingShadow")); SetRootComponent(Shadow);
    Shadow->SetFadeScreenSize(.0001f); Shadow->SetSortOrder(2); Shadow->SetVisibility(false);
    ShadowMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/M_NutLandingShadow.M_NutLandingShadow")));
}

double AMCNutLandingShadow::ServerTime() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

void AMCNutLandingShadow::Configure(AMCTongue* OnTongue,AMCFoodActor* Food,FVector Landing,float InFlightSeconds,float InRadius,float InOpacity)
{
    if(!HasAuthority() || !IsValid(OnTongue) || !IsValid(Food) || Landing.ContainsNaN()) return;
    Tongue=OnTongue; TrackedFood=Food;
    SurfaceAnchor=Tongue->GetActorTransform().InverseTransformPosition(Landing);
    FlightSeconds=FMath::IsFinite(InFlightSeconds)?FMath::Clamp(InFlightSeconds,.75f,3.f):1.65f;
    Radius=FMath::IsFinite(InRadius)?FMath::Clamp(InRadius,35.f,180.f):70.f;
    MaxOpacity=FMath::IsFinite(InOpacity)?FMath::Clamp(InOpacity,.03f,.35f):.20f;
    ImpactAt=ServerTime()+FlightSeconds; ForceNetUpdate();
}

void AMCNutLandingShadow::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer) {
        if(auto* Material=ShadowMaterial.LoadSynchronous()) {
            Shadow->SetDecalMaterial(Material); ShadowMID=Shadow->CreateDynamicMaterialInstance();
        }
    }
    if(HasAuthority()) SetLifeSpan(FlightSeconds+.35f);
    RefreshShadow();
}

float AMCNutLandingShadow::SecondsToImpact() const
{
    return float(ImpactAt-ServerTime());
}

bool AMCNutLandingShadow::IsWarningActive() const
{
    return IsValid(Tongue) && !Tongue->IsActorBeingDestroyed() && SecondsToImpact()>0 && !IsActorBeingDestroyed();
}

float AMCNutLandingShadow::GetShadowStrength() const
{
    const float Remaining=SecondsToImpact();
    if(Remaining<-.15f || Remaining>FlightSeconds) return 0;
    const float Progress=FMath::Clamp(1.f-Remaining/FMath::Max(.75f,FlightSeconds),0.f,1.f);
    const float FadeIn=FMath::Clamp(Progress*10.f,0.f,1.f);
    const float FadeOut=FMath::Clamp((Remaining+.15f)/.15f,0.f,1.f);
    return MaxOpacity*FMath::Lerp(.40f,1.f,Progress)*FadeIn*FadeOut;
}

void AMCNutLandingShadow::RefreshShadow()
{
    FHitResult Floor;
    if(!IsValid(Tongue) || !Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(SurfaceAnchor),Floor)) {
        Shadow->SetVisibility(false); return;
    }
    SetActorLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*3.f,Floor.ImpactNormal.Rotation());
    const float Progress=FMath::Clamp(1.f-SecondsToImpact()/FMath::Max(.75f,FlightSeconds),0.f,1.f);
    const float Size=Radius*FMath::Lerp(1.25f,1.f,Progress);
    Shadow->DecalSize=FVector(18.f,Size,Size);
    Shadow->UpdateBounds(); Shadow->MarkRenderTransformDirty();
    Shadow->SetVisibility(ShadowMID && GetShadowStrength()>0);
    if(ShadowMID) ShadowMID->SetScalarParameterValue(TEXT("ShadowStrength"),GetShadowStrength());
}

void AMCNutLandingShadow::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority() && (!TrackedFood.IsValid() || !TrackedFood->IsMouthEntryActive() || !IsValid(Tongue))) { Destroy(); return; }
    RefreshShadow();
}

void AMCNutLandingShadow::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutLandingShadow,Tongue); DOREPLIFETIME(AMCNutLandingShadow,SurfaceAnchor);
    DOREPLIFETIME(AMCNutLandingShadow,ImpactAt); DOREPLIFETIME(AMCNutLandingShadow,FlightSeconds);
    DOREPLIFETIME(AMCNutLandingShadow,Radius); DOREPLIFETIME(AMCNutLandingShadow,MaxOpacity);
}
