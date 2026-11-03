#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "RHITypes.h"
#include "PackedNormal.h"
#include "UObject/GCObject.h"

/** Retained native final-geometry transport for the indexed Composer river layer.
 * Consumes the already computed native rendered vertices/normals without changing
 * collision, water math, pressure, gameplay clocks, or the general Native arrays cap.
 * Prepare/initial Upload must occur during location preparation, not first use.
 * Texture allocation/queued upload is NOT material/PSO/location readiness proof.
 */
class FMCOriginalCoffeeRiverVertexTextures final : public FGCObject
{
public:
    static constexpr int32 Side=128;
    static constexpr int32 MaxVertices=Side*Side;
    static constexpr int32 MaxTriangles=32768;

    bool Prepare(int32 InVertexCount,FString& Error)
    {
        if(!IsInGameThread()||PositionTexture||NormalTexture||InVertexCount<=0||InVertexCount>MaxVertices)
        {Error=TEXT("Prepare one new bounded river texture owner on the game thread.");return false;}
        VertexCount=InVertexCount;
        PositionTexture=UTexture2D::CreateTransient(Side,Side,PF_A32B32G32R32F);
        NormalTexture=UTexture2D::CreateTransient(Side,Side,PF_A32B32G32R32F);
        if(!PositionTexture||!NormalTexture){Error=TEXT("River float texture allocation failed.");return false;}
        for(auto* Texture:{PositionTexture.Get(),NormalTexture.Get()})
        {
            Texture->SRGB=false;Texture->NeverStream=true;Texture->Filter=TF_Nearest;
            Texture->AddressX=TA_Clamp;Texture->AddressY=TA_Clamp;
            Texture->UpdateResource();
        }
        return true;
    }

    bool Upload(TConstArrayView<FVector> Positions,TConstArrayView<FVector> Normals,FString& Error)
    {
        if(!IsInGameThread()||Positions.Num()!=VertexCount||Normals.Num()!=VertexCount
            ||!PositionTexture||!NormalTexture||!PositionTexture->GetResource()||!NormalTexture->GetResource())
        {Error=TEXT("River upload requires the exact prepared vertex count and allocated texture resources.");return false;}
        // Bound staging allocations to two complete frames; never silently use a
        // dropped/old geometry update as proof that the current source was accepted.
        if(State->PendingTextures.Load()>2){Error=TEXT("River render upload queue is bounded; caller retains native fallback.");return false;}
        TArray<FLinearColor> P,N;P.SetNumZeroed(MaxVertices);N.SetNumZeroed(MaxVertices);
        for(int32 I=0;I<VertexCount;++I)
        {
            if(Positions[I].ContainsNaN()||Normals[I].ContainsNaN()||Normals[I].IsNearlyZero())
            {Error=TEXT("River source contains invalid rendered position/normal.");return false;}
            // Native UProceduralMeshComponent also converts its render vertices
            // to float positions. Keep that same rendered payload, not new math.
            const FVector3f FP(Positions[I]),FN=FPackedNormal(Normals[I]).ToFVector3f();
            if(!FMath::IsFinite(FP.X)||!FMath::IsFinite(FP.Y)||!FMath::IsFinite(FP.Z))
            {Error=TEXT("River rendered float position is not finite.");return false;}
            P[I]=FLinearColor(FP.X,FP.Y,FP.Z,1);N[I]=FLinearColor(FN.X,FN.Y,FN.Z,1);
        }
        Queue(PositionTexture,P);Queue(NormalTexture,N);++SubmittedSerial;
        return true;
    }

    UTexture2D* GetPositionTexture()const{return PositionTexture;}
    UTexture2D* GetNormalTexture()const{return NormalTexture;}
    uint64 GetSubmittedSerial()const{return SubmittedSerial;}
    int32 GetPendingTextureUploads()const{return State->PendingTextures.Load();}
    void AddReferencedObjects(FReferenceCollector& Collector)override
    {Collector.AddReferencedObject(PositionTexture);Collector.AddReferencedObject(NormalTexture);}
    FString GetReferencerName()const override{return TEXT("FMCOriginalCoffeeRiverVertexTextures");}

private:
    struct FUploadState {TAtomic<int32> PendingTextures{0};};
    TSharedRef<FUploadState,ESPMode::ThreadSafe> State=MakeShared<FUploadState,ESPMode::ThreadSafe>();
    TObjectPtr<UTexture2D> PositionTexture=nullptr;
    TObjectPtr<UTexture2D> NormalTexture=nullptr;
    int32 VertexCount=0;
    uint64 SubmittedSerial=0;
    void Queue(UTexture2D* Texture,const TArray<FLinearColor>& Pixels)
    {
        static_assert(sizeof(FLinearColor)==16);
        auto* Region=new FUpdateTextureRegion2D(0,0,0,0,Side,Side);
        auto* Bytes=static_cast<uint8*>(FMemory::Malloc(MaxVertices*sizeof(FLinearColor)));
        FMemory::Memcpy(Bytes,Pixels.GetData(),MaxVertices*sizeof(FLinearColor));
        const auto CapturedState=State;++CapturedState->PendingTextures;
        // Public UE5.8 API owns the buffers through the RHI-thread cleanup.
        // Never recreate these textures while an activation owns this bridge.
        Texture->UpdateTextureRegions(0,1,Region,Side*sizeof(FLinearColor),sizeof(FLinearColor),Bytes,
            [CapturedState](uint8* Data,const FUpdateTextureRegion2D* Regions)
            {FMemory::Free(Data);delete Regions;--CapturedState->PendingTextures;});
    }
};
