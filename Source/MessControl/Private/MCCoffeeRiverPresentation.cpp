#include "MCCoffeeRiverPresentation.h"
#include "MCCoffeeRiverVertexTextures.h"
#include "MCTongue.h"
#include "MCGameState.h"
#include "MCWorldPreloadSubsystem.h"
#include "NiagaraComponent.h"
#include "RenderCommandFence.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterStore.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"

namespace MCCoffeeRiverBinding
{
const FName SourceName(TEXT("User.RiverSourceMesh")),MeshName(TEXT("User.RiverMesh")),MaterialName(TEXT("User.RiverMaterial")),OriginalName(TEXT("User.RiverOriginalMaterial"));
template<typename T>T* Default(const UNiagaraSystem* NS,FName Name)
{
    const FNiagaraVariable Key(FNiagaraTypeDefinition(T::StaticClass()),Name);
    const auto& Store=NS->GetExposedParameters();
    return Store.IndexOf(Key)==INDEX_NONE?nullptr:Cast<T>(Store.GetUObject(Key).Get());
}
bool EpochMatches(const UWorld* World,FGuid Epoch)
{
    if(!World||!Epoch.IsValid())return false;
    const auto* State=World->GetGameState<AMCGameState>();
    return State&&State->GameplayLoadId.IsValid()?State->GameplayLoadId==Epoch:World->GetOutermost()==GetTransientPackage();
}
// Section vertex order is the native Tongue order. The cloned renderer may
// reorder vertices, so its UV0 is the exact source-index lookup, not its index.
bool Topology(UStaticMesh* Source,UStaticMesh* Indexed,FString& Error)
{
    TArray<FVector> A,B,NA,NB;TArray<int32> IA,IB;TArray<FVector2D> UA,UB;TArray<FProcMeshTangent> TA,TB;
    UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Source,0,0,A,IA,NA,UA,TA);
    UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Indexed,0,0,B,IB,NB,UB,TB);
    if(A.IsEmpty()||A.Num()>FMCOriginalCoffeeRiverVertexTextures::MaxVertices||IA.Num()%3||IA.Num()/3>FMCOriginalCoffeeRiverVertexTextures::MaxTriangles
        ||IA.Num()!=IB.Num()||B.Num()!=UB.Num()) {Error=TEXT("River source/indexed mesh exceeds bounds or has different topology.");return false;}
    TArray<int32> Map;Map.Reserve(B.Num());TSet<int32> Seen;
    for(int32 I=0;I<B.Num();++I) {
        const int32 X=FMath::FloorToInt(UB[I].X*128),Y=FMath::FloorToInt(UB[I].Y*128),V=Y*128+X;
        if(X<0||X>=128||Y<0||Y>=128||!A.IsValidIndex(V)||!B[I].Equals(A[V],.001)
            ||!FMath::IsNearlyEqual(UB[I].X,(X+.5)/128.,1.e-6)||!FMath::IsNearlyEqual(UB[I].Y,(Y+.5)/128.,1.e-6))
        {Error=TEXT("River mesh lookup does not preserve original section positions/index centers.");return false;}
        Map.Add(V);Seen.Add(V);
    }
    auto Face=[](int32 A0,int32 B0,int32 C0) {if(B0<A0&&B0<C0)return FIntVector(B0,C0,A0);if(C0<A0&&C0<B0)return FIntVector(C0,A0,B0);return FIntVector(A0,B0,C0);};
    TMap<FIntVector,int32> Faces;
    for(int32 I=0;I<IA.Num();I+=3)++Faces.FindOrAdd(Face(IA[I],IA[I+1],IA[I+2]));
    for(int32 I=0;I<IB.Num();I+=3) {
        if(!Map.IsValidIndex(IB[I])||!Map.IsValidIndex(IB[I+1])||!Map.IsValidIndex(IB[I+2]))return false;
        auto* Count=Faces.Find(Face(Map[IB[I]],Map[IB[I+1]],Map[IB[I+2]]));if(!Count||*Count<=0)return false;--*Count;
    }
    if(Seen.Num()!=A.Num()){Error=TEXT("River clone omits original source vertices.");return false;}
    return true;
}
}

FMCCoffeeRiverPresentation::FMCCoffeeRiverPresentation():Textures(MakeUnique<FMCOriginalCoffeeRiverVertexTextures>()){}
FMCCoffeeRiverPresentation::~FMCCoffeeRiverPresentation(){for(const auto& C:RenderWarmups)if(IsValid(C))C->DestroyComponent();}
bool FMCCoffeeRiverPresentation::Uses(const UNiagaraSystem* NS)
{return NS&&NS->GetExposedParameters().ReadParameterVariables().ContainsByPredicate([](const auto& V){return V.GetName()==MCCoffeeRiverBinding::MeshName;});}
bool FMCCoffeeRiverPresentation::Prepare(UWorld* World,AMCTongue* Tongue,FGuid Epoch,const TArray<UNiagaraSystem*>& Systems,FString& Error)
{
    using namespace MCCoffeeRiverBinding;
    if(!World||!Tongue||Tongue->GetWorld()!=World||!Tongue->SourceMesh||!EpochMatches(World,Epoch))return false;
    PreparedWorld=World;PreparedTongue=Tongue;PreparedEpoch=Epoch;SourceMesh=Tongue->SourceMesh;
    for(auto* NS:Systems) {
        if(!Uses(NS))continue;
        auto* OriginalSource=Default<UStaticMesh>(NS,SourceName);auto* Mesh=Default<UStaticMesh>(NS,MeshName);
        auto* Material=Default<UMaterialInterface>(NS,MaterialName);auto* Original=Default<UMaterialInterface>(NS,OriginalName);
        if(OriginalSource!=SourceMesh)continue;
        if(!Mesh||!Material||!Original||!Topology(SourceMesh,Mesh,Error))return false;
        auto AddVariant=[&](UMaterialInterface* Draw,UMaterialInterface* Base)->bool {
            if(!Draw||!Base){Error=TEXT("River variant needs both exact typed original/draw material defaults.");return false;}
            if(const auto* Existing=Layers.FindByPredicate([&](const auto& L){return L.System==NS&&L.Original==Base;})) {
                if(Existing->Material->Parent!=Draw){Error=TEXT("The same river original has conflicting draw materials in one NS.");return false;}
                return true;
            }
            FLayer Layer;Layer.System=NS;Layer.Mesh=Mesh;Layer.Original=Base;
            Layer.Material=UMaterialInstanceDynamic::Create(Draw,World->GetWorldSettings());if(!Layer.Material)return false;
            Layers.Add(Layer);
            if(!NativeVariants.ContainsByPredicate([Base](const auto& V){return V.Original==Base;})) {
                FNativeVariant Variant;Variant.Original=Base;Variant.Material=UMaterialInstanceDynamic::Create(Base,World->GetWorldSettings());
                if(!Variant.Material)return false;NativeVariants.Add(Variant);
            }
            return true;
        };
        if(!AddVariant(Material,Original))return false;
        // Optional variants are reflected NS resources, scoped to this location.
        // Missing pairs retain the legacy single-original contract; half pairs fail.
        for(const TCHAR* Suffix:{TEXT("Water"),TEXT("Coffee"),TEXT("Cola")}) {
            const FName DrawName(*FString::Printf(TEXT("User.RiverMaterial%s"),Suffix));
            const FName BaseName(*FString::Printf(TEXT("User.RiverOriginalMaterial%s"),Suffix));
            auto* Draw=Default<UMaterialInterface>(NS,DrawName);auto* Base=Default<UMaterialInterface>(NS,BaseName);
            const bool bDeclared=NS->GetExposedParameters().ReadParameterVariables().ContainsByPredicate([&](const auto& V){return V.GetName()==DrawName||V.GetName()==BaseName;});
            if(bDeclared&&!AddVariant(Draw,Base))return false;
        }
    }
    if(Layers.IsEmpty()){Error=TEXT("No indexed river NS matches the actual location Tongue.SourceMesh.");return false;}
    const auto& Floor=Tongue->CurrentWorldVertices();TArray<FVector> Normals;Normals.Init(FVector::UpVector,Floor.Num());
    if(!Textures->Prepare(Floor.Num(),Error)||!Textures->Upload(Floor,Normals,Error))return false;
    Bounds=FBox(Floor).ExpandBy(500.f);
    for(auto& L:Layers) {
        L.Material->SetTextureParameterValue(TEXT("RiverPosition"),Textures->GetPositionTexture());
        L.Material->SetTextureParameterValue(TEXT("RiverNormal"),Textures->GetNormalTexture());
    }
    return true;
}
bool FMCCoffeeRiverPresentation::IsFor(const AMCTongue* Tongue)const{return PreparedTongue.Get()==Tongue;}
bool FMCCoffeeRiverPresentation::Claim(AActor* Source,UMaterialInterface* Original,FString& Error)
{
    using namespace MCCoffeeRiverBinding;
    if(!IsValid(Source)||Source->GetWorld()!=PreparedWorld.Get()||!EpochMatches(Source->GetWorld(),PreparedEpoch)
        ||(bConsumed&&ClaimedSource.Get()!=Source)||!Original)
    {Error=TEXT("River resource owner/world/epoch does not match its location preparation.");return false;}
    const auto* Dynamic=Cast<UMaterialInstanceDynamic>(Original);
    const auto* Selected=NativeVariants.FindByPredicate([&](const auto& V){return Original==V.Original||Original==V.Material||(Dynamic&&Dynamic->Parent==V.Original);});
    if(!Selected){Error=TEXT("The actual native river material has no exact prepared NS variant.");return false;}
    // A layered event must support the chosen original in EVERY river NS. Never
    // activate a Coffee-only co-layer with a Water material or silently drop it.
    for(const auto& Layer:Layers)if(!Layers.ContainsByPredicate([&](const auto& Candidate){return Candidate.System==Layer.System&&Candidate.Original==Selected->Original;}))
    {Error=TEXT("A river co-layer does not support the actual native liquid variant.");return false;}
    if(HasPendingUploads()||!bWarmupSubmitted||!RenderFence||!RenderFence->IsFenceComplete()){Error=TEXT("Exact river renderer/upload has not crossed preparation before source claim.");return false;}
    ClaimedSource=Source;ClaimedOriginal=Selected->Original;NativeMaterial=Selected->Material;bConsumed=true;return true;
}
void FMCCoffeeRiverPresentation::Retire(AActor* Source){if(ClaimedSource.Get()==Source)ClaimedSource.Reset();}
bool FMCCoffeeRiverPresentation::Upload(AActor* Source,TConstArrayView<FVector> V,TConstArrayView<FVector> N,UMaterialInstanceDynamic* Native,FString& Error)
{
    if(ClaimedSource.Get()!=Source||!Native||Native!=NativeMaterial||!MCCoffeeRiverBinding::EpochMatches(PreparedWorld.Get(),PreparedEpoch))return false;
    if(!Textures->Upload(V,N,Error))return false;
    Bounds.Init();for(const auto& Point:V)Bounds+=Point;Bounds=Bounds.ExpandBy(10.f);
    const FName Scalars[]={TEXT("WaterTime"),TEXT("RiverCycleTime"),TEXT("FillAmount"),TEXT("DrainAmount"),TEXT("JetAmount"),TEXT("Filling"),TEXT("RippleHeight"),TEXT("FrontRadius"),TEXT("FrontWidth"),TEXT("FrontHeight"),TEXT("RiverFront"),TEXT("RiverTail"),TEXT("RiverEdge")};
    const FName Vectors[]={TEXT("RiverDirection"),TEXT("RiverOrigin"),TEXT("ArenaSize"),TEXT("ArenaCenter")};
    for(auto& L:Layers)if(L.Original==ClaimedOriginal) {
        for(FName Name:Scalars)L.Material->SetScalarParameterValue(Name,Native->K2_GetScalarParameterValue(Name));
        for(FName Name:Vectors)L.Material->SetVectorParameterValue(Name,Native->K2_GetVectorParameterValue(Name));
    }
    return true;
}
bool FMCCoffeeRiverPresentation::Validate(const UNiagaraSystem* NS,const UWorld* World,FString& Error)const
{
    if(PreparedWorld.Get()!=World||!ClaimedSource.IsValid()||!MCCoffeeRiverBinding::EpochMatches(World,PreparedEpoch)
        ||!Layers.ContainsByPredicate([&](const auto& L){return L.System==NS&&L.Original==ClaimedOriginal;}))
    {Error=TEXT("River layer has no exact claimed retained mesh/material context.");return false;}
    return true;
}
bool FMCCoffeeRiverPresentation::Bind(UNiagaraComponent* C,FString& Error)const
{
    if(!C||!Validate(C->GetAsset(),C->GetWorld(),Error))return false;
    const auto* L=Layers.FindByPredicate([&](const auto& Value){return Value.System==C->GetAsset()&&Value.Original==ClaimedOriginal;});
    C->SetAbsolute(true,true,true);C->SetWorldTransform(FTransform::Identity);
    C->SetVariableStaticMesh(MCCoffeeRiverBinding::MeshName,L->Mesh);C->SetVariableMaterial(MCCoffeeRiverBinding::MaterialName,L->Material);
    C->SetSystemFixedBounds(Bounds);return true;
}
bool FMCCoffeeRiverPresentation::PollRendering(bool& Waiting,FString& Error)
{
    Waiting=true;Error.Reset();auto* World=PreparedWorld.Get();
    if(!World||!MCCoffeeRiverBinding::EpochMatches(World,PreparedEpoch)){Error=TEXT("River rendering belongs to another world/load epoch.");return false;}
    if(HasPendingUploads())return true;
    bool bSystemsWaiting=false;TSet<UNiagaraSystem*> CheckedSystems;
    for(const auto& L:Layers) {
#if WITH_EDITOR
        if(L.Mesh->IsCompiling()||L.Material->IsCompiling())return true;
#endif
        if(!IsValid(L.System.Get())){Error=TEXT("Prepared river Niagara source was invalidated.");return false;}
        if(CheckedSystems.Contains(L.System.Get()))continue;CheckedSystems.Add(L.System.Get());
#if WITH_EDITOR
        // Deferred post-load VM requests are not part of the global compiling
        // job count. River preparation runs before the general Niagara poll,
        // so flush/retrieve its own work without blocking the loading thread.
        L.System->PollForCompilationComplete(true);
#endif
        const bool bReady=L.System->IsReadyToRun();
#if WITH_EDITOR
        if(L.System->HasOutstandingCompilationRequests(true)){bSystemsWaiting=true;continue;}
#endif
        if(!L.System->IsValid()) {
            Error=FString::Printf(TEXT("River Niagara source is invalid: %s"),*L.System->GetPathName());return false;
        }
        // Readiness can also wait for Niagara's PSO fence. Keep the location
        // behind its existing bounded barrier until the source can really run.
        bSystemsWaiting|=!bReady;
    }
    if(bSystemsWaiting)return true;
#if WITH_EDITOR
    for(const auto& Variant:NativeVariants)if(Variant.Material->IsCompiling())return true;
#endif
    if(!bWarmupSubmitted) {
        for(const auto& L:Layers) {
            auto* C=NewObject<UMCCoffeeRiverNiagaraComponent>(World->GetWorldSettings(),NAME_None,RF_Transient);
            if(!C){Error=TEXT("River renderer preparation allocation failed.");return false;}
            RenderWarmups.Add(C);C->SetAutoActivate(false);C->SetAutoDestroy(false);C->SetAsset(L.System);
            C->SetVariableStaticMesh(MCCoffeeRiverBinding::MeshName,L.Mesh);C->SetVariableMaterial(MCCoffeeRiverBinding::MaterialName,L.Material);
            C->SetAbsolute(true,true,true);C->SetWorldTransform(FTransform::Identity);C->SetSystemFixedBounds(Bounds);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);
            C->SetVisibility(false);C->SetHiddenInGame(true);C->RegisterComponentWithWorld(World);C->SetComponentTickEnabled(false);
            // Public Initialize creates the controller needed by CollectPSOPrecacheData;
            // no Activate, manual tick, accepted event or gameplay owner is invoked.
            C->InitializeSystem();
            if(!C->GetSystemInstanceController().IsValid()){Error=TEXT("River renderer has no actual prepared system controller.");return false;}
            C->PrecachePSOs();
        }
        bWarmupSubmitted=true;RenderFence=MakeUnique<FRenderCommandFence>();RenderFence->BeginFence();return true;
    }
    for(const auto& C:RenderWarmups)if(!IsValid(C)){Error=TEXT("Prepared river renderer was released.");return false;}else if(C->IsPSOPrecaching())return true;
    if(!RenderFence||!RenderFence->IsFenceComplete())return true;
    Waiting=false;return true;
}
bool FMCCoffeeRiverPresentation::HasPendingUploads()const{return Textures->GetPendingTextureUploads()!=0;}
void FMCCoffeeRiverPresentation::GetResources(TArray<UObject*>& Out)const
{
    Out.AddUnique(SourceMesh);Out.AddUnique(Textures->GetPositionTexture());Out.AddUnique(Textures->GetNormalTexture());
    for(const auto& Variant:NativeVariants){Out.AddUnique(Variant.Original);Out.AddUnique(Variant.Material);}
    for(const auto& L:Layers){Out.AddUnique(L.System);Out.AddUnique(L.Mesh);Out.AddUnique(L.Original);Out.AddUnique(L.Material);}
}
void FMCCoffeeRiverPresentation::GetWarmups(TArray<TPair<UNiagaraSystem*,UMaterialInterface*>>& Out)const
{for(const auto& L:Layers)Out.Emplace(L.System.Get(),L.Material.Get());}
void FMCCoffeeRiverPresentation::AddReferencedObjects(FReferenceCollector& C)
{C.AddReferencedObject(SourceMesh);C.AddReferencedObject(NativeMaterial);C.AddReferencedObject(ClaimedOriginal);for(auto& Variant:NativeVariants){C.AddReferencedObject(Variant.Original);C.AddReferencedObject(Variant.Material);}for(auto& Warmup:RenderWarmups)C.AddReferencedObject(Warmup);for(auto& L:Layers){C.AddReferencedObject(L.System);C.AddReferencedObject(L.Mesh);C.AddReferencedObject(L.Original);C.AddReferencedObject(L.Material);}}
bool UMCCoffeeRiverPresentationResources::PrepareLocation(const TArray<UObject*>& Loaded,FGuid Epoch,bool Preview,FString& Error)
{
    auto* World=GetWorld();const FString Map=World?World->GetOutermost()->GetName():FString();
    if(!World||(!Preview&&!Map.Contains(TEXT("L_Mouth"))&&!Map.Contains(TEXT("L_Tutorial"))))return true;
    if(Scope.IsValid()&&Scope!=Epoch)Surfaces.Reset();Scope=Epoch;
    if(!Surfaces.IsEmpty())return true;
    TArray<UNiagaraSystem*> Systems;for(auto* O:Loaded)if(auto* NS=Cast<UNiagaraSystem>(O);FMCCoffeeRiverPresentation::Uses(NS))Systems.AddUnique(NS);
    if(Systems.IsEmpty())return true;
    int32 Count=0;for(TActorIterator<AMCTongue> It(World);It;++It)if(!It->CurrentVertices().IsEmpty()) {
        if(++Count>8){Error=TEXT("River location supports at most8 actual tongue surfaces.");return false;}
        auto P=MakeShared<FMCCoffeeRiverPresentation>();if(!P->Prepare(World,*It,Epoch,Systems,Error))return false;Surfaces.Add(P);
    }
    if(!Count){Error=TEXT("Indexed river source has no actual tongue before readiness.");return false;}return true;
}
TSharedPtr<FMCCoffeeRiverPresentation> UMCCoffeeRiverPresentationResources::Claim(AMCTongue* Tongue,AActor* Source,UMaterialInterface* Original,FString& Error)
{
    auto* World=GetWorld();const auto* Loading=World?World->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
    if(!World||(World->GetOutermost()!=GetTransientPackage()&&(!Loading||!Loading->IsReady())))
    {Error=TEXT("River source cannot claim resources before the actual location barrier.");return nullptr;}
    for(auto& P:Surfaces)if(P->IsFor(Tongue)&&P->Claim(Source,Original,Error))return P;return nullptr;
}
bool UMCCoffeeRiverPresentationResources::PollRendering(bool& Waiting,FString& Error)
{Waiting=false;for(const auto& P:Surfaces){bool Pending=false;if(!P->PollRendering(Pending,Error))return false;Waiting|=Pending;}return true;}
bool UMCCoffeeRiverPresentationResources::HasPendingUploads()const
{for(const auto& P:Surfaces)if(P->HasPendingUploads())return true;return false;}
void UMCCoffeeRiverPresentationResources::GetResources(TArray<UObject*>& Out)const{for(const auto& P:Surfaces)P->GetResources(Out);}
void UMCCoffeeRiverPresentationResources::GetWarmups(TArray<TPair<UNiagaraSystem*,UMaterialInterface*>>& Out)const{for(const auto& P:Surfaces)P->GetWarmups(Out);}
void UMCCoffeeRiverPresentationResources::ResetUnclaimed(){Surfaces.Reset();Scope.Invalidate();}
void UMCCoffeeRiverPresentationResources::Deinitialize(){ResetUnclaimed();Super::Deinitialize();}
