#pragma once
#include "CoreMinimal.h"
#include "MCFreezeDeathSnapshot.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UNiagaraDataInterface;
class UMaterialInterface;
class USkeletalMeshComponent;
class USkeletalMesh;
class FSkinWeightVertexBuffer;
struct FNiagaraTypeDefinition;

/** One local fatal-freeze resource, not replicated and not a general NativeInputs payload.
 * Allocate/build before readiness; Seal once with the accepted CPU snapshot. */
class MESSCONTROL_API FMCFreezeCapturedGeometry final
{
public:
    static constexpr int32 MaxTriangles=32768;
    static constexpr int32 MaxSlots=8;
    static constexpr int32 Rows=6;
    static constexpr int32 Cells=36;
    bool Prepare(USkeletalMeshComponent* Source,FGuid Epoch,TConstArrayView<uint8> TriangleSlots,int32 Slots,FString& Error);
    bool Seal(const MCFreezeDeathSnapshot::FBuffer& Snapshot,USkeletalMeshComponent* Source,FGuid Epoch,FString& Error);
    bool ValidateAcceptedIdentity(const USkeletalMeshComponent* Source,FGuid Epoch,FString& Error) const;
    bool IsSealed() const {return bSealed;}
    int32 GetTriangleCount() const {return CellSlot.Num();}
    int32 GetSlotCount() const {return SlotCount;}
    int32 GetCount(int32 Slot) const {return Counts[Slot];}
    int32 GetOffset(int32 Slot) const {return Offsets[Slot];}
    TConstArrayView<FVector4f> GetRow(int32 Row) const {return MakeArrayView(Data[Row]);}
    TConstArrayView<int32> GetCells() const {return MakeArrayView(CellSlot);}
    const TArray<int32>& GetCellsArray() const {return CellSlot;}
    TConstArrayView<FVector4f> GetMotion(int32 Row) const {return MakeArrayView(Motion[Row]);}
    const FTransform& GetFrame() const {check(bSealed);return Frame;}
    FVector GetCubeCenter() const {check(bSealed);return CubeCenter;}
    float GetHalfSize() const {check(bSealed);return HalfSize;}
    double GetAcceptedAt() const {check(bSealed);return AcceptedAt;}
    const UWorld* GetWorld() const {return World.Get();}
    FGuid GetEpoch() const {return PreparedEpoch;}
    FVector GetPreparationOrigin() const {return PreparedOrigin;}
    SIZE_T GetAllocatedBytes() const;
private:
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<USkeletalMeshComponent> Subject;
    TWeakObjectPtr<USkeletalMesh> Appearance;
    TWeakObjectPtr<UMaterialInterface> SourceMaterials[MaxSlots];
    const void* RenderDataIdentity=nullptr;
    const FSkinWeightVertexBuffer* WeightIdentity=nullptr;
    FGuid PreparedEpoch;
    FVector PreparedOrigin=FVector::ZeroVector;
    int32 VertexCount=0,SlotCount=0;
    int32 Counts[MaxSlots]={},Offsets[MaxSlots]={};
    TArray<int32> Permutation,CellSlot;
    TArray<uint8> OriginalSlots;
    TArray<FVector4f> Data[Rows],Motion[3];
    FTransform Frame=FTransform::Identity;
    FVector CubeCenter=FVector::ZeroVector;
    float HalfSize=0;
    double AcceptedAt=0;
    bool bPrepared=false,bSealed=false;
};

/** Binding for one ALREADY CREATED component/layer. It never creates a component or DI.
 * Prime in loading, simulate actual particles and pause, then Commit once at fatal use.
 * Parent CompositionPlayer retains the component; this binding retains weak identities. */
class MESSCONTROL_API FMCPreparedFreezeGeometryLayer final
{
public:
    static bool IsCaptureName(FName Name);
    static bool FindType(FName Name,FNiagaraTypeDefinition& Out);
    static bool UsesCapture(const UNiagaraSystem* System);
    static bool ValidateDeclaredInputs(const UNiagaraSystem* System,FString& Error);
    static bool ValidateSystem(const UNiagaraSystem* System,FString& Error);
    bool Prime(UNiagaraComponent* Component,const TSharedRef<FMCFreezeCapturedGeometry>& Geometry,
        TConstArrayView<UMaterialInterface*> PreparedSlotMaterials,UMaterialInterface* PreparedGlass,FString& Error);
    /** Call after actual simulation produced N+37 particles, then paused. Not an RHI certificate. */
    bool VerifySimulatedAndPaused(FString& Error);
    bool ValidateAccepted(const FMCFreezeCapturedGeometry* Geometry,FString& Error);
    bool Commit(const TSharedRef<const FMCFreezeCapturedGeometry>& Geometry,float AgeSeconds,FString& Error,bool bResume=true);
    bool UpdateAge(float AgeSeconds,FString& Error);
    /** CPU data readiness only; not a draw/PSO or artistic acceptance certificate. */
    bool HasVisibleCapturedBody() const;
    void Reset();
private:
    bool ResolveAndValidateArrays(FString& Error,bool bRecordIdentity);
    bool WriteArrays(const FMCFreezeCapturedGeometry& Geometry,FString& Error);
    TWeakObjectPtr<UNiagaraComponent> Component;
    TWeakObjectPtr<UNiagaraSystem> System;
    TWeakObjectPtr<UNiagaraDataInterface> Interfaces[10];
    const void* ArrayMemory[10]={};
    int32 ArrayCapacity[10]={};
    TSharedPtr<const FMCFreezeCapturedGeometry> Packet;
    int32 Triangles=0,Slots=0;
    bool bPrimed=false,bSimulated=false,bCommitted=false;
};
