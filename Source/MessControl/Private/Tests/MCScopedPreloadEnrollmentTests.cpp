#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCPreloadManifest.h"
#include "MCGameState.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXPresentationLibrary.h"
#include "MCVFXSourceContracts.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "UObject/Package.h"

namespace MCScopedPreloadTest
{
struct FWorld
{
    UPackage* Package=CreatePackage(*FString::Printf(TEXT("/Temp/ScopedPreload_%s/L_Mouth"),*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UWorld* World=nullptr;
    AMCGameState* State=nullptr;
    UMCWorldPreloadSubsystem* Loading=nullptr;
    FWorld()
    {
        Package->SetFlags(RF_Transient);
        World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("L_Mouth"),Package);
        if(!World)return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->bAllowAudioPlayback=false;
        State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
        Loading=World->GetSubsystem<UMCWorldPreloadSubsystem>();
        if(Loading)Loading->SetTickableTickType(ETickableTickType::Never);
    }
    ~FWorld()
    {
        if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
        Package->SetDirtyFlag(false);
    }
};

class FPoll final : public IAutomationLatentCommand
{
public:
    explicit FPoll(TFunction<bool()> InPoll):Poll(MoveTemp(InPoll)){}
    virtual bool Update()override{return Poll();}
private:
    TFunction<bool()> Poll;
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCScopedPreloadEnrollment,
    "MessControl.Loading.Scope.FreezeEnrollment",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FMCScopedPreloadEnrollment::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    for(const TCHAR* Case:{TEXT("DelayedRequired"),TEXT("RequiredBeforeLibraryExpansion"),TEXT("NotRequiredAndEpochReset"),TEXT("CompletedRootWaveWaitsForScope")})
    {Names.Add(Case);Commands.Add(Case);}
}

bool FMCScopedPreloadEnrollment::RunTest(const FString& Case)
{
    using namespace MCScopedPreloadTest;
    auto T=MakeShared<FWorld>();
    if(!TestNotNull(TEXT("Isolated location has its actual preparation service"),T->Loading)
        || !TestNotNull(TEXT("Isolated location has an authoritative scope fixture"),T->State))return false;
    auto* Loading=T->Loading;auto* State=T->State;
    if(!TestTrue(TEXT("Fixture uses the production Mouth scope rules"),Loading->IsIcePresentationLocation()))return false;
    auto* Library=Cast<UMCVFXPresentationLibrary>(MCVFXSourceContracts::WorldLibraryPath().TryLoad());
    if(!TestNotNull(TEXT("Current saved world library supplies the real scoped edge"),Library))return false;
    const auto* Ref=Library->Profiles.Find(TEXT("Character.FreezeDeath"));
    if(!TestTrue(TEXT("Saved library has an authored Freeze profile"),Ref&&!Ref->IsNull()))return false;
    const FSoftObjectPath ProfilePath=Ref->ToSoftObjectPath();
    AddInfo(FString::Printf(TEXT("Saved Freeze profile %s initially resident=%d; service enrollment/retention is checked independently of global residency"),
        *ProfilePath.ToString(),ProfilePath.ResolveObject()!=nullptr));
    State->GameplayLoadId=FGuid::NewGuid();
    State->IcePresentationScope.GameplayLoadId=State->GameplayLoadId;
    State->IcePresentationScope.Scope=EMCIcePresentationScope::Unknown;
    TArray<FSoftObjectPath> Initial;
    FMCPreloadManifest::CollectSoftReferences(Library,Initial,T->World);
    TestFalse(TEXT("Unknown authoritative scope omits the Freeze edge during the original expansion"),Initial.Contains(ProfilePath));
    // Model completed generic library discovery. Its non-scoped siblings are
    // already enrolled; this service has never requested or retained Freeze.
    const bool bCompletedRoot=Case==TEXT("CompletedRootWaveWaitsForScope");
    if(!bCompletedRoot)Loading->SeenPaths.Add(MCVFXSourceContracts::WorldLibraryPath());
    for(const auto& Path:Initial)Loading->SeenPaths.Add(Path);
    if(!bCompletedRoot)Loading->RetainedAssets.Add(Library);
    if(!bCompletedRoot&&Case!=TEXT("RequiredBeforeLibraryExpansion"))Loading->ExpandedObjects.Add(Library);
    Loading->Phase=EMCWorldPreloadPhase::PreparingRendering;
    bool Required=false;
    if(Case==TEXT("DelayedRequired")) {
        const int32 Before=Loading->SeenPaths.Num();
        TestFalse(TEXT("Unknown scope waits without advancing presentation"),Loading->EnsureScopedPresentationDependencies(Required));
        TestFalse(TEXT("Unknown scope cannot fabricate Required"),Required);
        TestEqual(TEXT("Unknown scope does not enqueue speculative assets"),Loading->SeenPaths.Num(),Before);
        TestFalse(TEXT("Unknown scope owns no asynchronous wave"),Loading->ActiveHandle.IsValid());
    }
    if(Case==TEXT("NotRequiredAndEpochReset")) {
        State->IcePresentationScope.Scope=EMCIcePresentationScope::NotRequired;
        TestTrue(TEXT("An explicit non-Ice run proceeds without Freeze"),Loading->EnsureScopedPresentationDependencies(Required));
        TestFalse(TEXT("Non-Ice run reports NotRequired"),Required);
        TestFalse(TEXT("Non-Ice run never enrolls Freeze"),Loading->SeenPaths.Contains(ProfilePath));
        TestFalse(TEXT("Non-Ice run has no scoped load request"),Loading->ActiveHandle.IsValid());
        // Supply the phase precondition only to check epoch invalidation. This
        // is not a GPU readiness or first-visible-use certificate.
        Loading->Phase=EMCWorldPreloadPhase::Ready;
        TestTrue(TEXT("The supplied Ready phase belongs to this exact run"),Loading->IsReady());
        State->GameplayLoadId=FGuid::NewGuid();
        State->IcePresentationScope.Scope=EMCIcePresentationScope::Required;
        TestFalse(TEXT("A newer run cannot reuse the old readiness receipt"),Loading->IsReady());
        TestFalse(TEXT("An old epoch's Required decision waits"),Loading->EnsureScopedPresentationDependencies(Required));
        TestFalse(TEXT("An old decision cannot request Freeze for the newer run"),Loading->SeenPaths.Contains(ProfilePath));
        State->IcePresentationScope.GameplayLoadId=State->GameplayLoadId;
        TestFalse(TEXT("The matching new decision queues its real scoped dependency"),Loading->EnsureScopedPresentationDependencies(Required));
        TestTrue(TEXT("The new run enrolls Freeze despite the earlier expanded library"),Loading->SeenPaths.Contains(ProfilePath));
        auto OldWave=Loading->ActiveHandle;
        Loading->ResetPreparation();
        TestFalse(TEXT("Reset drops the old active request"),Loading->ActiveHandle.IsValid());
        TestTrue(TEXT("Reset releases or cancels the old request"),!OldWave.IsValid()||!OldWave->IsActive());
        TestTrue(TEXT("Reset clears old enrollment and retention"),Loading->SeenPaths.IsEmpty()&&Loading->RetainedAssets.IsEmpty());
        TestFalse(TEXT("Reset cannot leave the old readiness receipt"),Loading->IsReady());
        return true;
    }
    if(bCompletedRoot) {
        Loading->Phase=EMCWorldPreloadPhase::LoadingAssets;
        Loading->StartLoadWave({MCVFXSourceContracts::WorldLibraryPath()});
    } else {
        State->IcePresentationScope.Scope=EMCIcePresentationScope::Required;
        Loading->bRenderingWorkBuilt=true;Loading->bStreamingTargetsBuilt=true;
        TestFalse(TEXT("Required scope suspends rendering while its new dependency loads"),Loading->EnsureScopedPresentationDependencies(Required));
        TestTrue(TEXT("The authoritative Required decision is preserved"),Required);
        TestTrue(TEXT("Already-seen world library cannot hide the newly required profile"),Loading->SeenPaths.Contains(ProfilePath));
        TestTrue(TEXT("Scoped work uses the ordinary async asset phase"),Loading->Phase==EMCWorldPreloadPhase::LoadingAssets&&Loading->ActiveHandle.IsValid());
        TestFalse(TEXT("New dependency invalidates prior rendering work"),Loading->bRenderingWorkBuilt||Loading->bStreamingTargetsBuilt);
    }
    if(!Loading->ActiveHandle.IsValid())return false;
    const double Deadline=FPlatformTime::Seconds()+60.;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FPoll>([this,T,ProfilePath,Deadline,bWaitingForRoot=bCompletedRoot]() mutable {
        auto* Service=T->Loading;
        if(FPlatformTime::Seconds()>Deadline){AddError(TEXT("Actual scoped async closure exceeded the focused test deadline"));return true;}
        if(Service->HasFailed()){AddError(Service->GetDetailText().ToString());return true;}
        if(bWaitingForRoot) {
            if(!Service->ActiveHandle.IsValid()){AddError(TEXT("Initial root wave lost its request"));return true;}
            if(!Service->ActiveHandle->HasLoadCompleted())return false;
            const auto CompletedRootWave=Service->ActiveHandle;
            const int32 Completed=Service->CompletedPaths,Waves=Service->LoadWave;
            auto* Library=MCVFXSourceContracts::WorldLibraryPath().ResolveObject();
            Service->FinishLoadWave();
            TestEqual(TEXT("A completed root wave cannot advance counters with Unknown scope"),Service->CompletedPaths,Completed);
            TestEqual(TEXT("Unknown scope cannot start a dependent wave"),Service->LoadWave,Waves);
            TestFalse(TEXT("Unknown scope cannot permanently mark the root expanded"),Service->ExpandedObjects.Contains(Library));
            TestFalse(TEXT("Unknown scope cannot enroll an optional profile"),Service->SeenPaths.Contains(ProfilePath));
            TestTrue(TEXT("The completed request is retained while authoritative scope is unknown"),Service->ActiveHandle==CompletedRootWave&&Service->Phase==EMCWorldPreloadPhase::LoadingAssets);
            T->State->IcePresentationScope.Scope=EMCIcePresentationScope::Required;
            Service->FinishLoadWave();
            TestTrue(TEXT("Matching authoritative scope releases the same completed root wave"),Service->CompletedPaths>Completed);
            TestTrue(TEXT("The root is expanded only after its run parameters are known"),Service->ExpandedObjects.Contains(Library));
            TestTrue(TEXT("Ordinary root expansion now discovers the real scoped profile"),Service->SeenPaths.Contains(ProfilePath));
            bWaitingForRoot=false;
            return false;
        }
        if(Service->Phase==EMCWorldPreloadPhase::LoadingAssets) {
            if(!Service->ActiveHandle.IsValid()){AddError(TEXT("Asset phase lost its scoped request"));return true;}
            if(!Service->ActiveHandle->HasLoadCompleted())return false;
            Service->FinishLoadWave();return false;
        }
        bool RequiredNow=false;
        if(!Service->EnsureScopedPresentationDependencies(RequiredNow))return false;
        auto* Profile=Cast<UMCPerkVFXProfile>(ProfilePath.ResolveObject());
        if(TestNotNull(TEXT("Ordinary async waves actually made the saved Freeze profile resident"),Profile)) {
            TestTrue(TEXT("This service retains the real profile beyond its wave"),Service->RetainedAssets.Contains(Profile));
            TArray<FSoftObjectPath> Closure;FMCPreloadManifest::CollectSoftReferences(Profile,Closure,T->World);
            TestTrue(TEXT("The real saved profile supplies a nonempty resource closure"),!Closure.IsEmpty());
            for(const auto& Path:Closure) {
                TestTrue(TEXT("Freeze child is enrolled before rendering resumes: ")+Path.ToString(),Service->SeenPaths.Contains(Path));
                TestTrue(TEXT("Freeze child is retained before rendering resumes: ")+Path.ToString(),
                    Path.ResolveObject()&&Service->RetainedAssets.Contains(Path.ResolveObject()));
            }
            AddInfo(FString::Printf(TEXT("Scoped profile and %d direct resources retained through %d normal async discovery waves"),Closure.Num(),Service->LoadWave));
        }
        const int32 Waves=Service->LoadWave,Seen=Service->SeenPaths.Num();
        TestTrue(TEXT("A settled scoped closure is idempotent"),Service->EnsureScopedPresentationDependencies(RequiredNow));
        TestEqual(TEXT("Repeated scope polling creates no new wave"),Service->LoadWave,Waves);
        TestEqual(TEXT("Repeated scope polling creates no duplicate enrollment"),Service->SeenPaths.Num(),Seen);
        TestFalse(TEXT("Asset closure completion alone does not certify visual readiness"),Service->IsReady());
        return true;
    }));
    return true;
}
#endif
