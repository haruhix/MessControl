#pragma once
#include "CoreMinimal.h"

class AActor;
class UPrimitiveComponent;

/** Independent movement-query reasons. Physics contacts and world traces are unchanged. */
enum class EMCMovementContactReason : uint8
{
    PeerWalking=1,
    StableFood=2,
    CarriedFood=4,
    ScriptedFlight=8
};

/** Sole project writer for a character capsule's temporary actor exclusions. */
class MESSCONTROL_API FMCMovementContactPolicy
{
public:
    void Set(UPrimitiveComponent* Capsule,AActor* Other,EMCMovementContactReason Reason,bool Enabled);
    void Forget(UPrimitiveComponent* Capsule,AActor* Other);
    void Release();
    bool HasReason(const AActor* Other,EMCMovementContactReason Reason) const;
private:
    struct FEntry {uint8 Reasons=0;bool bInstalled=false;};
    TWeakObjectPtr<UPrimitiveComponent> Component;
    TMap<TWeakObjectPtr<AActor>,FEntry> Entries;
};
