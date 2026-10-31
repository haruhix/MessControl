#pragma once

#include "CoreMinimal.h"
#include "MCPerkPreviewAdapter.h"

class AMCActiveSkillAction;
class AMCToothCharacter;
class FPreviewScene;
class UMCPerkVFXProfile;
class UWorld;

struct FMCPerkPreviewEvent
{
    float Seconds=0;
    FName Stage;
};

struct FMCPerkPreviewClockStats
{
    uint64 TickCalls=0,SkippedSameFrameCalls=0,WorldSteps=0;
    double InputSeconds=0,AdvancedSeconds=0,DroppedCatchupSeconds=0;
    float LastInputDelta=0,LastAdvancedDelta=0;
};

/**
 * Editor-owned transient Game world: real tooth, physical arms and action factory.
 * One service clock owns advancement per engine frame, with bounded physics substeps.
 * Viewports render the scene and never advance its clock.
 * Never starts PIE, writes player saves or uses the editor world.
 */
class FMCPerkPreviewRunner final
{
public:
    FMCPerkPreviewRunner();
    ~FMCPerkPreviewRunner();
    FMCPerkPreviewRunner(const FMCPerkPreviewRunner&)=delete;
    FMCPerkPreviewRunner& operator=(const FMCPerkPreviewRunner&)=delete;

    bool Initialize();
    bool StartAction(FName PerkID,EMCPerkPreviewScenario Scenario,UMCPerkVFXProfile* ProfileOverride=nullptr);
    bool StartRocket(EMCPerkPreviewScenario Scenario,UMCPerkVFXProfile* ProfileOverride=nullptr);
    void Release();
    void Cancel();
    /** Removes every gameplay actor/cue owned by this sandbox, then restores a fresh idle tooth. */
    void Reset();
    void Tick(float DeltaSeconds);
    void SetPaused(bool bPaused);
    bool IsPaused() const;
    bool IsRunning() const;

    UWorld* GetWorld() const;
    /** Compatibility only: the current runner owns a world directly, not a legacy FPreviewScene. */
    FPreviewScene* GetPreviewScene() const;
    AMCToothCharacter* GetHero() const;
    AMCActiveSkillAction* GetAction() const;
    FString GetStatus() const;
    FString GetPhaseName() const;
    FString GetEventLog() const;
    float GetElapsed() const;
    EMCPerkPreviewScenario GetScenario() const;
    FName GetPerkID() const;
    const TArray<FMCPerkPreviewEvent>& GetEvents() const;
    FMCPerkPreviewClockStats GetClockStats() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    uint64 LastTickFrame=MAX_uint64;
    void TickStep(float DeltaSeconds);
};
