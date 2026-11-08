#include "MCNetworkPresentation.h"
#include "MCToothMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
struct FCompensationScope
{
    UMCToothMeshComponent* Mesh;
    explicit FCompensationScope(USkeletalMeshComponent* In):Mesh(Cast<UMCToothMeshComponent>(In)) {if(Mesh) Mesh->BeginNetworkPresentationCompensation();}
    ~FCompensationScope() {if(Mesh) Mesh->EndNetworkPresentationCompensation();}
};
}
bool FMCOwnerReconciliationPresentation::IsOwned() const
{
    const auto* Mesh=VisualMesh.Get();
    return bActive && Mesh && Parent.IsValid() && Mesh->GetAttachParent()==Parent.Get()
        && Mesh->GetAttachSocketName()==Socket && !Mesh->IsUsingAbsoluteLocation()
        && Mesh->IsUsingAbsoluteRotation()==bAbsoluteRotation && Mesh->IsUsingAbsoluteScale()==bAbsoluteScale
        && Mesh->GetRelativeTransform().Equals(LastWritten,.001f);
}
bool FMCOwnerReconciliationPresentation::Begin(USkeletalMeshComponent* Mesh,const FMCNetworkCorrectionContext& InContext)
{
    if(!Mesh || !Mesh->GetAttachParent() || Mesh->IsUsingAbsoluteLocation() || Mesh->GetComponentTransform().ContainsNaN()
        || !InContext.IsWellFormed() || bScopeOpen) return false;
    if(bActive && (!IsOwned() || VisualMesh.Get()!=Mesh || !Context.Equals(InContext))) {Release();return false;}
    if(!bActive) {
        VisualMesh=Mesh;Parent=Mesh->GetAttachParent();Socket=Mesh->GetAttachSocketName();
        bAbsoluteRotation=Mesh->IsUsingAbsoluteRotation();bAbsoluteScale=Mesh->IsUsingAbsoluteScale();
        BaseRelative=LastWritten=Mesh->GetRelativeTransform();WorldOffset=FVector::ZeroVector;WorldRotationOffset=FQuat::Identity;Context=InContext;bActive=true;
    }
    bAwaitingReplay=true;return true;
}
USkeletalMeshComponent* FMCOwnerReconciliationPresentation::BeginScope()
{
    if(bScopeOpen || !IsOwned()) {if(!bScopeOpen) Release();return nullptr;}
    bScopeOpen=true;ScopeWorld=VisualMesh->GetComponentTransform();
    if(!bTransactionOpen) {TransactionOffset=WorldOffset;TransactionRotationOffset=WorldRotationOffset;bTransactionOpen=true;}
    return VisualMesh.Get();
}
void FMCOwnerReconciliationPresentation::EndScope(bool Awaiting,double Now)
{
    if(!bScopeOpen) return;bScopeOpen=false;
    auto* Mesh=VisualMesh.Get();
    if(!Mesh || !Parent.IsValid() || Mesh->GetAttachParent()!=Parent.Get() || Mesh->GetAttachSocketName()!=Socket
        || Mesh->IsUsingAbsoluteLocation() || !Mesh->GetComponentTransform().Equals(ScopeWorld,.001f)) {*this={};return;}
    {
        FCompensationScope Guard(Mesh);
        // The native adjustment/replay has already rotated the canonical root.
        // Keep the scoped world orientation just as we keep its world position.
        Mesh->SetRelativeScale3D(BaseRelative.GetScale3D());
    }
    LastWritten=Mesh->GetRelativeTransform();
    const FTransform ParentSocketWorld=Parent->GetSocketTransform(Socket);
    WorldOffset=ParentSocketWorld.TransformVector(LastWritten.GetLocation()-BaseRelative.GetLocation());
    const FQuat CanonicalWorldRotation=bAbsoluteRotation?BaseRelative.GetRotation()
        :ParentSocketWorld.GetRotation()*BaseRelative.GetRotation();
    WorldRotationOffset=(Mesh->GetComponentQuat()*CanonicalWorldRotation.Inverse()).GetNormalized();
    // Corrupt/extreme coordinates are a safety reset, never a teleport classifier.
    if(WorldOffset.ContainsNaN() || WorldRotationOffset.ContainsNaN() || WorldOffset.SizeSquared()>FMath::Square(100000.)) {Release();return;}
    bAwaitingReplay=Awaiting;
    if(!Awaiting) {
        if(!bHasDecay || !WorldOffset.Equals(TransactionOffset,.001f) || !WorldRotationOffset.Equals(TransactionRotationOffset,.00001f)) {
            StartedAt=LastTickAt=Now;StartOffset=WorldOffset;StartRotationOffset=WorldRotationOffset;
            Duration=FMath::Clamp(.12f+float(WorldOffset.Size())/2000.f,.12f,.25f);bHasDecay=true;
        }
        bTransactionOpen=false;
        if(WorldOffset.IsNearlyZero(.001f) && WorldRotationOffset.Equals(FQuat::Identity,.00001f)) Release();
    }
}
void FMCOwnerReconciliationPresentation::WriteOffset(FVector OffsetValue,FQuat RotationOffsetValue)
{
    if(!IsOwned()) {*this={};return;}
    auto* Mesh=VisualMesh.Get();
    const FTransform ParentSocketWorld=Parent->GetSocketTransform(Socket);
    const FVector Local=ParentSocketWorld.InverseTransformVector(OffsetValue);
    const FQuat CanonicalWorldRotation=bAbsoluteRotation?BaseRelative.GetRotation()
        :ParentSocketWorld.GetRotation()*BaseRelative.GetRotation();
    const FQuat ShownWorldRotation=RotationOffsetValue*CanonicalWorldRotation;
    const FQuat LocalRotation=bAbsoluteRotation?ShownWorldRotation:ParentSocketWorld.GetRotation().Inverse()*ShownWorldRotation;
    FCompensationScope Guard(Mesh);
    Mesh->SetRelativeLocationAndRotation(BaseRelative.GetLocation()+Local,LocalRotation.GetNormalized(),false,nullptr,ETeleportType::None);
    LastWritten=Mesh->GetRelativeTransform();WorldOffset=OffsetValue;WorldRotationOffset=RotationOffsetValue;
}
void FMCOwnerReconciliationPresentation::Tick(double Now)
{
    if(!bActive) return;
    if(!IsOwned()) {*this={};return;}
    if(bAwaitingReplay || Now<=LastTickAt) return;LastTickAt=Now;
    const float T=FMath::Clamp(float((Now-StartedAt)/Duration),0.f,1.f),Ease=T*T*(3-2*T);
    WriteOffset(StartOffset*(1-Ease),FQuat::Slerp(StartRotationOffset,FQuat::Identity,Ease).GetNormalized());if(T>=1) Release();
}
void FMCOwnerReconciliationPresentation::Release()
{
    if(IsOwned()) WriteOffset(FVector::ZeroVector,FQuat::Identity);*this={};
}
