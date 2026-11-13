#pragma once

#include "MCGameplayUIWidget.h"
#include "MCRaidLoadoutWidget.generated.h"

class AMCPlayerController;
class UMCHubWidget;
class UMCUniversalUIWidget;
class UTexture2D;
class UVerticalBox;

UENUM(BlueprintType)
enum class EMCRaidPerkRowStyle : uint8 { Active, Passive, Stock, Team };

/** Shared, independently editable row for the loadout, inventory, and squad. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCRaidPerkRowWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()

public:
    UMCRaidPerkRowWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    void Present(EMCRaidPerkRowStyle Style,const FText& Name,const FText& Detail,UTexture2D* Icon,
        const FText& Tooltip,bool bAction,bool bSelected=false,bool bReady=false);
    FSimpleMulticastDelegate OnPressed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor ActiveFill;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor ActiveOutline;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor PassiveFill;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor PassiveOutline;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor StockFill;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor StockOutline;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Style") FLinearColor SelectedOutline;

private:
    UFUNCTION() void Pressed();
};

/** Full-viewport side rails for the Hub's existing location selector. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCRaidLoadoutWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()

public:
    UMCRaidLoadoutWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    void SetBackend(UMCHubWidget* Source);
    void RefreshFromHub();
    bool CloseStock();
    void Confirm();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Raid|Presentation")
    TSoftClassPtr<UMCRaidPerkRowWidget> RowClass;
    /** Keeps the two authored rails in the available margins around the mouths. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Raid|Presentation") bool bFitMouthGutters=true;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;

private:
    struct FPerkView
    {
        FName ID;
        FText Name;
        FText Description;
        TSoftObjectPtr<UTexture2D> Icon;
        int32 Copies=0;
        bool bPassive=false;
    };
    struct FMemberView
    {
        FText Name;
        bool bReady=false;
    };
    struct FView
    {
        TArray<FName> Active,Passive;
        TArray<FPerkView> Stock;
        TArray<FMemberView> Team;
        FText Status;
        bool bCanEdit=false,bCanReady=false,bReady=false,bCanStart=false;
    };

    TWeakObjectPtr<UMCHubWidget> Backend;
    UPROPERTY(Transient) TObjectPtr<UClass> PreparedRowClass;
    FView View;
    FString LastSignature;
    FVector2D LastViewportSize=FVector2D::ZeroVector;
    int32 EditSlot=INDEX_NONE;
    bool bEditPassive=false;
    FName EditingID;
    int32 LastDay=INDEX_NONE,LastSceneGeneration=INDEX_NONE;

    void BindControls();
    void FitGutters(FVector2D Size);
    void Present();
    void PresentCategory(bool bPassive);
    void PresentStock();
    void PresentTeam();
    void Edit(bool bPassive,int32 SlotIndex,FName ID);
    void RequestPerk(FName ID);
    const FPerkView* FindPerk(FName ID) const;
    bool IsAvailable(const FPerkView& Item,bool bPassive) const;
    UMCRaidPerkRowWidget* AddRow(UVerticalBox* Host,EMCRaidPerkRowStyle Style,const FText& Name,
        const FText& Detail=FText(),UTexture2D* Icon=nullptr,const FText& Tooltip=FText(),
        bool bAction=false,bool bSelected=false,bool bReady=false);
    UFUNCTION() void ToggleReady();
    UFUNCTION() void Back();
    UFUNCTION() void Start();
    UFUNCTION() void DismissStock();
};

