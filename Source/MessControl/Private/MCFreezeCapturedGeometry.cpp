#include "MCFreezeCapturedGeometry.h"
#include "MCGameState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraTypes.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace MCFreezeGeometryPrivate
{
constexpr TCHAR Prefix[]=TEXT("User.Native.FreezeCapture.");
FName RowName(int32 Index)
{
    // Only called in loading/schema validation; hot Commit uses precomputed names.
    static const FName Names[]={TEXT("User.Native.FreezeCapture.Row0"),TEXT("User.Native.FreezeCapture.Row1"),
        TEXT("User.Native.FreezeCapture.Row2"),TEXT("User.Native.FreezeCapture.Row3"),TEXT("User.Native.FreezeCapture.Row4"),
        TEXT("User.Native.FreezeCapture.Row5"),TEXT("User.Native.FreezeCapture.Motion0"),TEXT("User.Native.FreezeCapture.Motion1"),TEXT("User.Native.FreezeCapture.Motion2"),TEXT("User.Native.FreezeCapture.CellSlot")};
    return Names[Index];
}
FName CountName(int32 Slot)
{static const FName Names[]={TEXT("User.Native.FreezeCapture.Count0"),TEXT("User.Native.FreezeCapture.Count1"),TEXT("User.Native.FreezeCapture.Count2"),TEXT("User.Native.FreezeCapture.Count3"),TEXT("User.Native.FreezeCapture.Count4"),TEXT("User.Native.FreezeCapture.Count5"),TEXT("User.Native.FreezeCapture.Count6"),TEXT("User.Native.FreezeCapture.Count7")};return Names[Slot];}
FName OffsetName(int32 Slot)
{static const FName Names[]={TEXT("User.Native.FreezeCapture.Offset0"),TEXT("User.Native.FreezeCapture.Offset1"),TEXT("User.Native.FreezeCapture.Offset2"),TEXT("User.Native.FreezeCapture.Offset3"),TEXT("User.Native.FreezeCapture.Offset4"),TEXT("User.Native.FreezeCapture.Offset5"),TEXT("User.Native.FreezeCapture.Offset6"),TEXT("User.Native.FreezeCapture.Offset7")};return Names[Slot];}
FName MaterialName(int32 Slot)
{static const FName Names[]={TEXT("User.Native.FreezeCapture.Material0"),TEXT("User.Native.FreezeCapture.Material1"),TEXT("User.Native.FreezeCapture.Material2"),TEXT("User.Native.FreezeCapture.Material3"),TEXT("User.Native.FreezeCapture.Material4"),TEXT("User.Native.FreezeCapture.Material5"),TEXT("User.Native.FreezeCapture.Material6"),TEXT("User.Native.FreezeCapture.Material7")};return Names[Slot];}
bool CPUOnly(const UNiagaraSystem* NS)
{
    if(!NS||!NS->IsReadyToRun())return false;
    for(const auto& Handle:NS->GetEmitterHandles())if(Handle.GetIsEnabled())
    {const auto* Data=Handle.GetEmitterData();if(!Data||Data->SimTarget!=ENiagaraSimTarget::CPUSim)return false;}
    return true;
}
bool CurrentEpoch(const UWorld* World,FGuid Epoch)
{
    if(!World||!Epoch.IsValid()||(World->WorldType!=EWorldType::Game&&World->WorldType!=EWorldType::PIE))return false;
    if(const auto* State=World->GetGameState<AMCGameState>();State&&State->GameplayLoadId.IsValid())return State->GameplayLoadId==Epoch;
    // Exact private transient Game world created by Composer, never EditorPreview.
    return World->WorldType==EWorldType::Game&&World->GetOutermost()==GetTransientPackage()&&World->HasAnyFlags(RF_Transient);
}
}

bool FMCFreezeCapturedGeometry::Prepare(USkeletalMeshComponent* Source,FGuid Epoch,TConstArrayView<uint8> TriangleSlots,int32 Slots,FString& Error)
{
    if(bPrepared||!IsValid(Source)||!MCFreezeGeometryPrivate::CurrentEpoch(Source->GetWorld(),Epoch)||Slots<=0||Slots>MaxSlots
        ||TriangleSlots.IsEmpty()||TriangleSlots.Num()>MaxTriangles)
    {Error=TEXT("Freeze geometry preparation needs a finite actual appearance/epoch/slot topology.");return false;}
    auto* Asset=Source->GetSkeletalMeshAsset();const auto* Render=Asset?Asset->GetResourceForRendering():nullptr;
    auto* Weights=Source->GetSkinWeightBuffer(0);
    if(!Asset||!Render||Render->LODRenderData.IsEmpty()||!Weights||Source->GetNumMaterials()!=Slots)
    {Error=TEXT("Freeze preparation requires resident real LOD0, component weights and exact material slots.");return false;}
    const auto& LOD=Render->LODRenderData[0];
    if(LOD.GetNumVertices()<=0||LOD.GetNumVertices()>MCFreezeDeathSnapshot::MaxVertices)
    {Error=TEXT("Actual LOD0 render vertex count exceeds the bounded freeze sampler.");return false;}
    int32 OriginalTriangle=0;
    for(const auto& Section:LOD.RenderSections)
        for(uint32 I=0;I<Section.NumTriangles;++I)
            if(!TriangleSlots.IsValidIndex(OriginalTriangle)||TriangleSlots[OriginalTriangle++]!=Section.MaterialIndex)
            {Error=TEXT("Freeze partitions do not match actual complete source render sections.");return false;}
    if(OriginalTriangle!=TriangleSlots.Num()){Error=TEXT("Freeze preparation cannot omit or add an original body triangle.");return false;}
    for(uint8 Slot:TriangleSlots)if(Slot>=Slots){Error=TEXT("Freeze topology has an invalid material partition.");return false;}
    World=Source->GetWorld();Subject=Source;Appearance=Asset;RenderDataIdentity=Render;WeightIdentity=Weights;
    PreparedEpoch=Epoch;VertexCount=Render->LODRenderData[0].GetNumVertices();SlotCount=Slots;
    PreparedOrigin=Source->GetOwner()?Source->GetOwner()->GetActorLocation():Source->GetComponentLocation();
    OriginalSlots.Append(TriangleSlots);for(uint8 Slot:TriangleSlots)++Counts[Slot];
    for(int32 I=0;I<Slots;++I)SourceMaterials[I]=Source->GetMaterial(I);
    for(int32 I=1;I<MaxSlots;++I)Offsets[I]=Offsets[I-1]+Counts[I-1];
    Permutation.SetNumUninitialized(TriangleSlots.Num());int32 Write[MaxSlots]={};
    for(int32 I=0;I<MaxSlots;++I)Write[I]=Offsets[I];
    for(int32 I=0;I<TriangleSlots.Num();++I)Permutation[Write[TriangleSlots[I]]++]=I;
    for(auto& Row:Data)Row.SetNumZeroed(TriangleSlots.Num());CellSlot.SetNumZeroed(TriangleSlots.Num());
    for(auto& Row:Motion)Row.SetNumZeroed(Cells);
    // Slot membership is known before the pose/cell assignment. Every particle
    // exists already but Ready=false makes its artwork invisible.
    for(int32 Slot=0;Slot<Slots;++Slot)for(int32 I=0;I<Counts[Slot];++I)CellSlot[Offsets[Slot]+I]=Slot*Cells;
    bPrepared=true;return true;
}

bool FMCFreezeCapturedGeometry::ValidateAcceptedIdentity(const USkeletalMeshComponent* Source,FGuid Epoch,FString& Error) const
{
    const auto* Asset=Source?Source->GetSkeletalMeshAsset():nullptr;const auto* Render=Asset?Asset->GetResourceForRendering():nullptr;
    if(!bPrepared||!IsValid(Source)||Subject.Get()!=Source||World.Get()!=Source->GetWorld()||PreparedEpoch!=Epoch
        ||!MCFreezeGeometryPrivate::CurrentEpoch(Source->GetWorld(),Epoch)||Appearance.Get()!=Asset||RenderDataIdentity!=Render||!Render||Render->LODRenderData.IsEmpty()
        ||int32(Render->LODRenderData[0].GetNumVertices())!=VertexCount||Source->GetSkinWeightBuffer(0)!=WeightIdentity
        ||Source->GetNumMaterials()!=SlotCount)
    {Error=TEXT("Freeze source/world/epoch/appearance/LOD/weight/slot identity changed after readiness.");return false;}
    for(int32 I=0;I<SlotCount;++I)if(SourceMaterials[I].Get()!=Source->GetMaterial(I))
    {Error=TEXT("Freeze source material identity changed after readiness; reprepare exact artwork.");return false;}
    return true;
}

bool FMCFreezeCapturedGeometry::Seal(const MCFreezeDeathSnapshot::FBuffer& Snapshot,USkeletalMeshComponent* Source,FGuid Epoch,FString& Error)
{
    if(bSealed||!ValidateAcceptedIdentity(Source,Epoch,Error))return false;
    const auto Triangles=Snapshot.GetTriangles();const auto Shards=Snapshot.GetShards();
    if(!Snapshot.IsSealed()||Triangles.Num()!=CellSlot.Num()||Shards.Num()!=Cells||Snapshot.GetSlotCount()!=SlotCount)
    {Error=TEXT("Freeze capture is not the exact prepared immutable geometry.");return false;}
    const auto& AcceptedFrame=Snapshot.GetEffectFrame();
    if(AcceptedFrame.ContainsNaN()||!AcceptedFrame.GetRotation().IsNormalized()||AcceptedFrame.GetScale3D().GetAbsMin()<.0001
        ||!AcceptedFrame.GetScale3D().Equals(FVector::OneVector,.000001)
        ||Snapshot.GetCubeCenter().ContainsNaN()||!FMath::IsFinite(Snapshot.GetHalfSize())||Snapshot.GetHalfSize()<=0)
    {Error=TEXT("Freeze capture frame/cube has an invalid accepted transform.");return false;}
    // Validate all rows BEFORE mutating the packet; Source snapshot performed
    // finite position/normal/UV and exact36 native motion checks already.
    for(int32 I=0;I<Triangles.Num();++I)if(Triangles[I].Slot!=OriginalSlots[I]||Triangles[I].Cell>=Cells
        ||Triangles[I].A.GetAbsMax()>2048||Triangles[I].B.GetAbsMax()>2048||Triangles[I].C.GetAbsMax()>2048)
    {Error=TEXT("Freeze snapshot material/topology/cell membership differs from preparation.");return false;}
    for(int32 I=0;I<Permutation.Num();++I)
    {
        const auto& T=Triangles[Permutation[I]];
        Data[0][I]=FVector4f(T.A,T.UA.X);Data[1][I]=FVector4f(T.B,T.UA.Y);Data[2][I]=FVector4f(T.C,T.UB.X);
        Data[3][I]=FVector4f(T.NA,T.UB.Y);Data[4][I]=FVector4f(T.NB,T.UC.X);Data[5][I]=FVector4f(T.NC,T.UC.Y);
        CellSlot[I]=int32(T.Slot)*Cells+T.Cell;
    }
    for(int32 I=0;I<Cells;++I)
    {const auto& M=Shards[I];Motion[0][I]=FVector4f(FVector3f(M.Home),M.Floor);Motion[1][I]=FVector4f(FVector3f(M.Velocity),0);Motion[2][I]=FVector4f(FVector3f(M.Spin),0);}
    Frame=Snapshot.GetEffectFrame();CubeCenter=Snapshot.GetCubeCenter();HalfSize=Snapshot.GetHalfSize();AcceptedAt=Snapshot.GetAcceptedAt();bSealed=true;return true;
}

SIZE_T FMCFreezeCapturedGeometry::GetAllocatedBytes() const
{SIZE_T N=Permutation.GetAllocatedSize()+CellSlot.GetAllocatedSize()+OriginalSlots.GetAllocatedSize();for(const auto& R:Data)N+=R.GetAllocatedSize();for(const auto& R:Motion)N+=R.GetAllocatedSize();return N;}

bool FMCPreparedFreezeGeometryLayer::IsCaptureName(FName Name)
{return Name.ToString().StartsWith(MCFreezeGeometryPrivate::Prefix,ESearchCase::IgnoreCase);}
bool FMCPreparedFreezeGeometryLayer::FindType(FName Name,FNiagaraTypeDefinition& Out)
{
    using namespace MCFreezeGeometryPrivate;
    for(int32 I=0;I<9;++I)if(Name==RowName(I)){Out=FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat4::StaticClass());return true;}
    if(Name==RowName(9)){Out=FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayInt32::StaticClass());return true;}
    if(Name==TEXT("User.Native.FreezeCapture.Ready")){Out=FNiagaraTypeDefinition::GetBoolDef();return true;}
    if(Name==TEXT("User.Native.FreezeCapture.Version")){Out=FNiagaraTypeDefinition::GetIntDef();return true;}
    if(Name==TEXT("User.Native.FreezeCapture.Age")||Name==TEXT("User.Native.FreezeCapture.HalfSize")){Out=FNiagaraTypeDefinition::GetFloatDef();return true;}
    if(Name==TEXT("User.Native.FreezeCapture.FrameOrigin")){Out=FNiagaraTypeDefinition::GetPositionDef();return true;}
    if(Name==TEXT("User.Native.FreezeCapture.FrameRotation")){Out=FNiagaraTypeDefinition::GetQuatDef();return true;}
    if(Name==TEXT("User.Native.FreezeCapture.CubeCenter")||Name==TEXT("User.Native.FreezeCapture.FrameScale")){Out=FNiagaraTypeDefinition::GetVec3Def();return true;}
    if(Name==TEXT("User.Native.FreezeCapture.Glass")){Out=FNiagaraTypeDefinition(UMaterialInterface::StaticClass());return true;}
    for(int32 I=0;I<FMCFreezeCapturedGeometry::MaxSlots;++I)
    {
        if(Name==CountName(I)||Name==OffsetName(I)){Out=FNiagaraTypeDefinition::GetIntDef();return true;}
        if(Name==MaterialName(I)){Out=FNiagaraTypeDefinition(UMaterialInterface::StaticClass());return true;}
    }
    return false;
}
bool FMCPreparedFreezeGeometryLayer::UsesCapture(const UNiagaraSystem* NS)
{
    if(!NS)return false;TArray<FNiagaraVariable> Vars;NS->GetExposedParameters().GetParameters(Vars);
    for(const auto& V:Vars)if(IsCaptureName(V.GetName()))return true;return false;
}
bool FMCPreparedFreezeGeometryLayer::ValidateSystem(const UNiagaraSystem* NS,FString& Error)
{
    if(!MCFreezeGeometryPrivate::CPUOnly(NS)||!NS->SupportsLargeWorldCoordinates()){Error=TEXT("Captured freeze graph requires ready LWC stateful CPU emitters; stateless/GPU is unsupported.");return false;}
    return ValidateDeclaredInputs(NS,Error);
}

bool FMCPreparedFreezeGeometryLayer::ValidateDeclaredInputs(const UNiagaraSystem* NS,FString& Error)
{
    using namespace MCFreezeGeometryPrivate;
    if(!NS){Error=TEXT("Freeze declaration needs a loaded Niagara System.");return false;}
    TArray<FNiagaraVariable> Vars;NS->GetExposedParameters().GetParameters(Vars);
    for(const auto& V:Vars)if(IsCaptureName(V.GetName()))
    {FNiagaraTypeDefinition Expected;if(!FindType(V.GetName(),Expected)||V.GetType()!=Expected){Error=TEXT("Unknown or mistyped reserved freeze capture input: ")+V.GetName().ToString();return false;}}
    auto Has=[&](FName Name){FNiagaraTypeDefinition T;return FindType(Name,T)&&NS->GetExposedParameters().IndexOf(FNiagaraVariable(T,Name))!=INDEX_NONE;};
    for(int32 I=0;I<10;++I)if(!Has(RowName(I))){Error=TEXT("Freeze graph must declare every prepared data interface.");return false;}
    for(const auto* Name:{TEXT("Ready"),TEXT("Version"),TEXT("Age"),TEXT("HalfSize"),TEXT("FrameOrigin"),TEXT("FrameRotation"),TEXT("FrameScale"),TEXT("CubeCenter"),TEXT("Glass")})
        if(!Has(FName(*(FString(Prefix)+Name)))){Error=TEXT("Freeze graph is missing a required typed control.");return false;}
    for(int32 I=0;I<FMCFreezeCapturedGeometry::MaxSlots;++I)if(!Has(CountName(I))||!Has(OffsetName(I))||!Has(MaterialName(I)))
    {Error=TEXT("Freeze graph must declare the eight bounded original material partitions.");return false;}
    return true;
}

bool FMCPreparedFreezeGeometryLayer::ResolveAndValidateArrays(FString& Error,bool bRecord)
{
    auto* C=Component.Get();if(!C||C->GetAsset()!=System.Get()){Error=TEXT("Prepared freeze component/system identity changed.");return false;}
    for(int32 I=0;I<10;++I)
    {
        UClass* Class=I==9?UNiagaraDataInterfaceArrayInt32::StaticClass():UNiagaraDataInterfaceArrayFloat4::StaticClass();
        auto* DI=UNiagaraFunctionLibrary::GetDataInterface(Class,C,MCFreezeGeometryPrivate::RowName(I));
        if(!DI||DI->GetClass()!=Class||DI->GetOuter()!=C||DI->IsUsedWithGPUScript())
        {Error=TEXT("Freeze arrays require exact unique component-owned CPU interfaces.");return false;}
        int32 Num,Capacity;const void* Memory;
        if(I==9){const auto& A=CastChecked<UNiagaraDataInterfaceArrayInt32>(DI)->GetArrayReference();Num=A.Num();Capacity=A.Max();Memory=A.GetData();}
        else{const auto& A=CastChecked<UNiagaraDataInterfaceArrayFloat4>(DI)->GetArrayReference();Num=A.Num();Capacity=A.Max();Memory=A.GetData();}
        const int32 Expected=I>=6&&I<9?FMCFreezeCapturedGeometry::Cells:Triangles;
        if(!bRecord&&(Interfaces[I]!=DI||Num!=Expected||ArrayCapacity[I]!=Capacity||ArrayMemory[I]!=Memory))
        {Error=TEXT("Freeze array identity/length/capacity/storage changed after priming.");return false;}
        if(bRecord){Interfaces[I]=DI;ArrayCapacity[I]=Capacity;ArrayMemory[I]=Memory;}
    }
    return true;
}
bool FMCPreparedFreezeGeometryLayer::WriteArrays(const FMCFreezeCapturedGeometry& Geometry,FString& Error)
{
    if(!ResolveAndValidateArrays(Error,false))return false;
    for(int32 I=0;I<9;++I)
    {
        // Supported Game/PIE only: stock setter's Editor-only duplication branch
        // is false. Exact component-owned CPU DI, fixed length/capacity/storage.
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component.Get(),MCFreezeGeometryPrivate::RowName(I),I<6?Geometry.GetRow(I):Geometry.GetMotion(I-6));
    }
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(Component.Get(),MCFreezeGeometryPrivate::RowName(9),Geometry.GetCellsArray());
    return ResolveAndValidateArrays(Error,false);
}

bool FMCPreparedFreezeGeometryLayer::Prime(UNiagaraComponent* C,const TSharedRef<FMCFreezeCapturedGeometry>& Geometry,
    TConstArrayView<UMaterialInterface*> Materials,UMaterialInterface* Glass,FString& Error)
{
    if(bPrimed||!IsInGameThread()||!IsValid(C)||C->IsActive()||C->GetWorld()!=Geometry->GetWorld()
        ||Geometry->IsSealed()||Materials.Num()!=Geometry->GetSlotCount()||!IsValid(Glass)||!ValidateSystem(C->GetAsset(),Error))return false;
    for(auto* M:Materials)if(!IsValid(M)){Error=TEXT("Exact frozen slot MID/material was not retained during loading.");return false;}
    Component=C;System=C->GetAsset();Packet=Geometry;Triangles=Geometry->GetTriangleCount();Slots=Geometry->GetSlotCount();
    if(!ResolveAndValidateArrays(Error,true))return false;
    // Allocate fixed owner arrays here ONLY. Never create/resize them in Commit.
    for(int32 I=0;I<9;++I)UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(C,MCFreezeGeometryPrivate::RowName(I),I<6?Geometry->GetRow(I):Geometry->GetMotion(I-6));
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(C,MCFreezeGeometryPrivate::RowName(9),Geometry->GetCellsArray());
    if(!ResolveAndValidateArrays(Error,true))return false;
    if(!ResolveAndValidateArrays(Error,false))return false;
    for(int32 I=0;I<FMCFreezeCapturedGeometry::MaxSlots;++I)
    {C->SetVariableInt(MCFreezeGeometryPrivate::CountName(I),Geometry->GetCount(I));C->SetVariableInt(MCFreezeGeometryPrivate::OffsetName(I),Geometry->GetOffset(I));
     C->SetVariableMaterial(MCFreezeGeometryPrivate::MaterialName(I),I<Materials.Num()?Materials[I]:nullptr);}
    C->SetVariableMaterial(TEXT("User.Native.FreezeCapture.Glass"),Glass);
    C->SetVariableInt(TEXT("User.Native.FreezeCapture.Version"),1);C->SetVariableBool(TEXT("User.Native.FreezeCapture.Ready"),false);
    C->SetVariableFloat(TEXT("User.Native.FreezeCapture.Age"),0);C->SetVariableFloat(TEXT("User.Native.FreezeCapture.HalfSize"),70);
    C->SetVariablePosition(TEXT("User.Native.FreezeCapture.FrameOrigin"),FVector::ZeroVector);
    C->SetVariableQuat(TEXT("User.Native.FreezeCapture.FrameRotation"),FQuat::Identity);
    C->SetVariableVec3(TEXT("User.Native.FreezeCapture.FrameScale"),FVector::OneVector);
    C->SetVariableVec3(TEXT("User.Native.FreezeCapture.CubeCenter"),FVector::ZeroVector);bPrimed=true;return true;
}
bool FMCPreparedFreezeGeometryLayer::VerifySimulatedAndPaused(FString& Error)
{
    auto* C=Component.Get();if(!bPrimed||bCommitted||!C||!C->IsActive()||!C->IsPaused()||!ResolveAndValidateArrays(Error,false))return false;
    const auto Controller=C->GetSystemInstanceController();if(Controller.IsValid())Controller->WaitForConcurrentTickAndFinalize();
    const auto* Instance=Controller.IsValid()?Controller->GetSystemInstance_Unsafe():nullptr;
    if(!Instance){Error=TEXT("Freeze prewarm did not create an actual CPU Niagara system instance.");return false;}
    int32 Count=0;for(const auto& E:Instance->GetEmitters())Count+=E->GetNumParticles();
    if(Count!=Triangles+37){Error=TEXT("Freeze prewarm did not simulate the exact real triangle count plus cube/36glass particles.");return false;}
    bSimulated=true;return true;
}
bool FMCPreparedFreezeGeometryLayer::ValidateAccepted(const FMCFreezeCapturedGeometry* Geometry,FString& Error)
{
    const auto* C=Component.Get();
    if(!bSimulated||bCommitted||!C||!C->IsActive()||!C->IsPaused()||Packet.Get()!=Geometry||!Geometry||!Geometry->IsSealed()
        ||C->GetWorld()!=Geometry->GetWorld()||!MCFreezeGeometryPrivate::CurrentEpoch(C->GetWorld(),Geometry->GetEpoch())
        ||C->GetAsset()!=System.Get()||!C->GetComponentQuat().Equals(FQuat::Identity)||!C->GetComponentScale().Equals(FVector::OneVector)
        ||C->GetComponentLocation()!=Geometry->GetPreparationOrigin())
    {Error=TEXT("Freeze admission needs the exact sealed packet/world and paused unit-axis layer at its prepared LWC origin.");return false;}
    const auto Controller=C->GetSystemInstanceController();const auto* Instance=Controller.IsValid()?Controller->GetSystemInstance_Unsafe():nullptr;
    if(!Instance){Error=TEXT("Prepared freeze Niagara instance disappeared.");return false;}
    const FVector ConverterOrigin=Instance->GetLWCConverter().ConvertSimulationPositionToWorld(FNiagaraPosition(FVector3f::ZeroVector));
    const FVector RequiredOrigin=FVector(FLargeWorldRenderScalar::GetTileFor(Geometry->GetFrame().GetLocation()))*FLargeWorldRenderScalar::GetTileSize();
    if(ConverterOrigin!=RequiredOrigin)
    {Error=TEXT("Freeze crossed the prepared LWC tile; native fallback preserves coordinates without a first-use reset.");return false;}
    return ResolveAndValidateArrays(Error,false)&&VerifySimulatedAndPaused(Error);
}
bool FMCPreparedFreezeGeometryLayer::Commit(const TSharedRef<const FMCFreezeCapturedGeometry>& Geometry,float AgeSeconds,FString& Error,bool bResume)
{
    if(!FMath::IsFinite(AgeSeconds)||AgeSeconds<0||!ValidateAccepted(&Geometry.Get(),Error)||!WriteArrays(Geometry.Get(),Error))return false;
    auto* C=Component.Get();const auto& Frame=Geometry->GetFrame();
    C->SetVariablePosition(TEXT("User.Native.FreezeCapture.FrameOrigin"),Frame.GetLocation());
    C->SetVariableQuat(TEXT("User.Native.FreezeCapture.FrameRotation"),Frame.GetRotation());C->SetVariableVec3(TEXT("User.Native.FreezeCapture.FrameScale"),Frame.GetScale3D());
    C->SetVariableVec3(TEXT("User.Native.FreezeCapture.CubeCenter"),Geometry->GetCubeCenter());C->SetVariableFloat(TEXT("User.Native.FreezeCapture.HalfSize"),Geometry->GetHalfSize());
    C->SetVariableFloat(TEXT("User.Native.FreezeCapture.Age"),AgeSeconds);C->SetVariableBool(TEXT("User.Native.FreezeCapture.Ready"),true);
    Packet=Geometry;bCommitted=true;if(bResume)C->SetPaused(false);return true;
}
bool FMCPreparedFreezeGeometryLayer::UpdateAge(float AgeSeconds,FString& Error)
{
    auto* C=Component.Get();if(!bCommitted||!C||C->GetWorld()!=Packet->GetWorld()
        ||!MCFreezeGeometryPrivate::CurrentEpoch(C->GetWorld(),Packet->GetEpoch())||!FMath::IsFinite(AgeSeconds)||AgeSeconds<0)
    {Error=TEXT("Freeze age update does not match its committed local resource.");return false;}
    C->SetVariableFloat(TEXT("User.Native.FreezeCapture.Age"),AgeSeconds);return true;
}
void FMCPreparedFreezeGeometryLayer::Reset()
{Component.Reset();System.Reset();for(auto& DI:Interfaces)DI.Reset();Packet.Reset();bPrimed=bSimulated=bCommitted=false;Triangles=Slots=0;}

bool FMCPreparedFreezeGeometryLayer::HasVisibleCapturedBody() const
{
    UNiagaraComponent* C=Component.Get();if(!bCommitted||!C||!C->IsActive()||C->IsPaused()||C->GetAsset()!=System.Get())return false;
    const auto Controller=C->GetSystemInstanceController();if(!Controller.IsValid())return false;
    Controller->WaitForConcurrentTickAndFinalize();const auto* Instance=Controller->GetSystemInstance_Unsafe();if(!Instance)return false;
    int32 Seen=0;
    for(const auto& E:Instance->GetEmitters()) {
        const int32 Count=E->GetNumParticles();Seen+=Count;if(Count<0||Seen>FMCFreezeCapturedGeometry::MaxTriangles+37)return false;
        if(!Count)continue;
        const auto& Data=E->GetParticleData();
        const auto Scale=FNiagaraDataSetAccessor<FVector3f>::CreateReader(Data,TEXT("Particles.Scale"));
        const auto Cell=FNiagaraDataSetAccessor<int32>::CreateReader(Data,TEXT("Particles.FreezeCellSlot"));
        if(!Scale.IsValid()||!Cell.IsValid())continue; // Glass is not body geometry.
        for(int32 I=0;I<Count;++I) {
            const int32 Slot=Cell.GetSafe(I,-1);const FVector3f S=Scale.GetSafe(I,FVector3f::ZeroVector);
            if(Slot>=0&&Slot<Slots*36&&!S.ContainsNaN()&&S.GetAbsMax()>UE_SMALL_NUMBER)return true;
        }
    }
    return false;
}
