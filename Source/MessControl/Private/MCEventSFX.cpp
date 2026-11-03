#include "MCEventSFX.h"

#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"

bool FMCEventSFXSlot::HasCustomLayers() const
{
    return (Mode==EMCEventSFXMode::Add || Mode==EMCEventSFXMode::Replace)
        && Layers.ContainsByPredicate([](const auto& L){return L.bEnabled;});
}

bool FMCEventSFXSlot::Validate(FString& Error) const
{
    Error.Reset();
    if(uint8(Mode)>uint8(EMCEventSFXMode::Disabled) || Layers.Num()>32
        || !FMath::IsFinite(MaxDurationSeconds) || MaxDurationSeconds<.01f || MaxDurationSeconds>3600.f
        || !FMath::IsFinite(TailSeconds) || TailSeconds<0.f || TailSeconds>10.f)
    {Error=TEXT("SFX needs a valid mode, at most 32 layers, a .01–3600s duration and a 0–10s tail.");return false;}
    TSet<FGuid> IDs;
    for(const auto& L:Layers) {
        if(!L.ID.IsValid() || IDs.Contains(L.ID))
        {Error=TEXT("SFX layer IDs must be valid and unique within their event.");return false;}
        IDs.Add(L.ID);
        if(uint8(L.Space)>uint8(EMCEventSFXSpace::SourceSocket)
            || !FMath::IsFinite(L.DelaySeconds) || L.DelaySeconds<0.f || L.DelaySeconds>60.f
            || !FMath::IsFinite(L.Volume) || L.Volume<0.f || L.Volume>4.f
            || !FMath::IsFinite(L.Pitch) || L.Pitch<.125f || L.Pitch>4.f
            || !FMath::IsFinite(L.FadeInSeconds) || L.FadeInSeconds<0.f || L.FadeInSeconds>10.f
            || !FMath::IsFinite(L.FadeOutSeconds) || L.FadeOutSeconds<0.f || L.FadeOutSeconds>10.f
            || L.Offset.ContainsNaN() || !L.Offset.GetRotation().IsNormalized()
            || L.Offset.GetScale3D().GetAbsMin()<.0001)
        {Error=TEXT("SFX layer has an invalid transform, space, delay, gain, pitch or fade.");return false;}
        if(L.bEnabled && HasCustomLayers() && (L.Sound.IsNull() || L.DelaySeconds>=MaxDurationSeconds
            || (L.Space==EMCEventSFXSpace::SourceSocket && L.Socket.IsNone())))
        {Error=TEXT("Enabled SFX needs its original SoundBase, a delay inside its horizon and an explicit socket for socket attachment.");return false;}
    }
    return true;
}

void FMCEventSFXSlot::GetSoftResources(TArray<FSoftObjectPath>& Out) const
{
    // Group/child mute/disable does not erase authored dependencies from the location manifest.
    for(const auto& L:Layers) {
        if(!L.Sound.IsNull())Out.AddUnique(L.Sound.ToSoftObjectPath());
        if(!L.Attenuation.IsNull())Out.AddUnique(L.Attenuation.ToSoftObjectPath());
        if(!L.Concurrency.IsNull())Out.AddUnique(L.Concurrency.ToSoftObjectPath());
    }
}

bool MCEventSFX::Prepare(const FMCEventSFXSlot& Slot,bool bAllowLoad,FMCPreparedEventSFX& Out,FString& Error,bool* bWaiting)
{
    if(bWaiting)*bWaiting=false;
    if(!Slot.Validate(Error))return false;
    FMCPreparedEventSFX Candidate;Candidate.Slot=Slot;
    Candidate.Sounds.SetNum(Slot.Layers.Num());Candidate.Attenuations.SetNum(Slot.Layers.Num());Candidate.Concurrencies.SetNum(Slot.Layers.Num());
    for(int32 I=0;I<Slot.Layers.Num();++I) {
        const auto& L=Slot.Layers[I];
        Candidate.Sounds[I]=bAllowLoad && !L.Sound.IsNull()?L.Sound.LoadSynchronous():L.Sound.Get();
        Candidate.Attenuations[I]=bAllowLoad && !L.Attenuation.IsNull()?L.Attenuation.LoadSynchronous():L.Attenuation.Get();
        Candidate.Concurrencies[I]=bAllowLoad && !L.Concurrency.IsNull()?L.Concurrency.LoadSynchronous():L.Concurrency.Get();
        if(Slot.HasCustomLayers() && L.bEnabled && (!Candidate.Sounds[I] || (!L.Attenuation.IsNull() && !Candidate.Attenuations[I]) || (!L.Concurrency.IsNull() && !Candidate.Concurrencies[I])))
        {if(bWaiting)*bWaiting=true;Error=TEXT("SFX source/attenuation/concurrency is not resident; prepare the location before accepting this event: ")+L.Name.ToString();return false;}
    }
    Out=MoveTemp(Candidate);return true;
}

bool MCEventSFX::ValidateNativePolicy(const FMCEventSFXSlot& Slot,bool bCanSuppressNativeAudio,FString& Error)
{
    if((Slot.Mode==EMCEventSFXMode::Replace || Slot.Mode==EMCEventSFXMode::Disabled) && !bCanSuppressNativeAudio)
    {Error=TEXT("The original audio hook is not connected for this event; Replace/Disabled cannot be applied.");return false;}
    return Slot.Validate(Error);
}

bool MCEventSFX::CompileSharedGroupPolicy(const TArray<FGuid>& SharedGroupIDs,const TArray<FGuid>& DisabledGroupIDs,
    const TArray<FGuid>& MutedGroupIDs,FMCPreparedEventSFX& Prepared,FString& Error)
{
    Error.Reset();TSet<FGuid> Declared;
    for(const FGuid& ID:SharedGroupIDs) {
        if(!ID.IsValid() || Declared.Contains(ID))
        {Error=TEXT("Shared event-group GUIDs must be valid and unique; Default is implicit.");return false;}
        Declared.Add(ID);
    }
    auto Known=[&](FGuid ID){return !ID.IsValid() || Declared.Contains(ID);};
    for(const auto& L:Prepared.Slot.Layers)if(!Known(L.GroupID))
    {Error=TEXT("An SFX layer refers to a group outside its actual event.");return false;}
    for(const FGuid& ID:DisabledGroupIDs)if(!Known(ID))
    {Error=TEXT("Disabled audio group is not declared by the shared event.");return false;}
    for(const FGuid& ID:MutedGroupIDs)if(!Known(ID))
    {Error=TEXT("Muted audio group is not declared by the shared event.");return false;}
    Prepared.DisabledGroupIDs=DisabledGroupIDs;Prepared.MutedGroupIDs=MutedGroupIDs;return true;
}
