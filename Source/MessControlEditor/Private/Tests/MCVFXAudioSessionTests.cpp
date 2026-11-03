#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MCVFXActionSession.h"
#include "Editor.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "MCVFXRecipePreset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEventAudioSessionIdentity,
    "MessControl.Audio.Composer.SessionLayerIdentityUndoAndIndependentPolicy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEventAudioSessionIdentity::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run outside PIE."));return false;}
    FMCVFXActionSession Session;FString Error;
    if(!TestTrue(TEXT("Open real editable source"),Session.Initialize(TEXT("EditorOnly.NutEnemyDeath"),Error)))return false;
    auto* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins"));
    if(!TestNotNull(TEXT("Saved real project SoundBase exists"),Sound))return false;
    const FName Event(TEXT("Death"));const auto Visual=Session.GetOriginalSlot(Event);
    const FGuid Token=Session.GetSFXDraftToken();
    const FGuid A=Session.AddSFXLayer(Token,Event,Sound,FGuid(),Error);
    TestTrue(TEXT("First sound layer has stable identity"),A.IsValid());
    TestTrue(TEXT("Audio-only edit participates in the ordinary dirty state"),Session.IsDirty());
    TestEqual(TEXT("First sound adds without suppressing original audio"),Session.GetSFXSlot(Event)->Mode,EMCEventSFXMode::Add);
    TestEqual(TEXT("Adding sound preserves the original VFX mode"),Session.GetOriginalSlot(Event).Mode,Visual.Mode);
    const FGuid B=Session.DuplicateSFXLayer(Token,Event,A,Error);
    TestTrue(TEXT("Two instances of the same source have different identities"),B.IsValid() && A!=B);
    TestEqual(TEXT("Two simultaneous layers retained"),Session.GetSFXSlot(Event)->Layers.Num(),2);
    auto Edited=Session.GetSFXSlot(Event)->Layers[0];Edited.Volume=.32f;
    TestTrue(TEXT("Per-instance gain edit"),Session.SetSFXLayer(Token,Event,A,Edited,Error));
    TestEqual(TEXT("Other instance retains its own gain"),Session.GetSFXSlot(Event)->Layers[1].Volume,1.f);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores only edited gain"),Session.GetSFXSlot(Event)->Layers[0].Volume,1.f);
    TestEqual(TEXT("Undo retains the second layer identity"),Session.GetSFXSlot(Event)->Layers[1].ID,B);
    TestFalse(TEXT("Unmapped native sound cannot silently be disabled"),Session.SetSFXMode(Token,Event,EMCEventSFXMode::Disabled,Error));
    TestEqual(TEXT("Rejected suppression preserves additive audio"),Session.GetSFXSlot(Event)->Mode,EMCEventSFXMode::Add);
    TestTrue(TEXT("Remove the intended layer only"),Session.RemoveSFXLayer(Token,Event,A,Error));
    TestEqual(TEXT("The other source instance remains"),Session.GetSFXSlot(Event)->Layers[0].ID,B);
    Session.Clear();
    if(!TestTrue(TEXT("Reopen same source"),Session.Initialize(TEXT("EditorOnly.NutEnemyDeath"),Error)))return false;
    TestFalse(TEXT("Old same-source popup token cannot add a layer"),Session.AddSFXLayer(Token,Event,Sound,FGuid(),Error).IsValid());
    TestFalse(TEXT("Reopened source was not mutated by stale callback"),Session.IsDirty());
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(TestNotNull(TEXT("Real Niagara source for group guard"),System))
    {
        const FGuid VFX=Session.AddSystem(Event,System,Error);
        if(TestTrue(TEXT("VFX layer added"),VFX.IsValid()))
        {
            const auto* Recipe=Session.GetRecipe(Event);const int32 Index=Recipe->FindNode(VFX);
            auto Layer=Recipe->Layers[Index];Layer.GroupID=FGuid::NewGuid();Layer.bEnabled=false;
            TestFalse(TEXT("An unknown group cannot be saved even on a disabled VFX layer"),Session.SetLayer(Event,VFX,Layer,Error));
            TestFalse(TEXT("Rejected edit preserves the original group"),Session.GetRecipe(Event)->Layers[Index].GroupID.IsValid());
        }
    }
    Session.Clear();return true;
}
#endif
