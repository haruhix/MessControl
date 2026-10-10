#include "MCVFXLabToolStation.h"
#include "MCVFXLab.h"
#include "MCPlaytestBotController.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCGameMode.h"
#include "MCToothStatusComponent.h"
#include "MCToothCalculusComponent.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCFoodActor.h"
#include "MCDayPlan.h"
#include "MCColdCola.h"
#include "MCFirePatch.h"
#include "MCCoffeeFlood.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMCVFXLabToolStation::AMCVFXLabToolStation()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
    PrimaryActorTick.bStartWithTickEnabled=false;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("StationOrigin")); SetRootComponent(Root);
    Readout=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ObservedResults")); Readout->SetupAttachment(Root);
    Readout->SetRelativeLocation(FVector(0,0,240)); Readout->SetWorldSize(24);
    Readout->SetHorizontalAlignment(EHTA_Center); Readout->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ToothMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Meshes/SM_ToothProp.SM_ToothProp")));
    WorkerClass=TSoftClassPtr<AMCToothCharacter>(FSoftObjectPath(TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C")));
}

void AMCVFXLabToolStation::BeginPlay()
{
    Super::BeginPlay();
    SetActorTickEnabled(false);
    if(HasAuthority()) {
        bRunning=false;
        SetStatus(TEXT("Paused; waiting for a nearby spectator"));
        if(bAutoRun && !AMCVFXLab::Find(GetWorld())) SetRunning(true);
    }
}
void AMCVFXLabToolStation::OnRep_Status()
{
    Readout->SetText(FText::FromString(Status));
    Readout->SetTextRenderColor(Failed?FColor(255,100,90):Passed?FColor(110,255,160):FColor(230,230,230));
}
void AMCVFXLabToolStation::SetStatus(const FString& Text)
{
    const auto* Enum=StaticEnum<EMCVFXLabToolCase>();
    Status=FString::Printf(TEXT("%s\n%s\nCycles %d | pass %d | fail %d"),
        *Enum->GetNameStringByValue(int64(StationKind)),*Text,Cycles,PassedCycles,FailedCycles);
    OnRep_Status(); ForceNetUpdate();
}
FVector AMCVFXLabToolStation::GroundPoint(FVector LocalOffset) const
{
    FVector Point=GetActorTransform().TransformPosition(LocalOffset);
    FHitResult Hit;
    if(IsValid(Floor) && Floor->SurfacePoint(Point+FVector(0,0,800),Hit)) return Hit.ImpactPoint;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCLabToolFloor),false,this);
    Query.AddIgnoredActor(Worker);
    for(const auto& Actor:Fixtures) if(IsValid(Actor)) Query.AddIgnoredActor(Actor);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Point+FVector(0,0,800),Point-FVector(0,0,1000),ECC_Visibility,Query)) return Hit.ImpactPoint;
    return Point;
}
bool AMCVFXLabToolStation::SpawnWorker()
{
    UClass* PawnClass=WorkerClass.LoadSynchronous();
    if(!PawnClass || !PawnClass->IsChildOf(AMCToothCharacter::StaticClass())) {
        SetStatus(TEXT("FAIL: WorkerClass must reference the real tooth player Blueprint")); return false;
    }
    FActorSpawnParameters Params; Params.Owner=this; Params.ObjectFlags|=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Bot=GetWorld()->SpawnActor<AMCPlaytestBotController>(AMCPlaytestBotController::StaticClass(),FTransform::Identity,Params);
    auto* PS=Bot?Bot->GetPlayerState<AMCPlayerState>():nullptr;
    if(!PS) {SetStatus(TEXT("FAIL: bot has no normal PlayerState")); return false;}
    Bot->Configure(EMCPlaytestBotSkill::Regular,GetUniqueID()%10000,137);
    PS->SetIsSpectator(false); PS->SetIsOnlyASpectator(false); PS->SetIsABot(true);
    PS->SetPlayerName(FString::Printf(TEXT("Lab %s"),*StaticEnum<EMCVFXLabToolCase>()->GetNameStringByValue(int64(StationKind))));
    const auto* Defaults=PawnClass->GetDefaultObject<AMCToothCharacter>();
    const FVector Center=GroundPoint(FVector(-90,0,0))+FVector(0,0,Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4);
    const FTransform Pose(GetActorRotation(),Center);
    Worker=GetWorld()->SpawnActorDeferred<AMCToothCharacter>(PawnClass,Pose,Bot,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Worker) {SetStatus(TEXT("FAIL: worker spawn failed")); return false;}
    Worker->AutoPossessAI=EAutoPossessAI::Disabled;Worker->Tags.AddUnique(FName(TEXT("MC_VFXLabSubject")));
    Worker->FinishSpawning(Pose);
    Bot->Possess(Worker); Bot->SetLabControlled(true); Worker->ApplyPlayerColor(PS->PlayerColor);
    return true;
}
bool AMCVFXLabToolStation::GrantUpgrade(uint8 Kind)
{
    auto* PS=Worker?Worker->GetPlayerState<AMCPlayerState>():nullptr;
    if(!PS || !PS->Perks) return false;
    const auto Upgrade=EMCToolUpgrade(Kind);
    if(PS->Perks->HasToolUpgrade(Upgrade)) return true;
    for(FName ID:PS->Perks->GetToolRewardIDs()) {
        const auto* Row=PS->Perks->FindDefinition(ID);
        if(Row && Row->ToolUpgrade==Upgrade && Row->Rarity==EMCPerkRarity::Rare && PS->Perks->ServerGrantPerk(ID)) return true;
    }
    for(FName ID:PS->Perks->GetToolRewardIDs()) {
        const auto* Row=PS->Perks->FindDefinition(ID);
        if(Row && Row->ToolUpgrade==Upgrade && PS->Perks->ServerGrantPerk(ID)) return true;
    }
    return false;
}
AMCMouthSurface* AMCVFXLabToolStation::SpawnPatch(bool bUlcer,FVector Offset,float Half)
{
    const FTransform Pose(GetActorRotation(),GroundPoint(Offset)+FVector(0,0,5));
    auto* Result=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Result) return nullptr;
    Result->bUlcer=bUlcer; Result->bRandomizeLiquidSize=false; Result->LiquidHalfSize=Half;
    Result->PulseDamage=0; Result->DamagePerSecond=0; Result->DisturbDamage=0;
    Result->HealSeconds=4; Result->Batch=ResidueBatch; Result->bShowCareLabel=true;
    Result->FinishSpawning(Pose); Fixtures.Add(Result);
    if(!bUlcer) Result->Status->ApplyCoffee(1);
    return Result;
}
AMCArenaTooth* AMCVFXLabToolStation::SpawnTooth()
{
    UStaticMesh* Mesh=ToothMesh.LoadSynchronous();
    if(!Mesh) return nullptr;
    const FVector Scale(1.25,1.25,1.6);
    const FVector Extent=Mesh->GetBounds().BoxExtent*Scale;
    const FTransform Pose(GetActorRotation(),GroundPoint(FVector(140,0,0))+FVector(0,0,Extent.Z+3));
    auto* Result=GetWorld()->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Result) return nullptr;
    FMCArenaToothSettings Settings; Settings.InitialCalculusEveryNthTooth=0; Settings.bPianoEnabled=false; Settings.MaxHealth=1000;
    Result->SetAppearance(Mesh,Scale); Result->Initialize(1,Settings); Result->bShowCareLabel=true;
    Result->FinishSpawning(Pose); Fixtures.Add(Result); return Result;
}
AMCIceBlock* AMCVFXLabToolStation::SpawnIce(FVector Point,float Size,float Health)
{
    const FTransform Pose(GetActorRotation(),Point);
    auto* Result=GetWorld()->SpawnActorDeferred<AMCIceBlock>(AMCIceBlock::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Result) return nullptr;
    Result->Size=FVector(Size); Result->MaxHealth=Result->Health=Health;
    Result->FinishSpawning(Pose);
    Result->Body->SetSimulatePhysics(false); Result->Body->SetEnableGravity(false);
    Fixtures.Add(Result); Ice.Add(Result); return Result;
}
AMCFoodActor* AMCVFXLabToolStation::SpawnFood(FVector Point,bool bHard,float Health)
{
    const FTransform Pose(GetActorRotation(),Point);
    auto* Result=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Result) return nullptr;
    FMCFoodRow Row; Row.Health=Health; Row.Resistance=bHard?EMCFoodResistance::Hard:EMCFoodResistance::Soft;
    Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Meshes/SM_Food.SM_Food"))));
    Row.Scale=FVector(.7); Row.SpoilSeconds=3600; Row.AbsorbSeconds=10;
    FRandomStream Random(Cycles+1); Result->ConfigureItem(bHard?FName(TEXT("LabHardFood")):FName(TEXT("LabSoftFood")),Row,Random);
    Result->Batch=ResidueBatch; Result->FinishSpawning(Pose); Result->Initialize(false,FVector::ForwardVector);
    Result->Body->SetSimulatePhysics(false); Result->Body->SetEnableGravity(false);
    Fixtures.Add(Result); return Result;
}
void AMCVFXLabToolStation::SpawnWall(FVector Point,FVector HalfExtent,FRotator Rotation)
{
    FActorSpawnParameters Params; Params.Owner=this; Params.ObjectFlags|=RF_Transient;
    auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(Point,Rotation,Params);
    if(!Actor) return;
    Actor->SetReplicates(true);Actor->SetReplicateMovement(true);
    Wall=Actor->GetStaticMeshComponent();Wall->SetIsReplicated(true);Wall->SetMobility(EComponentMobility::Movable);
    Wall->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Wall->SetCollisionProfileName(TEXT("BlockAll"));Actor->SetActorScale3D(HalfExtent/50);
    Fixtures.Add(Actor);
}
void AMCVFXLabToolStation::HoldTool(uint8 Slot,bool bSelf)
{
    if(!Worker || !Worker->Inventory) return;
    if(Worker->Inventory->Selected!=EMCToolSlot(Slot)) {
        Worker->SetPrimaryInputHeld(false); Worker->Inventory->ServerSelect(EMCToolSlot(Slot));
    }
    Worker->SetSelfCareInput(bSelf); Worker->SetPrimaryInputHeld(true);
}
bool AMCVFXLabToolStation::PrepareCycle()
{
    // Knockdown/current can leave a live physical body outside the fixture.
    // Refill with a fresh participant rather than moving only its capsule.
    if((StationKind==EMCVFXLabToolCase::ChainsawWall || StationKind==EMCVFXLabToolCase::CoffeeWave) && Cycles>0) ClearAll();
    ClearFixtures();
    if(!IsValid(Worker) || !IsValid(Bot) || !Worker->Status->IsAlive()) {
        ClearAll(); if(!SpawnWorker()) return false;
    }
    Worker->CancelGameplayInput(); Worker->ClearFrozenLegs(); Worker->Status->Initialize(Worker->Status->State.MaxHealth);
    Bot->SetLabControlled(true); Worker->ResetContact();
    Worker->GetCharacterMovement()->StopMovementImmediately();
    Worker->SetActorLocation(GroundPoint(FVector(-90,0,0))+FVector(0,0,Worker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4),false,nullptr,ETeleportType::TeleportPhysics);
    Worker->SetActorRotation(GetActorRotation());
    EMCToolUpgrade Upgrade=EMCToolUpgrade::None;
    switch(StationKind) {
    case EMCVFXLabToolCase::FloorMeshaBrush: case EMCVFXLabToolCase::ToothMeshaBrush: Upgrade=EMCToolUpgrade::MeshaBrush; break;
    case EMCVFXLabToolCase::BufferCalculus: case EMCVFXLabToolCase::BufferAoE: case EMCVFXLabToolCase::BufferWall: Upgrade=EMCToolUpgrade::Buffer; break;
    case EMCVFXLabToolCase::WatergunCare: case EMCVFXLabToolCase::WatergunPressure: Upgrade=EMCToolUpgrade::Watergun; break;
    case EMCVFXLabToolCase::ChainsawFood: case EMCVFXLabToolCase::ChainsawWall: Upgrade=EMCToolUpgrade::Chainsaw; break;
    default: break;
    }
    if(Upgrade!=EMCToolUpgrade::None && !GrantUpgrade(uint8(Upgrade))) {SetStatus(TEXT("FAIL: real upgrade perk missing from DT_Perks")); return false;}
    ResidueBatch=1000000+int32(GetUniqueID()%100000);
    switch(StationKind) {
    case EMCVFXLabToolCase::FloorBrush: case EMCVFXLabToolCase::FloorMeshaBrush:
        Patch=SpawnPatch(false,FVector(60,0,0),85); if(!Patch) return false; break;
    case EMCVFXLabToolCase::ToothBrush: case EMCVFXLabToolCase::ToothMeshaBrush: case EMCVFXLabToolCase::ToothRepair:
    case EMCVFXLabToolCase::PickaxeCalculus: case EMCVFXLabToolCase::BufferCalculus:
        Tooth=SpawnTooth(); if(!Tooth) return false;
        if(StationKind==EMCVFXLabToolCase::ToothRepair) {Tooth->Status->Damage(300);Tooth->Status->Loosen();}
        else if(StationKind==EMCVFXLabToolCase::PickaxeCalculus || StationKind==EMCVFXLabToolCase::BufferCalculus) Tooth->Calculus->GrowCalculus(Cycles+7,3);
        else Tooth->SetCoffee(1); break;
    case EMCVFXLabToolCase::SelfBrush: Worker->Status->ApplyCoffee(1); break;
    case EMCVFXLabToolCase::SelfRepair: Worker->Status->Damage(65);Worker->Status->Loosen();break;
    case EMCVFXLabToolCase::SprayUlcer: case EMCVFXLabToolCase::WatergunCare:
        Patch=SpawnPatch(true,FVector(90,0,0)); if(!Patch) return false;break;
    case EMCVFXLabToolCase::SprayFire:
        Fire=AMCFirePatch::Ignite(this,GroundPoint(FVector(90,0,0)),65,0,ResidueBatch,false);
        if(!Fire) return false; Fire->SetOwner(this);Fixtures.Add(Fire);break;
    case EMCVFXLabToolCase::PickaxeIce:
        if(!SpawnIce(GroundPoint(FVector(60,0,0))+FVector(0,0,45),80,150)) return false;break;
    case EMCVFXLabToolCase::FrozenLegs: Worker->FreezeLegs(150);break;
    case EMCVFXLabToolCase::BufferAoE: case EMCVFXLabToolCase::BufferWall:
        // Place after the working pose has blended in; use the same tip sphere as the server hit.
        break;
    case EMCVFXLabToolCase::KnifeFood: case EMCVFXLabToolCase::ChainsawFood: case EMCVFXLabToolCase::WatergunPressure:
        Food=SpawnFood(Worker->GetActorLocation()+Worker->GetActorForwardVector()*110, false,StationKind==EMCVFXLabToolCase::WatergunPressure?12:75);
        if(!Food) return false;break;
    case EMCVFXLabToolCase::ChainsawWall:
        SpawnWall(GroundPoint(FVector(200,0,0))+FVector(0,0,100),FVector(20,180,100),GetActorRotation());
        if(!Wall) return false;break;
    case EMCVFXLabToolCase::CoffeeWave:
        if(!IsValid(Floor)) {SetStatus(TEXT("FAIL: CoffeeWave requires its own static AMCTongue Floor")); return false;}
        // The ordinary profile's points are authored around the default arena.
        // Start remaps that space onto the explicitly owned lab floor.
        CoffeePlan=NewObject<UMCDayPlan>(this);CoffeePlan->FloodHeight=155;
        CoffeePlan->CoffeeProfile=TSoftObjectPtr<UMCCoffeeProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_CoffeeWater.DA_CoffeeWater")));
        Flood=GetWorld()->SpawnActor<AMCCoffeeFlood>(); if(!Flood) return false;
        Flood->SetOwner(Floor);Flood->ResidueBatch=ResidueBatch;Fixtures.Add(Flood);Flood->Start(CoffeePlan);
        if(!Flood->IsActive()) {SetStatus(TEXT("FAIL: coffee event vetoed; disable the normal event director in lab")); return false;}
        {
            const FVector Entry=Flood->ArenaCenter-(Flood->bRiverFlood?Flood->RiverDirection:FVector::ZeroVector)*450;
            FHitResult Support;
            if(!Floor->InteriorSurfacePoint(Entry,180,Support)) {SetStatus(TEXT("FAIL: no supported coffee entry tile"));return false;}
            Worker->SetActorLocation(Support.ImpactPoint+FVector(0,0,Worker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4),false,nullptr,ETeleportType::TeleportPhysics);
            Worker->SetActorRotation((Flood->bRiverFlood?Flood->RiverDirection:GetActorForwardVector()).Rotation());
        }
        break;
    }
    StartedAt=GetWorld()->GetTimeSeconds(); bCycleActive=true; Passed=Failed=false;
    NextReadoutAt=StartedAt+1;bSawMultiHit=false;LastIceHealth.Reset();
    bHadCalculus=Tooth && Tooth->Calculus->HasCalculus();
    bWallOpened=bSawWetFloor=bSawCoffeeOnWorker=bPressureSelected=bBufferPlaced=false;
    InitialContacts=Worker->SuccessfulBrushContacts; InitialHits=Worker->ConfirmedHitCount;
    InitialRemaining=FMath::Max(Remaining(),1.f); Progress=0; SetStatus(TEXT("RUN: waiting for real input/contact progress")); return true;
}

float AMCVFXLabToolStation::Remaining() const
{
    switch(StationKind) {
    case EMCVFXLabToolCase::FloorBrush: case EMCVFXLabToolCase::FloorMeshaBrush: return Patch?Patch->RemainingLiquid():1;
    case EMCVFXLabToolCase::ToothBrush: case EMCVFXLabToolCase::ToothMeshaBrush: return Tooth?Tooth->RemainingGrime():1;
    case EMCVFXLabToolCase::SelfBrush: return Worker?Worker->Status->State.CoffeeLeft:1;
    case EMCVFXLabToolCase::SelfRepair: return Worker?Worker->Status->State.MaxHealth-Worker->Status->State.Health+Worker->Status->State.RepairLeft:1;
    case EMCVFXLabToolCase::ToothRepair: return Tooth?Tooth->Status->State.MaxHealth-Tooth->Status->State.Health+Tooth->Status->State.RepairLeft:1;
    case EMCVFXLabToolCase::SprayUlcer: case EMCVFXLabToolCase::WatergunCare: return Patch?1-Patch->Healing:1;
    case EMCVFXLabToolCase::SprayFire: return IsValid(Fire)?Fire->Heat:0;
    case EMCVFXLabToolCase::PickaxeCalculus: case EMCVFXLabToolCase::BufferCalculus: return Tooth?Tooth->Calculus->RemainingPieces():1;
    case EMCVFXLabToolCase::FrozenLegs: return Worker?Worker->IceLegHealth:1;
    case EMCVFXLabToolCase::KnifeFood: case EMCVFXLabToolCase::ChainsawFood: case EMCVFXLabToolCase::WatergunPressure: return IsValid(Food)&&!Food->IsDisposed()?Food->Health:0;
    case EMCVFXLabToolCase::ChainsawWall: return Worker?FMath::Max(0.f,Worker->Status->State.Health-(Worker->Status->State.MaxHealth-30)):1;
    case EMCVFXLabToolCase::PickaxeIce: case EMCVFXLabToolCase::BufferAoE: case EMCVFXLabToolCase::BufferWall: {
        if(Ice.IsEmpty()) return 1;float Sum=0;for(const auto& Block:Ice) if(IsValid(Block)&&!Block->bBroken) Sum+=Block->Health;return Sum;
    }
    case EMCVFXLabToolCase::CoffeeWave: return Flood&&Flood->IsActive()?1:0;
    }
    return 1;
}

void AMCVFXLabToolStation::DriveInputs(float Dt)
{
    const float Age=GetWorld()->GetTimeSeconds()-StartedAt;
    if(StationKind==EMCVFXLabToolCase::CoffeeWave) {
        bSawCoffeeOnWorker|=Worker->Status->State.CoffeeLeft>0 || Worker->bInCoffee;
        if(Flood && Flood->IsActive() && Worker->Status->IsAlive()) {
            const FVector Position=Worker->GetActorLocation();
            FVector Destination=Flood->ArenaCenter-Flood->RiverDirection*450;
            if(bSawCoffeeOnWorker) {
                // Escape the moving band sideways through ordinary movement,
                // retaining enough supported floor for the next physical pose.
                const FVector Side=FVector::CrossProduct(FVector::UpVector,Flood->RiverDirection);
                Destination=Flood->ArenaCenter+Side*850;
            } else {
                // Seek a genuinely wet supported tile as the front arrives.
                // Contains is the event's real player predicate, not a lab flag.
                double Best=DBL_MAX;
                for(float Along:{300.f,600.f,900.f,1200.f,1500.f}) {
                    const FVector Candidate=Flood->RiverOrigin+Flood->RiverDirection*Along;
                    FHitResult Support;
                    if(!Floor->InteriorSurfacePoint(Candidate,180,Support) || !Flood->Contains(Support.ImpactPoint+FVector(0,0,10))) continue;
                    const double Distance=FVector::DistSquared2D(Position,Support.ImpactPoint);
                    if(Distance<Best) {Best=Distance;Destination=Support.ImpactPoint;}
                }
            }
            const FVector Direction=(Destination-Position).GetSafeNormal2D();
            if(FVector::DistSquared2D(Destination,Position)>FMath::Square(35.f)) Worker->AddMovementInput(Direction);
            if(Worker->bInCoffee) Worker->ServerPaddle(FVector2D(Direction.X,Direction.Y));
        }
        for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if(It->Batch==ResidueBatch && !It->bUlcer) {
            if(!Fixtures.Contains(*It)) Fixtures.Add(*It);
            bSawWetFloor|=It->RemainingLiquid()>0 && !It->LiquidDepositMask.IsEmpty();
        }
        if(Flood && !Flood->IsActive()) {
            // Natural completion is required: explicit Stop deliberately creates no residue.
            FinishCycle(bSawWetFloor && bSawCoffeeOnWorker,
                FString::Printf(TEXT("Natural wave ended; actual residue mask %s; bot coffee contact %s"),
                    bSawWetFloor?TEXT("YES"):TEXT("NO"),bSawCoffeeOnWorker?TEXT("YES"):TEXT("NO")));
        }
        return;
    }
    if(!Worker->CanWork()) return;
    switch(StationKind) {
    case EMCVFXLabToolCase::ToothBrush: case EMCVFXLabToolCase::ToothMeshaBrush:
        Bot->DriveLabTask(Tooth,EMCPlaytestBotGoal::Clean);break;
    case EMCVFXLabToolCase::ToothRepair: Bot->DriveLabTask(Tooth,EMCPlaytestBotGoal::Repair);break;
    case EMCVFXLabToolCase::FloorBrush: case EMCVFXLabToolCase::FloorMeshaBrush:
        Bot->DriveLabTask(Patch,EMCPlaytestBotGoal::Clean);break;
    case EMCVFXLabToolCase::SelfBrush: case EMCVFXLabToolCase::SelfRepair: HoldTool(uint8(EMCToolSlot::Brush),true);break;
    case EMCVFXLabToolCase::PickaxeCalculus: case EMCVFXLabToolCase::BufferCalculus:
        if(!bHadCalculus && Tooth->Calculus->HasCalculus()) {bHadCalculus=true;InitialRemaining=FMath::Max(Remaining(),1.f);}
        Bot->DriveLabTask(Tooth,EMCPlaytestBotGoal::Calculus);break;
    case EMCVFXLabToolCase::SprayUlcer: case EMCVFXLabToolCase::SprayFire: case EMCVFXLabToolCase::WatergunCare: case EMCVFXLabToolCase::WatergunPressure: {
        if(Worker->Inventory->Selected!=EMCToolSlot::Spray) {Worker->SetPrimaryInputHeld(false);Worker->Inventory->ServerSelect(EMCToolSlot::Spray);}
        const FVector Aim=Food?Food->GetActorLocation():Fire?Fire->GetActorLocation()+FVector(0,0,20):Patch->GetActorLocation()+FVector(0,0,10);
        const FVector Eye=Worker->GetActorLocation()+FVector(0,0,55);
        Worker->Inventory->StoreSprayView(Eye,(Aim-Eye).GetSafeNormal());
        if(StationKind==EMCVFXLabToolCase::WatergunPressure && !bPressureSelected) {
            if(!Worker->Inventory->bPressureMode) Worker->Inventory->ServerSelect(EMCToolSlot::Spray);
            bPressureSelected=Worker->Inventory->bPressureMode;
        }
        if(StationKind==EMCVFXLabToolCase::WatergunCare && Worker->Inventory->bPressureMode) Worker->Inventory->ServerSelect(EMCToolSlot::Spray);
        Worker->SetPrimaryInputHeld(true);break;
    }
    case EMCVFXLabToolCase::PickaxeIce: case EMCVFXLabToolCase::FrozenLegs: HoldTool(uint8(EMCToolSlot::Pickaxe));break;
    case EMCVFXLabToolCase::BufferAoE: case EMCVFXLabToolCase::BufferWall: {
        HoldTool(uint8(EMCToolSlot::Pickaxe));
        if(!bBufferPlaced && Age>.8f) {
            FVector Tip;float Radius;
            if(Worker->Inventory->BufferContactSphere(Tip,Radius)) {
                if(StationKind==EMCVFXLabToolCase::BufferAoE) {
                    SpawnIce(Tip+Worker->GetActorRightVector()*28,24,150);
                    SpawnIce(Tip-Worker->GetActorRightVector()*28,24,150);
                } else {
                    SpawnWall(Tip+Worker->GetActorForwardVector()*20,FVector(5,90,90),Worker->GetActorRotation());
                    if(!Wall) {FinishCycle(false,TEXT("Could not spawn buffer wall"));return;}
                    SpawnIce(Tip+Worker->GetActorForwardVector()*42,24,150);
                }
                bBufferPlaced=Ice.Num()==(StationKind==EMCVFXLabToolCase::BufferAoE?2:1);
                InitialRemaining=FMath::Max(Remaining(),1.f);
                for(const auto& Block:Ice) LastIceHealth.Add(Block->Health);
                if(!bBufferPlaced) {FinishCycle(false,TEXT("Could not place real buffer contact fixtures"));return;}
            }
        }
        if(StationKind==EMCVFXLabToolCase::BufferWall && bBufferPlaced && !bWallOpened) {
            if(Remaining()<InitialRemaining-.01f) {FinishCycle(false,TEXT("FAIL: buffer damaged the target through its wall"));return;}
            if(Age>3) {Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);bWallOpened=true;}
        }
        if(StationKind==EMCVFXLabToolCase::BufferAoE && bBufferPlaced) {
            int32 Changed=0;
            for(int32 I=0;I<Ice.Num();++I) {
                const float Health=IsValid(Ice[I])?Ice[I]->Health:0;
                if(Health<LastIceHealth[I]-.01f) ++Changed;
                LastIceHealth[I]=Health;
            }
            bSawMultiHit|=Changed>1;
        }
        break;
    }
    case EMCVFXLabToolCase::KnifeFood: case EMCVFXLabToolCase::ChainsawFood: HoldTool(uint8(EMCToolSlot::Knife));break;
    case EMCVFXLabToolCase::ChainsawWall:
        HoldTool(uint8(EMCToolSlot::Knife)); Worker->AddMovementInput(GetActorForwardVector());break;
    default: break;
    }
    const float Left=Remaining(); Progress=FMath::Clamp(1-Left/InitialRemaining,0.f,1.f);
    const bool FloorCleanCase=StationKind==EMCVFXLabToolCase::FloorBrush || StationKind==EMCVFXLabToolCase::FloorMeshaBrush;
    const bool ToothCleanCase=StationKind==EMCVFXLabToolCase::ToothBrush || StationKind==EMCVFXLabToolCase::ToothMeshaBrush;
    // Native care ends when the last coffee layer is removed. Its spatial mask
    // deliberately retains a faint fringe, so requiring an all-zero mask would
    // wait forever after the game has stopped accepting further brush contacts.
    const bool Completed=FloorCleanCase?IsValid(Patch) && Patch->IsClean()
        :ToothCleanCase?IsValid(Tooth) && Tooth->Status->IsAlive() && !Tooth->Status->NeedsCare(true)
        :Left<=.001f;
    if(Completed && Age>.8f) {
        if((StationKind==EMCVFXLabToolCase::PickaxeCalculus || StationKind==EMCVFXLabToolCase::BufferCalculus) && !bHadCalculus) return;
        if((StationKind==EMCVFXLabToolCase::BufferAoE || StationKind==EMCVFXLabToolCase::BufferWall) && !bBufferPlaced) return;
        if(StationKind==EMCVFXLabToolCase::BufferAoE && !bSawMultiHit) {FinishCycle(false,TEXT("Targets broke, but no simultaneous AoE pulse was observed"));return;}
        if(FloorCleanCase || ToothCleanCase) {
            if(Worker->SuccessfulBrushContacts<=InitialContacts) {FinishCycle(false,TEXT("No authoritative brush contact observed"));return;}
            // These fixtures are seeded with the native full 255 mask each
            // cycle. Require actual byte-level wiping as well as care completion.
            const TArray<uint8>& Mask=FloorCleanCase?Patch->WipeMask:Tooth->GrimeMask;
            if(Mask.IsEmpty() || !Mask.ContainsByPredicate([](uint8 Value){return Value<255;}) || Left>=InitialRemaining) {
                FinishCycle(false,TEXT("Native care ended without an observed cleaning mask change"));return;
            }
        }
        Progress=1;
        FinishCycle(true,FString::Printf(TEXT("PASS: observed completion; brush contacts +%d, confirmed hits +%d"),Worker->SuccessfulBrushContacts-InitialContacts,Worker->ConfirmedHitCount-InitialHits));
    }
    else if(GetWorld()->GetTimeSeconds()>=NextReadoutAt) {
        NextReadoutAt=GetWorld()->GetTimeSeconds()+1;
        SetStatus(FString::Printf(TEXT("RUN: %.0f%% | remaining %.3f | contacts +%d | hits +%d"),Progress*100,Left,
            Worker->SuccessfulBrushContacts-InitialContacts,Worker->ConfirmedHitCount-InitialHits));
    }
}

void AMCVFXLabToolStation::FinishCycle(bool bSuccess,const FString& Reason)
{
    if(!bCycleActive) return;
    bCycleActive=false; Passed=bSuccess;Failed=!bSuccess; ++Cycles;
    if(bSuccess) ++PassedCycles; else ++FailedCycles;
    if(IsValid(Worker)) {Worker->CancelGameplayInput();Worker->ConsumeMovementInputVector();}
    if(IsValid(Bot)) Bot->StopMovement();
    NextCycleAt=GetWorld()->GetTimeSeconds()+FMath::Max(.5f,RefillPause); SetStatus(Reason);
}
void AMCVFXLabToolStation::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!HasAuthority() || !bRunning) return;
    if(!bCycleActive) {
        if(GetWorld()->GetTimeSeconds()>=NextCycleAt && !PrepareCycle()) {
            Failed=true;++FailedCycles;bRunning=false;
            SetActorTickEnabled(false);ClearAll();
            if(!Status.Contains(TEXT("FAIL:"))) SetStatus(TEXT("FAIL: fixture or authored tool profile could not be created"));
            else {OnRep_Status();ForceNetUpdate();}
        }
        return;
    }
    if(!IsValid(Worker) || !IsValid(Bot)) {FinishCycle(false,TEXT("FAIL: worker disappeared"));return;}
    DriveInputs(Dt);
    if(bCycleActive && GetWorld()->GetTimeSeconds()-StartedAt>FMath::Clamp(CycleSeconds,8.f,180.f))
        FinishCycle(false,FString::Printf(TEXT("FAIL: no completion in %.0fs; actual remaining %.3f"),CycleSeconds,Remaining()));
}
void AMCVFXLabToolStation::ClearFixtures()
{
    if(IsValid(Flood) && Flood->IsActive()) Flood->Stop();
    for(const auto& Actor:Fixtures) if(IsValid(Actor)) Actor->Destroy();
    Fixtures.Reset();Ice.Reset();Tooth=nullptr;Patch=nullptr;Food=nullptr;Fire=nullptr;Flood=nullptr;CoffeePlan=nullptr;
    Wall=nullptr;
}
void AMCVFXLabToolStation::ClearAll()
{
    if(IsValid(Worker)) Worker->CancelGameplayInput();
    ClearFixtures();
    if(IsValid(Bot)) {
        auto* PS=Bot->GetPlayerState<AMCPlayerState>();Bot->UnPossess();Bot->Destroy();if(IsValid(PS)) PS->Destroy();
    }
    if(IsValid(Worker)) Worker->Destroy();Bot=nullptr;Worker=nullptr;bCycleActive=false;
}
void AMCVFXLabToolStation::SetRunning(bool bEnabled)
{
    if(!HasAuthority() || !HasActorBegunPlay() || !GetWorld() || !GetWorld()->IsGameWorld() || bRunning==bEnabled) return;
    bRunning=bEnabled;
    SetActorTickEnabled(bRunning);
    if(!bEnabled) {
        // Distant workers still run movement, animation, inventory and physics.
        // Remove the complete owned simulation, not just its visible fixtures.
        ClearAll();SetStatus(TEXT("Paused; owned actors cleared"));
    }
    else {NextCycleAt=GetWorld()->GetTimeSeconds();SetStatus(TEXT("Starting"));}
}
void AMCVFXLabToolStation::Reset()
{
    if(!HasAuthority()) return;
    const bool Resume=bRunning;ClearAll();Cycles=PassedCycles=FailedCycles=0;Passed=Failed=false;Progress=0;
    NextCycleAt=GetWorld()->GetTimeSeconds();bRunning=Resume;SetActorTickEnabled(Resume);SetStatus(TEXT("Reset; fixtures will refill"));
}
void AMCVFXLabToolStation::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning=false;SetActorTickEnabled(false);if(HasAuthority()) ClearAll();Super::EndPlay(Reason);
}
void AMCVFXLabToolStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVFXLabToolStation,bRunning);DOREPLIFETIME(AMCVFXLabToolStation,Passed);DOREPLIFETIME(AMCVFXLabToolStation,Failed);
    DOREPLIFETIME(AMCVFXLabToolStation,Cycles);DOREPLIFETIME(AMCVFXLabToolStation,PassedCycles);DOREPLIFETIME(AMCVFXLabToolStation,FailedCycles);
    DOREPLIFETIME(AMCVFXLabToolStation,Status);DOREPLIFETIME(AMCVFXLabToolStation,Progress);DOREPLIFETIME(AMCVFXLabToolStation,Worker);
}
