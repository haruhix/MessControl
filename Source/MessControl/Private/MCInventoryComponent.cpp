#include "MCInventoryComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCGripComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCExpressionComponent.h"
#include "MCRewardChest.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "PhysicsEngine/BodyInstance.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

UMCInventoryComponent::UMCInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true);
    Profile=TSoftObjectPtr<UMCEquipmentProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_Equipment.DA_Equipment")));
}
double UMCInventoryComponent::Now() const
{ const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); }
void UMCInventoryComponent::BeginPlay()
{
    Super::BeginPlay(); Hero=Cast<AMCToothCharacter>(GetOwner()); Settings=Profile.LoadSynchronous();
    if(!Settings) Settings=NewObject<UMCEquipmentProfile>(this);
    if(Hero) {OriginalBrushMesh=Hero->Brush->GetStaticMesh();OriginalBrushTransform=Hero->Brush->GetRelativeTransform();}
    // Socket data also belongs to authority-only servers without a visual Tool.
    if(!Settings->SprayMesh.IsNull()) Settings->SprayMesh.LoadSynchronous();
    if(!Hero || GetNetMode()==NM_DedicatedServer) return;
    auto Part=[&](const TCHAR* Name) {
        auto* Mesh=NewObject<UStaticMeshComponent>(Hero,Name); Mesh->SetupAttachment(Hero->BrushPivot);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCanEverAffectNavigation(false);
        Hero->AddInstanceComponent(Mesh); Mesh->RegisterComponent(); return Mesh;
    };
    Tool=Part(TEXT("InventoryTool")); Detail=Part(TEXT("InventoryDetail"));
    if(auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_SprayMist.NS_SprayMist"))) {
        SprayMist=NewObject<UNiagaraComponent>(Hero,TEXT("TreatmentSprayNiagara"));
        SprayMist->SetupAttachment(Hero->GetRootComponent());SprayMist->SetAutoActivate(false);
        SprayMist->SetAsset(System);SprayMist->SetCastShadow(false);Hero->AddInstanceComponent(SprayMist);SprayMist->RegisterComponent();
    }
    if(auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_WatergunMist.NS_WatergunMist"))) {
        WaterMist=NewObject<UNiagaraComponent>(Hero,TEXT("WatergunSprayNiagara"));
        WaterMist->SetupAttachment(Hero->GetRootComponent());WaterMist->SetAutoActivate(false);
        WaterMist->SetAsset(System);WaterMist->SetCastShadow(false);Hero->AddInstanceComponent(WaterMist);WaterMist->RegisterComponent();
    }
    PrimaryComponentTick.AddPrerequisite(Hero,Hero->PrimaryActorTick); RefreshMesh();
}
void UMCInventoryComponent::ServerSelect_Implementation(EMCToolSlot Slot)
{
    if(uint8(Slot)>uint8(EMCToolSlot::Spray) || !Hero || !Hero->CanWork() || !Hero->CanSwitchTool()) return;
    if(Selected==Slot) {
        if(Slot==EMCToolSlot::Spray && HasUpgrade(EMCToolUpgrade::Watergun)) {
            CancelUpgradeUse();Hero->ServerSetPrimary(false);bPressureMode=!bPressureMode;
            HealingTarget=nullptr;FireTarget=nullptr;GetOwner()->ForceNetUpdate();
        }
        return;
    }
    CancelUpgradeUse();
    Hero->FoodCollection->Stop();
    Hero->ServerSetPrimary(false); Hero->ResetContact(); Hero->bSelfCare=false;
    Selected=Slot; GetOwner()->ForceNetUpdate();
}
void UMCInventoryComponent::UnlockWaterJet()
{ if(GetOwner()->HasAuthority()) { bWaterJetUnlocked=true; GetOwner()->ForceNetUpdate(); } }
float UMCInventoryComponent::CooldownSeconds() const { return HasUpgrade(EMCToolUpgrade::Watergun)?4.f:FMath::Max(1.f,Settings?Settings->SprayCooldown:8.f); }
float UMCInventoryComponent::SpraySecondsLeft() const { return FMath::Max(0.f,float((HasUpgrade(EMCToolUpgrade::Watergun) && bPressureMode?WaterShotReadyAt:SprayReadyAt)-Now())); }
bool UMCInventoryComponent::CanBreak(const AMCFoodActor* Food) const
{
    if(IsValid(Food) && Selected==EMCToolSlot::Knife && HasUpgrade(EMCToolUpgrade::Chainsaw)
        && Food->FoodData.Kind==EMCFoodKind::Food && !Food->bBrushTool && !Food->IsDisposed()) return true;
    return IsValid(Food) && !Food->bBrushTool && !Food->IsDisposed()
        && (Selected==EMCToolSlot::Pickaxe?Food->IsHardFood():Selected==EMCToolSlot::Knife && !Food->IsHardFood());
}
float UMCInventoryComponent::Damage() const
{
    if(Selected==EMCToolSlot::Knife && HasUpgrade(EMCToolUpgrade::Chainsaw)) return 2.f*FMath::Max(1.f,Settings?Settings->PickaxeDamage:40.f);
    if(Selected==EMCToolSlot::Pickaxe && HasUpgrade(EMCToolUpgrade::Buffer)) return 75.f;
    return FMath::Max(1.f,Selected==EMCToolSlot::Pickaxe?(Settings?Settings->PickaxeDamage:40.f):(Settings?Settings->KnifeDamage:25.f));
}
float UMCInventoryComponent::SwingPlayRate(EMCToolSlot Slot) { return Slot==EMCToolSlot::Pickaxe?1.05f/.65f:1.f; }
float UMCInventoryComponent::SwingDuration() const { return (Selected==EMCToolSlot::Pickaxe?1.05f:Selected==EMCToolSlot::Knife?.70f:.85f)/SwingPlayRate(Selected); }
float UMCInventoryComponent::SwingContactTime() const { return (Selected==EMCToolSlot::Pickaxe?.38f:Selected==EMCToolSlot::Knife?.28f:.16f)/SwingPlayRate(Selected); }
float UMCInventoryComponent::SwingAngle(EMCToolSlot Slot,float T)
{
    T*=SwingPlayRate(Slot);
    const bool Chop=Slot==EMCToolSlot::Knife;
    const float Wind=Slot==EMCToolSlot::Pickaxe?.30f:Chop?.20f:.11f;
    const float Hit=Slot==EMCToolSlot::Pickaxe?.44f:Chop?.34f:.22f;
    const float End=Slot==EMCToolSlot::Pickaxe?.95f:Chop?.64f:.44f;
    const float Back=(Slot==EMCToolSlot::Pickaxe || Chop)?115.f:-55.f,Front=Slot==EMCToolSlot::Pickaxe?-105.f:Chop?-100.f:65.f;
    if(T<0 || T>=End) return -12;
    if(T<Wind) return FMath::Lerp(-12.f,Back,FMath::SmoothStep(0.f,Wind,T));
    if(T<Hit) return FMath::Lerp(Back,Front,FMath::SmoothStep(Wind,Hit,T));
    return FMath::Lerp(Front,-12.f,FMath::SmoothStep(Hit,End,T));
}
FVector UMCInventoryComponent::SwingOffset(EMCToolSlot Slot,float T)
{
    T*=SwingPlayRate(Slot);
    if(Slot==EMCToolSlot::Knife) {
        if(T<0 || T>=.64f) return FVector::ZeroVector;
        const FVector Wind(-40,10,105),Contact(70,-8,25),Follow(55,-10,-38);
        if(T<.20f) return FMath::Lerp(FVector::ZeroVector,Wind,FMath::SmoothStep(0.f,.20f,T));
        if(T<.28f) return FMath::Lerp(Wind,Contact,FMath::SmoothStep(.20f,.28f,T));
        if(T<.34f) return FMath::Lerp(Contact,Follow,FMath::SmoothStep(.28f,.34f,T));
        return FMath::Lerp(Follow,FVector::ZeroVector,FMath::SmoothStep(.34f,.64f,T));
    }
    if(Slot!=EMCToolSlot::Pickaxe || T<0 || T>=.95f) return FVector::ZeroVector;
    const FVector Wind(-35,12,90),Strike(60,-10,30);
    if(T<.30f) return FMath::Lerp(FVector::ZeroVector,Wind,FMath::SmoothStep(0.f,.30f,T));
    if(T<.44f) return FMath::Lerp(Wind,Strike,FMath::SmoothStep(.30f,.44f,T));
    return FMath::Lerp(Strike,FVector::ZeroVector,FMath::SmoothStep(.44f,.95f,T));
}
bool UMCInventoryComponent::ShouldPresentTool() const
{
    if(!Hero || !Hero->Status->IsAlive() || Hero->HeldFood || Hero->FoodCollection->bCollecting || Hero->OrderJumpTarget || Hero->SwallowedBy || Hero->MimicCaptor) return false;
    if(Hero->RewardInteraction && (Hero->RewardInteraction->Stage==EMCRewardChestStage::Lockpicking
        || Hero->RewardInteraction->Stage==EMCRewardChestStage::Opening)) return false;
    if(Hero->Expression && Hero->Expression->BodyAlpha()>.001f) return false;
    const auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement());
    return (!Move || (!Move->IsSwimming() && !Move->IsClimbing()))
        && Hero->AnimationOrderPress<.05f && Hero->AnimationOrderFlight<.05f
        && Hero->AnimationClimb<.05f && Hero->AnimationSwim<.05f
        && !Hero->Grip->Frame.Food && !Hero->Grip->Secondary.Food && !Hero->Grip->GrabbedPlayer && Hero->Grip->Blend()<.05f;
}
FVector UMCInventoryComponent::ConstrainPickaxeGrip(const FTransform& WristWorld) const
{
    if(!ShouldPresentTool() || !Tool || !Tool->GetStaticMesh() || Selected!=EMCToolSlot::Pickaxe) return FVector::ZeroVector;
    const FTransform World=Tool->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform()*WristWorld;
    const FBoxSphereBounds Bounds=Tool->GetStaticMesh()->GetBounds();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCPickaxePose),false,Hero);
    FCollisionQueryParams EnamelQ(SCENE_QUERY_STAT(MCPickaxeEnamel),true,Hero);
    TArray<AMCArenaTooth*,TInlineAllocator<4>> Nearby;
    if(const auto* GS=Hero->GetWorld()->GetGameState<AMCGameState>())
        for(const auto& Tooth:GS->ArenaTeeth)
            if(Tooth && Tooth->IsAvailable() && Tooth->Visual->Bounds.GetBox().ExpandBy(320).IsInside(Hero->GetActorLocation())) Nearby.Add(Tooth);
    FVector Correction=FVector::ZeroVector;
    // Constrain the entire posed mesh, then move the hand branch with the tool.
    // This does not detach the tool from its wrist or teleport gameplay targets.
    for(int32 Pass=0;Pass<4;++Pass) {
        const FVector Previous=Correction;
        // Use the collision-free body as the anchor: the unconstrained wrist
        // may already be beyond the wall during the forward part of a swing.
        for(int32 I=0;I<8;++I) {
            const FVector LocalCorner=Bounds.Origin+FVector(I&1?Bounds.BoxExtent.X:-Bounds.BoxExtent.X,I&2?Bounds.BoxExtent.Y:-Bounds.BoxExtent.Y,I&4?Bounds.BoxExtent.Z:-Bounds.BoxExtent.Z);
            const FVector Corner=World.TransformPosition(LocalCorner)+Correction;
            FHitResult Hit;
            if(Hero->GetWorld()->SweepSingleByChannel(Hit,Hero->GetActorLocation(),Corner,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(4),Q) && !Hit.bStartPenetrating && FMath::Abs(Hit.ImpactNormal.Z)<.7f)
                Correction+=Hit.ImpactNormal*FMath::Max(0.,5.-FVector::DotProduct(Corner-Hit.ImpactPoint,Hit.ImpactNormal));
            // The gameplay box is smaller than the curved enamel. Query the
            // same visible surface used for cleaning and traversal contacts.
            for(const auto* Tooth:Nearby) {
                const FVector P=World.TransformPosition(LocalCorner)+Correction;
                const FVector Direction=(P-Hero->GetActorLocation()).GetSafeNormal();
                if(Tooth->BrushSurface->LineTraceComponent(Hit,Hero->GetActorLocation(),P+Direction*60,EnamelQ)
                    && !Hit.bStartPenetrating && FVector::DotProduct(Hit.ImpactNormal,Direction)<-.1f)
                    Correction+=Hit.ImpactNormal*FMath::Max(0.,5.-FVector::DotProduct(P-Hit.ImpactPoint,Hit.ImpactNormal));
            }
        }
        for(int32 I=0;I<8;++I) {
            const FVector Corner=World.TransformPosition(Bounds.Origin+FVector(I&1?Bounds.BoxExtent.X:-Bounds.BoxExtent.X,I&2?Bounds.BoxExtent.Y:-Bounds.BoxExtent.Y,I&4?Bounds.BoxExtent.Z:-Bounds.BoxExtent.Z))+Correction;
            FHitResult Hit;
            if(Hero->GetWorld()->LineTraceSingleByChannel(Hit,Corner+FVector(0,0,300),Corner,ECC_Visibility,Q) && Hit.ImpactNormal.Z>.4f)
                Correction+=Hit.ImpactNormal*FMath::Max(0.,5.-FVector::DotProduct(Corner-Hit.ImpactPoint,Hit.ImpactNormal));
        }
        // A complete unchanged pass has already checked every corner against
        // the same scene. Repeating it would issue identical collision queries.
        if(Correction.Equals(Previous,.0001f)) break;
    }
    return Correction.GetClampedToMaxSize(260);
}
bool UMCInventoryComponent::CalculusHandGoal(FTransform& HandWorld,float& Blend) const
{
    Blend=0;
    FVector Point,Normal;
    if(!ShouldPresentTool() || !Tool || !Tool->GetStaticMesh() || Selected!=EMCToolSlot::Pickaxe) return false;
    if(HasUpgrade(EMCToolUpgrade::Buffer)) {
        if(!IsUsingBuffer()) return false;
        FTransform WorkTool;
        if(!UpgradeWorkToolPose(WorkTool)) return false;
        const bool Contact=Hero->GetCalculusSwingContact(Point,Normal);
        const FTransform InHand=Tool->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
        const FVector Scale=WorkTool.GetScale3D();
        const FVector Tip=LocalPickaxeContactTip();
        FQuat Rotation=WorkTool.GetRotation();
        FVector TipWorld=WorkTool.TransformPosition(Tip);
        if(Contact) {
            // Aim from the braced rear grip, retaining the reference shoulder
            // pose instead of lifting the whole machine to the stone's height.
            const FVector Wrist=(InHand.Inverse()*WorkTool).GetLocation();
            Rotation=(FQuat::FindBetweenNormals((TipWorld-Wrist).GetSafeNormal(),(Point-Wrist).GetSafeNormal())*Rotation).GetNormalized();
            TipWorld=Point+Normal*.7f;
        }
        const FVector Forward=Rotation.GetAxisX(),Right=Rotation.GetAxisY(),Up=Rotation.GetAxisZ();
        // Keep the bit pressed into the locked patch across repeated uses.
        // A small axial feed and motor vibration replace the pickaxe windup.
        const auto* GS=Hero->GetWorld()->GetGameState();
        const float Time=GS?GS->GetServerWorldTimeSeconds():Hero->GetWorld()->GetTimeSeconds();
        TipWorld+=Forward*(.35f*FMath::Sin(Time*67))+Right*(.2f*FMath::Sin(Time*53))+Up*(.15f*FMath::Cos(Time*59));
        Rotation=(Rotation*FRotator(.25f*FMath::Sin(Time*43),.2f*FMath::Cos(Time*47),.4f*FMath::Sin(Time*61)).Quaternion()).GetNormalized();
        const FTransform DrillWorld(Rotation,TipWorld-Rotation.RotateVector(Tip*Scale),Scale);
        HandWorld=InHand.Inverse()*DrillWorld;
        if(!Contact) HandWorld.AddToTranslation(ConstrainPickaxeGrip(HandWorld));
        Blend=1;
        return !HandWorld.ContainsNaN();
    }
    if(!Hero->GetCalculusSwingContact(Point,Normal)) return false;
    const float PlayRate=SwingPlayRate(Selected);
    const float T=Hero->GetToolSwingElapsed()*PlayRate;
    const float Contact=SwingContactTime()*PlayRate;
    const float WindEnd=FMath::Max(.1f,Contact-.14f);
    const float RecoilEnd=Contact+.10f,RecoveryEnd=SwingDuration()*PlayRate-.17f;
    Blend=1;
    FVector Right=FVector::CrossProduct(FVector::UpVector,-Normal).GetSafeNormal();
    if(FVector::DotProduct(Right,Hero->GetActorRightVector())<0) Right=-Right;
    // Keep the handle upright beside the crown, with the head across the
    // surface. Pointing both blades along the normal put the back blade in
    // the player; a steep forward tilt instead buried the hand in enamel.
    FVector Tangent=FVector::VectorPlaneProject(FVector::UpVector,Normal).GetSafeNormal();
    if(Tangent.IsNearlyZero()) Tangent=FVector::VectorPlaneProject(Hero->GetActorForwardVector(),Normal).GetSafeNormal();
    if(Tangent.IsNearlyZero()) return false;
    const FVector HeadAxis=(-Normal*.38f-Right*.925f).GetSafeNormal();
    const FVector Side=FVector::CrossProduct(Tangent,HeadAxis).GetSafeNormal();
    const FQuat ContactRotation=FRotationMatrix::MakeFromXZ(Tangent,HeadAxis).ToQuat();
    // One complete chisel stroke: raise away from enamel, accelerate into the
    // locked sharp-point contact, rebound, then return to a nearby ready pose.
    // The diagonal strike also directs the opposite blade beside the player.
    const FVector2D Ready(20,35),Wind(55,55),Recoil(8,8);
    FVector2D Offset; float Pitch;
    if(T<WindEnd) {
        const float A=FMath::SmoothStep(0.f,WindEnd,T);
        Offset=FMath::Lerp(Ready,Wind,A); Pitch=FMath::Lerp(-8.f,-30.f,A);
    } else if(T<Contact) {
        const float A=FMath::SmoothStep(WindEnd,Contact,T);
        Offset=FMath::Lerp(Wind,FVector2D::ZeroVector,A); Pitch=FMath::Lerp(-30.f,0.f,A);
    } else if(T<RecoilEnd) {
        const float A=FMath::SmoothStep(Contact,RecoilEnd,T);
        Offset=Recoil*A; Pitch=-5.f*A;
    } else {
        const float A=FMath::SmoothStep(RecoilEnd,RecoveryEnd,T);
        Offset=FMath::Lerp(Recoil,Ready,A); Pitch=FMath::Lerp(-5.f,-8.f,A);
    }
    FQuat Rotation=(FQuat(Side,FMath::DegreesToRadians(Pitch))
        *ContactRotation).GetNormalized();
    const FTransform InHand=Tool->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
    const FVector Scale=InHand.GetScale3D()*Hero->GetMesh()->GetComponentScale();
    const FVector Tip=LocalPickaxeContactTip();
    // The sharp point, rather than the wrist or mesh origin, reaches the
    // replicated patch exactly at the authoritative contact time.
    const FVector SharpPoint=Point+Normal*Offset.X+Right*Offset.Y+Tangent*(Offset.Y*.35f);
    if(const auto* Body=Hero->GetMesh()->GetBodyInstance(Hero->RigBone(TEXT("body")))) {
        const FBox BodyBounds=Body->GetBodyBounds();
        const auto Bounds=Tool->GetStaticMesh()->GetBounds();
        const float Padding=FMath::Max(4.f,float(Bounds.BoxExtent.Y*FMath::Abs(Scale.Y)+2));
        const FBox Nearby=BodyBounds.ExpandBy(Padding);
        const auto Probe=FCollisionShape::MakeSphere(Padding);
        auto Penetrations=[&](const FQuat& R) {
            int32 Count=0;
            for(int32 I=0;I<5;++I) {
                const float A=I/4.f;
                const FVector Samples[]={FVector(FMath::Lerp(float(Bounds.Origin.X-Bounds.BoxExtent.X),float(Tip.X),A),0,0),
                    FVector(Tip.X,0,FMath::Lerp(float(Bounds.Origin.Z-Bounds.BoxExtent.Z),float(Bounds.Origin.Z+Bounds.BoxExtent.Z),A))};
                for(const FVector& Sample:Samples) {
                    const FVector P=SharpPoint+R.RotateVector((Sample-Tip)*Scale);
                    // Clearance must not trade player penetration for a hand
                    // inside the tooth. Small tip overlap is the actual strike.
                    if(FVector::DotProduct(P-Point,Normal)<-3) return MAX_int32;
                    if(Nearby.IsInside(P) && Body->OverlapTest(P,FQuat::Identity,Probe)) ++Count;
                }
            }
            return Count;
        };
        int32 Best=Penetrations(Rotation);
        // Bounded local queries against the crown only. Rotate around the
        // sharp point so clearance never changes the authoritative contact.
        for(int32 I=1;Best>0 && I<=3;++I) {
            const FQuat Candidate=(FQuat(Side,FMath::DegreesToRadians(Pitch-10.f*I))
                *ContactRotation).GetNormalized();
            const int32 Count=Penetrations(Candidate);
            if(Count<Best) { Best=Count; Rotation=Candidate; }
        }
    }
    const FTransform PickWorld(Rotation,SharpPoint-Rotation.RotateVector(Tip*Scale),Scale);
    HandWorld=InHand.Inverse()*PickWorld;
    return !HandWorld.ContainsNaN();
}
FVector UMCInventoryComponent::LocalPickaxeContactTip() const
{
    if(HasUpgrade(EMCToolUpgrade::Buffer) && Tool && Tool->GetStaticMesh() && !Tool->DoesSocketExist(TEXT("PickaxeTip")))
        return FVector(Tool->GetStaticMesh()->GetBoundingBox().Max.X,0,0);
    return Tool && Tool->DoesSocketExist(TEXT("PickaxeTip"))
        ?Tool->GetSocketTransform(TEXT("PickaxeTip"),RTS_Component).GetLocation()
        :Settings?Settings->PickaxeContactTip:FVector(59.53f,0,33.45f);
}
FVector UMCInventoryComponent::PickaxeContactTip() const
{ return Tool?Tool->GetComponentTransform().TransformPosition(LocalPickaxeContactTip()):FVector::ZeroVector; }
AMCMouthSurface* UMCInventoryComponent::FindSprayTarget() const
{
    if(!ShouldPresentTool() || !Hero->CanWork() || Hero->bInCoffee || Selected!=EMCToolSlot::Spray || (bPressureMode && HasUpgrade(EMCToolUpgrade::Watergun))) return nullptr;
    AMCMouthSurface* Best=nullptr; float Distance=FMath::Square(SprayReach());
    for(TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) {
        if(!It->bUlcer || It->bTreatmentBlocked || It->IsHealed() || It->IsBurning() || It->IsActorBeingDestroyed()) continue;
        const FVector D=It->GetActorLocation()-Hero->GetActorLocation();
        if(D.SizeSquared()>Distance || FVector::DotProduct(D.GetSafeNormal2D(),Hero->GetActorForwardVector())<.15f) continue;
        FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(MCSpray),false,Hero); Q.AddIgnoredActor(*It);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),It->GetActorLocation()+FVector(0,0,12),ECC_Visibility,Q)) continue;
        Best=*It; Distance=D.SizeSquared();
    }
    return Best;
}
void UMCInventoryComponent::ServerSpray_Implementation()
{
    // This request only selects a target. Healing time comes from the server tick.
    if(!Hero || !Hero->IsPrimaryHeld()) return;
    FireTarget=FindFireTarget();HealingTarget=FireTarget?nullptr:FindSprayTarget();
}
AMCFirePatch* UMCInventoryComponent::FindFireTarget() const
{
    if(!ShouldPresentTool() || !Hero->CanWork() || Hero->bInCoffee || Selected!=EMCToolSlot::Spray || (bPressureMode && HasUpgrade(EMCToolUpgrade::Watergun))) return nullptr;
    AMCFirePatch* Best=nullptr;float Distance=FMath::Square(SprayReach());
    for(TActorIterator<AMCFirePatch> It(GetWorld());It;++It) {
        if(!It->IsBurning()) continue;
        const FVector D=It->GetActorLocation()-Hero->GetActorLocation();
        if(D.SizeSquared()>Distance || FVector::DotProduct(D.GetSafeNormal2D(),Hero->GetActorForwardVector())<.15f) continue;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(MCFireSpray),false,Hero);Q.AddIgnoredActor(*It);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),It->GetActorLocation()+FVector(0,0,20),ECC_Visibility,Q)) continue;
        Best=*It;Distance=D.SizeSquared();
    }
    return Best;
}
FVector UMCInventoryComponent::SprayAim() const
{return FireTarget?FireTarget->GetActorLocation()+FVector(0,0,20):HealingTarget?HealingTarget->GetActorLocation()+FVector(0,0,10):Hero?Hero->GetActorLocation()+Hero->GetActorForwardVector()*180:FVector::ZeroVector;}
FVector UMCInventoryComponent::SprayOrigin() const
{
    if(!Hero) return FVector::ZeroVector;
    FVector Local(34,0,31);
    if(HasUpgrade(EMCToolUpgrade::Watergun) && Settings) {
        Local=(Tool?Tool->GetRelativeTransform():UpgradeAttachment()).TransformPosition(Settings->WatergunNozzle);
        return Hero->BrushPivot->GetComponentTransform().TransformPosition(Local);
    }
    if(const auto* Mesh=Settings?Settings->SprayMesh.Get():nullptr)
        if(const auto* Socket=Mesh->FindSocket(TEXT("SprayNozzle")))
            Local=Settings->SprayTransform.TransformPosition(Socket->RelativeLocation);
    return Hero->BrushPivot->GetComponentTransform().TransformPosition(Local);
}
FVector UMCInventoryComponent::SprayDirection() const
{
    return HealingTarget || FireTarget?(SprayAim()-SprayOrigin()).GetSafeNormal():Hero?Hero->GetActorForwardVector():FVector::ForwardVector;
}
void UMCInventoryComponent::ReactPlayersToSpray()
{
    if(HasUpgrade(EMCToolUpgrade::Watergun) && bPressureMode) return;
    if(!Hero || !Hero->HasAuthority() || Selected!=EMCToolSlot::Spray || !Hero->IsPrimaryHeld()
        || !Hero->CanWork() || Hero->bInCoffee || !ShouldPresentTool() || Now()<NextSocialSprayAt) return;
    NextSocialSprayAt=Now()+.1;
    const FVector Origin=SprayOrigin(),Direction=SprayDirection();
    const float Reach=SprayReach();
    const float Spread=FMath::Tan(FMath::DegreesToRadians(18.f));
    // A short nozzle cone and one sight query per candidate; no physical hit,
    // damage, care tick or network RPC per particle is needed for this reaction.
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Other=*It;
        if(Other==Hero || !Other->Status->IsAlive() || !Other->Expression || Other->SwallowedBy || Other->MimicCaptor) continue;
        const FVector Point=Other->GetActorLocation()+FVector(0,0,20),Delta=Point-Origin;
        const float Along=FVector::DotProduct(Delta,Direction);
        if(Along<=0 || Along>Reach || (Delta-Direction*Along).SizeSquared()>FMath::Square(24+Along*Spread)) continue;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(MCSocialSpray),false,Hero); Q.AddIgnoredActor(Other);
        FHitResult Hit;
        if(GetWorld()->LineTraceSingleByChannel(Hit,Origin,Point,ECC_Visibility,Q)) continue;
        Other->Expression->ReceiveSpray(Hero);
    }
}
FString UMCInventoryComponent::ToolName() const
{
    switch(Selected) {
    case EMCToolSlot::Pickaxe:return HasUpgrade(EMCToolUpgrade::Buffer)?TEXT("ТЯЖЁЛЫЙ ОЧИСТИТЕЛЬ"):TEXT("КИРКА");
    case EMCToolSlot::Knife:return HasUpgrade(EMCToolUpgrade::Chainsaw)?TEXT("БЕНЗОПИЛА"):TEXT("НОЖ");
    case EMCToolSlot::Spray:return HasUpgrade(EMCToolUpgrade::Watergun)?(bPressureMode?TEXT("ВОДЯНОЙ ПИСТОЛЕТ · ВЫСТРЕЛ"):TEXT("ВОДЯНОЙ ПИСТОЛЕТ · ЛЕЧЕНИЕ")):TEXT("СПРЕЙ");
    default:return HasUpgrade(EMCToolUpgrade::MeshaBrush)?TEXT("МЕГАЩЁТКА"):bWaterJetUnlocked?TEXT("ВОДОМЁТ"):TEXT("ЩЁТКА"); }
}
void UMCInventoryComponent::RefreshMesh()
{
    if(!Tool || !Detail) return;
    WorkingGripAlpha=0;
    Presented=Selected; bPresentedUpgrade=bWaterJetUnlocked; Detail->SetVisibility(false);
    PresentedUpgradeMask=UpgradeMask();
    const bool Mega=HasUpgrade(EMCToolUpgrade::MeshaBrush) && !Settings->MeshaBrushMesh.IsNull();
    Hero->Brush->SetStaticMesh(Mega?Settings->MeshaBrushMesh.LoadSynchronous():OriginalBrushMesh.Get());
    Hero->Brush->SetRelativeTransform(Mega?Settings->MeshaBrushTransform:OriginalBrushTransform);
    Hero->Brush->EmptyOverrideMaterials();
    Tool->EmptyOverrideMaterials();Detail->EmptyOverrideMaterials();
    UStaticMesh* Mesh=nullptr; FTransform Transform=FTransform::Identity;
    if(Selected==EMCToolSlot::Pickaxe) { Mesh=Settings->PickaxeMesh.LoadSynchronous(); Transform=Settings->PickaxeTransform; }
    if(Selected==EMCToolSlot::Knife) { Mesh=Settings->KnifeMesh.LoadSynchronous(); Transform=Settings->KnifeTransform; }
    if(Selected==EMCToolSlot::Spray) { Mesh=Settings->SprayMesh.LoadSynchronous(); Transform=Settings->SprayTransform; }
    if(Selected==EMCToolSlot::Pickaxe && HasUpgrade(EMCToolUpgrade::Buffer)) { Mesh=Settings->BufferMesh.LoadSynchronous();Transform=Settings->BufferTransform; }
    if(Selected==EMCToolSlot::Knife && HasUpgrade(EMCToolUpgrade::Chainsaw)) { Mesh=Settings->ChainsawMesh.LoadSynchronous();Transform=Settings->ChainsawTransform; }
    if(Selected==EMCToolSlot::Spray && HasUpgrade(EMCToolUpgrade::Watergun)) { Mesh=Settings->WatergunMesh.LoadSynchronous();Transform=Settings->WatergunTransform; }
    if(Selected==EMCToolSlot::Brush && bWaterJetUnlocked && !Mega) { Mesh=Settings->WaterJetMesh.LoadSynchronous(); Transform=Settings->WaterJetTransform; }
    bPresentedFallback=Mesh==nullptr;
    if(!Mesh && Selected==EMCToolSlot::Pickaxe) {
        Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Art/Meshes/Equipments/SM_Pick.SM_Pick"));
        Transform=FTransform(FRotator(0,0,90),FVector(25,0,0),FVector(.65));
    }
    if(!Mesh && (Selected==EMCToolSlot::Knife || Selected==EMCToolSlot::Spray)) {
        Mesh=LoadObject<UStaticMesh>(nullptr,Selected==EMCToolSlot::Knife?TEXT("/Engine/BasicShapes/Cube.Cube"):TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        Transform=Selected==EMCToolSlot::Knife?FTransform(FRotator::ZeroRotator,FVector(32,0,0),FVector(.58,.035,.11))
            :FTransform(FRotator::ZeroRotator,FVector(15,0,12),FVector(.19,.19,.32));
        Detail->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Detail->SetRelativeTransform(Selected==EMCToolSlot::Knife?FTransform(FRotator::ZeroRotator,FVector(-5,0,0),FVector(.24,.09,.10))
            :FTransform(FRotator::ZeroRotator,FVector(22,0,31),FVector(.23,.08,.08)));
    }
    Tool->SetStaticMesh(Mesh); Tool->SetRelativeTransform(Transform);
    // Untextured Blender imports stay readable; artist materials always take priority.
    if(auto* Neutral=Settings->UpgradeFallbackMaterial.LoadSynchronous()) {
        for(auto* Part:{Tool.Get(),Hero->Brush.Get()}) {
            const bool Upgraded=Part==Hero->Brush?Mega:(Selected==EMCToolSlot::Pickaxe && HasUpgrade(EMCToolUpgrade::Buffer))
                || (Selected==EMCToolSlot::Knife && HasUpgrade(EMCToolUpgrade::Chainsaw)) || (Selected==EMCToolSlot::Spray && HasUpgrade(EMCToolUpgrade::Watergun));
            if(Upgraded && Part->GetStaticMesh()) for(int32 I=0;I<Part->GetNumMaterials();++I)
                if(const auto* Source=Part->GetStaticMesh()->GetMaterial(I);Source && Source->GetName()==TEXT("WorldGridMaterial")) Part->SetMaterial(I,Neutral);
        }
    }
    if(Selected==EMCToolSlot::Spray && bPresentedFallback) {
        Tool->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_SprayCan.M_SprayCan")));
        Detail->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_SprayNozzle.M_SprayNozzle")));
    }
}
void UMCInventoryComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    TickUpgrades(Dt);
    if(Hero && GetOwner()->HasAuthority()) {
        auto* Fire=Hero->IsPrimaryHeld()?FindFireTarget():nullptr;
        if(FireTarget!=Fire) {FireTarget=Fire;GetOwner()->ForceNetUpdate();}
        if(Fire && Fire->Extinguish(Hero,Dt,HasUpgrade(EMCToolUpgrade::Watergun)?3.f:1.f)) {LastSprayAt=Now();SprayReadyAt=0;}
        auto* Target=Hero->IsPrimaryHeld() && !Fire?FindSprayTarget():nullptr;
        if(HealingTarget!=Target) { HealingTarget=Target; GetOwner()->ForceNetUpdate(); }
        if(Target && Target->Treat(Hero,Dt,HasUpgrade(EMCToolUpgrade::Watergun)?3.f:1.f)) {
            if(Now()-LastSprayAt>.8 && Hero->SoundPalette) Hero->SoundPalette->Play(this,TEXT("Brush"),Target->GetActorLocation());
            LastSprayAt=Now(); SprayReadyAt=0;
        }
        ReactPlayersToSpray();
    }
    if(!Hero || !Tool) return;
    if(Presented!=Selected || bPresentedUpgrade!=bWaterJetUnlocked || PresentedUpgradeMask!=UpgradeMask()) RefreshMesh();
    const auto Upgrade=SelectedUpgrade();
    const bool Working=Upgrade==EMCToolUpgrade::Buffer?IsUsingBuffer():Upgrade==EMCToolUpgrade::Watergun?IsUsingWatergun():false;
    WorkingGripAlpha=FMath::FInterpTo(WorkingGripAlpha,Working?1.f:0.f,Dt,12.f);
    if(Upgrade==EMCToolUpgrade::Buffer || Upgrade==EMCToolUpgrade::Watergun) Tool->SetRelativeTransform(UpgradeAttachment());
    const bool Visible=ShouldPresentTool();
    const bool Custom=Tool->GetStaticMesh()!=nullptr;
    Tool->SetVisibility(Visible && Custom); Detail->SetVisibility(Visible && Custom && bPresentedFallback && (Selected==EMCToolSlot::Knife || Selected==EMCToolSlot::Spray));
    if(Selected!=EMCToolSlot::Brush || Custom) Hero->Brush->SetVisibility(false);
    else Hero->Brush->SetVisibility(Visible && Hero->HasBrush());
    if(Hero->RewardInteraction && (Hero->RewardInteraction->Stage==EMCRewardChestStage::Lockpicking
        || Hero->RewardInteraction->Stage==EMCRewardChestStage::Opening)) Hero->Brush->SetVisibility(true);
    if(SprayMist) {
        const bool Emit=Visible && Selected==EMCToolSlot::Spray && !HasUpgrade(EMCToolUpgrade::Watergun) && Hero->CanWork() && (HealingTarget || FireTarget || Hero->IsPrimaryHeld());
        if(Emit) SprayMist->SetWorldLocationAndRotation(SprayOrigin(),FRotationMatrix::MakeFromZ(SprayDirection()).Rotator());
        if(Emit!=bSprayEmitting) {if(Emit) SprayMist->Activate(true);else SprayMist->Deactivate();bSprayEmitting=Emit;}
    }
    if(WaterMist) {
        const bool Emit=Visible && Upgrade==EMCToolUpgrade::Watergun && !bPressureMode
            && Hero->IsPrimaryHeld() && Hero->CanWork() && !Hero->bInCoffee;
        if(Emit) WaterMist->SetWorldLocationAndRotation(SprayOrigin(),FRotationMatrix::MakeFromZ(SprayDirection()).Rotator());
        if(Emit!=bWaterEmitting) {if(Emit) WaterMist->Activate(true);else WaterMist->Deactivate();bWaterEmitting=Emit;}
    }
}
void UMCInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCInventoryComponent,Selected);
    DOREPLIFETIME(UMCInventoryComponent,bWaterJetUnlocked); DOREPLIFETIME(UMCInventoryComponent,SprayReadyAt); DOREPLIFETIME(UMCInventoryComponent,LastSprayAt);
    DOREPLIFETIME(UMCInventoryComponent,HealingTarget);DOREPLIFETIME(UMCInventoryComponent,FireTarget);
    DOREPLIFETIME(UMCInventoryComponent,bPressureMode);DOREPLIFETIME(UMCInventoryComponent,bChargingWater);
    DOREPLIFETIME(UMCInventoryComponent,WaterChargeStartedAt);DOREPLIFETIME(UMCInventoryComponent,WaterShotReadyAt);
}
