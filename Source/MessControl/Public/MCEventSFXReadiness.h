#pragma once

#include "CoreMinimal.h"
#include "ContentStreaming.h"
#include "MCEventSFX.h"
#include "MCEventSFXReadiness.generated.h"

class USoundWave;
class FSoundWaveData;

/** Populated from actual source graph/data. No guessed wave list for dynamic Cue/MetaSound inputs. */
USTRUCT()
struct MESSCONTROL_API FMCEventSFXSamplePlan
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<USoundBase> Source;
    UPROPERTY() TArray<TObjectPtr<USoundWave>> SampleWaves;
    /** Required for graph/procedural sources; true means the source provider actually audited all runtime-selected inputs. */
    UPROPERTY() bool bDependencyAuditComplete=false;
    /** Provider has prepared source-specific graph resources and dependency closure. No DSP/operator warmup claim. */
    UPROPERTY() bool bGraphPrepared=false;
};

UENUM()
enum class EMCEventSFXReadiness : uint8 { Idle,Preparing,Ready,OutputUnavailable,Failed };

/** Asset/Precache/startup-compressed-byte barrier. Never loads an asset or plays audio; does not predecode DSP buffers. */
UCLASS(Transient)
class MESSCONTROL_API UMCEventSFXReadiness : public UObject
{
    GENERATED_BODY()
public:
    bool Begin(UWorld* World,const TArray<FMCEventSFXSamplePlan>& Plans,FString& Error);
    void Poll();
    void Reset();
    bool CertifyPrepared(FMCPreparedEventSFX& Prepared,FString& Error) const;
    EMCEventSFXReadiness GetState() const {return State;}
    bool IsReady() const;
    int32 GetPendingWaveCount() const;
    const FString& GetDiagnostic() const {return Diagnostic;}
private:
    friend class FMCEventSFXResourceStateInvalidation;
    bool ArePlaybackResourcesCurrent() const;
    struct FWaveWork
    {
        TWeakObjectPtr<USoundWave> Wave;
        TSharedPtr<const FSoundWaveData> WaveData;
        FAudioChunkHandle StartupChunk;
        bool bChunkRequested=false,bReady=false;
    };
    void Fail(const FString& Error);
    UPROPERTY(Transient) TArray<FMCEventSFXSamplePlan> RetainedPlans;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> RetainedWaves;
    UPROPERTY(Transient) TWeakObjectPtr<UWorld> PreparedWorld;
    TArray<FWaveWork> Work;
    EMCEventSFXReadiness State=EMCEventSFXReadiness::Idle;
    double StartedAt=0;
    FString Diagnostic;
};
