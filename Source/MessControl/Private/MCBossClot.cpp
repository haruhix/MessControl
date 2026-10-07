#include "MCBossClot.h"
#include "MCBossMouthAttackComponent.h"
#include "MCReactionVFX.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCBossClot::AMCBossClot()
{
    bReplicates=true;SetReplicateMovement(false);PrimaryActorTick.bCanEverTick=true;
    SetNetUpdateFrequency(20);
    Blob=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlackClot"));SetRootComponent(Blob);
    Lobe=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lobe"));Lobe->SetupAttachment(Blob);
    Tail=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tail"));Tail->SetupAttachment(Blob);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Black(TEXT("/Game/Gameplay/Boss/Phase3/VFX/M_BossClot"));
    for(auto* Part:{Blob.Get(),Lobe.Get(),Tail.Get()})
    {
        if(Sphere.Succeeded()) Part->SetStaticMesh(Sphere.Object);
        if(Black.Succeeded()) Part->SetMaterial(0,Black.Object);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCanEverAffectNavigation(false);
    }
    Lobe->SetRelativeLocation(FVector(-32,18,12));Lobe->SetRelativeScale3D(FVector(.62,.66,.70));
    Tail->SetRelativeLocation(FVector(-70,-8,-5));Tail->SetRelativeScale3D(FVector(.36,.43,.40));
}
double AMCBossClot::Now() const
{
    const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
FVector AMCBossClot::PositionAt(float Age) const
{
    return LaunchOrigin+LaunchVelocity*Age-FVector(0,0,.5f*Gravity*Age*Age);
}
AMCBossClot* AMCBossClot::Spawn(UMCBossMouthAttackComponent* InSource,int32 Serial,FVector Origin,FVector Landing,float FlightSeconds,float Size)
{
    if(!IsValid(InSource) || !InSource->GetOwner()->HasAuthority()) return nullptr;
    UWorld* World=InSource->GetWorld();const FTransform T(Origin);
    auto* Clot=World->SpawnActorDeferred<AMCBossClot>(StaticClass(),T,InSource->GetOwner(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Clot) return nullptr;
    Clot->Source=InSource;Clot->SalvoSerial=Serial;Clot->LaunchOrigin=Origin;
    FlightSeconds=FMath::Clamp(FlightSeconds,.35f,1.3f);Clot->Radius=FMath::Clamp(Size,8.f,20.f);
    Clot->LaunchVelocity=(Landing-Origin)/FlightSeconds+FVector(0,0,.5f*Clot->Gravity*FlightSeconds);
    Clot->StartedAt=Clot->Now();Clot->FinishSpawning(T);return Clot;
}
void AMCBossClot::BeginPlay()
{
    Super::BeginPlay();if(HasAuthority()) SetLifeSpan(3.f);
    if(GetOwner()) AddTickPrerequisiteActor(GetOwner());
}
void AMCBossClot::Tick(float Dt)
{
    Super::Tick(Dt);if(bImpacted) return;
    const float Age=FMath::Clamp(float(Now()-StartedAt),0.f,3.f);
    if(HasAuthority())
    {
        if(!Source.IsValid() || !Source->IsSalvoValid(SalvoSerial)) {Destroy();return;}
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_Pawn);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCBossClot),true,this);Params.AddIgnoredActor(GetOwner());
        // Substeps prevent tunnelling or tracing a straight chord through a curved arc at low FPS.
        const int32 Steps=FMath::Max(1,FMath::CeilToInt((Age-PreviousAge)/.035f));
        float FromAge=PreviousAge;
        for(int32 I=1;I<=Steps;++I)
        {
            const float ToAge=FMath::Lerp(PreviousAge,Age,I/float(Steps));FHitResult Hit;
            if(GetWorld()->SweepSingleByObjectType(Hit,PositionAt(FromAge),PositionAt(ToAge),FQuat::Identity,Objects,FCollisionShape::MakeSphere(Radius),Params))
            {
                bImpacted=true;ImpactPoint=Hit.ImpactPoint;ImpactNormal=Hit.ImpactNormal;
                Source->ResolveClotHit(SalvoSerial,Hit);OnRep_Impact();ForceNetUpdate();SetLifeSpan(.35f);return;
            }
            FromAge=ToAge;
        }
        PreviousAge=Age;
    }
    SetActorLocation(PositionAt(Age));SetActorRotation((LaunchVelocity-FVector(0,0,Gravity*Age)).Rotation());
    const float Pulse=1.f+.08f*FMath::Sin(Age*22.f+LaunchVelocity.Y);
    Blob->SetRelativeScale3D(FVector(Radius/50.f*1.20f/Pulse,Radius/50.f*Pulse,Radius/50.f*Pulse));
}
void AMCBossClot::OnRep_Impact()
{
    Blob->SetVisibility(false,true);SetActorLocation(ImpactPoint);
    if(bImpactPresented) return;bImpactPresented=true;
    // The replicated impact actor carries the cosmetic burst to rendering clients.
    if(HasAuthority()) AMCReactionVFX::Spawn(GetWorld(),ImpactPoint+ImpactNormal*3,EMCReactionEffect::BlackClotImpact,.38f,Radius*2,ImpactNormal);
}
void AMCBossClot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCBossClot,LaunchOrigin);DOREPLIFETIME(AMCBossClot,LaunchVelocity);
    DOREPLIFETIME(AMCBossClot,StartedAt);DOREPLIFETIME(AMCBossClot,Radius);DOREPLIFETIME(AMCBossClot,Gravity);
    DOREPLIFETIME(AMCBossClot,ImpactPoint);DOREPLIFETIME(AMCBossClot,ImpactNormal);DOREPLIFETIME(AMCBossClot,bImpacted);
}
