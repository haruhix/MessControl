#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCHubInteractionPromptWidget.generated.h"

class UTextBlock;

/** Designer-editable keycap shared by local wardrobe and vending-machine prompts. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCHubInteractionPromptWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub|Prompt")
    FText KeyLabel=FText::FromString(TEXT("E"));

    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;

protected:
    virtual void NativeOnInitialized() override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> HubInteractionPromptKey;
};
