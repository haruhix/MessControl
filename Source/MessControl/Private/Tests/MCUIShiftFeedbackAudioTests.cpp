#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "MCDataAssets.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCEventSFXPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"

namespace MCUIShiftFeedbackAudioTests
{
bool HasPIE()
{for(const auto& Context:GEngine->GetWorldContexts())if(Context.WorldType==EWorldType::PIE)return true;return false;}

/** Real local ownership graph and a genuinely prepared transient audio channel.
 * No GameInstance/viewport Init, BeginPlay, saved owner, real UI/window or audio
 * device startup. This is not a location-barrier or audible certification. */
struct FOwnedContext
{
    UWorld* World=nullptr;UGameInstance* Instance=nullptr;UGameViewportClient* Viewport=nullptr;
    ULocalPlayer* Local=nullptr;AMCPlayerController* Controller=nullptr;AMCToothCharacter* Hero=nullptr;
    UMCPrototypeWidget* Widget=nullptr;AMCGameState* State=nullptr;
    UMCWorldPresentationComponent* Wrapper=nullptr;UMCVFXPresentationComponent* Channel=nullptr;
    FOwnedContext()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);World->bAllowAudioPlayback=false;
        auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
        Instance=NewObject<UGameInstance>(GEngine);World->SetGameInstance(Instance);
        Controller=World->SpawnActor<AMCPlayerController>();Hero=World->SpawnActor<AMCToothCharacter>();
        World->AddController(Controller);Controller->SpawnPlayerCameraManager();
        Controller->Possess(Hero);Controller->SetViewTarget(Hero);Controller->SetAsLocalPlayerController();
        Viewport=NewObject<UGameViewportClient>(GEngine);
        FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("World"))->SetObjectPropertyValue_InContainer(Viewport,World);
        FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("GameInstance"))->SetObjectPropertyValue_InContainer(Viewport,Instance);
        Context.OwningGameInstance=Instance;Context.GameViewport=Viewport;Instance->OnWorldChanged(nullptr,World);
        Local=NewObject<ULocalPlayer>(GEngine);Instance->AddLocalPlayer(Local,PLATFORMUSERID_NONE);
        Local->ViewportClient=Viewport;Local->PlayerController=Controller;Controller->Player=Local;
        Widget=NewObject<UMCPrototypeWidget>(Instance);Widget->SetOwningPlayer(Controller);Controller->PrototypeWidget=Widget;
        State=World->SpawnActor<AMCGameState>();World->SetGameState(State);State->Phase=EMCShiftPhase::Working;
        Hero->SoundPalette=nullptr;
        Wrapper=NewObject<UMCWorldPresentationComponent>(Controller);Controller->AddInstanceComponent(Wrapper);Wrapper->RegisterComponent();
        Channel=NewObject<UMCVFXPresentationComponent>(Controller);Channel->SetIsReplicated(false);
        Channel->SourceID=TEXT("UI.ShiftFeedback");Channel->Events=MCVFXSourceContracts::Find(Channel->SourceID)->Events;
        Controller->AddInstanceComponent(Channel);Channel->RegisterComponent();
        auto* Property=FindFProperty<FMapProperty>(UMCWorldPresentationComponent::StaticClass(),TEXT("Channels"));
        FScriptMapHelper Map(Property,Property->ContainerPtrToValuePtr<void>(Wrapper));
        const int32 Index=Map.AddDefaultValue_Invalid_NeedsRehash();
        CastFieldChecked<FNameProperty>(Property->KeyProp)->SetPropertyValue(Map.GetKeyPtr(Index),Channel->SourceID);
        CastFieldChecked<FObjectPropertyBase>(Property->ValueProp)->SetObjectPropertyValue(Map.GetValuePtr(Index),Channel);Map.Rehash();
    }
    ~FOwnedContext()
    {
        Channel->StopPresentation(true);Controller->Player=nullptr;Local->PlayerController=nullptr;
        Instance->RemoveLocalPlayer(Local);Instance->OnWorldChanged(World,nullptr);
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    int32 AudioCount() const
    {int32 Count=0;for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)if(It->IsRunning())++Count;return Count;}
    bool Prepare(EMCEventSFXMode Mode,FString& Error)
    {
        Channel->StopPresentation(true);
        auto* Profile=NewObject<UMCPerkVFXProfile>();
        for(const FName Event:Channel->Events) {
            FMCEventSFXSlot Slot;Slot.Mode=Mode;Slot.MaxDurationSeconds=3.f;
            auto* Wave=NewObject<USoundWave>();Wave->Duration=1.f;
            FMCEventSFXLayer Layer;Layer.Sound=Wave;Layer.DelaySeconds=.8f;Slot.Layers.Add(Layer);
            Profile->SFXSlots.Add(Event,Slot);
        }
        return Channel->PrepareSourceProfile(Channel->SourceID,Channel->Events,Profile,Error,false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIShiftFeedbackPreparedAudio,
    "MessControl.Audio.UIShiftFeedback.PreparedOwnedBoundaryAndModes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUIShiftFeedbackPreparedAudio::RunTest(const FString&)
{
    using namespace MCUIShiftFeedbackAudioTests;
    if(HasPIE()){AddError(TEXT("Run outside PIE; only a disposable local ownership graph is used."));return false;}
    FOwnedContext F;FString Error;
    TestTrue(TEXT("Exact current local controller ownership is real"),F.Controller->HasLocalPresentationOwner());
    const auto* Contract=MCVFXSourceContracts::Find(TEXT("UI.ShiftFeedback"));
    TestTrue(TEXT("Phase audio uses only the existing finite Interface library"),Contract&&Contract->Library==MCVFXSourceContracts::InterfaceLibraryPath());
    TestFalse(TEXT("Unprepared events are refused without loading a library from the widget"),F.Controller->PresentShiftFeedback(F.Widget,TEXT("Start"),F.Hero));
    TestEqual(TEXT("Refused event creates no custom voice"),F.AudioCount(),0);
    for(const auto Mode:{EMCEventSFXMode::UseDefault,EMCEventSFXMode::Add,EMCEventSFXMode::Replace,EMCEventSFXMode::Disabled}) {
        if(!F.Prepare(Mode,Error)){AddError(Error);return false;}
        TestTrue(TEXT("A genuine prepared event is accepted even without a native palette/clip"),F.Controller->PresentShiftFeedback(F.Widget,TEXT("Start"),F.Hero));
        TestEqual(TEXT("Only Add/Replace schedule independent custom audio"),F.AudioCount(),Mode==EMCEventSFXMode::Add||Mode==EMCEventSFXMode::Replace?1:0);
        TestEqual(TEXT("Only Default/Add permit the original palette output"),F.Wrapper->UsesOriginalAudio(TEXT("UI.ShiftFeedback"),TEXT("Start")),Mode==EMCEventSFXMode::UseDefault||Mode==EMCEventSFXMode::Add);
        for(const FName Event:{FName(TEXT("Start")),FName(TEXT("Win")),FName(TEXT("Lose"))}) {
            const auto* Original=Contract->OriginalAudio.Find(Event);
            TestTrue(TEXT("Each real boundary has one original palette selector"),Original&&Original->bSuppressionWired
                &&Original->PaletteEvent==(Event==TEXT("Start")?FName(TEXT("DayStart")):Event));
        }
        auto* Palette=NewObject<UMCSoundPalette>();FMCSoundVariation Variation;
        auto* A=NewObject<USoundWave>();auto* B=NewObject<USoundWave>();A->Duration=B->Duration=1.f;
        Variation.Sounds={A,B};Variation.PitchMin=.9f;Variation.PitchMax=1.1f;Palette->Events.Add(TEXT("DayStart"),Variation);
        FMath::RandInit(507);(void)FMath::RandRange(0,1);(void)FMath::FRandRange(.9f,1.1f);const int32 Expected=FMath::Rand();
        FMath::RandInit(507);
        TestTrue(TEXT("Original widget WorldContext remains usable with a separate policy controller"),Palette->PlayWithParameters(F.Widget,TEXT("DayStart"),F.Hero->GetActorLocation(),.65f,.5f,TEXT("UI.ShiftFeedback"),TEXT("Start"),F.Controller));
        TestEqual(TEXT("Every mode preserves one native variation and pitch draw"),FMath::Rand(),Expected);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIShiftFeedbackOwnershipRefusal,
    "MessControl.Audio.UIShiftFeedback.OwnerPhaseAndResetIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUIShiftFeedbackOwnershipRefusal::RunTest(const FString&)
{
    using namespace MCUIShiftFeedbackAudioTests;
    if(HasPIE()){AddError(TEXT("Run outside PIE."));return false;}
    FOwnedContext F;FString Error;if(!F.Prepare(EMCEventSFXMode::Add,Error)){AddError(Error);return false;}
    TestFalse(TEXT("Won cue cannot be manufactured during Working"),F.Controller->PresentShiftFeedback(F.Widget,TEXT("Win"),F.Hero));
    auto* OtherWidget=NewObject<UMCPrototypeWidget>(F.Instance);OtherWidget->SetOwningPlayer(F.Controller);
    TestFalse(TEXT("A second widget is not inferred as the actual phase producer"),F.Controller->PresentShiftFeedback(OtherWidget,TEXT("Start"),F.Hero));
    F.Local->ViewportClient=nullptr;
    TestFalse(TEXT("No-viewport/private context cannot dispatch custom UI audio"),F.Controller->PresentShiftFeedback(F.Widget,TEXT("Start"),F.Hero));
    F.Local->ViewportClient=F.Viewport;
    TestEqual(TEXT("Refusals do not schedule audio"),F.AudioCount(),0);
    F.State->Phase=EMCShiftPhase::Won;
    TestTrue(TEXT("Actual Won phase selects its real event"),F.Controller->PresentShiftFeedback(F.Widget,TEXT("Win"),F.Hero));
    TestEqual(TEXT("One accepted boundary schedules one independent voice"),F.AudioCount(),1);
    F.Channel->StopPresentation(true);
    TestEqual(TEXT("The existing source reset cleans pending artist delay"),F.AudioCount(),0);
    TestFalse(TEXT("Unknown events cannot suppress unrelated originals"),MCVFXSourceContracts::CanSuppressOriginalSFX(TEXT("UI.ShiftFeedback"),TEXT("Other")));
    return !HasAnyErrors();
}

#endif
