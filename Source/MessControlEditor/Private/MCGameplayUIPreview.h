#pragma once

class UMCGameplayUITheme;
class UMCGameplayUIWidget;

/** Editor-only registration. Never creates play worlds or changes saved widget defaults. */
namespace MCGameplayUIPreview
{
    void EnsureRegistered();
    void Unregister();
    void OpenThemeEditor(UMCGameplayUITheme* Theme = nullptr);
    /** Apply fixture visibility/enabled states to transient Designer Slate wrappers. */
    void SynchronizeFixtureTree(UMCGameplayUIWidget* Widget);
}
