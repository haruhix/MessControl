#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPresentationEvents.h"
#include "MCPerkTypes.h"
#include "MCPerkComponent.h"
#include "MCPerkVFXProfile.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCVFXPresentationComponent.h"
#include "MCEventSFXPlayer.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPassivePresentationCanonicalContracts,
    "MessControl.Passives.Presentation.CanonicalRowsHaveRuntimeEventContracts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPassivePresentationCanonicalContracts::RunTest(const FString&)
{
    const auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Gameplay/Roguelike/DT_Perks.DT_Perks"));
    if(!TestNotNull(TEXT("Saved canonical perk table"),Table) || !TestTrue(TEXT("Correct row type"),Table->GetRowStruct()==FMCPerkDefinition::StaticStruct()))return false;
    int32 CurrentPassives=0;
    for(FName ID:Table->GetRowNames()) {
        const auto* Row=Table->FindRow<FMCPerkDefinition>(ID,TEXT("Passive event contract"),false);
        if(!Row || !Row->bEnabled || Row->Kind!=EMCPerkKind::Passive)continue;
        ++CurrentPassives;const auto& Events=MCPassivePresentation::GetAllowedEvents(ID);
        TestTrue(*(ID.ToString()+TEXT(" declares actual native passive events")),!Events.IsEmpty() && Events.Num()<=32);
        TestTrue(*(ID.ToString()+TEXT(" has true acquisition/incarnation lifecycle")),Events.Contains(TEXT("Equipped")) && Events.Contains(TEXT("State")));
        TSet<FName> Unique;for(FName Event:Events){TestFalse(TEXT("No empty declared event"),Event.IsNone());Unique.Add(Event);}
        TestEqual(TEXT("No duplicate event IDs"),Unique.Num(),Events.Num());
    }
    TestEqual(TEXT("All current27 enabled passive rows are covered"),CurrentPassives,27);
    TestTrue(TEXT("Active-skill legacy event contract is not replaced by the passive helper"),MCPassivePresentation::GetAllowedEvents(TEXT("Consumable_Rocket")).IsEmpty());
    TestTrue(TEXT("Unknown IDs do not acquire fabricated profile support"),MCPassivePresentation::GetAllowedEvents(TEXT("B1_NotARealPassive")).IsEmpty());
    TestTrue(TEXT("No hero/ownership/preparation retains old feedback"),MCPassivePresentation::Play(nullptr,TEXT("B2_CleanBreath"),TEXT("Proc"),FTransform::Identity)==EMCVFXPresentationResult::UseDefault);
    TestTrue(TEXT("A refused cosmetic also retains old feedback"),MCPassivePresentation::UsesNativeFallback(EMCVFXPresentationResult::Rejected));
    TestFalse(TEXT("Custom cosmetic cannot duplicate native fallback"),MCPassivePresentation::UsesNativeFallback(EMCVFXPresentationResult::Played));
    TestFalse(TEXT("Disabled cosmetic cannot leak native fallback"),MCPassivePresentation::UsesNativeFallback(EMCVFXPresentationResult::Suppressed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPassiveAudioOnlyEpisodeExpiration,
    "MessControl.Passives.Presentation.AudioOnlyAddRetainsEpisodeUntilExpiry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPassiveAudioOnlyEpisodeExpiration::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);World->bAllowAudioPlayback=false;
    struct FWorldCleanup{UWorld* W;~FWorldCleanup(){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}} Cleanup{World};
    auto* State=World->SpawnActor<AMCPlayerState>();auto* Hero=World->SpawnActor<AMCToothCharacter>();Hero->SetPlayerState(State);
    auto* Perks=State->Perks.Get();const FName ID(TEXT("B2_CleanAcceleration"));FString Error;
    if(!TestNotNull(TEXT("Native owned perk component"),Perks)||!Perks->EnsurePresentationForPawn())return false;
    auto* SavedTable=Perks->GetPerkTable();if(!TestNotNull(TEXT("Canonical table is read only"),SavedTable))return false;
    auto* Profile=NewObject<UMCPerkVFXProfile>();TStrongObjectPtr<UMCPerkVFXProfile> ProfileKeep(Profile);
    auto* Wave=NewObject<USoundWave>();Wave->Duration=5.f;TStrongObjectPtr<USoundWave> WaveKeep(Wave);
    FMCEventSFXLayer Layer;Layer.Sound=Wave;Layer.DelaySeconds=.5f;Layer.bRepeatOnFinish=true;
    FMCEventSFXSlot Audio;Audio.Mode=EMCEventSFXMode::Add;Audio.Layers.Add(Layer);Profile->SFXSlots.Add(TEXT("Proc"),Audio);
    if(!TestTrue(TEXT("Actual scoped audio-only profile prepares without audio output"),
        Hero->PassivePresentation->PrepareScopedOverride(ID,MCPassivePresentation::GetAllowedEvents(ID),Profile,Error)))
    {AddError(Error);return false;}
    if(!TestTrue(TEXT("Actual native grant owns the source before its presentation"),Perks->ServerGrantPerk(ID)))return false;
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=FGuid::NewGuid();Values.Duration=3.f;
    const auto Result=MCPassivePresentation::Play(Hero,ID,TEXT("Proc"),Hero->GetActorTransform(),Values);
    TestTrue(TEXT("Audio Add does not suppress the original VFX result"),Result==EMCVFXPresentationResult::UseDefault);
    TestTrue(TEXT("A genuine pending audio player owns the helper episode"),Hero->PassivePresentation->IsSourceEpisodeRunning(ID,Values.EpisodeID));
    int32 PendingBefore=0;for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)PendingBefore+=It->GetPendingLayerCount();
    TestTrue(TEXT("The test exercises a delayed sound, not a manufactured receipt"),PendingBefore>0);
    // Model the real buff-expiration boundary; no cleanup/timing/gameplay RNG is replaced.
    Perks->CleanSpeedUntil=-1.;MCPassivePresentation::TickStates(Hero);
    for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)It->Advance(.2f);
    TestFalse(TEXT("Actual helper expiration cancels its audio-only episode"),Hero->PassivePresentation->IsSourceEpisodeRunning(ID,Values.EpisodeID));
    int32 PendingAfter=0;for(TActorIterator<AMCEventSFXPlayer> It(World);It;++It)PendingAfter+=It->GetPendingLayerCount();
    TestEqual(TEXT("No delayed/repeating sound can start after the buff ended"),PendingAfter,0);
    return true;
}
#endif
