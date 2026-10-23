#include "MCCharacterCosmeticsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/SkeletalMesh.h"
#include "Net/UnrealNetwork.h"

UMCCharacterCosmeticsComponent::UMCCharacterCosmeticsComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
}

void UMCCharacterCosmeticsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMCCharacterCosmeticsComponent,EquippedLoadout);
}

UDataTable* UMCCharacterCosmeticsComponent::GetCatalog()
{
    if(!Catalog) Catalog=UMCCosmeticCatalog::GetDefaultCatalog();
    return Catalog;
}

void UMCCharacterCosmeticsComponent::BeginPlay()
{
    Super::BeginPlay();RefreshPresentation();
}

void UMCCharacterCosmeticsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearParts();Super::EndPlay(Reason);
}

bool UMCCharacterCosmeticsComponent::SetEquippedLoadout(const FMCCharacterCosmeticLoadout& Loadout)
{
    if(!GetOwner() || !GetOwner()->HasAuthority()) return false;
    FText Error;
    if(!UMCCosmeticCatalog::ValidateLoadout(GetCatalog(),Loadout,Error)) return false;
    EquippedLoadout=Loadout;bPreviewing=false;RefreshPresentation();GetOwner()->ForceNetUpdate();return true;
}

bool UMCCharacterCosmeticsComponent::PreviewLoadout(const FMCCharacterCosmeticLoadout& Loadout)
{
    const auto* Character=Cast<ACharacter>(GetOwner());
    if(!Character || !Character->IsLocallyControlled()) return false;
    FText Error;
    if(!UMCCosmeticCatalog::ValidateLoadout(GetCatalog(),Loadout,Error)) return false;
    Preview=Loadout;bPreviewing=true;RefreshPresentation();return true;
}

void UMCCharacterCosmeticsComponent::RestoreAuthoritativeLoadout()
{
    bPreviewing=false;RefreshPresentation();
}

void UMCCharacterCosmeticsComponent::OnRep_Loadout()
{
    bPreviewing=false;RefreshPresentation();
}

void UMCCharacterCosmeticsComponent::ClearParts()
{
    for(UStaticMeshComponent* Part:Parts) if(IsValid(Part)) Part->DestroyComponent();
    Parts.Reset();SetComponentTickEnabled(false);
}

void UMCCharacterCosmeticsComponent::RefreshPresentation()
{
    ClearParts();
    auto* Character=Cast<ACharacter>(GetOwner());
    auto* Target=Character?Character->GetMesh():nullptr;
    if(!Target || !Target->GetSkeletalMeshAsset() || GetNetMode()==NM_DedicatedServer) return;
    const auto& Loadout=bPreviewing?Preview:EquippedLoadout;
    if(Loadout.Slots.IsEmpty()) return;
    auto* Table=GetCatalog();
    FText Error;
    if(!UMCCosmeticCatalog::ValidateLoadout(Table,Loadout,Error)) return;
    for(const auto& Entry:Loadout.Slots) {
        if(Entry.ItemId.IsNone()) continue;
        const auto* Row=UMCCosmeticCatalog::Find(Table,Entry.ItemId);
        if(!Row || !Target->DoesSocketExist(Row->AttachBone)) continue;
        auto* Part=NewObject<UStaticMeshComponent>(Character,NAME_None,RF_Transient);
        Part->SetStaticMesh(Row->Mesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        Part->SetSimulatePhysics(false);
        Part->SetIsReplicated(false);
        Part->bUseAttachParentBound=true;
        Part->SetupAttachment(Target,Row->AttachBone);
        FTransform Fit=Row->LocalTransform;
        if(Row->bMeshSpaceFit) {
            const auto& Ref=Target->GetSkeletalMeshAsset()->GetRefSkeleton();
            FTransform ReferenceBone=FTransform::Identity;
            for(int32 Index=Ref.FindBoneIndex(Row->AttachBone);Index>=0;Index=Ref.GetParentIndex(Index))
                ReferenceBone=ReferenceBone*Ref.GetRefBonePose()[Index];
            Fit=Fit.GetRelativeTransform(ReferenceBone);
        }
        Part->SetRelativeTransform(Fit);
        for(int32 Index=0;Index<Row->Materials.Num();++Index) if(Row->Materials[Index]) Part->SetMaterial(Index,Row->Materials[Index]);
        Part->RegisterComponent();Parts.Add(Part);
    }
    SetComponentTickEnabled(!Parts.IsEmpty());SyncVisibility();
}

void UMCCharacterCosmeticsComponent::SyncVisibility()
{
    const auto* Character=Cast<ACharacter>(GetOwner());
    const auto* Target=Character?Character->GetMesh():nullptr;
    const bool bVisible=Target && Target->IsVisible() && !Target->bHiddenInGame && !Character->IsHidden();
    for(UStaticMeshComponent* Part:Parts) if(Part && Part->IsVisible()!=bVisible) Part->SetVisibility(bVisible);
}

void UMCCharacterCosmeticsComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction);SyncVisibility();
}

TArray<UStaticMeshComponent*> UMCCharacterCosmeticsComponent::GetAccessoryParts() const
{
    TArray<UStaticMeshComponent*> Result;for(UStaticMeshComponent* Part:Parts) if(IsValid(Part)) Result.Add(Part);return Result;
}
