#include "MCNiagaraMaterialContextComponent.h"
#include "MCToothMeshComponent.h"

#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MCGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "PSOPrecache.h"

namespace MCNiagaraMaterialContext
{
static const FName SubjectInput(TEXT("User.VisualSubject"));
static const FName MIDInput(TEXT("User.MaterialTarget"));
static const FName ResourcesInput(TEXT("User.MaterialContextResources"));

template<class T> FNiagaraVariable Key(FName Name)
{ return FNiagaraVariable(FNiagaraTypeDefinition(T::StaticClass()), Name); }
template<class T> T* Read(const UNiagaraComponent* C, FName Name)
{
    if (!C) return nullptr;
    const auto& Store = C->GetOverrideParameters();
    const auto Variable = Key<T>(Name);
    return Store.IndexOf(Variable) == INDEX_NONE ? nullptr : Cast<T>(Store.GetUObject(Variable).Get());
}
template<class T> bool Has(const UNiagaraSystem* System, FName Name)
{ return System && System->GetExposedParameters().IndexOf(Key<T>(Name)) != INDEX_NONE; }
bool Finite(const FLinearColor& C)
{ return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) && FMath::IsFinite(C.B) && FMath::IsFinite(C.A); }
bool SameWorld(const UObject* A, const UWorld* World)
{ return IsValid(A) && World && A->GetWorld() == World; }
bool AssignedTo(const UMeshComponent* Mesh, const UMaterialInstanceDynamic* MID)
{
    if (!IsValid(Mesh) || !IsValid(MID)) return false;
    for (auto* Material : Mesh->GetMaterials()) if (Material == MID) return true;
    if (Mesh->OverlayMaterial == MID) return true;
    for (const auto& Material : Mesh->MaterialSlotsOverlayMaterial) if (Material.Get() == MID) return true;
    return false;
}
bool ReadOwnScalar(const UMaterialInstanceDynamic* MID, FName Name, float& Out)
{
    if (!IsValid(MID)) return false;
    const FMaterialParameterInfo Info(Name);
    // GetScalarParameterValue(..., true) also searches parent MIC overrides in UE5.8.
    // Read only this exact MID's public override records; never mutate that array.
    for (const auto& P : MID->ScalarParameterValues) if (P.ParameterInfo == Info)
    { Out = P.ParameterValue; return FMath::IsFinite(Out); }
    return false;
}
UMaterialInterface* StaticParent(UMaterialInterface* M)
{
    for (int32 I=0; I<16; ++I) { auto* D=Cast<UMaterialInstanceDynamic>(M); if(!D)return M; M=D->Parent; }
    return nullptr;
}
UObject* Geometry(UMeshComponent* Mesh)
{
    if(auto* S=Cast<UStaticMeshComponent>(Mesh))return S->GetStaticMesh();
    if(auto* S=Cast<USkeletalMeshComponent>(Mesh))return S->GetSkeletalMeshAsset();
    return nullptr;
}
uint64 FactoryFlags(UMeshComponent* Mesh)
{
    FPSOPrecacheParams P;Mesh->SetupPrecachePSOParams(P);
    // An owned global overlay can add WPO after activation; its overlay PSOs are
    // prepared separately. Do not invalidate the base factory on our own write.
    P.bAnyMaterialHasWorldPositionOffset=false;return P.Data;
}
TArray<TWeakObjectPtr<UMaterialInterface>> MaterialSignature(UMeshComponent* Mesh)
{
    TArray<TWeakObjectPtr<UMaterialInterface>> Out;
    for(auto* M:Mesh->GetMaterials())Out.Add(StaticParent(M));
    // Slot overlays change the actual renderer's global-overlay applicability.
    TArray<TObjectPtr<UMaterialInterface>> Slots;Mesh->GetMaterialSlotsOverlayMaterial(Slots);
    for(const auto& M:Slots)Out.Add(StaticParent(M.Get()));
    return Out;
}
}

struct FMCNiagaraMaterialContextState
{
    struct FPool
    {
        TWeakObjectPtr<UNiagaraSystem> System;
        FName ID, TintName, OpacityName, ScalarName;
        EMCNiagaraMaterialContextOperation Operation;
        TWeakObjectPtr<UMaterialInterface> Material;
        int32 Priority = 0;
        TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> MIDs;
        TArray<TWeakObjectPtr<UNiagaraComponent>> ReservedBy;
        // Niagara auto-reactivates inactive pooled components. Foreign ownership must
        // therefore stay sticky for this activation, not disappear with the last lease.
        TArray<bool> Blocked;
        bool Matches(const UMCNiagaraMaterialContextComponent* C) const
        {
            return C && ID == C->ContextID && Operation == C->Operation && Priority == C->Priority
                && Material.Get() == C->OverlayMaterial.Get() && TintName == C->TintParameter
                && OpacityName == C->OpacityParameter && ScalarName == C->ScalarParameter;
        }
    };
    struct FAllowedScalar
    { TWeakObjectPtr<UMeshComponent> Mesh; TWeakObjectPtr<UMaterialInstanceDynamic> MID; FName Parameter; };
    struct FFactory
    {
        TWeakObjectPtr<UObject> Geometry;
        TWeakObjectPtr<UClass> Class;
        TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
        uint64 Flags=0;
        bool bNegativeScale=false;
        int32 ForcedLOD=0;
    };
    struct FReservation { int32 Pool = INDEX_NONE, Slot = INDEX_NONE; };
    struct FActivation
    { TWeakObjectPtr<UNiagaraComponent> Niagara; TArray<FReservation> Reservations; };
    struct FLease
    {
        uint64 ID = 0;
        TWeakObjectPtr<UMCNiagaraMaterialContextComponent> Client;
        TWeakObjectPtr<UNiagaraComponent> Niagara;
        TWeakObjectPtr<UMeshComponent> Mesh;
        TWeakObjectPtr<UMaterialInstanceDynamic> MID;
        EMCNiagaraMaterialContextOperation Operation;
        FName Parameter;
        int32 Priority = 0, Pool = INDEX_NONE, Slot = INDEX_NONE;
        FLinearColor Tint;
        float Opacity = 0, Scalar = 0;
    };
    struct FGroup
    {
        TWeakObjectPtr<UMeshComponent> Mesh;
        TWeakObjectPtr<UMaterialInstanceDynamic> MID;
        EMCNiagaraMaterialContextOperation Operation;
        FName Parameter;
        TWeakObjectPtr<UMaterialInterface> BaselineOverlay, LastOverlay;
        float BaselineScalar = 0, LastScalar = 0;
        bool bWritten = false, bContested = false;
        bool Contains(const FLease& L) const
        {
            return Operation == L.Operation && (Operation == EMCNiagaraMaterialContextOperation::ExistingMeshOverlay
                ? Mesh == L.Mesh : MID == L.MID && Parameter == L.Parameter);
        }
    };
    TArray<FPool> Pools;
    TArray<FAllowedScalar> Allowed;
    TArray<FFactory> Factories;
    TArray<FActivation> Activations;
    TArray<FLease> Leases;
    TArray<FGroup> Groups;
};

UMCNiagaraMaterialContextResources::UMCNiagaraMaterialContextResources()
    : State(MakePimpl<FMCNiagaraMaterialContextState>()) {}
UMCNiagaraMaterialContextResources::~UMCNiagaraMaterialContextResources() = default;
UWorld* UMCNiagaraMaterialContextResources::GetWorld() const { return PreparedWorld.Get(); }

bool UMCNiagaraMaterialContextResources::NeedsMaterialContext(const UNiagaraSystem* System)
{
    if (!IsValid(System)) return false;
    for (const auto& Handle : System->GetEmitterHandles()) if (const auto* Data = Handle.GetEmitterData())
        for (auto* Renderer : Data->GetRenderers())
            if (const auto* P = Cast<UNiagaraComponentRendererProperties>(Renderer))
                if (Cast<UMCNiagaraMaterialContextComponent>(P->TemplateComponent)) return true;
    return false;
}

bool UMCNiagaraMaterialContextResources::IsPreparedForEpoch(FGuid Epoch) const
{ return bSealed && IsCurrentScope() && Epoch.IsValid() && PreparedEpoch == Epoch; }
bool UMCNiagaraMaterialContextResources::IsReadyForPlayback(const UWorld* World) const
{ return bSealed && PreparedWorld.Get()==World && IsCurrentScope(); }

bool UMCNiagaraMaterialContextResources::IsCurrentScope() const
{
    if(!IsValid(GetWorld())||!PreparedEpoch.IsValid())return false;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return !GS || !GS->GameplayLoadId.IsValid() || GS->GameplayLoadId==PreparedEpoch;
}
bool UMCNiagaraMaterialContextResources::HasActiveActivations() const
{ return State->Activations.ContainsByPredicate([](const auto& A){return A.Niagara.IsValid();}); }
void UMCNiagaraMaterialContextResources::GetPreparedOverlayMaterials(TArray<UMaterialInterface*>& Out) const
{
    for(const auto& P:State->Pools)if(P.Operation==EMCNiagaraMaterialContextOperation::ExistingMeshOverlay && !P.MIDs.IsEmpty())
        if(auto* M=P.MIDs[0].Get())Out.AddUnique(M);
}
bool UMCNiagaraMaterialContextResources::ValidateTargetFactory(UMeshComponent* Mesh,FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    if(!IsValid(Mesh)||!IsValid(Geometry(Mesh))) {Error=TEXT("Material context needs an actual resident mesh asset.");return false;}
    // This exact native subclass changes pose/physics timing only. Its header
    // inherits the stock skeletal scene proxy and PSO setup unchanged. Keep
    // arbitrary subclasses outside this explicit prepared-factory contract.
    if(Mesh->GetClass()!=UStaticMeshComponent::StaticClass() && Mesh->GetClass()!=USkeletalMeshComponent::StaticClass()
        && Mesh->GetClass()!=UMCToothMeshComponent::StaticClass())
    {Error=TEXT("Material-context PSOs support stock StaticMeshComponent/SkeletalMeshComponent and the exact native ToothMeshComponent adapter only; other custom, instanced and procedural factories need an explicit adapter.");return false;}
    if(auto* S=Cast<USkeletalMeshComponent>(Mesh)) {
        // Cooked targets retain Nanite render data, not the editor build settings.
        bool bNanite=S->HasValidNaniteData();
#if WITH_EDITORONLY_DATA
        bNanite|=S->GetSkeletalMeshAsset()->IsNaniteEnabled();
#endif
        if(bNanite || S->GetCPUSkinningEnabled() || S->bRenderStatic)
        {Error=TEXT("Nanite/CPU/static skeletal skinning is outside the prepared context factory contract.");return false;}
    }
    return true;
}
bool UMCNiagaraMaterialContextResources::IsTargetPrepared(UMeshComponent* Mesh) const
{
    using namespace MCNiagaraMaterialContext;
    FString Error;if(!SameWorld(Mesh,GetWorld())||!ValidateTargetFactory(Mesh,Error))return false;
    const auto Materials=MaterialSignature(Mesh);const uint64 Flags=FactoryFlags(Mesh);
    const bool Negative=Mesh->GetRenderMatrix().Determinant()<0;
    const int32 LOD=Cast<UStaticMeshComponent>(Mesh)?CastChecked<UStaticMeshComponent>(Mesh)->GetForcedLodModel():CastChecked<USkeletalMeshComponent>(Mesh)->GetForcedLOD();
    return State->Factories.ContainsByPredicate([&](const auto& F){return F.Geometry.Get()==Geometry(Mesh) && F.Class==Mesh->GetClass()
        &&F.Materials==Materials&&F.Flags==Flags&&F.bNegativeScale==Negative&&F.ForcedLOD==LOD;});
}
bool UMCNiagaraMaterialContextResources::RegisterPreparedTarget(UMeshComponent* Mesh,FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    if(!IsInGameThread()||!SameWorld(Mesh,GetWorld())||!ValidateTargetFactory(Mesh,Error))return false;
    if(IsTargetPrepared(Mesh))return true;
    if(bSealed||HasActiveActivations()||State->Factories.Num()>=128)
    {Error=TEXT("New mesh factory requires an explicit preparation boundary (maximum128 scoped signatures).");return false;}
    auto& F=State->Factories.AddDefaulted_GetRef();F.Geometry=Geometry(Mesh);F.Class=Mesh->GetClass();F.Materials=MaterialSignature(Mesh);
    F.Flags=FactoryFlags(Mesh);F.bNegativeScale=Mesh->GetRenderMatrix().Determinant()<0;
    F.ForcedLOD=Cast<UStaticMeshComponent>(Mesh)?CastChecked<UStaticMeshComponent>(Mesh)->GetForcedLodModel():CastChecked<USkeletalMeshComponent>(Mesh)->GetForcedLOD();
    Retained.AddUnique(F.Geometry.Get());for(const auto& M:F.Materials)if(M.IsValid())Retained.AddUnique(M.Get());
    return true;
}

bool UMCNiagaraMaterialContextResources::BindPreparedScalarTarget(UNiagaraComponent* C, FString& Error) const
{
    using namespace MCNiagaraMaterialContext;
    if (!IsInGameThread() || !bSealed || !IsCurrentScope() || !SameWorld(C, GetWorld()))
    { Error = TEXT("Bind scalar context only from the same world's sealed resource scope."); return false; }
    auto* Mesh = Cast<UMeshComponent>(Read<USceneComponent>(C, SubjectInput));
    UMaterialInstanceDynamic* Match = nullptr; bool Needed = false;
    for (const auto& P : State->Pools)
        if (P.System == C->GetAsset() && P.Operation == EMCNiagaraMaterialContextOperation::ExistingMIDScalar)
        {
            Needed = true;
            const auto* Binding = State->Allowed.FindByPredicate([&](const auto& A){ return A.Mesh == Mesh && A.Parameter == P.ScalarName; });
            auto* MID = Binding ? Binding->MID.Get() : nullptr;
            if (!SameWorld(Mesh, GetWorld()) || !AssignedTo(Mesh, MID) || (Match && Match != MID))
            { Error = TEXT("This NS requires one exact registered local MID for all its scalar contexts."); return false; }
            Match = MID;
        }
    if (!Needed) return true;
    const auto Variable = Key<UMaterialInstanceDynamic>(MIDInput);
    auto& Store = C->GetOverrideParameters();
    if (Store.IndexOf(Variable) == INDEX_NONE)
    { Error = TEXT("Scalar context requires an exact typed MaterialTarget input."); return false; }
    Store.SetUObject(Match, Variable); return true;
}

bool UMCNiagaraMaterialContextResources::Initialize(UWorld* World, FGuid Epoch, FString& Error)
{
    if (!IsInGameThread() || !IsValid(World) || !Epoch.IsValid() || World->GetNetMode() == NM_DedicatedServer)
    { Error = TEXT("Material contexts require an explicit local world and presentation epoch."); return false; }
    const auto* GS=World->GetGameState<AMCGameState>();
    if(GS && GS->GameplayLoadId.IsValid() && GS->GameplayLoadId!=Epoch)
    {Error=TEXT("Material context preparation epoch must equal the actual world run identity.");return false;}
    Reset(); PreparedWorld = World; PreparedEpoch = Epoch; return true;
}

bool UMCNiagaraMaterialContextResources::PrepareSystem(UNiagaraSystem* System, int32 ConcurrentInstances, FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    if (!IsInGameThread() || !IsValid(GetWorld()) || !IsValid(System) || ConcurrentInstances < 1 || ConcurrentInstances > 128)
    { Error = TEXT("Invalid material-context preparation scope or capacity (1..128 per template)."); return false; }
    TSet<FName> IDs;
    TArray<const UMCNiagaraMaterialContextComponent*> Templates;
    for (const auto& Handle : System->GetEmitterHandles()) if (const auto* Data = Handle.GetEmitterData())
        for (auto* Renderer : Data->GetRenderers())
            if (const auto* Components = Cast<UNiagaraComponentRendererProperties>(Renderer))
                if (const auto* T = Cast<UMCNiagaraMaterialContextComponent>(Components->TemplateComponent))
                {
                    if (Data->SimTarget != ENiagaraSimTarget::CPUSim || Components->ComponentCountLimit != 1
                        || T->ContextID.IsNone() || IDs.Contains(T->ContextID))
                    { Error = TEXT("Context renderers require CPU simulation, limit 1 and a unique ContextID per NS."); return false; }
                    IDs.Add(T->ContextID); Templates.Add(T);
                }
    for (const auto& P : State->Pools) if (P.System == System && !IDs.Contains(P.ID))
    { Error = TEXT("A prepared context renderer was removed; explicit Reset/reprepare is required."); return false; }
    if (Templates.IsEmpty()) return true;
    if (!Has<USceneComponent>(System, SubjectInput) || !Has<UMCNiagaraMaterialContextResources>(System, ResourcesInput))
    { Error = TEXT("Context NS requires exact typed read-only VisualSubject and MaterialContextResources inputs."); return false; }
    for (const auto* T : Templates)
    {
        const int32 Existing = State->Pools.IndexOfByPredicate([&](const auto& P){ return P.System == System && P.ID == T->ContextID; });
        if (Existing != INDEX_NONE)
        {
            const auto& P = State->Pools[Existing];
            if (!P.Matches(T) || P.ReservedBy.Num() < ConcurrentInstances)
            { Error = TEXT("Prepared context template changed; explicit Reset/reprepare is required."); return false; }
            continue;
        }
        if (bSealed || !State->Activations.IsEmpty())
        { Error = TEXT("New context templates cannot be prepared during playback."); return false; }
        if (T->Operation == EMCNiagaraMaterialContextOperation::ExistingMeshOverlay)
        {
            FLinearColor Color; float Alpha = 0;
            if (!IsValid(T->OverlayMaterial) || T->TintParameter.IsNone() || T->OpacityParameter.IsNone()
                || !T->OverlayMaterial->GetVectorParameterValue(FMaterialParameterInfo(T->TintParameter), Color)
                || !T->OverlayMaterial->GetScalarParameterValue(FMaterialParameterInfo(T->OpacityParameter), Alpha)
                || !Finite(T->Tint) || !FMath::IsFinite(T->Opacity))
            { Error = TEXT("Overlay material and its actual color/opacity parameters must be resident and valid."); return false; }
            if (Retained.Num() + ConcurrentInstances > 512)
            { Error = TEXT("Scoped context resource budget exceeded; adjust the explicit loading plan."); return false; }
        }
        else if (T->Operation != EMCNiagaraMaterialContextOperation::ExistingMIDScalar
            || T->ScalarParameter.IsNone() || !Has<UMaterialInstanceDynamic>(System, MIDInput))
        { Error = TEXT("Scalar context requires a named allowed scalar and exact MaterialTarget MID input."); return false; }
        auto& P = State->Pools.AddDefaulted_GetRef();
        P.System = System; P.ID = T->ContextID; P.Operation = T->Operation; P.Material = T->OverlayMaterial;
        P.TintName = T->TintParameter; P.OpacityName = T->OpacityParameter; P.ScalarName = T->ScalarParameter; P.Priority = T->Priority;
        P.ReservedBy.SetNum(ConcurrentInstances); P.MIDs.SetNum(ConcurrentInstances); P.Blocked.Init(false, ConcurrentInstances);
        Retained.AddUnique(System);
        if (T->Operation == EMCNiagaraMaterialContextOperation::ExistingMeshOverlay)
            for (int32 I = 0; I < ConcurrentInstances; ++I)
            {
                auto* MID = UMaterialInstanceDynamic::Create(T->OverlayMaterial, this);
                if (!MID) { Error = TEXT("Failed to prepare overlay MID."); return false; }
                MID->SetVectorParameterValue(T->TintParameter, T->Tint);
                MID->SetScalarParameterValue(T->OpacityParameter, FMath::Clamp(T->Opacity, 0.f, 1.f));
                Retained.Add(MID); P.MIDs[I] = MID;
            }
    }
    return true;
}

bool UMCNiagaraMaterialContextResources::RegisterExistingScalarTarget(UMeshComponent* Subject,
    UMaterialInstanceDynamic* MID, FName Parameter, FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    float Value = 0;
    if (!IsInGameThread() || !SameWorld(Subject, GetWorld()) || !AssignedTo(Subject, MID)
        || MID->IsAsset() || Parameter.IsNone() || !ReadOwnScalar(MID, Parameter, Value)
        || !FMath::IsFinite(Value))
    { Error = TEXT("Register an actual assigned MID with an existing explicit cosmetic scalar override before sealing."); return false; }
    if (State->Allowed.ContainsByPredicate([&](const auto& A){ return A.MID == MID && A.Mesh != Subject; }))
    { Error = TEXT("A borrowed cosmetic MID must belong to one explicit mesh, not be shared across subjects."); return false; }
    if (State->Allowed.ContainsByPredicate([&](const auto& A){ return A.Mesh == Subject && A.Parameter == Parameter && A.MID != MID; }))
    { Error = TEXT("A subject/scalar binding cannot select between multiple MIDs implicitly."); return false; }
    if(State->Allowed.ContainsByPredicate([&](const auto& A){return A.Mesh==Subject&&A.MID==MID&&A.Parameter==Parameter;}))return true;
    if(bSealed){Error=TEXT("New scalar bindings require an explicit preparation boundary.");return false;}
    if (!State->Allowed.ContainsByPredicate([&](const auto& A){ return A.Mesh == Subject && A.MID == MID && A.Parameter == Parameter; }))
    { auto& A = State->Allowed.AddDefaulted_GetRef(); A.Mesh = Subject; A.MID = MID; A.Parameter = Parameter; Retained.AddUnique(MID); }
    return true;
}

bool UMCNiagaraMaterialContextResources::AdoptPreparedScalarTarget(UMeshComponent* Subject,
    UMaterialInstanceDynamic* MID,FName Parameter,FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    if(!IsInGameThread()||!IsReadyForPlayback(GetWorld())||!SameWorld(Subject,GetWorld()))
    {Error=TEXT("Resident scalar handoff requires the same world's sealed current epoch.");return false;}
    const bool Needed=State->Pools.ContainsByPredicate([&](const auto& Pool){return Pool.Operation==EMCNiagaraMaterialContextOperation::ExistingMIDScalar&&Pool.ScalarName==Parameter;});
    if(!Needed)return true; // No prepared scalar role consumes this native field.
    float Baseline=0;auto* Parent=StaticParent(MID);
    if(!IsTargetPrepared(Subject)||!AssignedTo(Subject,MID)||!IsValid(MID)||MID->IsAsset()||!IsValid(Parent)
        ||Parameter.IsNone()||!ReadOwnScalar(MID,Parameter,Baseline))
    {Error=TEXT("Resident scalar handoff needs an exact prepared factory and an assigned native MID with its own finite override.");return false;}
    if(State->Allowed.ContainsByPredicate([&](const auto& A){return A.MID==MID&&A.Mesh!=Subject;})
        ||State->Allowed.ContainsByPredicate([&](const auto& A){return A.Mesh==Subject&&A.Parameter==Parameter&&A.MID!=MID;}))
    {Error=TEXT("Resident scalar handoff cannot steal a MID or replace an existing subject/scalar owner.");return false;}
    if(State->Allowed.ContainsByPredicate([&](const auto& A){return A.Mesh==Subject&&A.MID==MID&&A.Parameter==Parameter;}))return true;
    // Reuse the already prepared shader family, not just a matching parameter
    // name. The factory check above also compares the complete material layout.
    const bool KnownParent=State->Allowed.ContainsByPredicate([&](const auto& A){return A.Parameter==Parameter&&StaticParent(A.MID.Get())==Parent;});
    if(!KnownParent||State->Allowed.Num()>=256)
    {Error=TEXT("Resident scalar handoff has no prepared static parent or exceeds256 scoped bindings.");return false;}
    auto& Binding=State->Allowed.AddDefaulted_GetRef();Binding.Mesh=Subject;Binding.MID=MID;Binding.Parameter=Parameter;
    Retained.AddUnique(MID);
    // Only native identity/reference enrollment. Do not change its value or
    // create/recompile a material, texture, mesh, Niagara component or PSO.
    return true;
}

bool UMCNiagaraMaterialContextResources::SealResources(FString& Error)
{
    if (!IsInGameThread() || !IsCurrentScope())
    { Error = TEXT("Context resources have no live initialized scope."); return false; }
    if(!State->Pools.IsEmpty() && State->Factories.IsEmpty())
    {Error=TEXT("Material contexts require at least one explicitly prepared actual target factory.");return false;}
    for (const auto& P : State->Pools) {
        if (P.Operation == EMCNiagaraMaterialContextOperation::ExistingMeshOverlay) {
            for (const auto& MID : P.MIDs) if (!MID.IsValid() || MID->Parent != P.Material.Get())
            { Error = TEXT("Prepared overlay MID was invalidated; reprepare before playback."); return false; }
        }
        else if(!State->Allowed.ContainsByPredicate([&](const auto& A){return A.Parameter==P.ScalarName
            &&MCNiagaraMaterialContext::AssignedTo(A.Mesh.Get(),A.MID.Get());}))
        {Error=TEXT("Scalar context has no explicit prepared native MID/parameter binding.");return false;}
    }
    bSealed = true; return true;
}

bool UMCNiagaraMaterialContextResources::ReserveActivation(UNiagaraComponent* C, FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    if (!IsInGameThread() || !bSealed || !IsCurrentScope() || !SameWorld(C, GetWorld()) || C->IsActive())
    { Error = TEXT("Reserve context resources in the prepared world before Niagara activation."); return false; }
    // This is a pure reuse/schema check once sealed; it cannot allocate a new pool.
    if (!PrepareSystem(C->GetAsset(), 1, Error)) return false;
    TArray<uint64> Expired;
    for (const auto& L : State->Leases) if (!L.Niagara.IsValid() || !L.Client.IsValid()) Expired.Add(L.ID);
    for (uint64 ID : Expired) Release(ID);
    for (int32 I = State->Activations.Num() - 1; I >= 0; --I) if (!State->Activations[I].Niagara.IsValid())
    {
        for (const auto& R : State->Activations[I].Reservations)
        { State->Pools[R.Pool].ReservedBy[R.Slot].Reset(); State->Pools[R.Pool].Blocked[R.Slot] = false; }
        State->Activations.RemoveAt(I);
    }
    const auto* Existing = State->Activations.FindByPredicate([&](const auto& A){ return A.Niagara == C; });
    TArray<FMCNiagaraMaterialContextState::FReservation> Plan;
    auto* Mesh = Cast<UMeshComponent>(Read<USceneComponent>(C, SubjectInput));
    auto* TargetMID = Read<UMaterialInstanceDynamic>(C, MIDInput);
    for (int32 PIndex = 0; PIndex < State->Pools.Num(); ++PIndex)
    {
        const auto& P = State->Pools[PIndex]; if (P.System != C->GetAsset()) continue;
        if (!SameWorld(Mesh, GetWorld())) { Error = TEXT("Explicit VisualSubject must be an actual same-world mesh."); return false; }
        if(!IsTargetPrepared(Mesh)) {Error=TEXT("Actual material-context mesh factory was not prepared before the barrier.");return false;}
        if(P.Operation==EMCNiagaraMaterialContextOperation::ExistingMeshOverlay && Mesh->IsA<USkeletalMeshComponent>()
            &&(!P.Material.IsValid() || !P.Material->GetUsageByFlag(MATUSAGE_SkeletalMesh)))
        {Error=TEXT("This actual overlay/target pair lacks authored SkeletalMesh usage; runtime never compiles or changes material usage.");return false;}
        if (P.Operation == EMCNiagaraMaterialContextOperation::ExistingMIDScalar
            && !State->Allowed.ContainsByPredicate([&](const auto& A){ return A.Mesh == Mesh && A.MID == TargetMID && A.Parameter == P.ScalarName; }))
        { Error = TEXT("Explicit MaterialTarget/subject/scalar was not registered before the barrier."); return false; }
        const auto* ExistingSlot = Existing ? Existing->Reservations.FindByPredicate([&](const auto& R){ return R.Pool == PIndex; }) : nullptr;
        const int32 Free = ExistingSlot ? ExistingSlot->Slot : P.ReservedBy.IndexOfByPredicate([](const auto& Owner){ return !Owner.IsValid(); });
        if (Free == INDEX_NONE) { Error = TEXT("Prepared context capacity exhausted; no runtime MID allocation is allowed."); return false; }
        if (ExistingSlot && P.Blocked[Free])
        { Error = TEXT("Foreign writer owns this activation; explicitly release it before a new activation."); return false; }
        Plan.Add({PIndex, Free});
    }
    if (Plan.IsEmpty()) return true;
    const auto ResourceKey = Key<UMCNiagaraMaterialContextResources>(ResourcesInput);
    auto& Store = C->GetOverrideParameters();
    if (Store.IndexOf(ResourceKey) == INDEX_NONE) { Error = TEXT("Missing exact resource input."); return false; }
    for (const auto& R : Plan) State->Pools[R.Pool].ReservedBy[R.Slot] = C;
    if (!Existing) { auto& A = State->Activations.AddDefaulted_GetRef(); A.Niagara = C; A.Reservations = MoveTemp(Plan); }
    Store.SetUObject(this, ResourceKey); return true;
}

bool UMCNiagaraMaterialContextResources::Apply(UMCNiagaraMaterialContextComponent* C, uint64& ID, FString& Error)
{
    using namespace MCNiagaraMaterialContext;
    auto* Niagara = C ? Cast<UNiagaraComponent>(C->GetAttachParent()) : nullptr;
    if (!IsInGameThread() || !bSealed || !IsCurrentScope() || !SameWorld(Niagara, GetWorld())
        || Read<UMCNiagaraMaterialContextResources>(Niagara, ResourcesInput) != this
        || !Finite(C->Tint) || !FMath::IsFinite(C->Opacity) || !FMath::IsFinite(C->ScalarValue))
    { Error = TEXT("Material context lost its explicit prepared binding or finite inputs."); return false; }
    const auto* A = State->Activations.FindByPredicate([&](const auto& V){ return V.Niagara == Niagara; });
    const auto* R = A ? A->Reservations.FindByPredicate([&](const auto& V){ return State->Pools[V.Pool].Matches(C); }) : nullptr;
    if (!R) { Error = TEXT("Context renderer has no matching pre-activation reservation."); return false; }
    if (State->Pools[R->Pool].Blocked[R->Slot])
    { Error = TEXT("This context activation yielded to a foreign writer and cannot reacquire automatically."); return false; }
    auto* Mesh = Cast<UMeshComponent>(Read<USceneComponent>(Niagara, SubjectInput));
    auto* MID = C->Operation == EMCNiagaraMaterialContextOperation::ExistingMeshOverlay
        ? State->Pools[R->Pool].MIDs[R->Slot].Get() : Read<UMaterialInstanceDynamic>(Niagara, MIDInput);
    if (!SameWorld(Mesh, GetWorld()) || !IsValid(MID) || !IsTargetPrepared(Mesh)) { Error = TEXT("Explicit material target/factory is unavailable or unprepared."); return false; }
    if(C->Operation==EMCNiagaraMaterialContextOperation::ExistingMeshOverlay && Mesh->IsA<USkeletalMeshComponent>()
        &&!MID->GetUsageByFlag(MATUSAGE_SkeletalMesh))
    {Error=TEXT("Actual overlay cannot target this skeletal factory without prepared material usage.");return false;}
    if (C->Operation == EMCNiagaraMaterialContextOperation::ExistingMIDScalar
        && (!AssignedTo(Mesh, MID) || !State->Allowed.ContainsByPredicate([&](const auto& V){ return V.Mesh == Mesh && V.MID == MID && V.Parameter == C->ScalarParameter; })))
    { Error = TEXT("Scalar target is no longer the registered actual mesh MID."); return false; }
    auto* L = State->Leases.FindByPredicate([&](const auto& V){ return V.ID == ID && V.Client == C; });
    if (L && (L->Mesh != Mesh || L->MID != MID || L->Parameter != C->ScalarParameter))
    { Release(ID); ID = 0; L = nullptr; }
    if (!L)
    {
        if (State->Leases.ContainsByPredicate([&](const auto& V){ return V.Pool == R->Pool && V.Slot == R->Slot; }))
        { Error = TEXT("More than one live component requested a single context renderer reservation."); return false; }
        auto& New = State->Leases.AddDefaulted_GetRef();
        New.ID = NextLeaseID++; New.Client = C; New.Niagara = Niagara; New.Mesh = Mesh; New.MID = MID;
        New.Operation = C->Operation; New.Parameter = C->ScalarParameter; New.Priority = C->Priority; New.Pool = R->Pool; New.Slot = R->Slot;
        ID = New.ID; L = &New;
    }
    L->Tint = C->Tint; L->Opacity = FMath::Clamp(C->Opacity, 0.f, 1.f); L->Scalar = C->ScalarValue;
    int32 Group = State->Groups.IndexOfByPredicate([&](const auto& G){ return G.Contains(*L); });
    if (Group == INDEX_NONE)
    {
        auto& G = State->Groups.AddDefaulted_GetRef(); G.Mesh = Mesh; G.MID = MID; G.Operation = L->Operation; G.Parameter = L->Parameter;
        G.BaselineOverlay = Mesh->OverlayMaterial; if (G.BaselineOverlay.IsValid()) Retained.AddUnique(G.BaselineOverlay.Get());
        if (L->Operation == EMCNiagaraMaterialContextOperation::ExistingMIDScalar
            && !ReadOwnScalar(MID, L->Parameter, G.BaselineScalar))
        { State->Groups.Pop(); Release(ID); ID = 0; Error = TEXT("Existing scalar override was invalidated."); return false; }
        Group = State->Groups.Num() - 1;
    }
    Recompute(Group);
    if (State->Groups.IsValidIndex(Group) && State->Groups[Group].bContested)
    { Error = TEXT("A foreign material writer took ownership; this context will not overwrite it."); return false; }
    return true;
}

void UMCNiagaraMaterialContextResources::Recompute(int32 Index)
{
    if (!State->Groups.IsValidIndex(Index)) return;
    auto& G = State->Groups[Index];
    auto* Mesh = G.Mesh.Get(); auto* MID = G.MID.Get();
    const bool Overlay = G.Operation == EMCNiagaraMaterialContextOperation::ExistingMeshOverlay;
    if (G.bWritten && !G.bContested)
    {
        float Current = 0;
        G.bContested = Overlay ? (!IsValid(Mesh) || Mesh->OverlayMaterial != G.LastOverlay.Get())
            : (!MCNiagaraMaterialContext::ReadOwnScalar(MID, G.Parameter, Current) || Current != G.LastScalar);
    }
    if (G.bContested)
        for (const auto& L : State->Leases) if (G.Contains(L)) State->Pools[L.Pool].Blocked[L.Slot] = true;
    const FMCNiagaraMaterialContextState::FLease* Winner = nullptr;
    for (const auto& L : State->Leases) if (G.Contains(L) && L.Client.IsValid())
        if (!Winner || L.Priority > Winner->Priority || (L.Priority == Winner->Priority && L.ID > Winner->ID)) Winner = &L;
    if (!Winner)
    {
        if (G.bWritten && !G.bContested)
        {
            if (Overlay && IsValid(Mesh)) Mesh->SetOverlayMaterial(G.BaselineOverlay.Get());
            else if (!Overlay && IsValid(MID)) MID->SetScalarParameterValue(G.Parameter, G.BaselineScalar);
        }
        State->Groups.RemoveAt(Index); return;
    }
    if (G.bContested) return;
    if (Overlay)
    {
        auto* ActiveMID = Winner->MID.Get(); if (!IsValid(Mesh) || !IsValid(ActiveMID)) return;
        const auto& P = State->Pools[Winner->Pool];
        ActiveMID->SetVectorParameterValue(P.TintName, Winner->Tint);
        ActiveMID->SetScalarParameterValue(P.OpacityName, Winner->Opacity);
        if (Mesh->OverlayMaterial != ActiveMID) Mesh->SetOverlayMaterial(ActiveMID);
        G.LastOverlay = ActiveMID;
    }
    else
    {
        if (!IsValid(MID)) return;
        MID->SetScalarParameterValue(G.Parameter, Winner->Scalar); G.LastScalar = Winner->Scalar;
    }
    G.bWritten = true;
}

void UMCNiagaraMaterialContextResources::Release(uint64 ID)
{
    const int32 Index = State->Leases.IndexOfByPredicate([&](const auto& L){ return L.ID == ID; });
    if (Index == INDEX_NONE) return;
    const auto Copy = State->Leases[Index]; State->Leases.RemoveAt(Index);
    const int32 G = State->Groups.IndexOfByPredicate([&](const auto& V){ return V.Contains(Copy); });
    if (G != INDEX_NONE) Recompute(G);
}

void UMCNiagaraMaterialContextResources::ReleaseActivation(UNiagaraComponent* C)
{
    check(IsInGameThread());
    TArray<uint64> IDs; for (const auto& L : State->Leases) if (L.Niagara == C) IDs.Add(L.ID);
    for (uint64 ID : IDs) Release(ID);
    const int32 A = State->Activations.IndexOfByPredicate([&](const auto& V){ return V.Niagara == C; });
    if (A != INDEX_NONE)
    {
        for (const auto& R : State->Activations[A].Reservations)
        { State->Pools[R.Pool].ReservedBy[R.Slot].Reset(); State->Pools[R.Pool].Blocked[R.Slot] = false; }
        State->Activations.RemoveAt(A);
    }
    if (IsValid(C) && MCNiagaraMaterialContext::Read<UMCNiagaraMaterialContextResources>(C, MCNiagaraMaterialContext::ResourcesInput) == this)
        C->GetOverrideParameters().SetUObject(nullptr, MCNiagaraMaterialContext::Key<UMCNiagaraMaterialContextResources>(MCNiagaraMaterialContext::ResourcesInput));
}

void UMCNiagaraMaterialContextResources::Reset()
{
    check(IsInGameThread());
    while (!State->Activations.IsEmpty())
    {
        if (auto* C = State->Activations.Last().Niagara.Get()) ReleaseActivation(C);
        else State->Activations.Pop();
    }
    while (!State->Leases.IsEmpty()) Release(State->Leases.Last().ID);
    State = MakePimpl<FMCNiagaraMaterialContextState>(); Retained.Reset(); PreparedWorld.Reset(); PreparedEpoch.Invalidate(); bSealed = false;
}
void UMCNiagaraMaterialContextResources::GetPreparedResources(TArray<UObject*>& Out) const
{ for (const auto& Object : Retained) if (IsValid(Object.Get())) Out.AddUnique(Object.Get()); }

UMCNiagaraMaterialContextComponent::UMCNiagaraMaterialContextComponent()
{
    PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork; SetAutoActivate(false);
}
void UMCNiagaraMaterialContextComponent::SetTint(FLinearColor V)
{ Tint = V; if (IsActive()) ApplyContext(); }
void UMCNiagaraMaterialContextComponent::SetOpacity(float V)
{ Opacity = V; if (IsActive()) ApplyContext(); }
void UMCNiagaraMaterialContextComponent::SetScalarValue(float V)
{ ScalarValue = V; if (IsActive()) ApplyContext(); }
void UMCNiagaraMaterialContextComponent::Activate(bool bReset)
{ if (bReset) ReleaseContext(); Super::Activate(bReset); if (IsActive()) ApplyContext(); }
void UMCNiagaraMaterialContextComponent::Deactivate()
{ ReleaseContext(); Super::Deactivate(); }
void UMCNiagaraMaterialContextComponent::TickComponent(float D, ELevelTick T, FActorComponentTickFunction* F)
{ Super::TickComponent(D, T, F); if (IsActive()) ApplyContext(); }
void UMCNiagaraMaterialContextComponent::ApplyContext()
{
    using namespace MCNiagaraMaterialContext;
    auto* Niagara = Cast<UNiagaraComponent>(GetAttachParent());
    auto* Resources = Read<UMCNiagaraMaterialContextResources>(Niagara, ResourcesInput);
    if (!Niagara || !Resources || !IsVisible() || !Niagara->IsVisible() || !Niagara->GetRenderingEnabled()
        || (!bApplyWhilePaused && Niagara->IsPaused()))
    { ReleaseContext(); return; }
    if (LeaseResources.Get() != Resources) { ReleaseContext(); LeaseResources = Resources; }
    if (!Resources->Apply(this, LeaseID, LastError)) { ReleaseContext(); Super::Deactivate(); }
    else LastError.Reset();
}
void UMCNiagaraMaterialContextComponent::ReleaseContext()
{ if (auto* R = LeaseResources.Get(); R && LeaseID) R->Release(LeaseID); LeaseID = 0; LeaseResources.Reset(); }
void UMCNiagaraMaterialContextComponent::OnUnregister()
{ ReleaseContext(); Super::OnUnregister(); }
void UMCNiagaraMaterialContextComponent::OnComponentDestroyed(bool B)
{ ReleaseContext(); Super::OnComponentDestroyed(B); }
void UMCNiagaraMaterialContextComponent::EndPlay(const EEndPlayReason::Type R)
{ ReleaseContext(); Super::EndPlay(R); }
void UMCNiagaraMaterialContextComponent::OnAttachmentChanged()
{ ReleaseContext(); Super::OnAttachmentChanged(); }
void UMCNiagaraMaterialContextComponent::OnVisibilityChanged()
{ Super::OnVisibilityChanged(); if (!IsVisible()) ReleaseContext(); }
