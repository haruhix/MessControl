#if WITH_DEV_AUTOMATION_TESTS
#include "MCGameUserSettings.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreferencesGainAndLookTest,"MessControl.Settings.AbsoluteGainsAndLocalLook",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPreferencesGainAndLookTest::RunTest(const FString& Parameters)
{
    auto* Settings=NewObject<UMCGameUserSettings>();
    Settings->SetToDefaults();
    TestEqual(TEXT("Default preferences preserve mouse deltas"),Settings->ScaleLookInput(FVector2D(2,-3),false),FVector2D(2,-3));
    TestEqual(TEXT("Default preferences preserve gamepad deltas"),Settings->ScaleLookInput(FVector2D(2,-3),true),FVector2D(2,-3));
    Settings->SetMasterVolume(.5f);Settings->SetMusicVolume(.4f);
    Settings->SetAmbienceVolume(.7f);Settings->SetSFXVolume(.8f);
    TestEqual(TEXT("Music gain independent of SFX"),Settings->GetEffectiveMusicVolume(),.2f);
    TestEqual(TEXT("Ambience gain independent of SFX"),Settings->GetEffectiveAmbienceVolume(),.35f);
    TestEqual(TEXT("SFX gain includes master"),Settings->GetEffectiveSFXVolume(),.4f);
    Settings->SetMasterVolume(0.f);TestEqual(TEXT("Master mutes all categories"),Settings->GetEffectiveMusicVolume()+Settings->GetEffectiveSFXVolume()+Settings->GetEffectiveAmbienceVolume(),0.f);
    Settings->SetMasterVolume(1.f);TestEqual(TEXT("Unmuting restores selected music gain"),Settings->GetEffectiveMusicVolume(),.4f);
    Settings->SetSFXVolume(-4.f);Settings->SetMusicVolume(9.f);
    TestEqual(TEXT("Volume clamps low"),Settings->GetSFXVolume(),0.f);
    TestEqual(TEXT("Volume clamps high"),Settings->GetMusicVolume(),1.f);
    Settings->SetMouseSensitivity(2.f);Settings->SetGamepadSensitivity(.5f);Settings->SetInvertY(true);
    TestEqual(TEXT("Mouse preference scales and inverts only look delta"),Settings->ScaleLookInput(FVector2D(2,-3),false),FVector2D(4,6));
    TestEqual(TEXT("Gamepad has separate sensitivity"),Settings->ScaleLookInput(FVector2D(2,-3),true),FVector2D(1,1.5));
    Settings->SetMouseSensitivity(-1.f);Settings->SetGamepadSensitivity(9.f);
    TestEqual(TEXT("Sensitivity clamps low"),Settings->GetMouseSensitivity(),.1f);
    TestEqual(TEXT("Sensitivity clamps high"),Settings->GetGamepadSensitivity(),5.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreferencesConfigRoundtripTest,"MessControl.Settings.ConfigRoundtripDoesNotChangeCulture",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPreferencesConfigRoundtripTest::RunTest(const FString& Parameters)
{
    const FString Before=FInternationalization::Get().GetCurrentLanguage()->GetName();
    auto* Written=NewObject<UMCGameUserSettings>();Written->SetToDefaults();
    Written->SetMasterVolume(.63f);Written->SetMusicVolume(.27f);Written->SetAmbienceVolume(.41f);Written->SetSFXVolume(.82f);
    Written->SetMouseSensitivity(1.7f);Written->SetGamepadSensitivity(.8f);Written->SetInvertY(true);
    TestTrue(TEXT("Table language code accepted"),Written->SetLanguageCode(TEXT("ZH_hans")));
    TestEqual(TEXT("Language canonicalized"),Written->GetLanguageCode(),FString(TEXT("zh-Hans")));
    TestFalse(TEXT("Unsupported language does not erase selected code"),Written->SetLanguageCode(TEXT("invalid-language")));
    TestEqual(TEXT("Selection has no immediate global-culture side effect"),FInternationalization::Get().GetCurrentLanguage()->GetName(),Before);
    // Isolated ini under E:; never alter the actual player's saved preferences.
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/Settings"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Filename=FPaths::Combine(Directory,FGuid::NewGuid().ToString()+TEXT(".ini"));
    Written->SaveConfig(CPF_Config,*Filename);
    auto* Read=NewObject<UMCGameUserSettings>();Read->SetToDefaults();Read->LoadConfig(Read->GetClass(),*Filename);
    TestEqual(TEXT("Master persists"),Read->GetMasterVolume(),Written->GetMasterVolume());
    TestEqual(TEXT("Music persists"),Read->GetMusicVolume(),Written->GetMusicVolume());
    TestEqual(TEXT("Ambience persists"),Read->GetAmbienceVolume(),Written->GetAmbienceVolume());
    TestEqual(TEXT("SFX persists"),Read->GetSFXVolume(),Written->GetSFXVolume());
    TestEqual(TEXT("Mouse persists"),Read->GetMouseSensitivity(),Written->GetMouseSensitivity());
    TestEqual(TEXT("Gamepad persists"),Read->GetGamepadSensitivity(),Written->GetGamepadSensitivity());
    TestEqual(TEXT("Invert persists"),Read->GetInvertY(),Written->GetInvertY());
    TestEqual(TEXT("Language persists"),Read->GetLanguageCode(),Written->GetLanguageCode());
    TestEqual(TEXT("Config roundtrip does not switch Designer/editor language"),FInternationalization::Get().GetCurrentLanguage()->GetName(),Before);
    IFileManager::Get().Delete(*Filename);
    return true;
}
#endif
