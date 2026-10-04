#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCPerkPickup.h"
#include "MCRoguelikeDirector.h"
#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

namespace
{
bool ValidRow(const FMCPerkDefinition* Row)
{
    return Row && (Row->Polarity==EMCPerkPolarity::Positive || Row->Polarity==EMCPerkPolarity::Negative)
        && FMath::IsFinite(Row->Weight) && Row->Weight>0 && Row->MaxStacks>0
        && (!Row->EffectClass || !Row->EffectClass->HasAnyClassFlags(CLASS_Abstract));
}

void EligibleRows(UDataTable* Table,const UMCPerkComponent* Recipient,TArray<FName>& Positive,TArray<FName>& Negative)
{
    if(!Table || Table->GetRowStruct()!=FMCPerkDefinition::StaticStruct()) return;
    if(Recipient && Recipient->GetPerkTable()!=Table) return;
    TArray<FName> Names=Table->GetRowNames();
    Names.Sort([](const FName& A,const FName& B) { return A.LexicalLess(B); });
    for(FName ID:Names) {
        const auto* Row=Table->FindRow<FMCPerkDefinition>(ID,TEXT("Reward loot"),false);
        if(!ValidRow(Row) || (Recipient && !Recipient->CanGrantPerk(ID))) continue;
        (Row->Polarity==EMCPerkPolarity::Positive?Positive:Negative).Add(ID);
    }
}
}

AMCRewardChest::AMCRewardChest()
{
    bReplicates=true; SetReplicateMovement(false); PrimaryActorTick.bCanEverTick=true;
    Scene=CreateDefaultSubobject<USceneComponent>(TEXT("RewardRoot")); SetRootComponent(Scene);
    Solid=CreateDefaultSubobject<UBoxComponent>(TEXT("ChestCollision")); Solid->SetupAttachment(Scene);
    Solid->SetCollisionObjectType(ECC_WorldDynamic); Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Solid->SetCollisionResponseToAllChannels(ECR_Block); Solid->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBottom")); Body->SetupAttachment(Scene);
    LidPivot=CreateDefaultSubobject<USceneComponent>(TEXT("LidHinge")); LidPivot->SetupAttachment(Scene);
    Lid=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestTop")); Lid->SetupAttachment(LidPivot);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BottomMesh(TEXT("/Game/FromBlender6/SM_Chest_Bot.SM_Chest_Bot"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> TopMesh(TEXT("/Game/FromBlender6/SM_Chest_Top.SM_Chest_Top"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ChestMaterial(TEXT("/Game/Art/Materials/Buster/MI_Chest.MI_Chest"));
    if(BottomMesh.Succeeded()) Body->SetStaticMesh(BottomMesh.Object);
    if(TopMesh.Succeeded()) Lid->SetStaticMesh(TopMesh.Object);
    if(ChestMaterial.Succeeded()) { Body->SetMaterial(0,ChestMaterial.Object); Lid->SetMaterial(0,ChestMaterial.Object); }
    Telegraph=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LandingTelegraph")); Telegraph->SetupAttachment(Scene);
    Telegraph->SetAbsolute(true,true,true); Telegraph->SetCollisionEnabled(ECollisionEnabled::NoCollision); Telegraph->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Disc(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if(Disc.Succeeded()) Telegraph->SetStaticMesh(Disc.Object);
    if(MarkerMaterial.Succeeded()) Telegraph->SetMaterial(0,MarkerMaterial.Object);
    Approach=CreateDefaultSubobject<USphereComponent>(TEXT("LivingPlayerApproach")); Approach->SetupAttachment(Scene);
    Approach->InitSphereRadius(OpenRadius); Approach->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Approach->SetCollisionResponseToAllChannels(ECR_Ignore); Approach->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    PickupClass=AMCPerkPickup::StaticClass();
    PerkTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")));
    ConfigureGeometry();
}

void AMCRewardChest::ConfigureGeometry()
{
    if(!Body->GetStaticMesh() || !Lid->GetStaticMesh()) return;
    const float Scale=FMath::IsFinite(ModelScale)?FMath::Clamp(ModelScale,.05f,2.f):.35f;
    const FBox Bottom=Body->GetStaticMesh()->GetBoundingBox(),Top=Lid->GetStaticMesh()->GetBoundingBox();
    const float BottomOffset=float(-Bottom.Min.Z*Scale),HingeZ=float((Bottom.Max.Z-Bottom.Min.Z)*Scale),HingeY=float(Top.Max.Y*Scale);
    Body->SetRelativeScale3D(FVector(Scale)); Body->SetRelativeLocation(FVector(0,0,BottomOffset));
    LidPivot->SetRelativeLocation(FVector(0,HingeY,HingeZ));
    Lid->SetRelativeScale3D(FVector(Scale)); Lid->SetRelativeLocation(FVector(0,-HingeY,-Top.Min.Z*Scale));
    FBox Closed=Bottom.TransformBy(FTransform(FQuat::Identity,FVector(0,0,BottomOffset),FVector(Scale)));
    Closed+=Top.TransformBy(FTransform(FQuat::Identity,FVector(0,0,HingeZ-Top.Min.Z*Scale),FVector(Scale)));
    Solid->SetBoxExtent(Closed.GetExtent()); Solid->SetRelativeLocation(Closed.GetCenter());
    Approach->SetSphereRadius(FMath::IsFinite(OpenRadius)?FMath::Clamp(OpenRadius,100.f,800.f):320.f);
    const double Radius=GetPlacementHalfExtent().X;
    Telegraph->SetWorldScale3D(FVector(Radius/50,Radius/50,.02));
}

FVector AMCRewardChest::GetPlacementHalfExtent() const
{
    if(!Body->GetStaticMesh() || !Lid->GetStaticMesh()) return FVector(230,230,100);
    const float Scale=FMath::IsFinite(ModelScale)?FMath::Clamp(ModelScale,.05f,2.f):.35f;
    const FBox Bottom=Body->GetStaticMesh()->GetBoundingBox(),Top=Lid->GetStaticMesh()->GetBoundingBox();
    const double Side=FMath::Max(95.,Bottom.GetExtent().X*Scale)+34;
    const double Front=Bottom.GetExtent().Y*Scale+70+34;
    // Enclose the three outside pickups and any yaw, not only the closed chest.
    const double Radius=FMath::Sqrt(Side*Side+Front*Front);
    const double Height=FMath::Max(180.,(Bottom.GetSize().Z+Top.GetSize().Z)*Scale+50);
    return FVector(Radius,Radius,Height*.5);
}

void AMCRewardChest::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform); ConfigureGeometry();
}

double AMCRewardChest::ServerNow() const
{
    const auto* State=GetWorld()->GetGameState();
    return State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}

void AMCRewardChest::InitializeReward(FVector Landing,FVector Start,int32 Seed,UDataTable* Table,
    EMCRewardSelectionPolicy Policy,AMCRewardDropZone* Zone)
{
    if(!HasAuthority()) return;
    LandingPoint=Landing; FallStart=Start; RollSeed=Seed; RewardTable=Table; SelectionPolicy=Policy; DropZone=Zone;
    bRewardInitialized=true; Stage=EMCRewardChestStage::Telegraph; StageStartedAt=ServerNow();
}

void AMCRewardChest::BeginPlay()
{
    Super::BeginPlay(); ConfigureGeometry();
    if(HasAuthority() && bPlacedReward && !bRewardInitialized) {
        RewardTable=PerkTable.LoadSynchronous(); LandingPoint=FallStart=GetActorLocation();
        const auto* State=GetWorld()->GetGameState<AMCGameState>();
        RollSeed=int32(FCrc::StrCrc32(*GetName()))^(State?State->RunSeed:0);
        DropZone=PlacedDropZone;
        if(!DropZone) for(TActorIterator<AMCRewardDropZone> It(GetWorld());It;++It)
            if(It->ContainsFootprint(LandingPoint,GetPlacementHalfExtent()*GetActorScale3D().GetAbs())) { DropZone=*It; break; }
        bRewardInitialized=true; Stage=EMCRewardChestStage::Landed; StageStartedAt=ServerNow();
        if(!DropZone) UE_LOG(LogTemp,Warning,TEXT("MC_REWARD_PLACED: %s needs an authored PlacedDropZone"),*GetName());
    }
    if(auto* MID=Telegraph->CreateDynamicMaterialInstance(0)) MID->SetVectorParameterValue(TEXT("Color"),FLinearColor(1,.68f,.05f));
    RefreshPresentation();
    if(HasAuthority() && bRewardInitialized) GetWorldTimerManager().SetTimer(ApproachTimer,this,&AMCRewardChest::PollApproach,.25f,true);
}

void AMCRewardChest::RefreshPresentation()
{
    SetActorHiddenInGame(!bRewardInitialized);
    const bool Landed=bRewardInitialized && Stage>=EMCRewardChestStage::Landed;
    Solid->SetCollisionEnabled(Landed?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    Approach->SetCollisionEnabled(Landed && Stage!=EMCRewardChestStage::Exhausted?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    Telegraph->SetVisibility(bRewardInitialized && (Stage==EMCRewardChestStage::Telegraph || Stage==EMCRewardChestStage::Falling));
    Telegraph->SetWorldLocation(LandingPoint+FVector(0,0,2));
    const bool Animated=bRewardInitialized && (Stage==EMCRewardChestStage::Telegraph || Stage==EMCRewardChestStage::Falling || Stage==EMCRewardChestStage::Opening);
    SetActorTickEnabled(Animated);
    if(bRewardInitialized && !Animated) {
        SetActorLocation(LandingPoint);
        LidPivot->SetRelativeRotation(FRotator(0,0,Stage>=EMCRewardChestStage::Open?-105.f:0.f));
    }
}

void AMCRewardChest::SetStage(EMCRewardChestStage Next)
{
    if(!HasAuthority()) return;
    Stage=Next; StageStartedAt=ServerNow(); RefreshPresentation(); ForceNetUpdate();
}

void AMCRewardChest::Tick(float Dt)
{
    Super::Tick(Dt); if(!bRewardInitialized) return;
    const double Age=FMath::Max(0.,ServerNow()-StageStartedAt);
    if(Stage==EMCRewardChestStage::Telegraph) {
        SetActorLocation(FallStart);
        if(HasAuthority() && Age>=FMath::Max(.1f,TelegraphSeconds)) SetStage(EMCRewardChestStage::Falling);
    } else if(Stage==EMCRewardChestStage::Falling) {
        const float Alpha=FMath::Clamp(float(Age/FMath::Max(.1f,FallSeconds)),0.f,1.f);
        SetActorLocation(FMath::Lerp(FallStart,LandingPoint,Alpha*Alpha));
    } else if(Stage==EMCRewardChestStage::Opening) {
        const float Alpha=FMath::Clamp(float(Age/FMath::Max(.1f,OpeningSeconds)),0.f,1.f);
        LidPivot->SetRelativeRotation(FRotator(0,0,-105.f*(Alpha*Alpha*(3-2*Alpha))));
        if(HasAuthority() && Alpha>=1) {
            SetStage(EMCRewardChestStage::Open);
            for(AMCPerkPickup* Pickup:Pickups) if(IsValid(Pickup)) { Pickup->SetActorEnableCollision(true); Pickup->StartArming(); }
        }
    }
}

bool AMCRewardChest::HasClearLanding() const
{
    if(!IsValid(DropZone) || !DropZone->ContainsFootprint(LandingPoint,GetPlacementHalfExtent())) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RewardTouchdown),true,this);
    FHitResult Floor;
    if(!GetWorld()->LineTraceSingleByChannel(Floor,LandingPoint+FVector(0,0,30),LandingPoint-FVector(0,0,30),ECC_Visibility,Query)
        || !DropZone->AcceptsFloor(Floor) || FMath::Abs(Floor.ImpactPoint.Z-LandingPoint.Z)>15) return false;
    Query.AddIgnoredActor(Floor.GetActor()); Query.bTraceComplex=false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    const FVector Extent=GetPlacementHalfExtent();
    return !GetWorld()->OverlapAnyTestByObjectType(LandingPoint+FVector(0,0,Extent.Z),FQuat::Identity,
        Objects,FCollisionShape::MakeBox(Extent),Query);
}

bool AMCRewardChest::IsLivingPlayer(const AMCToothCharacter* Player) const
{
    const auto* PS=IsValid(Player)?Player->GetPlayerState<AMCPlayerState>():nullptr;
    return PS && PS->GetPawn()==Player && Player->Status && Player->Status->IsAlive() && !Player->SwallowedBy
        && !Player->IsActorBeingDestroyed();
}

bool AMCRewardChest::HasValidLoot(UDataTable* Table,const UMCPerkComponent* Recipient)
{
    TArray<FName> Positive,Negative; EligibleRows(Table,Recipient,Positive,Negative);
    return Positive.Num()>=3 || Negative.Num()>=3;
}

void AMCRewardChest::PollApproach()
{
    if(!HasAuthority() || !bRewardInitialized) return;
    if(Stage==EMCRewardChestStage::Falling) {
        const double Age=ServerNow()-StageStartedAt;
        if(Age>=FMath::Max(.1f,FallSeconds)) {
            if(HasClearLanding()) SetStage(EMCRewardChestStage::Landed);
            else if(Age>FMath::Max(.1f,FallSeconds)+6) FinishReward(true);
        }
        return;
    }
    TArray<AActor*> Nearby;
    if(Stage==EMCRewardChestStage::Landed) {
        Approach->GetOverlappingActors(Nearby,AMCToothCharacter::StaticClass());
        Nearby.Sort([this](const AActor& A,const AActor& B) {
            const double DA=FVector::DistSquared(A.GetActorLocation(),GetActorLocation()),DB=FVector::DistSquared(B.GetActorLocation(),GetActorLocation());
            return DA==DB?A.GetPathName()<B.GetPathName():DA<DB;
        });
        for(AActor* Actor:Nearby) if(OpenFor(Cast<AMCToothCharacter>(Actor))) break;
    }
}

bool AMCRewardChest::OpenFor(AMCToothCharacter* Player)
{
    if(!HasAuthority() || Stage!=EMCRewardChestStage::Landed || !IsLivingPlayer(Player)
        || !IsValid(DropZone) || !PickupClass || !Body->GetStaticMesh() || !Lid->GetStaticMesh()) return false;
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    if(!PS->Perks || PS->Perks->GetPerkTable()!=RewardTable) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RewardApproach),true,this); Query.AddIgnoredActor(Player);
    FHitResult Block;
    const FVector Eye=Player->GetActorLocation()+FVector(0,0,35),Center=Solid->Bounds.Origin;
    if(GetWorld()->LineTraceSingleByChannel(Block,Eye,Center,ECC_Visibility,Query)) return false;
    if(LootIDs.IsEmpty()) {
        TArray<FName> Positive,Negative; EligibleRows(RewardTable,PS->Perks,Positive,Negative);
        if(Positive.Num()<3 && Negative.Num()<3) return false;
        FRandomStream Roll(RollSeed);
        Polarity=Positive.Num()<3?EMCPerkPolarity::Negative:Negative.Num()<3?EMCPerkPolarity::Positive:
            (Roll.RandRange(0,1)==0?EMCPerkPolarity::Positive:EMCPerkPolarity::Negative);
        TArray<FName> Pool=Polarity==EMCPerkPolarity::Positive?Positive:Negative;
        for(int32 I=0;I<3;++I) {
            double Total=0; for(FName ID:Pool) Total+=RewardTable->FindRow<FMCPerkDefinition>(ID,TEXT("Loot weight"),false)->Weight;
            double Draw=Roll.FRand()*Total; int32 Selected=Pool.Num()-1;
            for(int32 J=0;J<Pool.Num();++J) { Draw-=RewardTable->FindRow<FMCPerkDefinition>(Pool[J],TEXT("Loot draw"),false)->Weight; if(Draw<0) { Selected=J; break; } }
            LootIDs.Add(Pool[Selected]); Pool.RemoveAt(Selected);
        }
        ForceNetUpdate();
    }
    if(LootIDs.Num()!=3) return false;
    for(FName ID:LootIDs) if(!PS->Perks->CanGrantPerk(ID)) return false;
    FVector Points[3];
    const double Front=Body->GetStaticMesh()->GetBoundingBox().GetExtent().Y*FMath::Clamp(ModelScale,.05f,2.f)+70;
    for(int32 I=0;I<3;++I) {
        const FVector P=GetActorTransform().TransformPosition(FVector((I-1)*95,-Front,0)); FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,P+FVector(0,0,100),P-FVector(0,0,100),ECC_Visibility,Query)
            || !DropZone->AcceptsFloor(Floor) || !DropZone->ContainsFootprint(Floor.ImpactPoint,FVector(34,34,0))) return false;
        Points[I]=Floor.ImpactPoint+FVector(0,0,45);
        FCollisionQueryParams Clearance(SCENE_QUERY_STAT(RewardPickupSpace),false,this);
        Clearance.AddIgnoredActor(Player); Clearance.AddIgnoredActor(Floor.GetActor());
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        if(GetWorld()->OverlapAnyTestByObjectType(Points[I],FQuat::Identity,Objects,FCollisionShape::MakeSphere(34),Clearance)) return false;
    }
    TArray<TObjectPtr<AMCPerkPickup>> Staged;
    for(int32 I=0;I<3;++I) {
        const auto* Row=RewardTable->FindRow<FMCPerkDefinition>(LootIDs[I],TEXT("Display loot"),false);
        if(!ValidRow(Row)) { for(AMCPerkPickup* Spawned:Staged) Spawned->Destroy(); return false; }
        const FTransform Transform(FRotator(0,GetActorRotation().Yaw-90,0),Points[I]);
        auto* Pickup=GetWorld()->SpawnActorDeferred<AMCPerkPickup>(PickupClass,Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Pickup) { for(AMCPerkPickup* Spawned:Staged) Spawned->Destroy(); return false; }
        Pickup->InitializePickup(this,I,LootIDs[I],Row->DisplayName,Row->Description,Polarity);
        Pickup->FinishSpawning(Transform); Pickup->SetActorEnableCollision(false); Staged.Add(Pickup);
    }
    Pickups=MoveTemp(Staged); SetStage(EMCRewardChestStage::Opening);
    UE_LOG(LogTemp,Display,TEXT("MC_REWARD_OPEN %s polarity=%d ids=%s,%s,%s"),*GetName(),int32(Polarity),*LootIDs[0].ToString(),*LootIDs[1].ToString(),*LootIDs[2].ToString());
    return true;
}

bool AMCRewardChest::TryClaim(AMCPerkPickup* Pickup,AMCToothCharacter* Player)
{
    if(!HasAuthority() || bClaimInProgress || Stage!=EMCRewardChestStage::Open || !IsLivingPlayer(Player)
        || !IsValid(Pickup) || Pickup->bClaimed || Pickup->RewardChest!=this || !Pickups.Contains(Pickup)
        || !LootIDs.IsValidIndex(Pickup->LootIndex) || LootIDs[Pickup->LootIndex]!=Pickup->PerkID
        || (ClaimedMask&(1<<Pickup->LootIndex)) || FVector::DistSquared(Player->GetActorLocation(),Pickup->GetActorLocation())>FMath::Square(160.f)) return false;
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    if(!PS->Perks || PS->Perks->GetPerkTable()!=RewardTable) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RewardClaim),true,this); Query.AddIgnoredActor(Player); Query.AddIgnoredActor(Pickup);
    FHitResult Block;
    if(GetWorld()->LineTraceSingleByChannel(Block,Player->GetActorLocation()+FVector(0,0,35),Pickup->GetActorLocation(),ECC_Visibility,Query)) return false;
    TGuardValue<bool> Guard(bClaimInProgress,true);
    if(!PS->Perks->ServerGrantPerk(Pickup->PerkID)) return false;
    ClaimedMask|=uint8(1<<Pickup->LootIndex); Pickup->MarkClaimed();
    if(SelectionPolicy==EMCRewardSelectionPolicy::ChooseOne) {
        ClaimedMask=7; for(AMCPerkPickup* Other:Pickups) if(IsValid(Other)) Other->MarkClaimed();
    }
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_REWARD_CLAIM player=%s perk=%s"),*PS->GetPlayerName(),*Pickup->PerkID.ToString());
    if(ClaimedMask==7) FinishReward(false);
    return true;
}

void AMCRewardChest::FinishReward(bool bRequeue)
{
    if(!HasAuthority() || bReported) return;
    bReported=true; GetWorldTimerManager().ClearTimer(ApproachTimer);
    if(auto* Director=Cast<AMCRoguelikeDirector>(GetOwner())) Director->NotifyChestFinished(this,bRequeue);
    for(AMCPerkPickup* Pickup:Pickups) if(IsValid(Pickup)) { Pickup->MarkClaimed(); Pickup->SetLifeSpan(2); }
    SetStage(EMCRewardChestStage::Exhausted);
    if(!bPlacedReward) SetLifeSpan(bRequeue?.1f:2.f);
}

void AMCRewardChest::ResetPlacedReward()
{
    if(!HasAuthority() || !bPlacedReward) return;
    const FVector AuthoredLanding=bRewardInitialized?LandingPoint:GetActorLocation();
    GetWorldTimerManager().ClearTimer(ApproachTimer); SetLifeSpan(0);
    for(AMCPerkPickup* Pickup:Pickups) if(IsValid(Pickup)) Pickup->Destroy();
    Pickups.Reset(); LootIDs.Reset(); ClaimedMask=0; bReported=false; bClaimInProgress=false;
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    InitializeReward(AuthoredLanding,AuthoredLanding,int32(FCrc::StrCrc32(*GetName()))^(State?State->RunSeed:0),
        PerkTable.LoadSynchronous(),EMCRewardSelectionPolicy::ChooseOne,IsValid(PlacedDropZone)?PlacedDropZone.Get():DropZone.Get());
    SetStage(EMCRewardChestStage::Landed);
    GetWorldTimerManager().SetTimer(ApproachTimer,this,&AMCRewardChest::PollApproach,.25f,true);
}

void AMCRewardChest::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearAllTimersForObject(this);
    if(HasAuthority()) {
        for(AMCPerkPickup* Pickup:Pickups) if(IsValid(Pickup)) Pickup->Destroy();
        if(!bReported && Reason==EEndPlayReason::Destroyed)
            if(auto* Director=Cast<AMCRoguelikeDirector>(GetOwner())) Director->NotifyChestFinished(this,ClaimedMask==0);
    }
    Super::EndPlay(Reason);
}

void AMCRewardChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCRewardChest,SelectionPolicy); DOREPLIFETIME(AMCRewardChest,Stage); DOREPLIFETIME(AMCRewardChest,StageStartedAt);
    DOREPLIFETIME(AMCRewardChest,LandingPoint); DOREPLIFETIME(AMCRewardChest,FallStart); DOREPLIFETIME(AMCRewardChest,LootIDs);
    DOREPLIFETIME(AMCRewardChest,Polarity); DOREPLIFETIME(AMCRewardChest,ClaimedMask); DOREPLIFETIME(AMCRewardChest,bRewardInitialized);
}
