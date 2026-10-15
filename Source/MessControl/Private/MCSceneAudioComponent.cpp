#include "MCSceneAudioComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
    FMCSoundSceneLayer BoundedLayer(FMCSoundSceneLayer Layer)
    {
        Layer.Volume=FMath::IsFinite(Layer.Volume)?FMath::Clamp(Layer.Volume,0.f,2.f):0.f;
        Layer.FadeInSeconds=FMath::IsFinite(Layer.FadeInSeconds)?FMath::Clamp(Layer.FadeInSeconds,0.f,10.f):0.f;
        Layer.FadeOutSeconds=FMath::IsFinite(Layer.FadeOutSeconds)?FMath::Clamp(Layer.FadeOutSeconds,0.f,10.f):0.f;
        return Layer;
    }
}

UMCSceneAudioComponent::UMCSceneAudioComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    Voices.Init(nullptr,4);
}

void UMCSceneAudioComponent::SetScene(UMCSoundPalette* Palette,FName Scene)
{
    ApplyScene(Palette,Scene,false);
}

void UMCSceneAudioComponent::RefreshScene()
{
    ApplyScene(SoundPalette,SceneContext,true);
}

TArray<UAudioComponent*> UMCSceneAudioComponent::GetVoiceComponents() const
{
    TArray<UAudioComponent*> Result;
    for(const auto& Voice:Voices) if(IsValid(Voice.Get())) Result.Add(Voice.Get());
    return Result;
}

void UMCSceneAudioComponent::ApplyScene(UMCSoundPalette* Palette,FName Scene,bool Force)
{
    if(bShuttingDown || !GetWorld() || GetNetMode()==NM_DedicatedServer) return;
    if(const auto* Player=Cast<APlayerController>(GetOwner()); Player && !Player->IsLocalController()) return;
    FMCSoundScene Settings;
    if(IsValid(Palette)) Palette->GetScene(Scene,Settings);
    SoundPalette=Palette;SceneContext=Scene;
    ApplyLayer(0,Settings.Music,Force);
    ApplyLayer(1,Settings.Ambience,Force);
}

void UMCSceneAudioComponent::CancelLoad(int32 Layer)
{
    ++Layers[Layer].Revision;
    if(Layers[Layer].Load) {Layers[Layer].Load->CancelHandle();Layers[Layer].Load.Reset();}
}

void UMCSceneAudioComponent::ApplyLayer(int32 Layer,const FMCSoundSceneLayer& Settings,bool Force)
{
    auto& State=Layers[Layer];
    const auto Desired=BoundedLayer(Settings);
    if(!Force && State.Desired==Desired) return;
    CancelLoad(Layer);
    State.Desired=Desired;
    if(Desired.Sound.IsNull() || Desired.Volume<=0.f)
    {
        RetireCurrent(Layer,State.Active.FadeOutSeconds);
        return;
    }
    if(auto* Current=Voices[Layer*2].Get(); Current && Current->Sound==Desired.Sound.Get())
    {
        State.Active=Desired;
        Current->AdjustVolume(Desired.FadeInSeconds,Desired.Volume);
        return;
    }
    const uint64 Revision=State.Revision;
    if(Desired.Sound.Get()) StartLayer(Layer,Revision);
    else
    {
        State.Load=UAssetManager::GetStreamableManager().RequestAsyncLoad(Desired.Sound.ToSoftObjectPath(),
            FStreamableDelegate::CreateWeakLambda(this,[this,Layer,Revision]() {StartLayer(Layer,Revision);}));
        if(!State.Load) RetireCurrent(Layer,State.Active.FadeOutSeconds);
    }
}

void UMCSceneAudioComponent::StartLayer(int32 Layer,uint64 Revision)
{
    if(bShuttingDown || !GetWorld() || GetNetMode()==NM_DedicatedServer || Layers[Layer].Revision!=Revision) return;
    auto& State=Layers[Layer];
    USoundBase* Sound=State.Desired.Sound.Get();
    // Free an older retiring voice before creating another: never more than four.
    DestroyRetiring(Layer);
    // Fade levels are multipliers of the component's base volume: keep it at one.
    UAudioComponent* Incoming=Sound?UGameplayStatics::CreateSound2D(this,Sound,1.f,1.f,0.f,nullptr,false,false):nullptr;
    RetireCurrent(Layer,State.Active.FadeOutSeconds);
    if(Incoming)
    {
        Voices[Layer*2]=Incoming;
        State.Active=State.Desired;
        Incoming->bStopWhenOwnerDestroyed=true;
        Incoming->OnAudioFinishedNative.AddUObject(this,&UMCSceneAudioComponent::HandleFinished);
        Incoming->FadeIn(State.Active.FadeInSeconds,State.Active.Volume);
    }
    // The audio component retains its sound before releasing the streaming handle.
    State.Load.Reset();
}

void UMCSceneAudioComponent::RetireCurrent(int32 Layer,float FadeSeconds)
{
    DestroyRetiring(Layer);
    const int32 CurrentSlot=Layer*2,FadingSlot=CurrentSlot+1;
    UAudioComponent* Current=Voices[CurrentSlot];
    Voices[CurrentSlot]=nullptr;
    if(!Current) return;
    // Stop/FadeOut also broadcasts Finished; retiring audio must never repeat.
    Current->OnAudioFinishedNative.RemoveAll(this);
    FadeSeconds=FMath::IsFinite(FadeSeconds)?FMath::Clamp(FadeSeconds,0.f,10.f):0.f;
    if(!GetWorld() || FadeSeconds<=0.f)
    {
        Current->Stop();Current->DestroyComponent();
        return;
    }
    Voices[FadingSlot]=Current;
    Current->FadeOut(FadeSeconds,0.f);
    const TWeakObjectPtr<UAudioComponent> Retiring(Current);
    GetWorld()->GetTimerManager().SetTimer(Layers[Layer].RetireTimer,
        FTimerDelegate::CreateWeakLambda(this,[this,Layer,Retiring]()
        {
            if(Voices[Layer*2+1].Get()==Retiring.Get()) DestroyRetiring(Layer);
        }),FadeSeconds+.05f,false);
}

void UMCSceneAudioComponent::ReleaseVoice(int32 Slot)
{
    UAudioComponent* Voice=Voices[Slot];Voices[Slot]=nullptr;
    if(!Voice) return;
    Voice->OnAudioFinishedNative.RemoveAll(this);
    Voice->Stop();Voice->DestroyComponent();
}

void UMCSceneAudioComponent::DestroyRetiring(int32 Layer)
{
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Layers[Layer].RetireTimer);
    ReleaseVoice(Layer*2+1);
}

void UMCSceneAudioComponent::HandleFinished(UAudioComponent* Voice)
{
    if(bShuttingDown || !GetWorld() || !IsValid(Voice)) return;
    for(int32 Layer=0;Layer<2;++Layer) if(Voices[Layer*2].Get()==Voice)
    {
        // Plain Play resets the new active sound's fade level to one.
        // Repetition must retain the volume authored for this layer in the DA.
        if(Layers[Layer].Active.bRepeatOnFinish && !Voice->IsPlaying()) Voice->FadeIn(0.f,Layers[Layer].Active.Volume);
        else if(!Voice->IsPlaying()) ReleaseVoice(Layer*2);
        return;
    }
}

void UMCSceneAudioComponent::StopScene(float FadeOutSeconds)
{
    SoundPalette=nullptr;SceneContext=NAME_None;
    for(int32 Layer=0;Layer<2;++Layer)
    {
        CancelLoad(Layer);Layers[Layer].Desired=FMCSoundSceneLayer();
        RetireCurrent(Layer,FadeOutSeconds);
    }
}

void UMCSceneAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bShuttingDown=true;
    for(int32 Layer=0;Layer<2;++Layer)
    {
        CancelLoad(Layer);DestroyRetiring(Layer);ReleaseVoice(Layer*2);
    }
    SoundPalette=nullptr;SceneContext=NAME_None;
    Super::EndPlay(Reason);
}
