#include "MCGameplayUITheme.h"

#include "Engine/DataTable.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace MCGameplayUIThemePrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
}

FMCUIThemePalette::FMCUIThemePalette()
{
    using namespace MCGameplayUIThemePrivate;
    Surface=Color(TEXT("DCEBE6"));
    SurfaceAlt=Color(TEXT("C8DED8"));
    Ink=Color(TEXT("17384B"));
    Muted=Color(TEXT("55727B"));
    Marine=Color(TEXT("28516A"));
    Mint=Color(TEXT("B6D17B"));
    Cream=Color(TEXT("F4F1DC"));
    Benefit=Color(TEXT("286348"));
    Cost=Color(TEXT("B64F43"));
    Border=Color(TEXT("4C717A"));
    Backdrop=Color(TEXT("17384B")).CopyWithNewOpacity(.76f);
    Accent=Color(TEXT("82C8CB"));
    OnMarine=Color(TEXT("F4F1DC"));
    Button=Color(TEXT("DCEBE6"));
    ButtonHover=Color(TEXT("CCE1B5"));
    ButtonPressed=Color(TEXT("B6D17B"));
    ButtonDisabled=Color(TEXT("AEC4C2"));
    ButtonText=Color(TEXT("17384B"));
    Focus=Color(TEXT("82C8CB"));
    BossHealth=Color(TEXT("FF4B4E"));
    BossSurface=Color(TEXT("0B0C10"));
    BossRim=Color(TEXT("41474C"));
    BossRimHighlight=Color(TEXT("8C969C"));
    Shield=Color(TEXT("82C8CB"));
    Warning=Color(TEXT("D4AD61"));
    LCDBackground=Color(TEXT("C4D5AD"));
    LCDInk=Color(TEXT("294D46"));
    Stamina=Color(TEXT("B6D17B"));
    Cleanliness=Color(TEXT("82C8CB"));
}

UMCGameplayUITheme::UMCGameplayUITheme()
{
    BodyFont=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16.f);
    HeadingFont=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),22.f);
    DisplayFont=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),32.f);
    // A font asset survives package serialization; Slate-only CompositeFont
    // pointers do not. Artists can replace these three font assets in the DA.
    static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if(DefaultFont.Succeeded())
    {
        BodyFont.FontObject=DefaultFont.Object;
        HeadingFont.FontObject=DefaultFont.Object;
        DisplayFont.FontObject=DefaultFont.Object;
    }
    LocalizationTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/Data/DT_UI_Localization.DT_UI_Localization")));
    BoosterTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/Data/DT_UI_Boosters.DT_UI_Boosters")));
    CardTable=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/Data/DT_UI_Cards.DT_UI_Cards")));
}

FLinearColor UMCGameplayUITheme::GetColor(EMCUIColorToken Token) const
{
    switch(Token)
    {
    case EMCUIColorToken::Surface:return Palette.Surface;
    case EMCUIColorToken::SurfaceAlt:return Palette.SurfaceAlt;
    case EMCUIColorToken::Ink:return Palette.Ink;
    case EMCUIColorToken::Muted:return Palette.Muted;
    case EMCUIColorToken::Marine:return Palette.Marine;
    case EMCUIColorToken::Mint:return Palette.Mint;
    case EMCUIColorToken::Cream:return Palette.Cream;
    case EMCUIColorToken::Benefit:return Palette.Benefit;
    case EMCUIColorToken::Cost:return Palette.Cost;
    case EMCUIColorToken::Border:return Palette.Border;
    case EMCUIColorToken::Backdrop:return Palette.Backdrop;
    case EMCUIColorToken::Accent:return Palette.Accent;
    case EMCUIColorToken::OnMarine:return Palette.OnMarine;
    case EMCUIColorToken::Button:return Palette.Button;
    case EMCUIColorToken::ButtonHover:return Palette.ButtonHover;
    case EMCUIColorToken::ButtonPressed:return Palette.ButtonPressed;
    case EMCUIColorToken::ButtonDisabled:return Palette.ButtonDisabled;
    case EMCUIColorToken::ButtonText:return Palette.ButtonText;
    case EMCUIColorToken::Focus:return Palette.Focus;
    case EMCUIColorToken::BossHealth:return Palette.BossHealth;
    case EMCUIColorToken::BossSurface:return Palette.BossSurface;
    case EMCUIColorToken::BossRim:return Palette.BossRim;
    case EMCUIColorToken::BossRimHighlight:return Palette.BossRimHighlight;
    case EMCUIColorToken::Shield:return Palette.Shield;
    case EMCUIColorToken::Warning:return Palette.Warning;
    case EMCUIColorToken::LCDBackground:return Palette.LCDBackground;
    case EMCUIColorToken::LCDInk:return Palette.LCDInk;
    case EMCUIColorToken::Stamina:return Palette.Stamina;
    case EMCUIColorToken::Cleanliness:return Palette.Cleanliness;
    default:return Palette.Ink;
    }
}

FSlateFontInfo UMCGameplayUITheme::MakeFont(int32 Size,bool bBold) const
{
    FSlateFontInfo Result=bBold?(Size>=28?DisplayFont:HeadingFont):BodyFont;
    if(!Result.HasValidFont()) Result=FCoreStyle::GetDefaultFontStyle(bBold?TEXT("Bold"):TEXT("Regular"),Size);
    Result.Size=FMath::Clamp(Size,8,96);
    return Result;
}

void UMCGameplayUITheme::NotifyThemeChanged()
{
    OnThemeChanged.Broadcast();
}

#if WITH_EDITOR
void UMCGameplayUITheme::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    NotifyThemeChanged();
}

void UMCGameplayUITheme::PostEditUndo()
{
    Super::PostEditUndo();
    NotifyThemeChanged();
}
#endif
