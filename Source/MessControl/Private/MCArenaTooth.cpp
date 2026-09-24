#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

void FMCArenaToothSettings::Sanitize()
{
    auto Safe=[](float V,float Default,float Min,float Max){ return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default; };
    MaxHealth=Safe(MaxHealth,100,1,10000); LooseHealthFraction=Safe(LooseHealthFraction,0.4f,0,1);
    BrushHitDamage=Safe(BrushHitDamage,25,0,10000); HitSquash=Safe(HitSquash,0.18f,0,0.4f);
    WobbleDegrees=Safe(WobbleDegrees,12,0,25); ReactionSeconds=Safe(ReactionSeconds,0.8f,0.2f,3);
    FallAnticipation=Safe(FallAnticipation,0.25f,0,1); FallLift=Safe(FallLift,310,0,1000); FallSpeed=Safe(FallSpeed,220,0,1000);
}
AMCArenaTooth::AMCArenaTooth()
{
    PrimaryActorTick.bCanEverTick=true; bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
    Body=CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicalBody")); SetRootComponent(Body);
    Body->SetBoxExtent(FVector(48,48,78)); Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Body->SetNotifyRigidBodyCollision(true); Body->SetLinearDamping(0.6f); Body->SetAngularDamping(1.5f);
    Body->BodyInstance.bUseCCD=true;
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnimatedEnamel")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Art/Meshes/SM_ToothProp"));
    if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ToothIdentity")); Label->SetupAttachment(Body);
    Label->SetRelativeLocation(FVector(0,0,112)); Label->SetWorldSize(19); Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetTextRenderColor(FColor(135,255,218)); Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void AMCArenaTooth::Initialize(int32 Id,const FMCArenaToothSettings& Defaults)
{
    if (!HasAuthority()) return;
    Settings=Defaults; Settings.Sanitize(); State.ToothId=Id; State.Health=Settings.MaxHealth;
}
void AMCArenaTooth::BeginPlay()
{
    Super::BeginPlay();
    Body->OnComponentHit.AddDynamic(this,&AMCArenaTooth::OnBodyHit);
    Body->SetMassOverrideInKg(NAME_None,14,true);
    if (const UStaticMesh* Mesh=Visual->GetStaticMesh())
    {
        const FBoxSphereBounds Bounds=Mesh->GetBounds();
        MeshBaseScale=FVector(156.f/FMath::Max(1.f,Bounds.BoxExtent.Z*2));
        MeshBaseLocation=-Bounds.Origin*MeshBaseScale;
        Visual->SetRelativeScale3D(MeshBaseScale); Visual->SetRelativeLocation(MeshBaseLocation);
    }
    if (auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_ArenaTooth.M_ArenaTooth")))
    {
        Material=UMaterialInstanceDynamic::Create(Base,this);
        for (int32 I=0;I<Visual->GetNumMaterials();++I) Visual->SetMaterial(I,Material);
    }
}
bool AMCArenaTooth::ReceiveArenaHit(float Damage,FVector Direction)
{
    if (!HasAuthority() || !IsAvailable() || !FMath::IsFinite(Damage) || Damage<=0 || Direction.ContainsNaN()) return false;
    State.Health=FMath::Max(0.f,State.Health-Damage); State.bLost=State.Health<=0;
    State.HitDirection=Direction.GetSafeNormal2D();
    if (State.HitDirection.IsNearlyZero()) State.HitDirection=FVector(0,1,0);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    State.HitTime=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); ++State.HitSerial;
    ForceNetUpdate(); return true;
}
void AMCArenaTooth::SetCoffee(float Amount)
{
    if (!HasAuthority() || !IsAvailable() || !FMath::IsFinite(Amount)) return;
    State.Coffee=FMath::Clamp(Amount,0.f,1.f); ForceNetUpdate();
}
void AMCArenaTooth::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Since=FMath::Max(0.,Now-State.HitTime);
    const float T=Since/Settings.ReactionSeconds;
    const float Envelope=T<1?FMath::Exp(-T*5):0;
    const float Squash=Settings.HitSquash*Envelope*FMath::Cos(T*PI*4);
    const float Wobble=IsLoose()?Settings.WobbleDegrees*FMath::Sin(Now*5.5):0;
    if (!bFallStarted)
    {
        Visual->SetRelativeScale3D(MeshBaseScale*FVector(1+Squash*0.5,1+Squash*0.5,1-Squash));
        Visual->SetRelativeLocation(MeshBaseLocation+FVector(0,0,-Squash*50));
        const FVector Local=GetActorRotation().UnrotateVector(State.HitDirection);
        const float Kick=Settings.WobbleDegrees*2*Envelope*FMath::Sin(T*PI*5);
        Visual->SetRelativeRotation(FRotator(Kick*Local.X+Wobble*0.25,0,-Kick*Local.Y+Wobble));
        if (State.bLost && Since>=Settings.FallAnticipation)
        {
            bFallStarted=true; Label->SetVisibility(false);
            Visual->SetRelativeScale3D(MeshBaseScale); Visual->SetRelativeLocation(MeshBaseLocation); Visual->SetRelativeRotation(FRotator::ZeroRotator);
            Body->SetSimulatePhysics(true);
            if (HasAuthority())
            {
                Body->AddImpulse(State.HitDirection*Settings.FallSpeed+FVector(0,0,Settings.FallLift),NAME_None,true);
                Body->AddAngularImpulseInDegrees(FVector(-State.HitDirection.Y,State.HitDirection.X,0.15f)*220,NAME_None,true);
                ForceNetUpdate();
            }
        }
    }
    if (Material)
    {
        Material->SetScalarParameterValue(TEXT("Coffee"),State.Coffee);
        Material->SetScalarParameterValue(TEXT("Damage"),1-State.Health/Settings.MaxHealth);
        Material->SetScalarParameterValue(TEXT("HitFlash"),Since<0.16f?(1-Since/0.16f):0);
    }
    Label->SetText(FText::FromString(FString::Printf(TEXT("%02d  |  %.0f"),State.ToothId,State.Health)));
    Label->SetTextRenderColor(IsLoose()?FColor(255,177,69):FColor(135,255,218));
    if (const APlayerController* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCArenaTooth::OnBodyHit(UPrimitiveComponent*,AActor*,UPrimitiveComponent* Other,FVector Impulse,const FHitResult& Hit)
{
    if (!HasAuthority() || !IsAvailable() || !Other || !Other->IsSimulatingPhysics()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    // Normal impulse gives the approaching speed even when Chaos already stopped the other body.
    const float Speed=Impulse.Size()/FMath::Max(1.f,Other->GetMass());
    if (Speed<230 || Now-LastPhysicsHit<0.6) return;
    LastPhysicsHit=Now; ReceiveArenaHit(FMath::Clamp(Speed*0.05f,10.f,50.f),-Hit.ImpactNormal);
}
void AMCArenaTooth::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCArenaTooth,State); DOREPLIFETIME(AMCArenaTooth,Settings);
}
