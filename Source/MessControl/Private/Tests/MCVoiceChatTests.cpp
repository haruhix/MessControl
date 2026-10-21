#if WITH_DEV_AUTOMATION_TESTS
#include "MCGameUserSettings.h"
#include "MCVoiceChatSubsystem.h"
#include "MCVoiceSoundClass.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVoiceTransmitPolicyTest,"MessControl.Voice.TransmissionPrivacyGates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVoiceTransmitPolicyTest::RunTest(const FString& Parameters)
{
    using Mode=EMCVoiceInputMode;
    auto Allows=[](bool Enabled,bool Muted,Mode Input,bool Held,bool Room,bool Focus,bool Test)
    { return UMCVoiceChatSubsystem::ShouldTransmit(Enabled,Muted,Input,Held,Room,Focus,Test); };
    TestFalse(TEXT("PTT never sends before button press"),Allows(true,false,Mode::PushToTalk,false,true,true,false));
    TestTrue(TEXT("PTT can send while held in a focused room"),Allows(true,false,Mode::PushToTalk,true,true,true,false));
    TestTrue(TEXT("Open microphone does not require PTT"),Allows(true,false,Mode::OpenMic,false,true,true,false));
    for(Mode Input:{Mode::PushToTalk,Mode::OpenMic})
    {
        TestFalse(TEXT("Disabled chat cannot transmit"),Allows(false,false,Input,true,true,true,false));
        TestFalse(TEXT("Self mute cannot be bypassed by PTT"),Allows(true,true,Input,true,true,true,false));
        TestFalse(TEXT("No room means no network transmission"),Allows(true,false,Input,true,false,true,false));
        TestFalse(TEXT("Focus loss prevents transmission"),Allows(true,false,Input,true,true,false,false));
        TestFalse(TEXT("Mic test never broadcasts into a room"),Allows(true,false,Input,true,true,true,true));
        TestFalse(TEXT("Rebinding suppresses PTT and open microphone immediately"),
            UMCVoiceChatSubsystem::ShouldTransmit(true,false,Input,true,true,true,false,true));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVoicePreferencesTest,"MessControl.Voice.PreferencesPersistWithoutCaptureSideEffects",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVoicePreferencesTest::RunTest(const FString& Parameters)
{
    const auto* GainCVar=IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicInputGain"));
    const float OriginalGain=GainCVar?GainCVar->GetFloat():1.f;
    const FString OriginalLanguage=FInternationalization::Get().GetCurrentLanguage()->GetName();
    auto* Preferences=NewObject<UMCGameUserSettings>();Preferences->SetToDefaults();
    TestEqual(TEXT("Default is explicit push to talk"),Preferences->GetVoiceInputMode(),EMCVoiceInputMode::PushToTalk);
    TestEqual(TEXT("Default PTT key"),Preferences->GetPushToTalkKey(),EKeys::V);
    Preferences->SetMicrophoneGain(100.f);Preferences->SetVoiceActivationThreshold(-1.f);Preferences->SetVoiceVolume(-1.f);
    TestEqual(TEXT("Mic gain clamps"),Preferences->GetMicrophoneGain(),3.f);
    TestEqual(TEXT("Threshold clamps"),Preferences->GetVoiceActivationThreshold(),0.f);
    TestEqual(TEXT("Voice volume clamps"),Preferences->GetVoiceVolume(),0.f);
    TestFalse(TEXT("An analog axis cannot become PTT"),Preferences->SetPushToTalkKey(EKeys::MouseX));
    TestFalse(TEXT("Modifier-only PTT is rejected"),Preferences->SetPushToTalkKey(EKeys::LeftControl));
    TestTrue(TEXT("Digital PTT can be rebound"),Preferences->SetPushToTalkKey(EKeys::B));
    Preferences->SetVoiceInputMode(EMCVoiceInputMode::OpenMic);Preferences->SetSelfMuted(true);
    Preferences->SetMasterVolume(.6f);Preferences->SetSFXVolume(0.f);Preferences->SetVoiceVolume(.8f);
    TestEqual(TEXT("SFX mute does not mute the separate voice gain"),Preferences->GetEffectiveVoiceVolume(),.48f);
    TestNull(TEXT("Native voice class is an independent root"),GetDefault<UMCVoiceSoundClass>()->ParentClass.Get());
    TestEqual(TEXT("AudioSettings can resolve native voice root before the first talker"),
        Cast<USoundClass>(FSoftObjectPath(TEXT("/Script/MessControl.Default__MCVoiceSoundClass")).TryLoad()),
        static_cast<USoundClass*>(const_cast<UMCVoiceSoundClass*>(GetDefault<UMCVoiceSoundClass>())));
    Preferences->SetVoicePlayerMuted(TEXT("76561198000000001"),true);
    Preferences->SetVoicePlayerMuted(TEXT("76561198000000001"),true);
    Preferences->SetVoicePlayerMuted(TEXT(""),true);
    TestEqual(TEXT("Mute IDs are unique and empty identity cannot mute everyone"),Preferences->GetMutedVoicePlayerIds().Num(),1);
    Preferences->ApplyGameplaySettings(); // Non-singleton authoring/test object must be inert.
    TestEqual(TEXT("Preference authoring never changes live microphone gain"),GainCVar?GainCVar->GetFloat():1.f,OriginalGain);
    TestEqual(TEXT("Preference authoring never changes global language"),FInternationalization::Get().GetCurrentLanguage()->GetName(),OriginalLanguage);
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/VoicePreferences"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Filename=FPaths::Combine(Directory,FGuid::NewGuid().ToString()+TEXT(".ini"));
    Preferences->SaveConfig(CPF_Config,*Filename);
    auto* Restored=NewObject<UMCGameUserSettings>();Restored->LoadConfig(Restored->GetClass(),*Filename);
    TestEqual(TEXT("Voice mode survives restart"),Restored->GetVoiceInputMode(),Preferences->GetVoiceInputMode());
    TestEqual(TEXT("PTT binding survives restart"),Restored->GetPushToTalkKey(),Preferences->GetPushToTalkKey());
    TestEqual(TEXT("Microphone gain survives restart"),Restored->GetMicrophoneGain(),Preferences->GetMicrophoneGain());
    TestEqual(TEXT("Threshold survives restart"),Restored->GetVoiceActivationThreshold(),Preferences->GetVoiceActivationThreshold());
    TestEqual(TEXT("Voice volume survives restart"),Restored->GetVoiceVolume(),Preferences->GetVoiceVolume());
    TestTrue(TEXT("Self mute survives restart"),Restored->GetSelfMuted());
    TestTrue(TEXT("Individual online identity mute survives restart"),Restored->IsVoicePlayerMuted(TEXT("76561198000000001")));
    Restored->SetVoicePlayerMuted(TEXT("76561198000000001"),false);
    TestFalse(TEXT("Unmute removes that identity"),Restored->IsVoicePlayerMuted(TEXT("76561198000000001")));
    GConfig->UnloadFile(Filename);IFileManager::Get().Delete(*Filename);
    return true;
}
#endif
