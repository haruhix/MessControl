#include "MCSmallMintCandy.h"
#include "MCEnemyWeaponContact.h"
#include "MCHazardWave.h"
#include "MCFrostNovaVFXComponent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

void FMCSmallMintCandySettings::Sanitize()
{
    auto Safe=[](float Value,float Fallback,float Min,float Max) {return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Fallback;};
    HealthPerPlayer=Safe(HealthPerPlayer,180,1,5000);Diameter=Safe(Diameter,135,40,250);
    FallSeconds=Safe(FallSeconds,1.4f,.5f,5);FallHeight=Safe(FallHeight,850,200,2000);
    FirstWaveMinSeconds=Safe(FirstWaveMinSeconds,3,3,5);
    FirstWaveMaxSeconds=Safe(FirstWaveMaxSeconds,5,FirstWaveMinSeconds,5);
    WaveRestMinSeconds=Safe(WaveRestMinSeconds,3,1,20);
    WaveRestMaxSeconds=Safe(WaveRestMaxSeconds,FMath::Max(5.f,WaveRestMinSeconds),WaveRestMinSeconds,20);
    WaveRadius=Safe(WaveRadius,2600,200,6000);WaveTravelSeconds=Safe(WaveTravelSeconds,2.8f,1,6);
    FootIceHealth=Safe(FootIceHealth,80,1,1000);
}

AMCSmallMintCandy::AMCSmallMintCandy()
{
    bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SmallMint"));SetRootComponent(Body);
    FrostNovaVFX=CreateDefaultSubobject<UMCFrostNovaVFXComponent>(TEXT("FrostNovaVFX"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Gameplay/Cold/Candy/SM_WinterCandy.SM_WinterCandy"));
    Body->SetStaticMesh(Mesh.Object);Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);Body->SetVisibility(false);
}

double AMCSmallMintCandy::Now() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

bool AMCSmallMintCandy::Start(AMCTongue* Surface,const FMCSmallMintCandySettings& Config,int32 Seed)
{
    if(!HasAuthority() || Stage!=EMCSmallMintStage::Idle || !IsValid(Surface) || !Body->GetStaticMesh()) return false;
    Settings=Config;Settings.Sanitize();Tongue=Surface;Random.Initialize(Seed);
    FHitResult Floor;
    if(!Tongue->InteriorSurfacePoint(Tongue->Surface->Bounds.Origin,Settings.Diameter*.5f+100,Floor)
        && !Tongue->RandomInteriorPoint(Random,Settings.Diameter*.5f+100,0,TArray<FVector>(),Floor)) {
        Stage=EMCSmallMintStage::Failed;ForceNetUpdate();return false;
    }
    Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    LandingPoint=Floor.ImpactPoint;StartedAt=Now();LandedAt=NextWaveAt=0;WavesEmitted=0;
    int32 Workers=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(It->GetController() && It->Status && It->Status->IsAlive()) ++Workers;
    Health=MaxHealth=Settings.HealthPerPlayer*FMath::Clamp(Workers,1,8);
    Stage=EMCSmallMintStage::Falling;UpdatePresentation();ForceNetUpdate();return true;
}

void AMCSmallMintCandy::UpdatePresentation()
{
    const bool Visible=Stage==EMCSmallMintStage::Falling || Stage==EMCSmallMintStage::Active;
    Body->SetVisibility(Visible);
    Body->SetCollisionEnabled(Stage==EMCSmallMintStage::Active?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    if(!Visible || !Body->GetStaticMesh()) return;
    // Small-mint waves have no per-wave warning. Prepare their exact render
    // components during the fall/3..5 second wait instead of at first impact.
    if(GetNetMode()!=NM_DedicatedServer && FrostNovaVFX) FrostNovaVFX->PrepareForWave();
    const FBox Bounds=Body->GetStaticMesh()->GetBoundingBox();
    const double Scale=Settings.Diameter/FMath::Max(1.,Bounds.GetSize().GetMax());
    Body->SetWorldScale3D(FVector(Scale));
    FHitResult Floor;
    FVector Point=LandingPoint;
    if(IsValid(Tongue) && Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(Anchor),Floor)) Point=Floor.ImpactPoint;
    const double Progress=Stage==EMCSmallMintStage::Falling?FMath::Clamp((Now()-StartedAt)/Settings.FallSeconds,0.,1.):1.;
    // The bottom of the authored mesh, rather than its origin, rests on the tongue.
    Point.Z-=Bounds.Min.Z*Scale;Point.Z+=Settings.FallHeight*(1-Progress*Progress);
    SetActorLocation(Point);
}

void AMCSmallMintCandy::OnRep_State() {UpdatePresentation();}

void AMCSmallMintCandy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);UpdatePresentation();
    if(!HasAuthority()) return;
    const double Time=Now();
    if(Stage==EMCSmallMintStage::Falling && Time>=StartedAt+Settings.FallSeconds) {
        Stage=EMCSmallMintStage::Active;LandedAt=Time;
        NextWaveAt=Time+Random.FRandRange(Settings.FirstWaveMinSeconds,Settings.FirstWaveMaxSeconds);
        UpdatePresentation();ForceNetUpdate();
    }
    if(Stage!=EMCSmallMintStage::Active || Time<NextWaveAt) return;
    Waves.RemoveAll([](const TWeakObjectPtr<AMCHazardWave>& Wave) {return !Wave.IsValid();});
    FHitResult Floor;FVector WaveOrigin=LandingPoint;
    if(IsValid(Tongue) && Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(Anchor),Floor)) WaveOrigin=Floor.ImpactPoint;
    const FTransform Transform(GetActorRotation(),WaveOrigin);
    auto* Wave=GetWorld()->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Wave) {Stage=EMCSmallMintStage::Failed;Stop();return;}
    Wave->Source=this;Wave->bFreezeLegs=true;Wave->FootIceHealth=Settings.FootIceHealth;
    Wave->Damage=0;Wave->WarningSeconds=0;Wave->TravelSeconds=Settings.WaveTravelSeconds;Wave->MaxRadius=Settings.WaveRadius;
    Wave->OnLegsFrozen.AddUObject(this,&AMCSmallMintCandy::RecordFrozenHero);
    Wave->FinishSpawning(Transform);Waves.Add(Wave);++WavesEmitted;
    NextWaveAt=Time+Settings.WaveTravelSeconds+Random.FRandRange(Settings.WaveRestMinSeconds,Settings.WaveRestMaxSeconds);
    ForceNetUpdate();
}

FVector AMCSmallMintCandy::ContactPoint(FVector From) const
{
    FVector Point;
    if(Body->GetClosestPointOnCollision(From,Point)>=0.f) return Point;
    return Body->Bounds.GetBox().GetClosestPointTo(From);
}

bool AMCSmallMintCandy::HitWithPickaxe(AMCToothCharacter* Worker,float Damage)
{
    if(!HasAuthority() || Stage!=EMCSmallMintStage::Active || !IsValid(Worker) || Worker->GetWorld()!=GetWorld()
        || !Worker->CanWork() || !Worker->Inventory || Worker->Inventory->Selected!=EMCToolSlot::Pickaxe
        || !FMath::IsFinite(Damage) || Damage<=0) return false;
    const FVector Point=ContactPoint(Worker->GetActorLocation());
    if(Worker->Inventory->HasUpgrade(EMCToolUpgrade::Buffer)) {
        if(!Worker->Inventory->CanBufferContact(this,Point)) return false;
    }
    else if(!MCEnemyWeaponContact::CanHit(Worker,this,Point)) return false;
    if(const auto* Perks=UMCPerkComponent::ForActor(Worker)) Damage=Perks->WeaponDamage(Damage,this);
    Health=FMath::Max(0.f,Health-Damage);
    if(Health<=0) {Stage=EMCSmallMintStage::Complete;Multicast_Shatter(Body->Bounds.Origin);Stop();}
    ForceNetUpdate();return true;
}

void AMCSmallMintCandy::Multicast_Shatter_Implementation(FVector Point)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    if(auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_IceShatter.NS_IceShatter")))
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Point,FRotator::ZeroRotator,FVector(.8f));
}

void AMCSmallMintCandy::Stop()
{
    if(HasAuthority() && (Stage==EMCSmallMintStage::Active || Stage==EMCSmallMintStage::Falling)) Stage=EMCSmallMintStage::Idle;
    if(HasAuthority()) for(auto& Wave:Waves) if(Wave.IsValid()) Wave->Destroy();
    if(HasAuthority()) for(const auto& Hero:FrozenHeroes) if(Hero.IsValid()) Hero->ClearFrozenLegs();
    FrozenHeroes.Reset();
    if(FrostNovaVFX) FrostNovaVFX->Stop();
    Waves.Reset();UpdatePresentation();
    if(HasAuthority()) ForceNetUpdate();
}

void AMCSmallMintCandy::RecordFrozenHero(AMCToothCharacter* Hero) {if(IsValid(Hero)) FrozenHeroes.Add(Hero);}

void AMCSmallMintCandy::EndPlay(const EEndPlayReason::Type Reason) {Stop();Super::EndPlay(Reason);}

void AMCSmallMintCandy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCSmallMintCandy,Stage);DOREPLIFETIME(AMCSmallMintCandy,Settings);DOREPLIFETIME(AMCSmallMintCandy,Tongue);
    DOREPLIFETIME(AMCSmallMintCandy,Anchor);DOREPLIFETIME(AMCSmallMintCandy,LandingPoint);
    DOREPLIFETIME(AMCSmallMintCandy,StartedAt);DOREPLIFETIME(AMCSmallMintCandy,LandedAt);DOREPLIFETIME(AMCSmallMintCandy,NextWaveAt);
    DOREPLIFETIME(AMCSmallMintCandy,Health);DOREPLIFETIME(AMCSmallMintCandy,MaxHealth);DOREPLIFETIME(AMCSmallMintCandy,WavesEmitted);
}
