#include "MCMovementContactPolicy.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

void FMCMovementContactPolicy::Set(UPrimitiveComponent* Capsule,AActor* Other,EMCMovementContactReason Reason,bool Enabled)
{
    if(!Capsule || !Other || Other==Capsule->GetOwner()) return;
    if(Component.Get()!=Capsule) {Release();Component=Capsule;}
    const TWeakObjectPtr<AActor> Key(Other);
    const uint8 Bit=uint8(Reason);
    if(Enabled) {
        auto& Entry=Entries.FindOrAdd(Key);Entry.Reasons|=Bit;
        // A pre-existing exclusion belongs to its original writer. A reason
        // must not acquire the right to remove it merely by observing it.
        if(!Capsule->GetMoveIgnoreActors().Contains(Other)) {
            Capsule->IgnoreActorWhenMoving(Other,true);Entry.bInstalled=true;
        }
    } else if(auto* Entry=Entries.Find(Key)) {
        Entry->Reasons&=~Bit;
        if(!Entry->Reasons) Forget(Capsule,Other);
    }
}
void FMCMovementContactPolicy::Forget(UPrimitiveComponent* Capsule,AActor* Other)
{
    if(Component.Get()!=Capsule || !Other) return;
    const TWeakObjectPtr<AActor> Key(Other);
    if(const auto* Entry=Entries.Find(Key);Entry && Entry->bInstalled && Capsule->GetMoveIgnoreActors().Contains(Other))
        Capsule->IgnoreActorWhenMoving(Other,false);
    Entries.Remove(Key);
}
void FMCMovementContactPolicy::Release()
{
    if(auto* Capsule=Component.Get()) for(const auto& Pair:Entries)
        if(Pair.Value.bInstalled) if(auto* Other=Pair.Key.Get(true)) Capsule->IgnoreActorWhenMoving(Other,false);
    Entries.Reset();Component.Reset();
}
bool FMCMovementContactPolicy::HasReason(const AActor* Other,EMCMovementContactReason Reason) const
{
    const auto* Entry=Other?Entries.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Other))):nullptr;
    return Entry && (Entry->Reasons&uint8(Reason))!=0;
}
