#include "MCVFXLab.h"

#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCPlayerNameComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "InputCoreTypes.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
int32 ReadCount(const AActor* Actor,const FName Name)
{
    const auto* Property=FindFProperty<FIntProperty>(Actor->GetClass(),Name);
    if (Property) return Property->GetPropertyValue_InContainer(Actor);
    const auto* Flag=FindFProperty<FBoolProperty>(Actor->GetClass(),Name);
    return Flag && Flag->GetPropertyValue_InContainer(Actor)?1:0;
}
bool ReadFlag(const AActor* Actor,const FName Name)
{
    const auto* Property=FindFProperty<FBoolProperty>(Actor->GetClass(),Name);
    return Property && Property->GetPropertyValue_InContainer(Actor);
}
FString ReadStatus(const AActor* Actor)
{
    const auto* Property=FindFProperty<FStrProperty>(Actor->GetClass(),TEXT("Status"));
    return Property?Property->GetPropertyValue_InContainer(Actor):TEXT("Waiting for station status");
}
bool IsStation(const AActor* Actor)
{
    return Actor && Actor->ActorHasTag(TEXT("MCVFXLabStation"));
}
void SetStationRunning(AActor* Actor,const bool bEnabled)
{
    if (UFunction* Function=Actor->FindFunction(TEXT("SetRunning")))
    {
        struct FParameters { bool bEnabled; } Parameters{bEnabled};
        Actor->ProcessEvent(Function,&Parameters);
    }
}

FString StationDisplayName(const FString& Name)
{
    int32 Separator=INDEX_NONE;
    Name.FindLastChar(TEXT('_'),Separator);
    const FString Key=Separator==INDEX_NONE?Name:Name.Mid(Separator+1);
    static const TMap<FString,FString> Names={
        {TEXT("TankMelee"),TEXT("Танк: ближний удар")},
        {TEXT("TankCharge"),TEXT("Танк: рывок")},
        {TEXT("TankJump"),TEXT("Танк: прыжок")},
        {TEXT("TankRoll"),TEXT("Танк: крутилка")},
        {TEXT("TankEntrance"),TEXT("Танк: появление")},
        {TEXT("TankShieldHit"),TEXT("Танк: удар по щиту")},
        {TEXT("TankShieldBreakOverflow"),TEXT("Танк: разрушение щита")},
        {TEXT("TankDeath"),TEXT("Танк: смерть")},
        {TEXT("MageMelee"),TEXT("Маг: ближний удар")},
        {TEXT("MageFireball"),TEXT("Маг: огненный шар")},
        {TEXT("MageSummon"),TEXT("Маг: призыв")},
        {TEXT("MageNutRain"),TEXT("Маг: ореховый дождь")},
        {TEXT("MageTeleport"),TEXT("Маг: телепорт")},
        {TEXT("MageHurt"),TEXT("Маг: получение урона")},
        {TEXT("MageDeath"),TEXT("Маг: смерть")},
        {TEXT("MageEntrance"),TEXT("Маг: появление")},
        {TEXT("ZombiePunchLeft"),TEXT("Зомби: левый удар")},
        {TEXT("ZombiePunchRight"),TEXT("Зомби: правый удар")},
        {TEXT("ZombieKick"),TEXT("Зомби: удар ногой")},
        {TEXT("ZombieHurt"),TEXT("Зомби: получение урона")},
        {TEXT("ZombieDeath"),TEXT("Зомби: смерть")},
        {TEXT("ZombieRoar"),TEXT("Зомби: рёв")},
        {TEXT("Phase3PunchRight"),TEXT("Босс фазы 3: удар")},
        {TEXT("Phase3AreaAttack"),TEXT("Босс фазы 3: атака по площади")},
        {TEXT("FloorBrush"),TEXT("Пол: чистка щёткой")},
        {TEXT("FloorMeshaBrush"),TEXT("Пол: чистка меша-щёткой")},
        {TEXT("ToothBrush"),TEXT("Зуб: чистка щёткой")},
        {TEXT("ToothMeshaBrush"),TEXT("Зуб: чистка меша-щёткой")},
        {TEXT("SelfBrush"),TEXT("Чистка себя")},
        {TEXT("SelfRepair"),TEXT("Ремонт себя")},
        {TEXT("ToothRepair"),TEXT("Ремонт зуба")},
        {TEXT("SprayUlcer"),TEXT("Спрей: лечение язвы")},
        {TEXT("SprayFire"),TEXT("Спрей: тушение огня")},
        {TEXT("WatergunCare"),TEXT("Водомёт: лечение")},
        {TEXT("WatergunPressure"),TEXT("Водомёт: напор")},
        {TEXT("PickaxeCalculus"),TEXT("Кирка: зубной камень")},
        {TEXT("BufferCalculus"),TEXT("Перфоратор: зубной камень")},
        {TEXT("PickaxeIce"),TEXT("Кирка: лёд")},
        {TEXT("FrozenLegs"),TEXT("Инструмент: замороженные ноги")},
        {TEXT("BufferAoE"),TEXT("Перфоратор: область поражения")},
        {TEXT("BufferWall"),TEXT("Перфоратор: упор в стену")},
        {TEXT("KnifeFood"),TEXT("Нож: еда")},
        {TEXT("ChainsawFood"),TEXT("Бензопила: еда")},
        {TEXT("ChainsawWall"),TEXT("Бензопила: упор в стену")},
        {TEXT("CoffeeWave"),TEXT("Волна кофе и лужи")},
        {TEXT("FreezeThawAndRescue"),TEXT("Заморозка, оттаивание и спасение")},
        {TEXT("NovaProtection"),TEXT("Ледяная нова и безопасная зона")},
        {TEXT("CrystalAndIcicles"),TEXT("Кристалл и падающие сосульки")},
        {TEXT("CentralCrystal"),TEXT("Разрушение центрального кристалла")},
        {TEXT("IceBlocks"),TEXT("Падающие ледяные блоки")},
        {TEXT("PhysicsReaction"),TEXT("Отбрасывание и физические реакции")}
    };
    if (const FString* Display=Names.Find(Key)) return *Display;
    return Key;
}

/** Local viewport menu. Weak actor references cannot keep a PIE world alive. */
class SMCVFXLabNavigator : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCVFXLabNavigator) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AMCVFXLabPlayerController>,Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments)
    {
        Controller=Arguments._Controller;
        if (const auto* PC=Controller.Get()) Lab=AMCVFXLab::Find(PC->GetWorld());
        ChildSlot
        [
            // This diagnostic panel uses viewport pixel dimensions, independently of the game's DPI curve.
            SNew(SDPIScaler).DPIScale(this,&SMCVFXLabNavigator::InverseViewportScale)
            [
            SNew(SOverlay)
            +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(this,&SMCVFXLabNavigator::PanelWidth).HeightOverride(this,&SMCVFXLabNavigator::PanelHeight)
                [
                    SNew(SBorder).Padding(14).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(FLinearColor(.025f,.035f,.05f,.98f))
                    [
                        SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,7)
                        [
                            SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(STextBlock).Text(FText::FromString(TEXT("VFX-полигон"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),20)).ColorAndOpacity(FLinearColor::White)]
                            +SHorizontalBox::Slot().AutoWidth()
                            [SNew(SButton).OnClicked(this,&SMCVFXLabNavigator::Close).ContentPadding(FMargin(10,5))
                                [SNew(STextBlock).Text(FText::FromString(TEXT("Закрыть · F3")))]]
                        ]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,7)
                        [SNew(STextBlock).Text(this,&SMCVFXLabNavigator::Summary).AutoWrapText(true)
                            .ColorAndOpacity(FLinearColor(.7f,.83f,.95f)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,7)
                        [
                            SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                            [SNew(SButton).OnClicked_Lambda([this]{if (auto* PC=Controller.Get()) PC->ServerToggleRunning();return FReply::Handled();})
                                [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Lab.IsValid() && Lab->bRunning?TEXT("Пауза демонстраций"):TEXT("Продолжить демонстрации"));}).AutoWrapText(true)]]
                            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)
                            [SNew(SButton).IsEnabled_Lambda([this]{return Controller.IsValid() && Controller->StationIndex>=0;})
                                .OnClicked_Lambda([this]{if (auto* PC=Controller.Get()) PC->ServerResetCurrent();return FReply::Handled();})
                                [SNew(STextBlock).Text(FText::FromString(TEXT("Повторить текущий стенд"))).AutoWrapText(true)]]
                        ]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,7)
                        [SNew(SButton).OnClicked_Lambda([this]{return Select(INDEX_NONE);}).ContentPadding(FMargin(10,8))
                            [SNew(STextBlock).Text(FText::FromString(TEXT("Fab VFX — перейти в галерею эталонов"))).AutoWrapText(true)]]
                        +SVerticalBox::Slot().FillHeight(1)
                        [SAssignNew(List,SScrollBox).ScrollBarAlwaysVisible(true)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("Клик — телепорт к стенду. Колесо — список. F3 / Esc — закрыть.")))
                            .AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity(FLinearColor(.65f,.72f,.8f))]
                    ]
                ]
            ]
            ]
        ];
        RebuildList();
    }

    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
    {
        if (Event.GetKey()==EKeys::F3 || Event.GetKey()==EKeys::Escape) return Close();
        return FReply::Unhandled();
    }
    virtual void Tick(const FGeometry& Geometry,const double Time,const float DeltaSeconds) override
    {
        SCompoundWidget::Tick(Geometry,Time,DeltaSeconds);
        if (Time>=NextRefreshAt) {
            NextRefreshAt=Time+.5;
            if (!Lab.IsValid() && Controller.IsValid()) Lab=AMCVFXLab::Find(Controller->GetWorld());
            if (Lab.IsValid() && Lab->Results.Num()!=ListedCount) RebuildList();
        }
    }
private:
    float InverseViewportScale() const
    {
        if (const auto* PC=Controller.Get())
            if (const auto* Viewport=PC->GetWorld()->GetGameViewport())
                return 1.f/FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(Viewport));
        return 1.f;
    }
    FOptionalSize PanelWidth() const {return FOptionalSize(FMath::Clamp(float(ViewportSize().X)-24.f,200.f,680.f));}
    FOptionalSize PanelHeight() const {return FOptionalSize(FMath::Clamp(float(ViewportSize().Y)-24.f,120.f,820.f));}
    FVector2D ViewportSize() const
    {
        FVector2D Size(680,820);
        if (const auto* PC=Controller.Get()) if (const auto* Viewport=PC->GetWorld()->GetGameViewport()) Viewport->GetViewportSize(Size);
        return Size;
    }
    const FMCVFXLabResult* Row(int32 Index) const
    {return Lab.IsValid() && Lab->Results.IsValidIndex(Index)?&Lab->Results[Index]:nullptr;}
    FText Summary() const
    {
        int32 Active=0,Checked=0,Failures=0;
        if (Lab.IsValid()) for (const auto& Result:Lab->Results) {Active+=Result.bRunning;Checked+=Result.Passed>0;Failures+=Result.Failed;}
        return FText::FromString(FString::Printf(TEXT("Активны рядом: %d · Проверено: %d / %d · Ошибок: %d"),Active,Checked,ListedCount,Failures));
    }
    FReply Close()
    {if (auto* PC=Controller.Get()) if (PC->IsLabNavigationOpen()) PC->ToggleLabNavigation();return FReply::Handled();}
    FReply Select(int32 Index)
    {if (auto* PC=Controller.Get()) {PC->ServerSelectStation(Index);if (PC->IsLabNavigationOpen()) PC->ToggleLabNavigation();}return FReply::Handled();}
    void RebuildList()
    {
        List->ClearChildren();ListedCount=Lab.IsValid()?Lab->Results.Num():0;
        FString PreviousGroup;
        TSharedPtr<SWidget> CurrentButton;
        for (int32 Index=0;Index<ListedCount;++Index)
        {
            const auto& Entry=Lab->Results[Index];
            const FString Group=Entry.Name.Contains(TEXT("Boss"))?TEXT("Боссы"):Entry.Name.Contains(TEXT("Tools"))?TEXT("Инструменты и кофе"):TEXT("Зима и физика");
            if (Group!=PreviousGroup) {
                PreviousGroup=Group;
                List->AddSlot().Padding(2,9,2,5)
                [SNew(STextBlock).Text(FText::FromString(Group)).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),15))
                    .ColorAndOpacity(FLinearColor(.65f,.8f,1))];
            }
            TSharedPtr<SButton> Button;
            List->AddSlot().Padding(0,2)
            [
                SAssignNew(Button,SButton).ContentPadding(FMargin(10,7)).OnClicked_Lambda([this,Index]{return Select(Index);})
                .ToolTipText_Lambda([this,Index]{const auto* Result=Row(Index);return FText::FromString(Result?Result->Status:FString());})
                .ButtonColorAndOpacity_Lambda([this,Index]{return FSlateColor(Controller.IsValid() && Controller->StationIndex==Index?FLinearColor(.18f,.34f,.5f):FLinearColor(.08f,.11f,.15f));})
                [
                    SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text_Lambda([this,Index]{const auto* Result=Row(Index);return FText::FromString(FString::Printf(TEXT("%s%02d · %s"),
                        Controller.IsValid() && Controller->StationIndex==Index?TEXT("ЗДЕСЬ · "):TEXT(""),Index+1,Result?*StationDisplayName(Result->Name):TEXT("")));})
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).AutoWrapText(true).ColorAndOpacity(FLinearColor::White)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)
                    [SNew(STextBlock).Text_Lambda([this,Index]{const auto* Result=Row(Index);return FText::FromString(Result?FString::Printf(
                        TEXT("%s · Циклы %d · Успех %d · Ошибки %d"),Result->bRunning?TEXT("Активен"):TEXT("Неактивен"),Result->Cycles,Result->Passed,Result->Failed):FString());})
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).AutoWrapText(true)
                        .ColorAndOpacity_Lambda([this,Index]{const auto* Result=Row(Index);return FSlateColor(Result && Result->Failed>0?FLinearColor(1,.55f,.4f):FLinearColor(.6f,.84f,.7f));})]
                ]
            ];
            if (Controller.IsValid() && Controller->StationIndex==Index) CurrentButton=Button;
        }
        if (CurrentButton.IsValid()) List->ScrollDescendantIntoView(CurrentButton,false,EDescendantScrollDestination::Center);
    }
    TWeakObjectPtr<AMCVFXLabPlayerController> Controller;
    TWeakObjectPtr<AMCVFXLab> Lab;
    TSharedPtr<SScrollBox> List;
    int32 ListedCount=0;
    double NextRefreshAt=0;
};
}

AMCVFXLabFloor::AMCVFXLabFloor()
{
    bAutomaticYawns=false;
    GameplaySpawnNearDepth=0;
    GameplaySpawnSplitDepth=.5f;
    GameplaySpawnFarDepth=1;
}
void AMCVFXLabFloor::BeginPlay()
{
    Super::BeginPlay();
    Settings.IdleHeight=0;
    Settings.bAutomaticJolts=false;
    PressureSettings.bEnabled=false;
    bAutomaticYawns=false;
    ResetPain(); ResetPressure(); ResetYawn();
    SetActorTickEnabled(false);
    ForceNetUpdate();
}

AMCVFXLab::AMCVFXLab()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("LabRoot")));
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.5f;
    SetNetUpdateFrequency(2);
}
AMCVFXLab* AMCVFXLab::Find(const UWorld* World)
{
    for (TActorIterator<AMCVFXLab> It(World);It;++It) return *It;
    return nullptr;
}
void AMCVFXLab::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if (IsStation(*It)) Stations.Add(*It);
    for (TActorIterator<ANiagaraActor> It(GetWorld());It;++It)
        if (It->ActorHasTag(TEXT("FabVFXLabSample")))
            if (UNiagaraComponent* Component=It->GetNiagaraComponent())
            {
                FSample& Sample=Samples.AddDefaulted_GetRef();
                Sample.Component=Component;
                Sample.bAutoRepeat=!Component->bAutoActivate;
                Component->DeactivateImmediate();
                Component->SetComponentTickEnabled(false);
                Component->SetVisibility(false);
            }
    // Wait for every station's BeginPlay before enabling nearby fixtures.
    if (HasAuthority()) UpdateResults();
}
void AMCVFXLab::UpdateStations()
{
    if (!HasAuthority()) return;
    TArray<FVector,TInlineAllocator<4>> Viewers;
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if (const auto* PC=Cast<AMCVFXLabPlayerController>(It->Get()))
            if (PC->GetPawn()) Viewers.Add(PC->GetLabViewerLocation());
    for (const auto& Entry:Stations)
    {
        AActor* Station=Entry.Get();
        if (!Station || !Station->HasActorBegunPlay()) continue;
        const bool bWasRunning=ReadFlag(Station,TEXT("bRunning"));
        const float Radius=bWasRunning?FMath::Max(ActivationDistance,ReleaseDistance):FMath::Max(0.f,ActivationDistance);
        FBox Bounds(Station->GetActorLocation(),Station->GetActorLocation());
        if (const auto* Property=FindFProperty<FObjectPropertyBase>(Station->GetClass(),TEXT("Floor")))
            if (const auto* Floor=Cast<AActor>(Property->GetObjectPropertyValue_InContainer(Station)))
            {
                FVector Center,Extent;
                Floor->GetActorBounds(true,Center,Extent);
                Bounds=FBox(Center-Extent,Center+Extent);
            }
        bool bNearby=!bProximityActivation;
        for (const FVector& View:Viewers)
            if (Bounds.ComputeSquaredDistanceToPoint(View)<=FMath::Square(Radius)) { bNearby=true; break; }
        const bool bDesired=bRunning && bNearby;
        if (bDesired!=bWasRunning) SetStationRunning(Station,bDesired);
    }
}
void AMCVFXLab::UpdateResults()
{
    Results.Reset();
    for (const auto& Entry:Stations)
    {
        AActor* Station=Entry.Get();
        if (!Station) continue;
        FMCVFXLabResult& Row=Results.AddDefaulted_GetRef();
        Row.Station=Station;
        Row.Name=Station->Tags.Num()>1?Station->Tags[1].ToString():Station->GetName();
        Row.Status=ReadStatus(Station);
        Row.Passed=ReadCount(Station,FindFProperty<FIntProperty>(Station->GetClass(),TEXT("PassedCycles"))?TEXT("PassedCycles"):TEXT("Passed"));
        Row.Failed=ReadCount(Station,FindFProperty<FIntProperty>(Station->GetClass(),TEXT("FailedCycles"))?TEXT("FailedCycles"):TEXT("Failed"));
        Row.Cycles=ReadCount(Station,TEXT("Cycles"));
        Row.bRunning=ReadFlag(Station,TEXT("bRunning"));
    }
    Results.Sort([](const FMCVFXLabResult& A,const FMCVFXLabResult& B){return A.Name<B.Name;});
}
void AMCVFXLab::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority()) { UpdateStations(); UpdateResults(); }
    // Reference effects belong to each local viewer, not to the server's camera.
    const APlayerController* LocalController=nullptr;
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if (It->IsValid() && It->Get()->IsLocalController()) { LocalController=It->Get(); break; }
    FVector ViewLocation=FVector::ZeroVector;
    FRotator ViewRotation;
    if (LocalController) LocalController->GetPlayerViewPoint(ViewLocation,ViewRotation);
    for (FSample& Sample:Samples)
        if (UNiagaraComponent* Component=Sample.Component.Get())
        {
            const float Radius=Sample.bNearby?FMath::Max(ActivationDistance,ReleaseDistance):FMath::Max(0.f,ActivationDistance);
            const bool bNearby=bRunning && LocalController && (!bProximityActivation || FVector::DistSquared(ViewLocation,Component->GetComponentLocation())<=FMath::Square(Radius));
            if (bNearby!=Sample.bNearby)
            {
                Sample.bNearby=bNearby;
                Component->SetVisibility(bNearby);
                Component->SetComponentTickEnabled(bNearby);
                if (bNearby) { Component->Activate(true); Sample.NextRepeatAt=GetWorld()->GetTimeSeconds()+FMath::Max(1.f,SampleRepeatSeconds); }
                else Component->DeactivateImmediate();
            }
            if (bNearby && GetWorld()->GetTimeSeconds()>=Sample.NextRepeatAt)
            {
                Sample.NextRepeatAt=GetWorld()->GetTimeSeconds()+FMath::Max(1.f,SampleRepeatSeconds);
                if (Sample.bAutoRepeat || !Component->IsActive()) Component->Activate(true);
            }
        }
    if (!LocalController) return;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if (It->PlayerNameLabel) It->PlayerNameLabel->SetHiddenInGame(FVector::DistSquared(ViewLocation,It->GetActorLocation())>FMath::Square(3000.f));
    for (const auto& Entry:Stations)
        if (AActor* Station=Entry.Get())
        {
            TInlineComponentArray<UTextRenderComponent*> Labels;
            Station->GetComponents(Labels);
            for (UTextRenderComponent* Label:Labels)
                if (FVector::DistSquared(ViewLocation,Label->GetComponentLocation())<FMath::Square(2500.f))
                    Label->SetWorldRotation((ViewLocation-Label->GetComponentLocation()).Rotation());
        }
}
void AMCVFXLab::SetRunning(bool bEnabled)
{
    if (!HasAuthority()) return;
    bRunning=bEnabled;
    UpdateStations();
    UpdateResults();
    ForceNetUpdate();
}
void AMCVFXLab::ResetStation(int32 Index)
{
    if (!HasAuthority() || !Results.IsValidIndex(Index) || !IsValid(Results[Index].Station)) return;
    AActor* Station=Results[Index].Station;
    if (UFunction* Function=Station->FindFunction(TEXT("Reset"))) Station->ProcessEvent(Function,nullptr);
}
void AMCVFXLab::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVFXLab,Results);
    DOREPLIFETIME(AMCVFXLab,bRunning);
}

void AMCVFXLabPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (InputComponent)
        InputComponent->BindKey(EKeys::F3,IE_Pressed,this,&AMCVFXLabPlayerController::ToggleLabNavigation).bExecuteWhenPaused=true;
}
bool AMCVFXLabPlayerController::IsLabNavigationOpen() const
{
    return NavigationWidget.IsValid();
}
void AMCVFXLabPlayerController::ToggleLabNavigation()
{
    if (IsLabNavigationOpen()) { CloseLabNavigation(); return; }
    if (!IsLocalController() || !GetWorld() || !AMCVFXLab::Find(GetWorld())) return;
    UGameViewportClient* Viewport=GetWorld()->GetGameViewport();
    if (!Viewport) return;
    NavigationWidget=SNew(SMCVFXLabNavigator).Controller(TWeakObjectPtr<AMCVFXLabPlayerController>(this));
    NavigationViewport=Viewport;
    Viewport->AddViewportWidgetContent(NavigationWidget.ToSharedRef(),100);
    if (APawn* ControlledPawn=GetPawn())
    {
        ControlledPawn->ConsumeMovementInputVector();
        if (UPawnMovementComponent* Movement=ControlledPawn->GetMovementComponent()) Movement->StopMovementImmediately();
    }
    SetIgnoreMoveInput(true);
    SetIgnoreLookInput(true);
    SetShowMouseCursor(true);
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(NavigationWidget);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Mode.SetHideCursorDuringCapture(false);
    SetInputMode(Mode);
}
void AMCVFXLabPlayerController::CloseLabNavigation()
{
    if (!NavigationWidget.IsValid()) return;
    if (UGameViewportClient* Viewport=NavigationViewport.Get())
        Viewport->RemoveViewportWidgetContent(NavigationWidget.ToSharedRef());
    NavigationWidget.Reset();
    NavigationViewport.Reset();
    SetIgnoreMoveInput(false);
    SetIgnoreLookInput(false);
    SetShowMouseCursor(false);
    SetInputMode(FInputModeGameOnly());
}
void AMCVFXLabPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseLabNavigation();
    Super::EndPlay(Reason);
}
void AMCVFXLabPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (!IsLocalController()) return;
    const AMCVFXLab* Lab=AMCVFXLab::Find(GetWorld());
    if (!Lab || Lab->Results.IsEmpty()) return;
    if (!bInitialFrame && GetPawn()) { bInitialFrame=true; ServerNavigate(0); }
    if (!HasAuthority() && GetPawn() && GetWorld()->GetTimeSeconds()>=NextViewerReportAt)
    {
        NextViewerReportAt=GetWorld()->GetTimeSeconds()+.25;
        ServerReportViewerPosition(GetPawn()->GetActorLocation());
    }
    if (IsLabNavigationOpen()) return;
    if (WasInputKeyJustPressed(EKeys::PageDown)) ServerNavigate(1);
    if (WasInputKeyJustPressed(EKeys::PageUp)) ServerNavigate(-1);
    if (WasInputKeyJustPressed(EKeys::P)) ServerToggleRunning();
    if (WasInputKeyJustPressed(EKeys::R)) ServerResetCurrent();
}
void AMCVFXLabPlayerController::ServerNavigate_Implementation(int32 Direction)
{
    const AMCVFXLab* Lab=AMCVFXLab::Find(GetWorld());
    if (!Lab || Lab->Results.IsEmpty() || !GetPawn()) return;
    Direction=FMath::Clamp(Direction,-1,1);
    const int32 Index=StationIndex==INDEX_NONE?(Direction<0?Lab->Results.Num()-1:0):(StationIndex+Direction+Lab->Results.Num())%Lab->Results.Num();
    GotoStation(Index);
}
void AMCVFXLabPlayerController::ServerSelectStation_Implementation(int32 Index)
{
    GotoStation(Index);
}
void AMCVFXLabPlayerController::GotoStation(int32 Index)
{
    const AMCVFXLab* Lab=AMCVFXLab::Find(GetWorld());
    if (!HasAuthority() || !Lab || !GetPawn()) return;
    FVector Target,View;
    if (Index==INDEX_NONE)
    {
        TArray<ANiagaraActor*> Gallery;
        for (TActorIterator<ANiagaraActor> It(GetWorld());It;++It)
            if (It->ActorHasTag(TEXT("FabVFXLabSample"))) Gallery.Add(*It);
        Gallery.Sort([](const ANiagaraActor& A,const ANiagaraActor& B){return A.GetName()<B.GetName();});
        if (Gallery.IsEmpty()) return;
        Target=Gallery[0]->GetActorLocation()+FVector(0,0,80);
        View=Target+FVector(1200,-1200,700);
    }
    else
    {
        if (Lab->Results.IsEmpty()) return;
        Index=FMath::Clamp(Index,0,Lab->Results.Num()-1);
        const auto& Row=Lab->Results[Index];
        if (!IsValid(Row.Station)) return;
        Target=Row.Station->GetActorLocation()+FVector(180,0,120);
        View=Target+FVector(1000,-1000,560);
    }
    StationIndex=Index;
    if (UPawnMovementComponent* Movement=GetPawn()->GetMovementComponent()) Movement->StopMovementImmediately();
    GetPawn()->SetActorLocation(View,false,nullptr,ETeleportType::TeleportPhysics);
    const FRotator Rotation=(Target-View).Rotation();
    SetControlRotation(Rotation);
    ReportedViewerLocation=View;
    ViewerReportedAt=GetWorld()->GetTimeSeconds();
    ClientSetLocation(View,Rotation);
    ForceNetUpdate();
}
FVector AMCVFXLabPlayerController::GetLabViewerLocation() const
{
    if (!IsLocalController() && GetWorld()->GetTimeSeconds()-ViewerReportedAt<=2)
        return ReportedViewerLocation;
    return GetPawn()?GetPawn()->GetActorLocation():FVector::ZeroVector;
}
void AMCVFXLabPlayerController::ServerReportViewerPosition_Implementation(FVector_NetQuantize10 Location)
{
    const FVector Point=Location;
    if (!GetPawn() || !AMCVFXLab::Find(GetWorld()) || Point.ContainsNaN() || Point.GetAbsMax()>1000000) return;
    ReportedViewerLocation=Point;
    ViewerReportedAt=GetWorld()->GetTimeSeconds();
}
void AMCVFXLabPlayerController::ServerToggleRunning_Implementation()
{
    if (auto* Lab=AMCVFXLab::Find(GetWorld())) Lab->SetRunning(!Lab->bRunning);
}
void AMCVFXLabPlayerController::ServerResetCurrent_Implementation()
{
    if (auto* Lab=AMCVFXLab::Find(GetWorld())) Lab->ResetStation(StationIndex);
}
void AMCVFXLabPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCVFXLabPlayerController,StationIndex);
}

void AMCVFXLabHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    const AMCVFXLab* Lab=AMCVFXLab::Find(GetWorld());
    const auto* PC=Cast<AMCVFXLabPlayerController>(GetOwningPlayerController());
    if (!Lab || !PC) return;
    int32 Covered=0,Failures=0,Active=0;
    for (const auto& Row:Lab->Results) { Covered+=Row.Passed>0; Failures+=Row.Failed; Active+=Row.bRunning; }
    DrawRect(FLinearColor(0,0,0,.75f),18,18,760,130);
    DrawText(FString::Printf(TEXT("VFX LAB   %s   Active %d / %d   Checked %d   Failures %d"),Lab->bRunning?TEXT("NEARBY"):TEXT("PAUSED"),Active,Lab->Results.Num(),Covered,Failures),FLinearColor::White,30,28);
    DrawText(TEXT("F3: list + teleport   PageUp / PageDown: station   R: reset   P: pause/resume"),FLinearColor(.7f,.85f,1),30,52);
    if (Lab->Results.IsValidIndex(PC->StationIndex))
    {
        const auto& Row=Lab->Results[PC->StationIndex];
        DrawText(FString::Printf(TEXT("%02d   %s   cycles %d   passed %d   failed %d"),PC->StationIndex+1,*Row.Name,Row.Cycles,Row.Passed,Row.Failed),FLinearColor::White,30,78);
        DrawText(Row.Status.Replace(TEXT("\n"),TEXT(" | ")).Left(105),Row.Failed>0?FLinearColor(1,.5f,.3f):FLinearColor(.55f,1,.75f),30,104);
    }
    else if (PC->StationIndex==INDEX_NONE)
    {
        DrawText(TEXT("FAB VFX GALLERY"),FLinearColor::White,30,78);
        DrawText(TEXT("Nearby reference effects are active. F3: choose another stand."),FLinearColor(.55f,1,.75f),30,104);
    }
}

AMCVFXLabGameMode::AMCVFXLabGameMode()
{
    GameStateClass=AMCGameState::StaticClass();
    PlayerStateClass=AMCPlayerState::StaticClass();
    PlayerControllerClass=AMCVFXLabPlayerController::StaticClass();
    DefaultPawnClass=ASpectatorPawn::StaticClass();
    HUDClass=AMCVFXLabHUD::StaticClass();
}
void AMCVFXLabGameMode::StartPlay()
{
    if (auto* GS=GetGameState<AMCGameState>())
    {
        GS->Day=1; GS->Phase=EMCShiftPhase::Working;
        GS->bDevManualEvents=true;
        GS->PhaseEndsAt=GetWorld()->GetTimeSeconds()+86400;
    }
    Super::StartPlay();
}
