#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCGameUserSettings.h"
#include "Blueprint/UserWidget.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/StrongObjectPtr.h"

namespace MCUIFactoryPrivate
{
    TStrongObjectPtr<UMCGameplayUITheme> NewTheme;
    bool TriedNewTheme=false;
    FOnMCUIPresentationChanged Changed;

    void ConfigureNewPalette(UMCGameplayUITheme* Theme)
    {
        auto Color=[](const TCHAR* Hex){return FLinearColor(FColor::FromHex(Hex));};
        auto& P=Theme->Palette;
        P.Surface=Color(TEXT("4F76AB"));P.SurfaceAlt=Color(TEXT("355C70"));
        P.Ink=Color(TEXT("35252D"));P.Muted=Color(TEXT("555B66"));
        P.Marine=Color(TEXT("1E266A"));P.OnMarine=FLinearColor::White;
        P.Mint=Color(TEXT("B0FCF3"));P.Cream=Color(TEXT("FFF9F8"));
        P.Border=Color(TEXT("1E266A"));P.Accent=Color(TEXT("AE4D78"));
        P.Button=P.Cream;P.ButtonText=P.Marine;
        P.ButtonHover=P.Mint;P.ButtonPressed=Color(TEXT("9EBBE0"));
        P.ButtonDisabled=Color(TEXT("B6BDC6"));P.Focus=P.Mint;
        P.Backdrop=Color(TEXT("152731")).CopyWithNewOpacity(.78f);
        P.Benefit=Color(TEXT("26A661"));P.Cost=Color(TEXT("BD263E"));
        P.BossHealth=Color(TEXT("D14B4D"));P.Warning=Color(TEXT("FFE000"));
        P.LCDBackground=Color(TEXT("3D4141"));P.LCDInk=P.Cream;
        P.Stamina=P.Mint;P.Cleanliness=P.BossHealth;P.Shield=P.Surface;
        Theme->CornerRadius=16.f;Theme->OutlineWidth=3.f;
    }
}

UMCGameplayUITheme* UMCGameplayUIFactory::GetMasterTheme()
{
    // Existing presenters retain their authored UI. The unified V2 root owns
    // its own theme and layout; F3 selects the root rather than tinting legacy screens.
    return GetThemeForVersion(EMCUIVersion::Current);
}

UMCGameplayUITheme* UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion Version)
{
    // A soft-loaded master has no owner otherwise. Keep its authored palette
    // and class map alive across gameplay garbage collections.
    static TStrongObjectPtr<UMCGameplayUITheme> Master;
    if(!Master.IsValid()) {
        constexpr const TCHAR* Path=TEXT("/Game/Gameplay/UI/Theme/DA_GameplayUITheme.DA_GameplayUITheme");
        Master.Reset(FindObject<UMCGameplayUITheme>(nullptr,Path));
        if(!Master.IsValid()) Master.Reset(LoadObject<UMCGameplayUITheme>(nullptr,Path,nullptr,LOAD_NoWarn|LOAD_Quiet));
    }
    auto* Current=Master.IsValid()?Master.Get():GetMutableDefault<UMCGameplayUITheme>();
    if(Version!=EMCUIVersion::New) return Current;
    using namespace MCUIFactoryPrivate;
    if(!TriedNewTheme)
    {
        TriedNewTheme=true;
        NewTheme.Reset(LoadObject<UMCGameplayUITheme>(nullptr,
            TEXT("/Game/Gameplay/UI/V2/Theme/DA_UITheme_V2.DA_UITheme_V2"),nullptr,LOAD_NoWarn|LOAD_Quiet));
    }
    if(!NewTheme.IsValid())
    {
        // Copy definitions and font/localization roots, not gameplay state. The saved current theme is never edited.
        NewTheme.Reset(DuplicateObject<UMCGameplayUITheme>(Current,GetTransientPackage()));
        ConfigureNewPalette(NewTheme.Get());
    }
    return NewTheme.Get();
}

EMCUIVersion UMCGameplayUIFactory::GetPresentationVersion()
{
#if UE_BUILD_SHIPPING
    return EMCUIVersion::Current;
#else
    const auto* Settings=UMCGameUserSettings::Get();
    return Settings && Settings->GetUIPresentationVersion()==EMCUIVersion::New?EMCUIVersion::New:EMCUIVersion::Current;
#endif
}

bool UMCGameplayUIFactory::SetPresentationVersion(EMCUIVersion Version,bool bSavePreference)
{
#if UE_BUILD_SHIPPING
    return false;
#else
    if(Version!=EMCUIVersion::Current && Version!=EMCUIVersion::New) return false;
    auto* Settings=UMCGameUserSettings::Get();
    if(!Settings) return false;
    if(GetPresentationVersion()==Version) return true;
    if(bSavePreference && (!GConfig || GGameUserSettingsIni.IsEmpty())) return false;
    // Resolve before publishing the change; normal loading also prepares both finite UI roots.
    if(!GetThemeForVersion(Version)) return false;
    Settings->SetUIPresentationVersion(Version);
    if(bSavePreference)
    {
        // F3 can be opened over an unconfirmed graphics/settings page. Persist
        // only this key, not the page's other pending preferences/resolution.
        GConfig->SetString(*Settings->GetClass()->GetPathName(),TEXT("UIPresentationVersion"),
            *StaticEnum<EMCUIVersion>()->GetNameStringByValue(int64(Version)),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }
    MCUIFactoryPrivate::Changed.Broadcast();
    return true;
#endif
}

FOnMCUIPresentationChanged& UMCGameplayUIFactory::OnPresentationChanged() {return MCUIFactoryPrivate::Changed;}
bool UMCGameplayUIFactory::IsPresentationTheme(const UMCGameplayUITheme* Theme)
{
    return Theme && (Theme==GetThemeForVersion(EMCUIVersion::Current) || Theme==MCUIFactoryPrivate::NewTheme.Get());
}
void UMCGameplayUIFactory::InvalidateNewTheme()
{
    MCUIFactoryPrivate::NewTheme.Reset();MCUIFactoryPrivate::TriedNewTheme=false;
    MCUIFactoryPrivate::Changed.Broadcast();
}

UClass* UMCGameplayUIFactory::ResolveClass(FName Role,UClass* NativeFallback)
{
    return ResolveClassForTheme(Role,NativeFallback,GetMasterTheme());
}

UClass* UMCGameplayUIFactory::ResolveClassForTheme(FName Role,UClass* NativeFallback,const UMCGameplayUITheme* Master)
{
    constexpr EClassFlags Rejected=CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists;
    if(!NativeFallback || !NativeFallback->IsChildOf(UUserWidget::StaticClass()) || NativeFallback->HasAnyClassFlags(Rejected)) return nullptr;
    if(!Master || Role.IsNone()) return NativeFallback;
    const auto* Entry=Master->WidgetClasses.Find(Role);
    if(!Entry || Entry->IsNull()) return NativeFallback;
    UClass* Authored=Entry->LoadSynchronous();
    return Authored && Authored->IsChildOf(NativeFallback) && !Authored->HasAnyClassFlags(Rejected)?Authored:NativeFallback;
}
