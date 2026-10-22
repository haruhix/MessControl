#include "MCRewardChest.h"
#include "MCGameDirector.h"
#include "MCRewardDropZone.h"
#include "MCPerkPickup.h"
#include "MCRoguelikeDirector.h"
#include "MCSingleDayDirector.h"
#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "MCToothStatusComponent.h"
#include "MCExpressionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

namespace
{
bool ValidRow(const FMCPerkDefinition* Row)
{
    return Row && uint8(Row->Polarity)<=uint8(EMCPerkPolarity::Risky)
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
        if(!ValidRow(Row) || Row->ToolUpgrade!=EMCToolUpgrade::None || (Recipient && !Recipient->CanGrantPerk(ID))) continue;
        (Row->Polarity==EMCPerkPolarity::Positive?Positive:Negative).Add(ID);
    }
}
}

AMCRewardChest::AMCRewardChest()
{
    bReplicates=true; SetReplicateMovement(false); PrimaryActorTick.bCanEverTick=true;
    Scene=CreateDefaultSubobject<USceneComponent>(TEXT("RewardRoot")); SetRootComponent(Scene);
    PresentationPivot=CreateDefaultSubobject<USceneComponent>(TEXT("ChestPresentation")); PresentationPivot->SetupAttachment(Scene);
    Solid=CreateDefaultSubobject<UBoxComponent>(TEXT("ChestCollision")); Solid->SetupAttachment(Scene);
    Solid->SetCollisionObjectType(ECC_WorldDynamic); Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Solid->SetCollisionResponseToAllChannels(ECR_Block); Solid->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestBottom")); Body->SetupAttachment(PresentationPivot);
    LidPivot=CreateDefaultSubobject<USceneComponent>(TEXT("LidHinge")); LidPivot->SetupAttachment(PresentationPivot);
    Lid=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestTop")); Lid->SetupAttachment(LidPivot);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BottomMesh(TEXT("/Game/FromBlender/SM_Chest_Bot.SM_Chest_Bot"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> TopMesh(TEXT("/Game/FromBlender/SM_Chest_Top.SM_Chest_Top"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ChestMaterial(TEXT("/Game/Art/Materials/Buster/MI_Chest.MI_Chest"));
    if(BottomMesh.Succeeded()) Body->SetStaticMesh(BottomMesh.Object);
    if(TopMesh.Succeeded()) Lid->SetStaticMesh(TopMesh.Object);
    if(ChestMaterial.Succeeded()) for(auto* Part:{Body.Get(),Lid.Get()}) {
        // Preserve a Blueprint/component archetype's authored surface. The
        // imported meshes have a WorldGrid default, which alone needs fallback.
        const auto* Authored=Part->GetMaterial(0);
        if(!Authored || Authored->GetPathName().Contains(TEXT("WorldGridMaterial")))
            Part->SetMaterial(0,ChestMaterial.Object);
    }
    MimicMouth=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MimicMouth")); MimicMouth->SetupAttachment(Body);
    MimicLowerTeeth=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MimicLowerTeeth")); MimicLowerTeeth->SetupAttachment(Body);
    MimicUpperTeeth=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MimicUpperTeeth")); MimicUpperTeeth->SetupAttachment(Lid);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> MouthCube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ToothCone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if(MouthCube.Succeeded()) MimicMouth->SetStaticMesh(MouthCube.Object);
    if(ToothCone.Succeeded()) { MimicLowerTeeth->SetStaticMesh(ToothCone.Object); MimicUpperTeeth->SetStaticMesh(ToothCone.Object); }
    for(auto* Part:{MimicMouth.Get(),static_cast<UStaticMeshComponent*>(MimicLowerTeeth.Get()),static_cast<UStaticMeshComponent*>(MimicUpperTeeth.Get())}) {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false); Part->SetVisibility(false);
    }
    Telegraph=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LandingTelegraph")); Telegraph->SetupAttachment(Scene);
    Telegraph->SetAbsolute(true,true,true); Telegraph->SetCollisionEnabled(ECollisionEnabled::NoCollision); Telegraph->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    // Retain the named subobject for saved Blueprint compatibility, but no
    // solid ground marker or fallback primitive is part of this presentation.
    Telegraph->SetVisibility(false);Telegraph->SetHiddenInGame(true);
    if(MarkerMaterial.Succeeded()) {
        MimicMouth->SetMaterial(0,MarkerMaterial.Object);
        MimicLowerTeeth->SetMaterial(0,MarkerMaterial.Object); MimicUpperTeeth->SetMaterial(0,MarkerMaterial.Object);
    }
    Approach=CreateDefaultSubobject<USphereComponent>(TEXT("LivingPlayerApproach")); Approach->SetupAttachment(Scene);
    Approach->InitSphereRadius(OpenRadius); Approach->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Approach->SetCollisionResponseToAllChannels(ECR_Ignore); Approach->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    RogueLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("RogueRewardLabel"));RogueLabel->SetupAttachment(Scene);
    RogueLabel->SetRelativeLocation(FVector(0,0,160));RogueLabel->SetWorldSize(22);
    RogueLabel->SetHorizontalAlignment(EHTA_Center);RogueLabel->SetVerticalAlignment(EVRTA_TextCenter);
    RogueLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);RogueLabel->SetCastShadow(false);
    ClaimRing=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClaimRing")); ClaimRing->SetupAttachment(Scene);
    ClaimSparks=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ClaimSparks")); ClaimSparks->SetupAttachment(Scene);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> EffectPlane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(EffectPlane.Succeeded()) {ClaimRing->SetStaticMesh(EffectPlane.Object);ClaimSparks->SetStaticMesh(EffectPlane.Object);}
    for(auto* Effect:{ClaimRing.Get(),static_cast<UStaticMeshComponent*>(ClaimSparks.Get())}) {
        Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision); Effect->SetGenerateOverlapEvents(false);
        Effect->SetCastShadow(false); Effect->SetVisibility(false); Effect->SetReceivesDecals(false);
    }
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMaterial(TEXT("/Game/Gameplay/VFX/RewardChest/M_RewardChest_ClaimRing.M_RewardChest_ClaimRing"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SparkMaterial(TEXT("/Game/Gameplay/VFX/RewardChest/M_RewardChest_ClaimSpark.M_RewardChest_ClaimSpark"));
    if(RingMaterial.Succeeded()) ClaimRing->SetMaterial(0,RingMaterial.Object);
    if(SparkMaterial.Succeeded()) ClaimSparks->SetMaterial(0,SparkMaterial.Object);
    PerkTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks")));
    ConfigureGeometry();
}

void AMCRewardChest::ConfigureGeometry()
{
    // Saved Blueprint component templates can retain the former RewardRoot
    // attachment. Keep the new cosmetic pivot effective after their construction.
    for(auto* Part:{static_cast<USceneComponent*>(Body.Get()),LidPivot.Get()})
        if(Part->GetAttachParent()!=PresentationPivot) {
            if(Part->IsRegistered()) Part->AttachToComponent(PresentationPivot,FAttachmentTransformRules::KeepRelativeTransform);
            else Part->SetupAttachment(PresentationPivot);
        }
    if(!Body->GetStaticMesh() || !Lid->GetStaticMesh()) return;
    const float Scale=FMath::IsFinite(ModelScale)?FMath::Clamp(ModelScale,.05f,2.f):.35f;
    const FBox Bottom=Body->GetStaticMesh()->GetBoundingBox(),Top=Lid->GetStaticMesh()->GetBoundingBox();
    const float SeatOffset=FMath::IsFinite(LidSeatOffset)?LidSeatOffset:0.f;
    const float BottomOffset=float(-Bottom.Min.Z*Scale),HingeZ=float((Bottom.Max.Z-Bottom.Min.Z+SeatOffset)*Scale),HingeX=float(Top.Min.X*Scale);
    Body->SetRelativeScale3D(FVector(Scale)); Body->SetRelativeLocation(FVector(0,0,BottomOffset));
    LidPivot->SetRelativeLocation(FVector(HingeX,0,HingeZ));
    Lid->SetRelativeScale3D(FVector(Scale)); Lid->SetRelativeLocation(FVector(-HingeX,0,-Top.Min.Z*Scale));
    FBox Closed=Bottom.TransformBy(FTransform(FQuat::Identity,FVector(0,0,BottomOffset),FVector(Scale)));
    Closed+=Top.TransformBy(FTransform(FQuat::Identity,FVector(0,0,HingeZ-Top.Min.Z*Scale),FVector(Scale)));
    Solid->SetBoxExtent(Closed.GetExtent()); Solid->SetRelativeLocation(Closed.GetCenter());
    BodyRestLocation=Body->GetRelativeLocation(); BodyRestScale=Body->GetRelativeScale3D();
    LidRestLocation=LidPivot->GetRelativeLocation();
    MimicMouth->SetRelativeLocation(FVector(Bottom.GetCenter().X,Bottom.GetCenter().Y,Bottom.Max.Z-5));
    MimicMouth->SetRelativeScale3D(FVector(Bottom.GetSize().X*.78/100,Bottom.GetSize().Y*.78/100,.12));
    MimicLowerTeeth->ClearInstances(); MimicUpperTeeth->ClearInstances();
    for(int32 I=0;I<5;++I) {
        const double Fraction=.18+.16*I,ToothHeight=I%2==0?40.:31.;
        const FVector ToothScale(16./100,16./100,ToothHeight/100);
        MimicLowerTeeth->AddInstance(FTransform(FQuat::Identity,FVector(Bottom.Max.X-20,FMath::Lerp(Bottom.Min.Y,Bottom.Max.Y,Fraction),Bottom.Max.Z+ToothHeight*.25),ToothScale));
        MimicUpperTeeth->AddInstance(FTransform(FRotator(180,0,0),FVector(Top.Max.X-20,FMath::Lerp(Top.Min.Y,Top.Max.Y,Fraction),Top.Min.Z-ToothHeight*.25),ToothScale));
    }
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
    // A conservative envelope includes the lid sweep and any landing yaw.
    const double Radius=FMath::Sqrt(Side*Side+Front*Front);
    const double Height=FMath::Max(180.,(Bottom.GetSize().Z+Top.GetSize().Z)*Scale+50);
    return FVector(Radius,Radius,Height*.5);
}

FTransform AMCRewardChest::GetLockpickContact() const
{
    if(Body->DoesSocketExist(TEXT("Lockpick"))) return Body->GetSocketTransform(TEXT("Lockpick"));
    // Center of the actual ring aperture in SM_Chest_Bot, before ModelScale.
    const FVector Point=Body->GetComponentTransform().TransformPosition(FVector(147.810,0,68.042));
    return FTransform(Body->GetComponentQuat(),Point);
}

bool AMCRewardChest::CanReachLockpick(const AMCToothCharacter* Player) const
{
    if(!IsValid(Player)) return false;
    if(bTeamStageBooster) return FVector::DistSquared(Player->GetActorLocation(),GetActorLocation())<=FMath::Square(FMath::Clamp(OpenRadius,100.f,800.f));
    const FTransform Contact=GetLockpickContact();
    const FVector Delta=Player->GetActorLocation()-Contact.GetLocation();
    const FVector Front=Contact.GetUnitAxis(EAxis::X).GetSafeNormal2D();
    const float Radius=FMath::Clamp(OpenRadius,100.f,260.f);
    // A broad front sector keeps E forgiving while preventing an arm reaching
    // through the chest from behind. No navigation or player teleport is used.
    return Delta.SizeSquared2D()<=FMath::Square(Radius)
        && FVector::DotProduct(Delta.GetSafeNormal2D(),Front)>=.25f
        && FVector::DotProduct(Delta,Front)>=Player->GetCapsuleComponent()->GetScaledCapsuleRadius()+8;
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

void AMCRewardChest::RollMimic()
{
    if(bTeamStageBooster) {bMimic=false;return;}
    MimicSwallowSeconds=FMath::IsFinite(MimicSwallowSeconds)?FMath::Clamp(MimicSwallowSeconds,.1f,5.f):.8f;
    RescueSeconds=FMath::IsFinite(RescueSeconds)?FMath::Clamp(RescueSeconds,.1f,30.f):3.f;
    const float Chance=FMath::IsFinite(MimicChance)?FMath::Clamp(MimicChance,0.f,1.f):.2f;
    // Keep the existing loot stream untouched, including its first polarity draw.
    FRandomStream MimicRoll(RollSeed^0x4D494D49);
    bMimic=MimicTestOverride>=0?MimicTestOverride!=0:RogueKind==EMCRogueRewardKind::Ordinary && MimicRoll.FRand()<Chance;
}

void AMCRewardChest::ForceMimicForTest(bool Enabled)
{
    if(!HasAuthority() || bTeamStageBooster || IsMimicActive()) return;
    MimicTestOverride=Enabled?1:0; bMimic=Enabled; RefreshPresentation(); ForceNetUpdate();
}

bool AMCRewardChest::IsMimicActive() const
{
    return Stage==EMCRewardChestStage::MimicSwallowing || Stage==EMCRewardChestStage::MimicOccupied;
}

bool AMCRewardChest::IsHoldingCaptive() const { return IsValid(CaptivePlayer); }

FVector AMCRewardChest::GetMimicCaptureLocation() const
{
    if(Body->GetStaticMesh()) return Body->GetComponentTransform().TransformPosition(Body->GetStaticMesh()->GetBoundingBox().GetCenter());
    return Solid->Bounds.Origin;
}

float AMCRewardChest::GetMimicSwallowAlpha() const
{
    if(Stage==EMCRewardChestStage::MimicOccupied) return 1.f;
    if(Stage!=EMCRewardChestStage::MimicSwallowing) return 0.f;
    return FMath::Clamp(float((ServerNow()-StageStartedAt)/FMath::Max(.1f,MimicSwallowSeconds)),0.f,1.f);
}

void AMCRewardChest::CaptureOpener()
{
    if(!HasAuthority() || !OpenerCanContinue()) { CancelOpening(); return; }
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(State && (State->bLobbyWaiting || State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost)) { CancelOpening(); return; }
    auto* Player=OpeningPlayer.Get();
    if(IsValid(Player->MimicCaptor)) { CancelOpening(); return; }
    CapturedEntryLocation=Player->GetActorLocation();
    ReleaseOpener(); LootIDs.Reset(); CaptivePlayer=Player; RescuePlayer=nullptr; RescueStartedAt=0;
    SetStage(EMCRewardChestStage::MimicSwallowing);
    Player->BeginMimicCapture(this); ForceNetUpdate();
}

bool AMCRewardChest::CanRescue(const AMCToothCharacter* Player) const
{
    if(Stage!=EMCRewardChestStage::MimicOccupied || !IsValid(CaptivePlayer) || Player==CaptivePlayer
        || !IsLivingPlayer(Player) || !Player->CanWork() || (IsValid(RescuePlayer) && RescuePlayer!=Player)) return false;
    const FVector Closest=Solid->Bounds.GetBox().GetClosestPointTo(Player->GetActorLocation());
    if(FVector::DistSquared(Closest,Player->GetActorLocation())>FMath::Square(260.f)) return false;
    if(const auto* PC=Cast<AMCPlayerController>(Player->GetController())) if(PC->IsMoveInputIgnored()) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MimicRescue),true,this);
    Query.AddIgnoredActor(Player); Query.AddIgnoredActor(CaptivePlayer);
    FHitResult Block;
    return !GetWorld()->LineTraceSingleByChannel(Block,Player->GetActorLocation()+FVector(0,0,20),GetMimicCaptureLocation(),ECC_Visibility,Query);
}

bool AMCRewardChest::BeginRescue(AMCToothCharacter* Player)
{
    if(!HasAuthority() || !CanRescue(Player)) return false;
    if(RescuePlayer==Player) return true;
    RescuePlayer=Player; RescueStartedAt=ServerNow(); ForceNetUpdate(); return true;
}

void AMCRewardChest::EndRescue(AMCToothCharacter* Player)
{
    if(!HasAuthority() || RescuePlayer!=Player) return;
    RescuePlayer=nullptr; RescueStartedAt=0; ForceNetUpdate();
}

float AMCRewardChest::RescueProgress() const
{
    if(Stage!=EMCRewardChestStage::MimicOccupied || !IsValid(RescuePlayer)) return 0.f;
    const auto* Perks=UMCPerkComponent::ForActor(RescuePlayer);
    return FMath::Clamp(float((ServerNow()-RescueStartedAt)*(Perks?Perks->RogueValue(EMCRogueStat::RescueRate):1.f)/FMath::Max(.1f,RescueSeconds)),0.f,1.f);
}

FVector AMCRewardChest::FindMimicReleaseLocation(const AMCToothCharacter* Player) const
{
    if(!IsValid(Player)) return CapturedEntryLocation;
    const auto* Capsule=Player->GetCapsuleComponent();
    const float Radius=Capsule->GetScaledCapsuleRadius(),Height=Capsule->GetScaledCapsuleHalfHeight();
    const FVector Center=Solid->Bounds.Origin,Forward=GetActorForwardVector().GetSafeNormal2D(),Side=GetActorRightVector().GetSafeNormal2D();
    const float Distance=float(Solid->Bounds.BoxExtent.Size2D())+Radius+35;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MimicRelease),true,this); Query.AddIgnoredActor(Player);
    for(const FVector Direction:{Forward,Side,-Side,-Forward}) {
        const FVector Probe=Center+Direction*Distance;
        FHitResult Floor;
        bool HasTongue=false,HasFloor=false;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
            HasTongue=true;
            if(It->SurfacePoint(Probe,Floor) && Floor.ImpactNormal.Z>=.6) { HasFloor=true; break; }
        }
        // In a mouth, the tongue is the floor. A generic downward ray from
        // above the chest could instead land the released player on the roof.
        if(HasTongue && !HasFloor) continue;
        if(!HasFloor && (!GetWorld()->LineTraceSingleByChannel(Floor,Probe+FVector(0,0,300),Probe-FVector(0,0,900),ECC_Visibility,Query)
            || Floor.ImpactNormal.Z<.6)) continue;
        const FVector Position=Floor.ImpactPoint+FVector(0,0,Height+3);
        if(!GetWorld()->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Height),Query)) return Position;
    }
    return CapturedEntryLocation;
}

void AMCRewardChest::ReleaseCaptive(bool FindClearFloor)
{
    if(!HasAuthority()) return;
    auto* Player=CaptivePlayer.Get();
    CaptivePlayer=nullptr; RescuePlayer=nullptr; RescueStartedAt=0;
    if(IsValid(Player) && Player->MimicCaptor==this)
        Player->EndMimicCapture(FindClearFloor?FindMimicReleaseLocation(Player):CapturedEntryLocation);
    ForceNetUpdate();
}

void AMCRewardChest::UpdateMimicPresentation(float Age)
{
    // Only the visuals wobble; the fixed collision remains a reliable rescue target.
    const float Roll=FMath::Sin(Age*7.1f)*3.5f,Pitch=FMath::Sin(Age*5.3f)*2.5f;
    const float Puff=FMath::Sin(Age*4.3f)*.045f;
    Body->SetRelativeLocation(BodyRestLocation+FVector(FMath::Sin(Age*6.2f)*3,0,FMath::Abs(FMath::Sin(Age*4.3f))*5));
    Body->SetRelativeRotation(FRotator(Pitch,0,Roll));
    Body->SetRelativeScale3D(BodyRestScale*FVector(1+Puff,1+Puff,1-Puff*.6f));
    LidPivot->SetRelativeLocation(LidRestLocation+FVector(FMath::Sin(Age*6.2f)*3,0,FMath::Abs(FMath::Sin(Age*4.3f))*5));
    const float Swallow=GetMimicSwallowAlpha();
    const float Angle=Stage==EMCRewardChestStage::MimicSwallowing?FMath::Lerp(105.f,16.f,Swallow*Swallow*(3-2*Swallow)):
        16.f+9.f*FMath::Sin(Age*3.6f)+RescueProgress()*50.f;
    LidPivot->SetRelativeRotation(FRotator(Angle+Pitch,0,Roll));
}

void AMCRewardChest::InitializeReward(FVector Landing,FVector Start,int32 Seed,UDataTable* Table,
    EMCRewardSelectionPolicy Policy,AMCRewardDropZone* Zone)
{
    if(!HasAuthority()) return;
    ReleaseCaptive(); ReleaseOpener(); LootIDs.Reset(); ClaimedMask=0; bReported=false; bClaimInProgress=false;
    LandingPoint=Landing; FallStart=Start; RollSeed=Seed; RewardTable=Table; SelectionPolicy=Policy; DropZone=Zone;
    RollMimic();
    bRewardInitialized=true; Stage=EMCRewardChestStage::Telegraph; StageStartedAt=ServerNow();
    LandingPresentationStartedAt=-1.; LastPresentationStage=EMCRewardChestStage::Telegraph;
    ResetPresentationDeformation();
}

void AMCRewardChest::BeginPlay()
{
    Super::BeginPlay(); ConfigureGeometry();
    if(SurfaceMaterialOverride) for(auto* Part:{Body.Get(),Lid.Get()}) Part->SetMaterial(0,SurfaceMaterialOverride);
    ClaimRingMaterial=ClaimRing->CreateDynamicMaterialInstance(0);
    ClaimSparkMaterial=ClaimSparks->CreateDynamicMaterialInstance(0);
    // Fixed small burst: twelve planes, one shared material, no physics or tick-spawned actors.
    ClaimSparks->ClearInstances();
    for(int32 I=0;I<12;++I) ClaimSparks->AddInstance(FTransform(FVector::ZeroVector));
    CacheRewardEffectsVisibility();
    if(HasAuthority() && bPlacedReward && !bRewardInitialized) {
        RewardTable=PerkTable.LoadSynchronous(); LandingPoint=FallStart=GetActorLocation();
        const auto* State=GetWorld()->GetGameState<AMCGameState>();
        RollSeed=int32(FCrc::StrCrc32(*GetName()))^(State?State->RunSeed:0);
        RollMimic();
        DropZone=PlacedDropZone;
        if(!DropZone) for(TActorIterator<AMCRewardDropZone> It(GetWorld());It;++It)
            if(It->ContainsFootprint(LandingPoint,GetPlacementHalfExtent()*GetActorScale3D().GetAbs())) { DropZone=*It; break; }
        bRewardInitialized=true; Stage=EMCRewardChestStage::Landed; StageStartedAt=ServerNow();
        if(!DropZone) UE_LOG(LogTemp,Warning,TEXT("MC_REWARD_PLACED: %s needs an authored PlacedDropZone"),*GetName());
    }
    // The character cavity uses a masked face texture. Primitive cube UVs need
    // an opaque interior, and explicit ivory teeth must survive BP construction.
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))) {
        if(auto* MID=MimicMouth->CreateDynamicMaterialInstance(0,Material))
            MID->SetVectorParameterValue(TEXT("Color"),FLinearColor(.018f,.002f,.004f));
        for(auto* Teeth:{MimicLowerTeeth.Get(),MimicUpperTeeth.Get()})
            if(auto* MID=Teeth->CreateDynamicMaterialInstance(0,Material)) MID->SetVectorParameterValue(TEXT("Color"),FLinearColor(.91f,.86f,.68f));
    }
    RefreshPresentation();
    if(HasAuthority() && bRewardInitialized) GetWorldTimerManager().SetTimer(ApproachTimer,this,&AMCRewardChest::PollApproach,.25f,true);
}

void AMCRewardChest::CacheRewardEffectsVisibility()
{
    // Blueprint construction has finished; keep each authored switch, including
    // effects that are intentionally off, for a cancelled opening or run reset.
    static const FName Names[]={TEXT("RewardGlowRays"),TEXT("RewardSpotlightBeam"),TEXT("RewardFloorHalo"),
        TEXT("RewardMagicSparks"),TEXT("RewardSeamLight"),TEXT("RewardInnerGlow"),TEXT("RewardOverheadSpot")};
    TInlineComponentArray<USceneComponent*> Components(this);
    for(auto* Component:Components) for(const FName Name:Names)
        if(Component->GetFName()==Name) RewardEffectsVisibility.Add(Component,Component->GetVisibleFlag());
}

void AMCRewardChest::UpdateRewardEffectsVisibility(bool Suppressed)
{
    for(const auto& Effect:RewardEffectsVisibility)
        if(auto* Component=Effect.Key.Get()) Component->SetVisibility(!Suppressed && !bTeamStageBooster && Effect.Value);
}

void AMCRewardChest::RefreshPresentation()
{
    if(Stage==EMCRewardChestStage::Landed && LastPresentationStage==EMCRewardChestStage::Falling)
        LandingPresentationStartedAt=StageStartedAt;
    if(Stage==EMCRewardChestStage::Telegraph) LandingPresentationStartedAt=-1.;
    LastPresentationStage=Stage;
    if(RogueLabel) {
        RogueLabel->SetText(FText::FromString(RogueKind==EMCRogueRewardKind::CursedContract?TEXT("ПРОКЛЯТЫЙ ДОГОВОР"):RogueKind==EMCRogueRewardKind::RiskyAltar?TEXT("СИЛА С ЦЕНОЙ"):TEXT("")));
        RogueLabel->SetTextRenderColor(RogueKind==EMCRogueRewardKind::CursedContract?FColor(255,80,120):FColor(255,175,60));
        RogueLabel->SetVisibility(Stage!=EMCRewardChestStage::Exhausted);
    }
    SetActorHiddenInGame(!bRewardInitialized);
    const bool Exhausted=Stage==EMCRewardChestStage::Exhausted;
    const bool Landed=bRewardInitialized && Stage>=EMCRewardChestStage::Landed && !Exhausted;
    Solid->SetCollisionEnabled(Landed?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    Approach->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // No floor disc/ring/halo: the accelerating chest and brief airborne
    // touchdown sparks provide the arrival cue without painting the arena.
    Telegraph->SetVisibility(false);Telegraph->SetHiddenInGame(true);
    const bool Revealed=bRewardInitialized && bMimic && (Stage==EMCRewardChestStage::Opening || IsMimicActive());
    UpdateRewardEffectsVisibility(Revealed || Exhausted);
    MimicMouth->SetVisibility(Revealed); MimicLowerTeeth->SetVisibility(Revealed); MimicUpperTeeth->SetVisibility(Revealed);
    const bool LandingBounce=LandingPresentationStartedAt>=0 && ServerNow()-LandingPresentationStartedAt<.65;
    const bool Animated=bRewardInitialized && (Stage==EMCRewardChestStage::Telegraph || Stage==EMCRewardChestStage::Falling || Stage==EMCRewardChestStage::Lockpicking || Stage==EMCRewardChestStage::Opening || Exhausted || LandingBounce || IsMimicActive());
    SetActorTickEnabled(Animated);
    ResetPresentationDeformation();
    if(!IsMimicActive()) {
        Body->SetRelativeLocation(BodyRestLocation); Body->SetRelativeRotation(FRotator::ZeroRotator); Body->SetRelativeScale3D(BodyRestScale);
        LidPivot->SetRelativeLocation(LidRestLocation);
    }
    if(bRewardInitialized && Stage==EMCRewardChestStage::Landed) SetActorLocation(LandingPoint);
    if(bRewardInitialized && Stage==EMCRewardChestStage::Falling) {
        const float Alpha=FMath::Clamp(float((ServerNow()-StageStartedAt)/FMath::Max(.1f,FallSeconds)),0.f,1.f);
        // FinishSpawning applies its input transform after deferred initialization.
        // Restore the airborne pose before first presentation, without a floor blink.
        SetActorLocation(FMath::Lerp(FallStart,LandingPoint,Alpha*Alpha));
    }
    if(bRewardInitialized && !Animated) {
        SetActorLocation(LandingPoint);
        LidPivot->SetRelativeRotation(FRotator(Stage>=EMCRewardChestStage::Open?105.f:0.f,0,0));
    }
    if(Exhausted) {
        SetActorLocation(LandingPoint);
        UpdateClaimPresentation(float(FMath::Max(0.,ServerNow()-StageStartedAt)));
    }
}

void AMCRewardChest::ResetPresentationDeformation()
{
    PresentationPivot->SetRelativeTransform(FTransform::Identity);
    ClaimRing->SetVisibility(false); ClaimSparks->SetVisibility(false);
}

void AMCRewardChest::UpdateArrivalPresentation(float Age)
{
    // An underdamped spring at a bottom-centred visual pivot. XY compensates Z
    // so the authored silhouette retains its volume; Solid never deforms.
    const float T=FMath::Clamp(Age,0.f,.65f);
    const float Z=Age<.65f?1.f-.34f*FMath::Exp(-7.f*T)*FMath::Cos(23.f*T):1.f;
    const float XY=1.f/FMath::Sqrt(FMath::Max(.5f,Z));
    PresentationPivot->SetRelativeScale3D(FVector(XY,XY,Z));
    PresentationPivot->SetRelativeLocation(FVector(0,0,FMath::Max(0.f,Z-1.f)*25.f));
    const float Burst=FMath::Clamp(Age/.42f,0.f,1.f);
    const float Fade=FMath::Square(1.f-Burst)*FMath::Clamp(Age/.025f,0.f,1.f);
    const float Width=FMath::Max(50.f,float(Solid->GetUnscaledBoxExtent().GetMax())*1.3f);
    UpdateSparkPresentation(Burst,Fade*.7f,Width,true);
}

void AMCRewardChest::UpdateClaimPresentation(float Age)
{
    constexpr float Duration=.78f;
    const auto Smooth=[](float Value) {const float T=FMath::Clamp(Value,0.f,1.f);return T*T*(3.f-2.f*T);};
    if(Age>=Duration) {
        // Placed chests remain rearmable by ResetPlacedReward, but never leave a
        // visible or collidable exhausted shell. Spawned rewards are destroyed.
        SetActorHiddenInGame(true); SetActorTickEnabled(false);
        ClaimRing->SetVisibility(false); ClaimSparks->SetVisibility(false);
        return;
    }
    const float Anticipation=Smooth(Age/.10f),Stretch=Smooth((Age-.10f)/.12f);
    const float Z=FMath::Lerp(FMath::Lerp(1.f,.76f,Anticipation),1.2f,Stretch);
    const float Collapse=1.f-Smooth((Age-.20f)/.18f);
    const float XY=1.f/FMath::Sqrt(Z);
    PresentationPivot->SetRelativeScale3D(FVector(XY,XY,Z)*FMath::Max(.001f,Collapse));
    PresentationPivot->SetRelativeLocation(FVector(0,0,Smooth((Age-.14f)/.24f)*44.f));
    const float Burst=FMath::Clamp((Age-.26f)/.52f,0.f,1.f);
    const float Fade=FMath::Square(1.f-Burst)*Smooth((Age-.26f)/.035f);
    const bool Visible=Age>=.26f && Burst<1.f;
    ClaimRing->SetVisibility(false); ClaimSparks->SetVisibility(Visible);
    const float Width=FMath::Max(50.f,float(Solid->GetUnscaledBoxExtent().GetMax())*1.5f);
    UpdateSparkPresentation(Burst,Visible?Fade:0.f,Width,false);
}

void AMCRewardChest::UpdateSparkPresentation(float Progress,float Opacity,float Width,bool bTouchdown)
{
    const float Burst=FMath::Clamp(Progress,0.f,1.f);
    ClaimSparks->SetVisibility(Opacity>.001f && Burst<1.f);
    if(ClaimSparkMaterial) ClaimSparkMaterial->SetScalarParameterValue(TEXT("BurstOpacity"),Opacity);
    FQuat Facing=FQuat::Identity;
    if(const auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager)
        Facing=FRotationMatrix::MakeFromZ(PC->PlayerCameraManager->GetCameraRotation().Vector()*-1.f).ToQuat();
    // World-facing planes share one transform update / render-state update.
    const FQuat LocalFacing=GetActorQuat().Inverse()*Facing;
    for(int32 I=0;I<12;++I) {
        const float Angle=I*UE_TWO_PI/12.f+.21f;
        // Width is 1.3 times the largest half-extent on touchdown. Its 1.4
        // multiplier clears even the diagonal of the maximally squashed body
        // (sqrt(2)/1.3/sqrt(.66) < 1.34), rather than hiding bright sparks
        // inside opaque geometry until their opacity is almost zero.
        const float Radial=Width*((bTouchdown?1.4f:.6f)+Burst*(.30f+.08f*(I%3)));
        const float Lift=bTouchdown?14.f+Width*(Burst*(.48f+.10f*(I%4))-.32f*Burst*Burst)
            :35.f+Width*(Burst*(.65f+.12f*(I%4))-.38f*Burst*Burst);
        const float Size=(.14f+.03f*(I%3))*(1.f-.45f*Burst);
        ClaimSparks->UpdateInstanceTransform(I,FTransform(LocalFacing,
            FVector(FMath::Cos(Angle)*Radial,FMath::Sin(Angle)*Radial,Lift),FVector(Size)),false,false,true);
    }
    ClaimSparks->MarkRenderStateDirty();
}

void AMCRewardChest::SetStage(EMCRewardChestStage Next)
{
    if(!HasAuthority()) return;
    Stage=Next; StageStartedAt=ServerNow(); RefreshPresentation(); ForceNetUpdate();
    if(RogueLabel) {
        RogueLabel->SetText(FText::FromString(RogueKind==EMCRogueRewardKind::CursedContract?TEXT("ПРОКЛЯТЫЙ ДОГОВОР"):RogueKind==EMCRogueRewardKind::RiskyAltar?TEXT("СИЛА С ЦЕНОЙ"):TEXT("")));
        RogueLabel->SetTextRenderColor(RogueKind==EMCRogueRewardKind::CursedContract?FColor(255,80,120):FColor(255,175,60));
    }
}

void AMCRewardChest::Tick(float Dt)
{
    Super::Tick(Dt); if(!bRewardInitialized) return;
    const double Age=FMath::Max(0.,ServerNow()-StageStartedAt);
    if(Stage==EMCRewardChestStage::Exhausted) {UpdateClaimPresentation(float(Age));return;}
    if(LandingPresentationStartedAt>=0) UpdateArrivalPresentation(float(ServerNow()-LandingPresentationStartedAt));
    if(IsMimicActive()) {
        if(HasAuthority()) {
            const auto* State=GetWorld()->GetGameState<AMCGameState>();
            if(State && (State->bLobbyWaiting || State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost)) { FinishReward(false); return; }
            if(!IsLivingPlayer(CaptivePlayer) || CaptivePlayer->MimicCaptor!=this) {
                ReleaseCaptive(); FinishReward(false); return;
            }
            if(Stage==EMCRewardChestStage::MimicSwallowing && Age>=FMath::Clamp(MimicSwallowSeconds,.1f,5.f))
                SetStage(EMCRewardChestStage::MimicOccupied);
            if(IsValid(RescuePlayer)) {
                if(!CanRescue(RescuePlayer)) EndRescue(RescuePlayer);
                else if(RescueProgress()>=1.f) { ReleaseCaptive(); FinishReward(false); return; }
            }
        }
        UpdateMimicPresentation(float(FMath::Max(0.,ServerNow()-StageStartedAt))); return;
    }
    if(HasAuthority() && Stage==EMCRewardChestStage::Lockpicking && IsValid(OpeningPlayer)) {
        const FVector Facing=(GetLockpickContact().GetLocation()-OpeningPlayer->GetActorLocation()).GetSafeNormal2D();
        if(!Facing.IsNearlyZero()) OpeningPlayer->SetActorRotation(FMath::RInterpTo(
            OpeningPlayer->GetActorRotation(),FRotator(0,Facing.Rotation().Yaw,0),Dt,8.f));
    }
    if(Stage==EMCRewardChestStage::Telegraph) {
        SetActorLocation(FallStart);
        if(HasAuthority() && Age>=FMath::Max(.1f,TelegraphSeconds)) SetStage(EMCRewardChestStage::Falling);
    } else if(Stage==EMCRewardChestStage::Falling) {
        const float Alpha=FMath::Clamp(float(Age/FMath::Max(.1f,FallSeconds)),0.f,1.f);
        SetActorLocation(FMath::Lerp(FallStart,LandingPoint,Alpha*Alpha));
        const float Z=1.f+.16f*Alpha*Alpha;
        PresentationPivot->SetRelativeScale3D(FVector(1.f/FMath::Sqrt(Z),1.f/FMath::Sqrt(Z),Z));
    } else if(Stage==EMCRewardChestStage::Opening) {
        const float Alpha=FMath::Clamp(float(Age/FMath::Max(.1f,OpeningSeconds)),0.f,1.f);
        LidPivot->SetRelativeRotation(FRotator(105.f*(Alpha*Alpha*(3-2*Alpha)),0,0));
        if(HasAuthority() && Alpha>=1) {
            if(bMimic) { CaptureOpener(); return; }
            SetStage(EMCRewardChestStage::Open);
            if(IsValid(OpeningPlayer) && OpeningPlayer->Expression) OpeningPlayer->Expression->PlayChestCelebration();
            if(RogueKind!=EMCRogueRewardKind::Ordinary) PublishRogueOffer();
            else if(IsValid(OpeningPlayer)) if(auto* PC=Cast<AMCPlayerController>(OpeningPlayer->GetController()))
                PC->ClientShowPerkChoices(this,LootIDs,Polarity,bTeamStageBooster);
        }
    }
    if((Stage==EMCRewardChestStage::Landed || Stage==EMCRewardChestStage::Open)
        && LandingPresentationStartedAt>=0 && ServerNow()-LandingPresentationStartedAt>=.65) {
        PresentationPivot->SetRelativeTransform(FTransform::Identity);
        SetActorTickEnabled(false);
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
    if(bTeamStageBooster && Stage==EMCRewardChestStage::Landed) {PollTeamBoosterApproach();return;}
    if(Stage==EMCRewardChestStage::Falling) {
        const double Age=ServerNow()-StageStartedAt;
        if(Age>=FMath::Max(.1f,FallSeconds)) {
            if(HasClearLanding()) SetStage(EMCRewardChestStage::Landed);
            else if(Age>FMath::Max(.1f,FallSeconds)+6) FinishReward(true);
        }
        return;
    }
    if(Stage==EMCRewardChestStage::Lockpicking || Stage==EMCRewardChestStage::Opening || Stage==EMCRewardChestStage::Open) {
        if(!OpenerCanContinue()) { CancelOpening(); return; }
        if(Stage==EMCRewardChestStage::Lockpicking && ServerNow()-StageStartedAt>=FMath::Clamp(LockpickingSeconds,.1f,60.f))
            SetStage(EMCRewardChestStage::Opening);
    }
}

EMCPerkRarity AMCRewardChest::RollToolRarity(int32 Seed,float LegendaryPercent,float RarePercent)
{
    FRandomStream Roll(Seed^0x544F4F4C);
    const float Chance=Roll.FRand()*100.f;
    const float Legendary=FMath::IsFinite(LegendaryPercent)?FMath::Clamp(LegendaryPercent,0.f,100.f):.02f;
    const float Rare=FMath::IsFinite(RarePercent)?FMath::Clamp(RarePercent,0.f,100.f-Legendary):1.f;
    return Chance<Legendary?EMCPerkRarity::Legendary:Chance<Legendary+Rare?EMCPerkRarity::Rare:EMCPerkRarity::Standard;
}
bool AMCRewardChest::BeginLockpicking(AMCToothCharacter* Player)
{
    if(bTeamStageBooster) return BeginTeamBoosterChoice(Player);
    if(!HasAuthority() || Stage!=EMCRewardChestStage::Landed || !IsLivingPlayer(Player)
        || !Player->CanWork() || !CanReachLockpick(Player) || !IsValid(DropZone) || !Body->GetStaticMesh() || !Lid->GetStaticMesh()
        || FVector::DistSquared(Player->GetActorLocation(),GetActorLocation())>FMath::Square(FMath::Clamp(OpenRadius,100.f,800.f))) return false;
    if(const auto* Director=AMCGameDirector::Find(GetWorld()); Director && Director->IsManagingEvents()
        && !Director->CanStartRewardInteraction()) return false;
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    auto* PC=Cast<AMCPlayerController>(Player->GetController());
    if(!PC || (!bMimic && (!PS->Perks || PS->Perks->GetPerkTable()!=RewardTable))) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RewardApproach),true,this); Query.AddIgnoredActor(Player);
    FHitResult Block;
    const FVector Eye=Player->GetActorLocation()+FVector(0,0,35),Center=Solid->Bounds.Origin;
    if(GetWorld()->LineTraceSingleByChannel(Block,Eye,Center,ECC_Visibility,Query)) return false;
    LootIDs.Reset();
    if(!bMimic && RogueKind!=EMCRogueRewardKind::Ordinary) {
        if(!PrepareRogueLoot(PS->Perks)) return false;ForceNetUpdate();
    }
    if(!bMimic && RogueKind==EMCRogueRewardKind::Ordinary) {
        TArray<FName> Positive,Negative; EligibleRows(RewardTable,PS->Perks,Positive,Negative);
        if(Positive.Num()<3 && Negative.Num()<3) return false;
        FRandomStream Roll(RollSeed);
        // One independent, deterministic roll per chest. Cancellation cannot reroll rarity.
        FRandomStream ToolRoll(RollSeed^0x544F4F4C);
        ToolRoll.FRand(); // Type and card draws follow the same reserved rarity draw.
        const auto Rarity=RollToolRarity(RollSeed,LegendaryToolChancePercent,RareToolChancePercent);
        TArray<FName> Tools;
        if(Rarity!=EMCPerkRarity::Standard) {
            for(FName ID:RewardTable->GetRowNames()) if(const auto* Row=RewardTable->FindRow<FMCPerkDefinition>(ID,TEXT("Tool reward"),false))
                if(ValidRow(Row) && Row->ToolUpgrade!=EMCToolUpgrade::None && Row->Rarity==Rarity && Row->Polarity==EMCPerkPolarity::Positive
                    && PS->Perks->CanGrantPerk(ID)) Tools.Add(ID);
            Tools.Sort([](FName A,FName B){return A.LexicalLess(B);});
        }
        if(Positive.Num()<3) return false;
        Polarity=EMCPerkPolarity::Positive;
        if(!Tools.IsEmpty() && Positive.Num()>=3) Polarity=EMCPerkPolarity::Positive;
        if(!PS->Perks->BuildRogueOffer(EMCPerkPolarity::Positive,RollSeed,LootIDs,false,false,false,TArray<FName>(),false)) {
            for(FName ID:{FName(TEXT("Fallback_Mouthwash")),FName(TEXT("Fallback_BrushFury")),FName(TEXT("Fallback_Grenade"))})
                if(PS->Perks->CanGrantPerk(ID)) LootIDs.Add(ID);
        }
        if(!Tools.IsEmpty() && LootIDs.Num()==3) {
            // Keep the chest's rare tool roll separate; replace an existing legendary if needed.
            int32 Slot=ToolRoll.RandRange(0,2);
            if(Rarity==EMCPerkRarity::Legendary) for(int32 I=0;I<3;++I)
                if(PS->Perks->FindDefinition(LootIDs[I])->Rarity==EMCPerkRarity::Legendary) Slot=I;
            LootIDs[Slot]=Tools[ToolRoll.RandRange(0,Tools.Num()-1)];
        }
        ForceNetUpdate();
        if(LootIDs.Num()!=3) return false;
        for(FName ID:LootIDs) if(!PS->Perks->CanGrantPerk(ID)) return false;
    }
    OpeningPlayer=Player; Player->RewardInteraction=this;
    Player->GetCharacterMovement()->StopMovementImmediately(); Player->ForceNetUpdate();
    SetStage(EMCRewardChestStage::Lockpicking);
    Player->CancelGameplayInput();
    PC->ClientShowRewardOpening(this,StageStartedAt+FMath::Clamp(LockpickingSeconds,.1f,60.f));
    UE_LOG(LogTemp,Display,TEXT("MC_REWARD_LOCKPICK %s player=%s"),*GetName(),*PS->GetPlayerName());
    return true;
}

bool AMCRewardChest::OpenerCanContinue() const
{
    return IsLivingPlayer(OpeningPlayer) && Cast<AMCPlayerController>(OpeningPlayer->GetController())
        && OpeningPlayer->RewardInteraction==this
        && FVector::DistSquared(OpeningPlayer->GetActorLocation(),GetActorLocation())<=FMath::Square(FMath::Clamp(OpenRadius,100.f,800.f)+100.f);
}

void AMCRewardChest::ReleaseOpener()
{
    if(IsValid(OpeningPlayer)) {
        if(OpeningPlayer->RewardInteraction==this) { OpeningPlayer->RewardInteraction=nullptr; OpeningPlayer->ForceNetUpdate(); }
        if(auto* PC=Cast<AMCPlayerController>(OpeningPlayer->GetController())) PC->ClientClosePerkChoices(this);
    }
    OpeningPlayer=nullptr;
}

void AMCRewardChest::CancelOpening()
{
    ReleaseOpener(); LootIDs.Reset(); SetStage(EMCRewardChestStage::Landed);
}

bool AMCRewardChest::TryClaim(AMCPerkPickup*,AMCToothCharacter*) { return false; }

bool AMCRewardChest::TryChooseCard(AMCToothCharacter* Player,int32 Index)
{
    if(bTeamStageBooster) return TryChooseTeamBooster(Player,Index);
    if(!HasAuthority() || bClaimInProgress || Stage!=EMCRewardChestStage::Open || OpeningPlayer!=Player
        || !OpenerCanContinue() || LootIDs.Num()!=3 || !LootIDs.IsValidIndex(Index) || ClaimedMask!=0) return false;
    auto* PS=Player->GetPlayerState<AMCPlayerState>();
    if(!PS->Perks || PS->Perks->GetPerkTable()!=RewardTable) return false;
    const FName ChosenID=LootIDs[Index];
    const auto* Row=RewardTable->FindRow<FMCPerkDefinition>(ChosenID,TEXT("Reward card"),false);
    if(!ValidRow(Row) || Row->Polarity!=Polarity) return false;
    if(RogueKind==EMCRogueRewardKind::CursedContract) {
        if(ContractStep==0) {ContractCurse=ChosenID;ContractStep=1;LootIDs=ContractRewards;Polarity=EMCPerkPolarity::Positive;}
        else if(ContractStep==1) {ContractReward=ChosenID;ContractStep=2;}
        else return false;
        ForceNetUpdate();PublishRogueOffer();return true;
    }
    TGuardValue<bool> Guard(bClaimInProgress,true);
    if(!PS->Perks->ServerGrantReward(ChosenID)) return false;
    ClaimedMask=7;
    PS->RecordChestRewardChoice();
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_REWARD_CARD player=%s perk=%s"),*PS->GetPlayerName(),*ChosenID.ToString());
    FinishReward(false);
    return true;
}

void AMCRewardChest::FinishReward(bool bRequeue)
{
    if(!HasAuthority() || bReported) return;
    bReported=true; GetWorldTimerManager().ClearTimer(ApproachTimer);
    if(auto* Director=Cast<AMCRoguelikeDirector>(GetOwner())) Director->NotifyChestFinished(this,bRequeue);
    if(bTeamStageBooster) if(auto* Director=Cast<AMCSingleDayDirector>(GetOwner())) Director->NotifyStageRewardFinished(this,bRequeue);
    ReleaseCaptive(); ReleaseOpener();
    SetStage(EMCRewardChestStage::Exhausted);
    // Keep the actor for the short claim burst, then remove spawned chests.
    // Authored placed rewards stay hidden and can be reset on a future run.
    if(!bPlacedReward) SetLifeSpan(bRequeue?.1f:.85f);
}

void AMCRewardChest::ResetPlacedReward()
{
    if(!HasAuthority() || !bPlacedReward) return;
    const FVector AuthoredLanding=bRewardInitialized?LandingPoint:GetActorLocation();
    GetWorldTimerManager().ClearTimer(ApproachTimer); SetLifeSpan(0);
    ReleaseCaptive(); ReleaseOpener(); LootIDs.Reset(); ClaimedMask=0; bReported=false; bClaimInProgress=false;
    ContractRewards.Reset();ContractCurses.Reset();ContractCurse=ContractReward=NAME_None;ContractStep=0;
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
        ReleaseCaptive(Reason==EEndPlayReason::Destroyed); ReleaseOpener();
        if(!bReported && Reason==EEndPlayReason::Destroyed)
            if(auto* Director=Cast<AMCRoguelikeDirector>(GetOwner())) Director->NotifyChestFinished(this,!bMimic && ClaimedMask==0);
        if(bTeamStageBooster && !bReported && Reason==EEndPlayReason::Destroyed)
            if(auto* Director=Cast<AMCSingleDayDirector>(GetOwner())) Director->NotifyStageRewardFinished(this,ClaimedMask==0);
    }
    Super::EndPlay(Reason);
}

void AMCRewardChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCRewardChest,SelectionPolicy); DOREPLIFETIME(AMCRewardChest,Stage); DOREPLIFETIME(AMCRewardChest,StageStartedAt);
    DOREPLIFETIME(AMCRewardChest,LandingPoint); DOREPLIFETIME(AMCRewardChest,FallStart); DOREPLIFETIME(AMCRewardChest,LootIDs);
    DOREPLIFETIME(AMCRewardChest,Polarity); DOREPLIFETIME(AMCRewardChest,ClaimedMask); DOREPLIFETIME(AMCRewardChest,bRewardInitialized);
    DOREPLIFETIME(AMCRewardChest,RogueKind);DOREPLIFETIME(AMCRewardChest,ContractRewards);DOREPLIFETIME(AMCRewardChest,ContractCurses);
    DOREPLIFETIME(AMCRewardChest,ContractCurse);DOREPLIFETIME(AMCRewardChest,ContractReward);DOREPLIFETIME(AMCRewardChest,ContractStep);
    DOREPLIFETIME(AMCRewardChest,OpeningPlayer);
    DOREPLIFETIME(AMCRewardChest,bTeamStageBooster);
    DOREPLIFETIME(AMCRewardChest,bMimic); DOREPLIFETIME(AMCRewardChest,CaptivePlayer); DOREPLIFETIME(AMCRewardChest,RescuePlayer);
    DOREPLIFETIME(AMCRewardChest,RescueStartedAt); DOREPLIFETIME(AMCRewardChest,CapturedEntryLocation);
    DOREPLIFETIME(AMCRewardChest,MimicSwallowSeconds); DOREPLIFETIME(AMCRewardChest,RescueSeconds);
}
