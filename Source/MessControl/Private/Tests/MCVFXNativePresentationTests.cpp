#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXNativePresentationSettings.h"
#include "MCVFXPresentationComponent.h"
#include "MCNutCombatEffect.h"
#include "MCNutBoss.h"
#include "MCNutSpellProjectile.h"
#include "MCPreloadManifest.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCTongue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Curves/CurveFloat.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace MCNativePresentationTests
{
const FName Shield(TEXT("Boss.NutKnight.Shield")),Fireball(TEXT("Boss.NutMage.Fireball"));
const TArray<FName> ShieldEvents={TEXT("Hit"),TEXT("Break")};
const TArray<FName> FireballEvents={TEXT("Charge"),TEXT("Release"),TEXT("Flight"),TEXT("Impact")};
UMCVFXNativePresentationSettings* ContactStyle(FString& Error)
{
    auto* S=NewObject<UMCVFXNativePresentationSettings>();
    if(!S->InitFromNativeDefaults(Shield,Error))return nullptr;
    auto Part=*S->FindPart(TEXT("Hit"),TEXT("ShieldContactMagic"));
    S->Events.Reset();S->PersistentParts.Reset();S->Events.FindOrAdd(TEXT("Hit")).Parts.Add(Part);return S;
}
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    AMCNutBoss* Boss=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        Boss=World->SpawnActor<AMCNutBoss>();Boss->SetActorTickEnabled(false);
        Boss->BossRole=EMCNutBossRole::Tank;Boss->State=EMCNutBossState::Idle;
        Boss->Attack=EMCNutBossAttack::None;Boss->Health=400;Boss->Settings.MaxHealth=400;
        Boss->ShieldHealth=50;Boss->Tongue=World->SpawnActor<AMCTongue>();
    }
    ~FWorld(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeRendererDefaults,
    "MessControl.VFX.NativeArt.ExactDefaultsAndProtectedParts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeRendererDefaults::RunTest(const FString&)
{
    using namespace MCNativePresentationTests;FString Error;
    auto* S=NewObject<UMCVFXNativePresentationSettings>();auto* F=NewObject<UMCVFXNativePresentationSettings>();
    if(!TestTrue(TEXT("Shield defaults initialize"),S->InitFromNativeDefaults(Shield,Error))
        || !TestTrue(TEXT("Fireball defaults initialize"),F->InitFromNativeDefaults(Fireball,Error)))return false;
    TestTrue(TEXT("Exact native defaults satisfy their actual source contracts"),S->ValidateForSource(Shield,ShieldEvents,Error)
        &&F->ValidateForSource(Fireball,FireballEvents,Error));
    TestTrue(TEXT("Contact uses the original arcane material"),S->FindPart(TEXT("Hit"),TEXT("ShieldContactMagic"))->Material==
        GetDefault<AMCNutCombatEffect>()->MagicMaterial);
    TestTrue(TEXT("Broken shell retains the original lit fracture material"),S->FindPart(TEXT("Break"),TEXT("BrokenShell"))->Material==
        GetDefault<AMCNutCombatEffect>()->ShieldFractureMaterial);
    const auto* Cracks=S->FindPart(NAME_None,TEXT("ShieldCracks"));
    TestTrue(TEXT("Cracks retain authored five-column strip data"),Cracks&&!Cracks->Cracks.Samples.IsEmpty()
        &&Cracks->Cracks.Samples[0].Points.Num()==5&&!Cracks->Cracks.Paths.IsEmpty());
    TestTrue(TEXT("Flight uses its existing authored flame material"),F->FindPart(TEXT("Flight"),TEXT("FlightFlames"))->Material==
        GetDefault<AMCNutSpellProjectile>()->FlameMaterial);
    const auto Descriptors=UMCVFXNativePresentationSettings::DescribeParts(Fireball,TEXT("Flight"));
    const auto* Flames=Descriptors.FindByPredicate([](const auto& D){return D.PartID==TEXT("FlightFlames");});
    TestTrue(TEXT("Flame seed/hotness vertex channels are not exposed as fake Tint"),Flames&&!Flames->EditableProperties.Contains(TEXT("Tint")));
    auto* Body=F->Events[TEXT("Flight")].Parts.FindByPredicate([](const auto& P){return P.PartID==TEXT("WalnutBody");});
    Body->bEnabled=false;
    TestFalse(TEXT("Art settings cannot hide the gameplay projectile body"),F->ValidateForSource(Fireball,FireballEvents,Error));Body->bEnabled=true;
    auto* Ring=F->Events[TEXT("Impact")].Parts.FindByPredicate([](const auto& P){return P.PartID==TEXT("ImpactRing");});
    Ring->Tint=FLinearColor::Black;
    TestFalse(TEXT("Art settings cannot alter the protected footprint material/tint"),F->ValidateForSource(Fireball,FireballEvents,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeModeCompatibility,
    "MessControl.VFX.NativeArt.LegacyAndExplicitAdditiveModes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeModeCompatibility::RunTest(const FString&)
{
    using namespace MCNativePresentationTests;FMCVFXPreparedNativePresentation Legacy;FString Error;
    TestTrue(TEXT("Null native source preserves Default"),Legacy.ShouldRenderNativeAccents(TEXT("Hit"),EMCPerkVFXSlotMode::UseDefault));
    TestFalse(TEXT("Null native source preserves legacy replacement suppression"),Legacy.ShouldRenderNativeAccents(TEXT("Hit"),EMCPerkVFXSlotMode::Composition));
    TestFalse(TEXT("Disabled always suppresses accents"),Legacy.ShouldRenderNativeAccents(TEXT("Hit"),EMCPerkVFXSlotMode::Disabled));
    auto* Style=NewObject<UMCVFXNativePresentationSettings>();Style->InitFromNativeDefaults(Shield,Error);
    FMCVFXPreparedNativePresentation Prepared;Prepared.Snapshot=Style;
    TestFalse(TEXT("Creating defaults alone does not silently make old compositions additive"),Prepared.ShouldRenderNativeAccents(TEXT("Hit"),EMCPerkVFXSlotMode::Replace));
    Style->Events[TEXT("Hit")].bKeepNativeAccentsWithComposition=true;
    TestTrue(TEXT("Explicit Keep combines native with composition"),Prepared.ShouldRenderNativeAccents(TEXT("Hit"),EMCPerkVFXSlotMode::Composition));
    TestFalse(TEXT("Keep cannot override Disabled"),Prepared.ShouldRenderNativeAccents(TEXT("Hit"),EMCPerkVFXSlotMode::Disabled));
    TestFalse(TEXT("Additive choice is event-specific"),Prepared.ShouldRenderNativeAccents(TEXT("Break"),EMCPerkVFXSlotMode::Composition));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeImmutablePreparation,
    "MessControl.VFX.NativeArt.ImmutableCurvesAndPrivatePreparedMaterials",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeImmutablePreparation::RunTest(const FString&)
{
    using namespace MCNativePresentationTests;FWorld T;FString Error;auto* Style=ContactStyle(Error);
    if(!TestNotNull(TEXT("Actual renderer default style exists"),Style))return false;
    auto* Curve=NewObject<UCurveFloat>();Curve->FloatCurve.AddKey(0,.25f);Curve->FloatCurve.AddKey(1,1);
    Style->Events[TEXT("Hit")].Parts[0].OpacityCurve=Curve;
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->NativePresentation=Style;
    if(!TestTrue(TEXT("Explicit preparation creates native variants before any contact"),T.Boss->ShieldPresentation->PrepareProfileOverride(Profile,Error)))
    {AddError(Error);return false;}
    const auto* Prepared=T.Boss->ShieldPresentation->GetPreparedNativeSource(Shield);
    if(!TestNotNull(TEXT("Component retains the immutable native source"),Prepared))return false;
    const auto* Part=Prepared->FindPart(TEXT("Hit"),TEXT("ShieldContactMagic"));const uint64 Hash=Prepared->Reference.SettingsHash;
    TestTrue(TEXT("Curve is a private snapshot, not the artist source"),Part&&Part->OpacityCurve.Get()!=Curve);
    TestEqual(TEXT("Prepared curve keeps the actual authored key"),Part->OpacityCurve.Get()->GetFloatValue(0),.25f);
    auto* A=Prepared->CreatePrivateMaterial(TEXT("Hit"),TEXT("ShieldContactMagic"),T.Boss);
    auto* B=Prepared->CreatePrivateMaterial(TEXT("Hit"),TEXT("ShieldContactMagic"),T.Boss);
    if(!TestNotNull(TEXT("Preprepared material produces the first private native instance"),A)
        ||!TestNotNull(TEXT("Second cue has its own material state"),B))return false;
    A->SetScalarParameterValue(TEXT("Age"),.73f);B->SetScalarParameterValue(TEXT("Age"),.12f);
    const float Age=A->K2_GetScalarParameterValue(TEXT("Age"));
    TestTrue(TEXT("Private cue Age does not share the next cue's uniforms"),A!=B&&FMath::IsNearlyEqual(Age,.73f));
    Curve->FloatCurve.UpdateOrAddKey(0,.8f);Style->Events[TEXT("Hit")].Parts[0].Tint=FLinearColor::Red;
    TestEqual(TEXT("Author edits do not mutate an accepted curve"),Part->OpacityCurve.Get()->GetFloatValue(0),.25f);
    TestTrue(TEXT("In-place source curve/settings edits have a different version"),Style->ComputeSettingsHash()!=Hash);
    TestTrue(TEXT("Prepared source remains the accepted version"),Prepared->Reference.SettingsHash==Hash);
    auto* Invalid=NewObject<UMCPerkVFXProfile>();Invalid->NativePresentation=Style;
    Style->SourceID=Fireball;
    TestFalse(TEXT("Wrong-source replacement fails atomically"),T.Boss->ShieldPresentation->PrepareProfileOverride(Invalid,Error));
    TestTrue(TEXT("Invalid replacement retains the previous source snapshot"),T.Boss->ShieldPresentation->GetPreparedNativeSource(Shield)->Reference.SettingsHash==Hash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeSourceManifest,
    "MessControl.VFX.NativeArt.ReflectedResourceLoadWaves",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeSourceManifest::RunTest(const FString&)
{
    auto* P=NewObject<UMCPerkVFXProfile>();const FSoftObjectPath Path(TEXT("/Game/Gameplay/VFX/Profiles/DA_NativeShield.DA_NativeShield"));
    P->NativePresentation=Path;TArray<FSoftObjectPath> Refs;FMCPreloadManifest::CollectSoftReferences(P,Refs);
    TestTrue(TEXT("Runtime profile reaches its native settings DA"),Refs.Contains(Path));
    auto* S=NewObject<UMCVFXNativePresentationSettings>();FString Error;S->InitFromNativeDefaults(MCNativePresentationTests::Shield,Error);
    const FSoftObjectPath Curve(TEXT("/Game/Tests/VFXPractice/Curve_NativeOpacity.Curve_NativeOpacity"));
    S->Events[TEXT("Hit")].Parts[0].OpacityCurve=Curve;Refs.Reset();FMCPreloadManifest::CollectSoftReferences(S,Refs);
    TestTrue(TEXT("Nested event parts reach authored curves without eager loading"),Refs.Contains(Curve));
    TestTrue(TEXT("Original lit fracture remains reachable for preload/cook"),Refs.Contains(GetDefault<AMCNutCombatEffect>()->ShieldFractureMaterial.ToSoftObjectPath()));
    TestTrue(TEXT("Default impact Niagara remains reachable through nested native parts"),Refs.Contains(GetDefault<AMCNutCombatEffect>()->ShieldHitSystem.ToSoftObjectPath()));
    TestNull(TEXT("Manifest walking did not create a fixture asset"),Path.ResolveObject());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeShieldConsumesSettings,
    "MessControl.VFX.NativeArt.RealShieldContactConsumesAuthoredGeometry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativeShieldConsumesSettings::RunTest(const FString&)
{
    using namespace MCNativePresentationTests;FWorld T;FString Error;auto* Style=ContactStyle(Error);
    if(!TestNotNull(TEXT("Contact style initialized"),Style))return false;
    Style->Events[TEXT("Hit")].Parts[0].Contact.Corners=8;
    auto* Profile=NewObject<UMCPerkVFXProfile>();Profile->NativePresentation=Style;
    if(!TestTrue(TEXT("Authored geometry prepares before native damage"),T.Boss->ShieldPresentation->PrepareProfileOverride(Profile,Error)))
    {AddError(Error);return false;}
    T.Boss->ReceiveToolDamage(10,nullptr);AMCNutCombatEffect* Contact=nullptr;
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)if(It->Cue.Type==EMCNutCombatCue::ShieldHit)Contact=*It;
    if(!TestNotNull(TEXT("Actual shield damage retains the original native cue"),Contact))return false;
    TestEqual(TEXT("Native cue carries the immutable style token"),Contact->Cue.NativeStyle.SettingsHash,
        T.Boss->ShieldPresentation->GetPreparedNativeSource(Shield)->Reference.SettingsHash);
    Contact->PreviewAtAge(.04f);
    auto* Section=Contact->Magic->GetProcMeshSection(0);
    if(!TestNotNull(TEXT("Actual native star geometry was evaluated"),Section))return false;
    TestEqual(TEXT("Eight authored corners are consumed by the original contact renderer"),Section->ProcVertexBuffer.Num(),24);
    TestEqual(TEXT("Cosmetic shape changes preserve shield durability"),T.Boss->ShieldHealth,40.f);
    TestEqual(TEXT("Cosmetic shape changes preserve boss HP"),T.Boss->Health,400.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativePrewarmRebuildStability,
    "MessControl.VFX.NativeArt.PrewarmRebuildKeepsSingleSnapshot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNativePrewarmRebuildStability::RunTest(const FString&)
{
    using namespace MCNativePresentationTests;FWorld T;FString Error;
    // A package-scoped authoring source models the saved location closure;
    // no package is written, modified or added to the Asset Registry.
    auto* Package=CreatePackage(*FString::Printf(TEXT("/Game/Tests/NativePrewarm_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* Source=NewObject<UMCVFXNativePresentationSettings>(Package,TEXT("Style"));
    Source->InitFromNativeDefaults(Shield,Error);
    const auto Contact=*Source->FindPart(TEXT("Hit"),TEXT("ShieldContactMagic"));
    Source->Events.Reset();Source->PersistentParts.Reset();Source->Events.FindOrAdd(TEXT("Hit")).Parts.Add(Contact);
    if(!TestNotNull(TEXT("Actual native contact material is resident for the preload fixture"),Contact.Material.LoadSynchronous()))return false;
    auto* Loading=NewObject<UMCWorldPreloadSubsystem>(T.World);Loading->RetainedAssets.Add(Source);
    if(!TestTrue(TEXT("First native preparation succeeds"),Loading->PrepareNativeStylesForLocation(Error))) {AddError(Error);return false;}
    const auto* First=Loading->NativeStyles.Find(Source);
    if(!TestNotNull(TEXT("Location retains its first immutable native snapshot"),First))return false;
    auto* Snapshot=First->Snapshot.Get();const int32 ResourceCount=Loading->RetainedAssets.Num();
    const int32 WarmupCount=Loading->WarmupComponents.Num();
    for(int32 Pass=0;Pass<3;++Pass) {
        TestTrue(TEXT("Waiting in the same epoch reuses submitted native factories"),Loading->PrepareNativeStylesForLocation(Error));
        TestEqual(TEXT("An unchanged preparation does not duplicate native factories"),Loading->WarmupComponents.Num(),WarmupCount);
    }
    for(int32 Pass=0;Pass<3;++Pass) {
        // This is the existing rendering rebuild lifecycle, which retains the
        // location assets/cache while destroying previous warmup components.
        Loading->ReleaseWarmupComponents();
        if(!TestTrue(TEXT("Repeated rendering preparation succeeds"),Loading->PrepareNativeStylesForLocation(Error)))
        {Loading->ReleaseWarmupComponents();AddError(Error);return false;}
        TestEqual(TEXT("Only the canonical source is cached"),Loading->NativeStyles.Num(),1);
        TestTrue(TEXT("Unchanged source reuses its immutable snapshot"),Loading->NativeStyles.FindChecked(Source).Snapshot==Snapshot);
        TestEqual(TEXT("Rebuilding does not retain duplicate materials/curves/snapshots"),Loading->RetainedAssets.Num(),ResourceCount);
        TestEqual(TEXT("Only one bounded set of procedural warmup factories exists"),Loading->WarmupComponents.Num(),WarmupCount);
    }
    Loading->ReleaseWarmupComponents();
    // Render-plan ownership regression only. This supplies an already-built
    // plan flag; it does not bypass or certify the real map rendering barrier.
    UWorld* PlanWorld=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(PlanWorld);
    ON_SCOPE_EXIT { GEngine->DestroyWorldContext(PlanWorld);PlanWorld->DestroyWorld(false); };
    auto* Plan=NewObject<UMCWorldPreloadSubsystem>(PlanWorld);
    Plan->SettleWorldAndRefreshManifest(); // Capture the real initial world.
    Plan->bRenderingWorkBuilt=true;
    Plan->SettleWorldAndRefreshManifest();Plan->SettleWorldAndRefreshManifest();
    if(!TestTrue(TEXT("Fixture snapshot reaches world stability without a fake Ready phase"),Plan->bRenderingWorkBuilt&&Plan->StableWorldFrames>=3))return false;
    Plan->NextWarmupRequest=7;Plan->bStreamingTargetsBuilt=true;
    // Both native procedural factories (WorldSettings outer) and ordinary
    // subsystem-outer warmup factories are real registered components.
    auto* NativeFactory=NewObject<UProceduralMeshComponent>(PlanWorld->GetWorldSettings(),NAME_None,RF_Transient);
    Plan->WarmupComponents.Add(NativeFactory);NativeFactory->RegisterComponentWithWorld(PlanWorld);
    auto* StaticFactory=NewObject<UStaticMeshComponent>(Plan,NAME_None,RF_Transient);
    Plan->WarmupComponents.Add(StaticFactory);StaticFactory->RegisterComponentWithWorld(PlanWorld);
    TestTrue(TEXT("Actual owned warmups do not invalidate their own completed plan"),Plan->SettleWorldAndRefreshManifest());
    TestTrue(TEXT("Owned warmups survive settling until their actual release"),NativeFactory->IsRegistered()&&StaticFactory->IsRegistered());
    TestEqual(TEXT("Owned warmup creation does not rewind queue progress"),Plan->NextWarmupRequest,7);
    TestTrue(TEXT("Owned warmup creation preserves streaming plan"),Plan->bStreamingTargetsBuilt);
    Plan->ReleaseWarmupComponents();
    TestTrue(TEXT("Destroying only owned warmups also preserves the source snapshot"),Plan->SettleWorldAndRefreshManifest());
    TestTrue(TEXT("Owned warmup release preserves the render-plan epoch"),Plan->bRenderingWorkBuilt);
    auto* RealActor=PlanWorld->SpawnActor<AActor>();
    RealActor->SetFlags(RF_Transient);RealActor->SetActorHiddenInGame(true);
    Plan->bRenderingWorkBuilt=true;
    Plan->SettleWorldAndRefreshManifest();
    TestFalse(TEXT("A real newly spawned actor invalidates the plan even when transient and hidden"),Plan->bRenderingWorkBuilt);
    Plan->bRenderingWorkBuilt=true;
    Plan->SettleWorldAndRefreshManifest();Plan->SettleWorldAndRefreshManifest();
    auto AddRealComponent=[&]() {
        auto* C=NewObject<UProceduralMeshComponent>(RealActor,NAME_None,RF_Transient);
        RealActor->AddInstanceComponent(C);C->SetVisibility(false);C->RegisterComponentWithWorld(PlanWorld);return C;
    };
    auto* RealComponent=AddRealComponent();
    Plan->bRenderingWorkBuilt=true;
    Plan->SettleWorldAndRefreshManifest();
    TestFalse(TEXT("A genuine registered component on an unchanged actor requires fresh enrollment"),Plan->bRenderingWorkBuilt);
    TestTrue(TEXT("Invalidation never destroys a genuine gameplay component"),RealComponent->IsRegistered());
    Plan->bRenderingWorkBuilt=true;
    Plan->SettleWorldAndRefreshManifest();Plan->SettleWorldAndRefreshManifest();
    RealComponent->DestroyComponent();
    auto* Replacement=AddRealComponent();
    Plan->SettleWorldAndRefreshManifest();
    TestFalse(TEXT("Same-count replacement of a real component cannot reuse stale resource enrollment"),Plan->bRenderingWorkBuilt);
    TestTrue(TEXT("Replacement remains owned by its real actor"),Replacement->IsRegistered()&&Replacement->GetOwner()==RealActor);
    Plan->bRenderingWorkBuilt=true;
    Plan->SettleWorldAndRefreshManifest();Plan->SettleWorldAndRefreshManifest();
    Replacement->UnregisterComponent();Plan->SettleWorldAndRefreshManifest();
    TestFalse(TEXT("A real component unregister invalidates the previous registered set"),Plan->bRenderingWorkBuilt);
    return true;
}
#endif
