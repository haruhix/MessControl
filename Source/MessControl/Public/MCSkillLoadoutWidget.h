#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCSkillLoadoutWidget.generated.h"
class UMCPerkComponent;
class UTextBlock;
class UImage;
class UWrapBox;
class UScrollBox;
/** Presentation of PlayerState loadout; no independent ownership or activation state. */
UCLASS()
class MESSCONTROL_API UMCSkillLoadoutWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    virtual void BuildPresentationTree() override;
    void Refresh(const UMCPerkComponent* Perks);
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Titles;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatusLabels;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Icons;
    UPROPERTY(Transient) TObjectPtr<UWrapBox> Passives;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> PassiveScroll;
    FString PassiveSignature;
};
