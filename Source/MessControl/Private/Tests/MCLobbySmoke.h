#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MCLobbySmoke.generated.h"

/** Opt-in four-process gameplay lobby validation; never created in Shipping or ordinary play. */
UCLASS()
class UMCLobbySmoke : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override { return WorldType==EWorldType::Game; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool IsTickable() const override { return IsInitialized() && !bFinished; }
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMCLobbySmoke,STATGROUP_Tickables); }
private:
    void Fail(const TCHAR* Reason);
    bool bFinished=false;
    bool bSawWaiting=false;
    bool bSawFourLoaded=false;
    bool bCheckedIncompleteGuard=false;
    bool bRejectedRemote=false;
    bool bStarted=false;
    bool bVerifiedDayOne=false;
    bool bVerifiedPause=false;
    bool bPauseOpen=false;
    double StartedAt=0;
    double WaitingSeenAt=0;
    double AllLoadedAt=0;
    double PauseOpenedAt=0;
    double PassedAt=0;
};
