#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "Blueprint/UserWidget.h"
#include "UObject/StrongObjectPtr.h"

UMCGameplayUITheme* UMCGameplayUIFactory::GetMasterTheme()
{
    // A soft-loaded master has no owner otherwise. Keep its authored palette
    // and class map alive across gameplay garbage collections.
    static TStrongObjectPtr<UMCGameplayUITheme> Master;
    if(!Master.IsValid()) {
        constexpr const TCHAR* Path=TEXT("/Game/Gameplay/UI/Theme/DA_GameplayUITheme.DA_GameplayUITheme");
        Master.Reset(FindObject<UMCGameplayUITheme>(nullptr,Path));
        if(!Master.IsValid()) Master.Reset(LoadObject<UMCGameplayUITheme>(nullptr,Path,nullptr,LOAD_NoWarn|LOAD_Quiet));
    }
    return Master.IsValid()?Master.Get():GetMutableDefault<UMCGameplayUITheme>();
}

UClass* UMCGameplayUIFactory::ResolveClass(FName Role,UClass* NativeFallback)
{
    constexpr EClassFlags Rejected=CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists;
    if(!NativeFallback || !NativeFallback->IsChildOf(UUserWidget::StaticClass()) || NativeFallback->HasAnyClassFlags(Rejected)) return nullptr;
    const auto* Master=GetMasterTheme();
    if(!Master || Role.IsNone()) return NativeFallback;
    const auto* Entry=Master->WidgetClasses.Find(Role);
    if(!Entry || Entry->IsNull()) return NativeFallback;
    UClass* Authored=Entry->LoadSynchronous();
    return Authored && Authored->IsChildOf(NativeFallback) && !Authored->HasAnyClassFlags(Rejected)?Authored:NativeFallback;
}
