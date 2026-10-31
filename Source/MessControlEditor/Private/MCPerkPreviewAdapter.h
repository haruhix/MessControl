#pragma once

#include "CoreMinimal.h"

class AActor;
class ACameraActor;
class APlayerController;
class AStaticMeshActor;
class AMCActiveSkillAction;
class AMCPlayerState;
class AMCToothCharacter;
class UWorld;
class UNiagaraSystem;
struct FMCPerkDefinition;

enum class EMCPerkPreviewScenario : uint8
{
    Target,
    NearWall,
    Timeout
};

/** Only the editor-owned preview world is supplied; never PIE or the editor map. */
struct FMCPerkPreviewContext
{
    UWorld* World=nullptr;
    AMCToothCharacter* Hero=nullptr;
    APlayerController* Controller=nullptr;
    AMCPlayerState* PlayerState=nullptr;
    ACameraActor* AimCamera=nullptr;
    AMCActiveSkillAction* Action=nullptr;
    const FMCPerkDefinition* Definition=nullptr;
    FName PerkID;
    EMCPerkPreviewScenario Scenario=EMCPerkPreviewScenario::Target;
    float Elapsed=0;
    TArray<TWeakObjectPtr<AActor>> Targets;
    TFunction<AStaticMeshActor*(FName,FVector,FVector)> AddBox;
    TFunction<void(FName)> RecordEvent;
    TFunction<void(const FString&)> ReportStatus;
};

/** Scenario adapters drive actual mechanics/input; they do not fabricate cue timelines. */
class IMCPerkPreviewAdapter
{
public:
    virtual ~IMCPerkPreviewAdapter()=default;
    /** Set up targets/surface/input before the standard perk factory prepares its action. */
    virtual bool Prepare(FMCPerkPreviewContext& Context,FString& Error)=0;
    virtual bool ValidateAction(const AMCActiveSkillAction& Action,FString& Error) const {return true;}
    virtual void OnCommitted(FMCPerkPreviewContext& Context) {}
    virtual void Tick(FMCPerkPreviewContext& Context,float DeltaSeconds)=0;
    /** Release the real input/action; the adapter decides when its scenario releases. */
    virtual void Release(FMCPerkPreviewContext& Context);
    virtual void Cancel(FMCPerkPreviewContext& Context);
    virtual void Reset(FMCPerkPreviewContext& Context) {}
    /** Passive scenarios have no action actor; they explicitly report completion. */
    virtual bool IsComplete(const FMCPerkPreviewContext& Context) const {return false;}
    virtual FString GetPhaseName(const FMCPerkPreviewContext& Context) const=0;
    virtual float GetTailSeconds() const {return 3.f;}
};

enum class EMCPerkPreviewAdapterKind : uint8 {ActiveSkill,Passive,Context};

struct FMCPerkPreviewAdapterEntry
{
    FName PerkID;
    /** Optional row indirection for context providers; empty Context entries need no DT row. */
    FName SourceRowID;
    FText Label;
    TArray<FName> Stages;
    TMap<FName,FText> StageLabels;
    /** Only real defaults read from the native cue owner; absence means no default. */
    TMap<FName,TSoftObjectPtr<UNiagaraSystem>> DefaultSystems;
    TArray<EMCPerkPreviewScenario> Scenarios;
    /** Context-specific names for the real supported scenarios; no UI inference. */
    TMap<EMCPerkPreviewScenario,FText> ScenarioLabels;
    EMCPerkPreviewAdapterKind Kind=EMCPerkPreviewAdapterKind::ActiveSkill;
    /** False when current runtime gameplay ignores a row VFXProfile; never fake a binding. */
    bool bSupportsProfileOverride=true;
    TFunction<TUniquePtr<IMCPerkPreviewAdapter>()> Create;
};

/** Registration contains metadata/factories only; no assets are loaded at static init. */
namespace MCPerkPreviewAdapters
{
    void Register(FMCPerkPreviewAdapterEntry Entry);
    const FMCPerkPreviewAdapterEntry* Find(FName PerkID);
    TArray<FMCPerkPreviewAdapterEntry> GetEntries();
}
