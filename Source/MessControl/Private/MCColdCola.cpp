#include "MCColdCola.h"
#include "MCGameDirector.h"
#include "MCCoffeeFlood.h"
#include "MCLocomotionSurface.h"
#include "MCDayPlan.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGameMode.h"
#include "MCPlayerState.h"
#include "Engine/StaticMesh.h"
#include "MCTongue.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AMCIceBlock::AMCIceBlock()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
    Body=CreateDefaultSubobject<UMCIceSurfaceComponent>(TEXT("IceBody")); SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Gameplay/Cold/M_StylizedIce.M_StylizedIce"));
    ShapeMeshes={Cube.Object,Sphere.Object,Cylinder.Object}; IceMaterial=Material.Object;
    Body->SetStaticMesh(Cube.Object); Body->SetMaterial(0,IceMaterial);
    Body->SetCollisionProfileName(TEXT("PhysicsActor")); Body->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Body->SetLinearDamping(2.f); Body->SetAngularDamping(4.f); Body->BodyInstance.bUseCCD=true;
}
void AMCIceBlock::BeginPlay() { Super::BeginPlay(); RefreshAppearance(); }
void AMCIceBlock::RefreshAppearance()
{
    if(ShapeMeshes.IsValidIndex(FMath::Clamp(Shape,0,2))) Body->SetStaticMesh(ShapeMeshes[FMath::Clamp(Shape,0,2)]);
    Body->SetWorldScale3D(Size.ComponentMax(FVector(5))/100);
    if(!IceMID) IceMID=Body->CreateDynamicMaterialInstance(0,IceMaterial);
    if(IceMID) IceMID->SetScalarParameterValue(TEXT("DamageAmount"),1-FMath::Clamp(Health/FMath::Max(1.f,MaxHealth),0.f,1.f));
    Body->SetVisibility(!bBroken); if(bBroken) Body->SetSimulatePhysics(false);
    Body->SetCollisionEnabled(bBroken?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    Body->SetSimulatePhysics(HasAuthority() && !bBroken);
    if(HasAuthority() && !bBroken) Body->SetMassOverrideInKg(NAME_None,12,true);
}
bool AMCIceBlock::HitWithPickaxe(AMCToothCharacter* Hero,float Damage)
{
    if(!HasAuthority() || bBroken || !Hero || !Hero->CanWork() || Hero->Inventory->Selected!=EMCToolSlot::Pickaxe || !FMath::IsFinite(Damage) || Damage<=0) return false;
    const FVector Point=Body->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation());
    const FVector Offset=Point-Hero->GetActorLocation();
    if(Offset.SizeSquared()>FMath::Square(180.f) || FVector::DotProduct(Offset.GetSafeNormal2D(),Hero->GetActorForwardVector())<.25f || !Hero->CanContact(this)) return false;
    Health=FMath::Max(0.f,Health-Damage); bBroken=Health<=0; ForceNetUpdate();
    if(bBroken) {
        Shatter(Body->Bounds.Origin,Size.GetMax()); SetLifeSpan(3);
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTask(Hero,EMCScoreTask::Ice);
    }
    RefreshAppearance(); return true;
}
void AMCIceBlock::Shatter_Implementation(FVector Position,float Diameter)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    if(auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_IceShatter.NS_IceShatter")))
        if(auto* FX=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Position,FRotator::ZeroRotator,FVector(FMath::Clamp(Diameter/95.f,.3f,2.f)),true,true))
        { FTimerHandle Timer; GetWorldTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(FX,[FX](){FX->Deactivate();}),.12f,false); }
}
void AMCIceBlock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCIceBlock,Health); DOREPLIFETIME(AMCIceBlock,MaxHealth); DOREPLIFETIME(AMCIceBlock,bBroken); DOREPLIFETIME(AMCIceBlock,Shape); DOREPLIFETIME(AMCIceBlock,Size); }

AMCColdColaEvent::AMCColdColaEvent() { bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; }
double AMCColdColaEvent::Now() const { const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); }
void AMCColdColaEvent::Start(const UMCDayPlan* Plan,int32 IceCountOverride)
{
    if(!HasAuthority() || !Plan) return;
    if(const auto* Director=AMCGameDirector::Find(GetWorld()); Director && Director->IsManagingEvents()
        && !Director->IsLaunchingEvent(EMCGameDirectorEvent::ColdCola)) return;
    Stop(); Profile=Plan->ColdColaProfile.LoadSynchronous(); if(!Profile) Profile=NewObject<UMCColdColaProfile>(this);
    IceTarget=FMath::Clamp(IceCountOverride>0?IceCountOverride:Profile->IceCount,1,12);
    Center=Plan->ArenaCenter; Extent=Plan->ArenaHalfSize; Spawned=0; NextIceAttempt=0; SpawnPositions.Reset(); StartedAt=Now(); ThawStartedAt=0; bActive=true;
    SetActorTickEnabled(true);
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(!It->CurrentVertices().IsEmpty())
    {const FBox Bounds=It->Surface->Bounds.GetBox();Center=Bounds.GetCenter();Extent=Bounds.GetExtent();break;}
    auto* DrinkPlan=DuplicateObject<UMCDayPlan>(Plan,this); DrinkPlan->CoffeeProfile=Profile->Drink;
    if(DrinkPlan->CoffeeProfile.IsNull()) DrinkPlan->CoffeeProfile=TSoftObjectPtr<UMCCoffeeProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_ColdColaLiquid.DA_ColdColaLiquid")));
    Drink=GetWorld()->SpawnActor<AMCCoffeeFlood>(); Drink->Start(DrinkPlan);
    SlipperyFloor=GetWorld()->SpawnActor<AMCLocomotionSurface>(Center,FRotator::ZeroRotator);
    SlipperyFloor->HalfExtent=FVector(Extent.X,Extent.Y,800); SlipperyFloor->Priority=100;
    SlipperyFloor->Surface=EMCGroundSurface::Slippery; SlipperyFloor->RefreshBounds(); SlipperyFloor->ForceNetUpdate(); ForceNetUpdate();
}
int32 AMCColdColaEvent::IceLeft() const { int32 Left=0; for(const auto& B:Blocks) if(B.IsValid() && !B->bBroken) ++Left; return Left; }
float AMCColdColaEvent::FrostAmount() const
{
    if(!bActive || !Profile) return 0;
    const double T=Now(); return ThawStartedAt>0?1-FMath::Clamp(float((T-ThawStartedAt)/FMath::Max(1.f,Profile->ThawSeconds)),0.f,1.f):FMath::Clamp(float((T-StartedAt)/FMath::Max(1.f,Profile->FrostRiseSeconds)),0.f,1.f);
}
bool AMCColdColaEvent::IsComplete() const { return !bActive || (ThawStartedAt>0 && FrostAmount()<=0); }
void AMCColdColaEvent::SetFrost(float Amount)
{
    // A late completion/reset notification must not clear a newer cola's climate.
    if(Amount<=0) for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It)
        if(*It!=this && It->bActive && !It->IsComplete()) return;
    if(auto* MPC=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/Gameplay/Cold/MPC_MouthClimate.MPC_MouthClimate")))
        GetWorld()->GetParameterCollectionInstance(MPC)->SetScalarParameterValue(TEXT("ColdAmount"),Amount);
}
void AMCColdColaEvent::OnRep_Timeline()
{
    SetActorTickEnabled(bActive && Profile && !IsComplete());
    if(!bActive || IsComplete()) SetFrost(0);
}
void AMCColdColaEvent::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!bActive || !Profile) {OnRep_Timeline();return;}
    SetFrost(FrostAmount());
    if(IsComplete()) {
        // End the timeline without deleting ice that still needs to be broken.
        if(HasAuthority()) {bActive=false;ForceNetUpdate();}
        SetActorTickEnabled(false);return;
    }
    if(!HasAuthority()) return;
    const double Age=Now()-StartedAt;
    if(ThawStartedAt<=0 && Spawned<IceTarget && Age>1.5+Spawned*.6 && Now()>=NextIceAttempt) {
        NextIceAttempt=Now()+.5;
        const int32 I=Spawned;
        const FVector Size=FVector(Profile->IceSize)*(I%3==2?FVector(.8,.8,1.5):I==3?FVector(1.8,.65,.7):FVector::OneVector);
        FRandomStream Random(HashCombine(GetTypeHash(StartedAt),GetTypeHash(I)));
        FHitResult Floor; bool Found=false;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
            if(It->RandomInteriorPoint(Random,Size.GetMax()+60,Size.GetMax()*2+60,SpawnPositions,Floor)) {Found=true;break;}
        if(Found) {
            FVector P=Floor.ImpactPoint+FVector(0,0,650);
            if(Drink) P.Z=FMath::Max(P.Z,double(Drink->SurfaceHeightAt(P)+450));
            const FTransform T(FRotator(8+I*11,I*31,12),P);
            auto* B=GetWorld()->SpawnActorDeferred<AMCIceBlock>(AMCIceBlock::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(B) { B->Shape=I%3; B->Size=Size; B->Health=B->MaxHealth=Profile->IceHealth; B->FinishSpawning(T); Blocks.Add(B); SpawnPositions.Add(Floor.ImpactPoint); ++Spawned; }
        }
    }
    if(ThawStartedAt<=0 && Spawned>=IceTarget
        && ((IceLeft()==0 && (!Drink || !Drink->IsActive())) || Age>=Profile->ColdSeconds)) {
        ThawStartedAt=Now(); if(SlipperyFloor) { SlipperyFloor->Destroy(); SlipperyFloor=nullptr; } ForceNetUpdate();
    }
}
void AMCColdColaEvent::Stop()
{
    if(!HasAuthority()) return;
    bActive=false; SetActorTickEnabled(false); SetFrost(0); if(Drink) { Drink->Stop(); Drink->Destroy(); Drink=nullptr; }
    if(SlipperyFloor) { SlipperyFloor->Destroy(); SlipperyFloor=nullptr; }
    for(auto& B:Blocks) if(B.IsValid()) B->Destroy(); Blocks.Empty(); ForceNetUpdate();
}
void AMCColdColaEvent::EndPlay(const EEndPlayReason::Type Reason) { SetFrost(0); if(HasAuthority()) Stop(); Super::EndPlay(Reason); }
void AMCColdColaEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCColdColaEvent,bActive); DOREPLIFETIME(AMCColdColaEvent,StartedAt); DOREPLIFETIME(AMCColdColaEvent,ThawStartedAt); DOREPLIFETIME(AMCColdColaEvent,Profile); }
