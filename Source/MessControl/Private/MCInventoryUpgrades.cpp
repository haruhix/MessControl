#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCBossCharacter.h"
#include "MCFoodActor.h"
#include "MCReactionVFX.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool UMCInventoryComponent::HasUpgrade(EMCToolUpgrade Kind) const
{
    const auto* Pawn=Hero?Hero.Get():Cast<AMCToothCharacter>(GetOwner());
    const auto* PS=Pawn?Pawn->GetPlayerState<AMCPlayerState>():nullptr;
    return PS && PS->Perks && PS->Perks->HasToolUpgrade(Kind);
}
uint8 UMCInventoryComponent::UpgradeMask() const
{
    uint8 Mask=0;for(uint8 I=1;I<=4;++I) if(HasUpgrade(EMCToolUpgrade(I))) Mask|=1<<(I-1);return Mask;
}
float UMCInventoryComponent::CleaningSpeedMultiplier() const {return HasUpgrade(EMCToolUpgrade::MeshaBrush)?2.f:1.f;}
float UMCInventoryComponent::CleaningRadius(float DirtRadius) const
{
    return HasUpgrade(EMCToolUpgrade::MeshaBrush)?FMath::Clamp(DirtRadius,36.f,240.f):36.f;
}
FVector UMCInventoryComponent::BrushContactLocal() const
{
    return HasUpgrade(EMCToolUpgrade::MeshaBrush) && Settings?Settings->MeshaBrushContact:FVector(72,0,-20);
}
float UMCInventoryComponent::SprayReach() const
{
    const float Reach=HasUpgrade(EMCToolUpgrade::Watergun)?(Settings?Settings->WatergunCareReach:1000.f):(Settings?Settings->SprayReach:235.f);
    return FMath::IsFinite(Reach)?FMath::Clamp(Reach,10.f,3000.f):235.f;
}
bool UMCInventoryComponent::IsChainsawRunning() const
{
    return Hero && Selected==EMCToolSlot::Knife && HasUpgrade(EMCToolUpgrade::Chainsaw) && Hero->IsPrimaryHeld()
        && Hero->CanWork() && !Hero->bInCoffee && !Hero->HeldFood && !Hero->ClingTooth && ShouldPresentTool();
}
bool UMCInventoryComponent::IsUsingWatergun() const
{
    const double ShotAge=Now()-(WaterShotReadyAt-4.);
    const bool Recovering=WaterShotReadyAt>0 && ShotAge>=0 && ShotAge<.42;
    return Hero && Selected==EMCToolSlot::Spray && HasUpgrade(EMCToolUpgrade::Watergun) && (Hero->IsPrimaryHeld() || Recovering)
        && Hero->CanWork() && !Hero->bInCoffee && ShouldPresentTool();
}
float UMCInventoryComponent::WaterRecoil() const
{
    if(WaterShotReadyAt<=0) return 0;
    const float Age=Now()-(WaterShotReadyAt-4.);
    if(Age<0 || Age>=.42f) return 0;
    return FMath::SmoothStep(0.f,.045f,Age)*(1-FMath::SmoothStep(.055f,.42f,Age));
}
bool UMCInventoryComponent::IsUsingBuffer() const
{
    if(!Hero || Selected!=EMCToolSlot::Pickaxe || !HasUpgrade(EMCToolUpgrade::Buffer)
        || !Hero->CanWork() || Hero->bInCoffee || !ShouldPresentTool()) return false;
    const float Elapsed=Hero->GetToolSwingElapsed();
    return Hero->IsPrimaryHeld() || (Elapsed>=0 && Elapsed<SwingDuration());
}
EMCToolUpgrade UMCInventoryComponent::SelectedUpgrade() const
{
    const auto Kind=Selected==EMCToolSlot::Brush?EMCToolUpgrade::MeshaBrush:Selected==EMCToolSlot::Pickaxe?EMCToolUpgrade::Buffer:
        Selected==EMCToolSlot::Knife?EMCToolUpgrade::Chainsaw:EMCToolUpgrade::Watergun;
    return HasUpgrade(Kind)?Kind:EMCToolUpgrade::None;
}
bool UMCInventoryComponent::UpgradeIdleGrip(FTransform& RightHandWorld) const
{
    if(!Hero || !Settings) return false;
    FTransform Attachment,Idle;
    switch(SelectedUpgrade()) {
    case EMCToolUpgrade::MeshaBrush:Attachment=Settings->MeshaBrushTransform;Idle=Settings->MeshaBrushIdlePose;break;
    case EMCToolUpgrade::Chainsaw:Attachment=Settings->ChainsawTransform;Idle=Settings->ChainsawIdlePose;break;
    case EMCToolUpgrade::Buffer:Attachment=Settings->BufferTransform;Idle=Settings->BufferIdlePose;break;
    case EMCToolUpgrade::Watergun:Attachment=Settings->WatergunTransform;Idle=Settings->WatergunIdlePose;break;
    default:return false;
    }
    if(SelectedUpgrade()==EMCToolUpgrade::Buffer || SelectedUpgrade()==EMCToolUpgrade::Watergun) Attachment=UpgradeAttachment();
    const FTransform InHand=Attachment*Hero->BrushPivot->GetRelativeTransform();
    Idle.SetScale3D(InHand.GetScale3D());
    // Solve the wrist from the authored tool pose, retaining the artist's
    // attachment offset and the character rig's brush-pivot correction.
    RightHandWorld=InHand.Inverse()*Idle*Hero->GetActorTransform();
    return !RightHandWorld.ContainsNaN();
}
FTransform UMCInventoryComponent::UpgradeAttachment() const
{
    if(!Settings) return FTransform::Identity;
    const bool Buffer=SelectedUpgrade()==EMCToolUpgrade::Buffer;
    const FTransform Idle=Buffer?Settings->BufferTransform:Settings->WatergunTransform;
    if(!Settings->bUseWorkingToolPoses) return Idle;
    FTransform Result;Result.Blend(Idle,Buffer?Settings->BufferWorkTransform:Settings->WatergunWorkTransform,WorkingGripAlpha);
    return Result;
}
bool UMCInventoryComponent::UpgradeWorkToolPose(FTransform& ToolWorld) const
{
    if(!Hero || !Settings) return false;
    const auto Kind=SelectedUpgrade();
    if(Kind!=EMCToolUpgrade::Buffer && Kind!=EMCToolUpgrade::Watergun) return false;
    FTransform Pose=Kind==EMCToolUpgrade::Buffer
        ?(Settings->bUseWorkingToolPoses?Settings->BufferWorkPose:Settings->BufferIdlePose)
        :(Settings->bUseWorkingToolPoses?Settings->WatergunWorkPose:Settings->WatergunIdlePose);
    Pose.SetScale3D(UpgradeAttachment().GetScale3D()*Hero->BrushPivot->GetRelativeScale3D());
    ToolWorld=Pose*Hero->GetActorTransform();
    return !ToolWorld.ContainsNaN();
}
bool UMCInventoryComponent::WatergunHandGoal(FTransform& RightHandWorld) const
{
    FTransform ToolWorld;
    if(!Tool || !UpgradeWorkToolPose(ToolWorld)) return false;
    const FTransform InHand=Tool->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
    const FVector Wrist=(InHand.Inverse()*ToolWorld).GetLocation();
    FQuat Rotation=ToolWorld.GetRotation();
    if(HealingTarget || FireTarget) {
        const FVector Aim=(SprayAim()-Wrist).GetSafeNormal();
        Rotation=(FQuat::FindBetweenNormals(ToolWorld.GetUnitAxis(EAxis::X),Aim)*Rotation).GetNormalized();
    }
    const float Recoil=WaterRecoil();
    Rotation=(Rotation*FRotator(6*Recoil,0,0).Quaternion()).GetNormalized();
    ToolWorld.SetRotation(Rotation);
    const FVector LocalGrip=InHand.InverseTransformPosition(FVector::ZeroVector);
    ToolWorld.SetLocation(Wrist-Rotation.RotateVector(LocalGrip*ToolWorld.GetScale3D())-Hero->GetActorForwardVector()*(6*Recoil));
    RightHandWorld=InHand.Inverse()*ToolWorld;
    return !RightHandWorld.ContainsNaN();
}
bool UMCInventoryComponent::UpgradeSupportGrip(const FTransform& RightHandWorld,FTransform& LeftHandWorld) const
{
    if(!Hero || !Settings) return false;
    FTransform Attachment;FVector Point;FRotator Rotation;const UStaticMesh* Mesh=nullptr;
    switch(SelectedUpgrade()) {
    case EMCToolUpgrade::MeshaBrush:Attachment=Settings->MeshaBrushTransform;Point=Settings->MeshaBrushSupportGrip;Rotation=Settings->MeshaBrushSupportRotation;Mesh=Settings->MeshaBrushMesh.Get();break;
    case EMCToolUpgrade::Chainsaw:Attachment=Settings->ChainsawTransform;Point=Settings->ChainsawSupportGrip;Rotation=Settings->ChainsawSupportRotation;Mesh=Settings->ChainsawMesh.Get();break;
    case EMCToolUpgrade::Buffer:Attachment=Settings->BufferTransform;Point=Settings->BufferSupportGrip;Rotation=Settings->BufferSupportRotation;Mesh=Settings->BufferMesh.Get();break;
    case EMCToolUpgrade::Watergun:Attachment=Settings->WatergunTransform;Point=Settings->WatergunSupportGrip;Rotation=Settings->WatergunSupportRotation;Mesh=Settings->WatergunMesh.Get();break;
    default:return false;
    }
    if(Settings->bUseWorkingToolPoses && (SelectedUpgrade()==EMCToolUpgrade::Buffer || SelectedUpgrade()==EMCToolUpgrade::Watergun)) {
        const bool Buffer=SelectedUpgrade()==EMCToolUpgrade::Buffer;
        Attachment=UpgradeAttachment();
        Point=FMath::Lerp(Point,Buffer?Settings->BufferWorkSupportGrip:Settings->WatergunWorkSupportGrip,WorkingGripAlpha);
        Rotation=FQuat::Slerp(Rotation.Quaternion(),(Buffer?Settings->BufferWorkSupportRotation:Settings->WatergunWorkSupportRotation).Quaternion(),WorkingGripAlpha).Rotator();
    }
    if(Mesh) if(const auto* Socket=Mesh->FindSocket(TEXT("GripLeft"))) {Point=Socket->RelativeLocation;Rotation=Socket->RelativeRotation;}
    const FTransform ToolWorld=Attachment*Hero->BrushPivot->GetRelativeTransform()*RightHandWorld;
    LeftHandWorld=FTransform((ToolWorld.GetRotation()*Rotation.Quaternion()).GetNormalized(),ToolWorld.TransformPosition(Point));
    return !LeftHandWorld.ContainsNaN();
}
FQuat UMCInventoryComponent::ToolHandRotation(const FQuat& ToolWorldRotation) const
{
    if(!Hero) return ToolWorldRotation;
    const auto* Mesh=Selected==EMCToolSlot::Brush?Hero->Brush.Get():Tool.Get();
    const FTransform InHand=(Mesh?Mesh->GetRelativeTransform():FTransform::Identity)*Hero->BrushPivot->GetRelativeTransform();
    return (ToolWorldRotation*InHand.GetRotation().Inverse()).GetNormalized();
}
float UMCInventoryComponent::MovementMultiplier() const
{
    if(Selected==EMCToolSlot::Pickaxe && HasUpgrade(EMCToolUpgrade::Buffer)) return .5f;
    return IsChainsawRunning()?1.4f:1.f;
}
float UMCInventoryComponent::WaterChargeFraction() const
{
    return bChargingWater?FMath::Clamp(float(Now()-WaterChargeStartedAt)/FMath::Max(.1f,Settings?Settings->WatergunChargeSeconds:1.5f),0.f,1.f):0.f;
}
void UMCInventoryComponent::CancelUpgradeUse()
{
    bChargingWater=false;
    if(Hero && bSawMotionActive) Hero->GetCharacterMovement()->RemoveRootMotionSourceByID(SawMotionId);
    bSawMotionActive=false;
}
void UMCInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelUpgradeUse();Super::EndPlay(Reason);
}
void UMCInventoryComponent::TickChainsaw(float Dt)
{
    auto* Movement=Hero->GetCharacterMovement();
    const bool Run=IsChainsawRunning() && (Movement->IsMovingOnGround() || Movement->IsFalling());
    if(GetOwner()->HasAuthority() || Hero->IsLocallyControlled()) {
        if(Run) {
            const FVector Pull=Hero->GetActorForwardVector().GetSafeNormal2D()*260;
            auto Current=bSawMotionActive?Movement->GetRootMotionSourceByID(SawMotionId):nullptr;
            if(Current.IsValid() && !Current->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval))
                static_cast<FMCLocomotionRootMotionSource*>(Current.Get())->Force=Pull;
            else {
                auto Source=MakeShared<FMCLocomotionRootMotionSource>();Source->InstanceName=TEXT("MCChainsawPull");
                Source->Priority=120;Source->AccumulateMode=ERootMotionAccumulateMode::Additive;
                Source->Duration=-1;Source->Force=Pull;Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
                SawMotionId=Movement->ApplyRootMotionSource(Source);bSawMotionActive=true;
            }
        } else if(bSawMotionActive) {Movement->RemoveRootMotionSourceByID(SawMotionId);bSawMotionActive=false;}
    }
    if(!GetOwner()->HasAuthority()) return;
    if(SawStunEndsAt>0 && Now()>=SawStunEndsAt && Hero->Status->IsAlive()) {
        if(Hero->ToothPhysics->GetBodyState()!=EMCBodyState::Ragdoll || Hero->ToothPhysics->TryRecover()) SawStunEndsAt=-100;
    }
    if(Run && Now()>=NextSawContactAt) {NextSawContactAt=Now()+.12;SawContact();}
}
void UMCInventoryComponent::HandleChainsawCollision(AActor* Other,const FHitResult& Hit)
{
    if(!Hero || !Hero->HasAuthority() || !IsChainsawRunning() || Cast<AMCToothCharacter>(Other) || Cast<AMCFoodActor>(Other)
        || FMath::Abs(Hit.ImpactNormal.Z)>.6f || FVector::DotProduct(Hero->GetActorForwardVector(),Hit.ImpactNormal)>-.25f) return;
    if(Hero->GetVelocity().Size2D()<120 || Now()<SawStunEndsAt) return;
    Hero->CancelGameplayInput();CancelUpgradeUse();SawStunEndsAt=Now()+2.5;
    Hero->Status->Damage(30.f,-Hero->GetActorForwardVector());
    const float Fall=FMath::Max(650.f,Hero->ToothPhysics->Settings.FallThreshold+100.f);
    Hero->ToothPhysics->ApplyHit(-Hero->GetActorForwardVector()*Fall+FVector(0,0,130),Hit.ImpactPoint);
    AMCReactionVFX::Spawn(GetWorld(),Hit.ImpactPoint,EMCReactionEffect::Impact,.6f,65);Hero->ForceNetUpdate();
}
void UMCInventoryComponent::SawContact()
{
    const FVector Origin=Hero->GetActorLocation(),Forward=Hero->GetActorForwardVector();
    // Visible blade-side contact, bounded reach, and per-target cadence prevent frame-rate damage.
    AMCFoodActor* Best=nullptr;float Distance=FMath::Square(190.f);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
        if(!CanBreak(*It) || It->FoodData.Kind!=EMCFoodKind::Food || Now()<SawContacts.FindRef(*It)) continue;
        FHitResult Hit;if(!It->FindToolContact(Hero,190,Hit)) continue;
        const float D=FVector::DistSquared(Origin,Hit.ImpactPoint);
        if(D<Distance) {Distance=D;Best=*It;}
    }
    if(Best) {
        // Health is the authored full-food health, rather than remaining health: exactly two clean contacts.
        if(Best->HitFood(FMath::Max(1.f,Best->FoodData.Health*.5f),Forward,Hero)) SawContacts.Add(Best,Now()+.45);
    }
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Other=*It;if(Other==Hero || !Other->Status->IsAlive() || Now()<SawContacts.FindRef(Other)) continue;
        const FVector Point=Other->ToothPhysics->CanAct()?Other->GetActorLocation():Other->ToothPhysics->PhysicalLocation();
        const FVector Offset=Point-Origin;
        if(Offset.SizeSquared2D()>FMath::Square(175.f) || FMath::Abs(Offset.Z)>120 || FVector::DotProduct(Offset.GetSafeNormal2D(),Forward)<.45f) continue;
        FHitResult Block;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCChainsawAlly),false,Hero);Q.AddIgnoredActor(Other);
        if(GetWorld()->LineTraceSingleByChannel(Block,Origin,Point,ECC_Visibility,Q)) continue;
        if(Other->Status->Damage(Damage(),Forward)) {SawContacts.Add(Other,Now()+.65);Other->ToothPhysics->ApplyHit(Forward*220+FVector(0,0,40),Point);}
    }
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        if(!It->CanReceiveWeaponHit() || Now()<SawContacts.FindRef(*It)) continue;
        const FVector Point=It->GetMeleeTargetPoint(Origin),D=Point-Origin;
        if(D.SizeSquared()>FMath::Square(190.f) || FVector::DotProduct((It->GetActorLocation()-Origin).GetSafeNormal2D(),Forward)<.35f) continue;
        FHitResult Block;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCChainsawBoss),false,Hero);Q.AddIgnoredActor(*It);
        if(!GetWorld()->LineTraceSingleByChannel(Block,Origin,Point,ECC_Visibility,Q) && It->ReceiveBossDamage(Damage(),Hero)>0) SawContacts.Add(*It,Now()+.65);
    }
    if(SawContacts.Num()>64) for(auto It=SawContacts.CreateIterator();It;++It) if(!It.Key().IsValid() || It.Value()<Now()-2) It.RemoveCurrent();
}
void UMCInventoryComponent::FireChargedWater(float Charge)
{
    if(!Hero->HasAuthority() || Now()<WaterShotReadyAt) return;
    const FVector Origin=SprayOrigin(),Direction=Hero->GetActorForwardVector().GetSafeNormal();
    const float Reach=FMath::Lerp(600.f,1400.f,Charge),Power=FMath::Lerp(.75f,3.f,Charge)*FMath::Max(1.f,Settings?Settings->PickaxeDamage:40.f);
    FCollisionObjectQueryParams Objects;for(auto Type:{ECC_WorldStatic,ECC_WorldDynamic,ECC_PhysicsBody,ECC_Pawn}) Objects.AddObjectTypesToQuery(Type);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCWatergunShot),true,Hero);FHitResult Hit;
    FVector End=Origin+Direction*Reach;
    if(GetWorld()->SweepSingleByObjectType(Hit,Origin,End,FQuat::Identity,Objects,FCollisionShape::MakeSphere(FMath::Lerp(5.f,12.f,Charge)),Query)) {
        End=Hit.ImpactPoint;
        if(auto* Food=Cast<AMCFoodActor>(Hit.GetActor())) Food->HitFood(Power,Direction,Hero);
        else if(auto* Boss=Cast<AMCBossCharacter>(Hit.GetActor())) Boss->ReceiveBossDamage(Power,Hero);
        else if(auto* Player=Cast<AMCToothCharacter>(Hit.GetActor())) {
            Player->Status->Damage(Power,Direction);Player->ToothPhysics->ApplyHit(Direction*FMath::Lerp(150.f,480.f,Charge)+FVector(0,0,50),End);
        }
        AMCReactionVFX::Spawn(GetWorld(),End,EMCReactionEffect::WaterImpact,.5f,40+Charge*40,Hit.ImpactNormal);
    }
    AMCReactionVFX::Spawn(GetWorld(),Origin,EMCReactionEffect::WaterShot,.5f,12+Charge*12,Direction,FVector::Distance(Origin,End));
    WaterShotReadyAt=Now()+4.;LastSprayAt=Now();Hero->ForceNetUpdate();
}
void UMCInventoryComponent::TickUpgrades(float Dt)
{
    if(!Hero) return;TickChainsaw(Dt);
    if(!Hero->HasAuthority()) return;
    const bool Pressure=Selected==EMCToolSlot::Spray && HasUpgrade(EMCToolUpgrade::Watergun) && bPressureMode
        && Hero->CanWork() && !Hero->bInCoffee && ShouldPresentTool();
    if(Pressure && Hero->IsPrimaryHeld() && Now()>=WaterShotReadyAt) {
        if(!bChargingWater) {bChargingWater=true;WaterChargeStartedAt=Now();Hero->ForceNetUpdate();}
    } else if(bChargingWater) {
        const float Charge=WaterChargeFraction();bChargingWater=false;
        if(Pressure && !Hero->IsPrimaryHeld()) FireChargedWater(Charge);Hero->ForceNetUpdate();
    }
}
