#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCVFXGameplayInputs.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCVFXTypedSubjectPreactivation,
    "MessControl.VFX.Context.TypedVisualSubjectBeforeActivation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCVFXTypedSubjectPreactivation::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    if(!TestNotNull(TEXT("A private property-contract fixture world exists"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    struct FCleanup {UWorld* W;~FCleanup(){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}} Cleanup{World};
    auto* Owner=World->SpawnActor<AActor>();auto* VisibleOther=World->SpawnActor<AActor>();
    if(!TestNotNull(TEXT("The presentation owner exists"),Owner)||!TestNotNull(TEXT("A distinct visible subject exists"),VisibleOther))return false;
    auto* Root=NewObject<USceneComponent>(Owner);Owner->SetRootComponent(Root);Root->RegisterComponent();
    auto* Subject=NewObject<USceneComponent>(VisibleOther);VisibleOther->SetRootComponent(Subject);Subject->RegisterComponent();
    const FName SubjectName(TEXT("User.VisualSubject")),UnrelatedName(TEXT("User.Artist.Unrelated"));
    const FNiagaraVariable TypedSubject(FNiagaraTypeDefinition(USceneComponent::StaticClass()),SubjectName);
    const FNiagaraVariable WrongGenericSubject(FNiagaraTypeDefinition(UObject::StaticClass()),SubjectName);
    const FNiagaraVariable Unrelated(FNiagaraTypeDefinition(UObject::StaticClass()),UnrelatedName);
    auto* Source=NewObject<UNiagaraSystem>();
    UObject* Marker=NewObject<USceneComponent>(Owner,NAME_None,RF_Transient);
    if(!TestNotNull(TEXT("The unrelated object slot has a concrete marker distinct from the subject"),Marker))return false;
    Source->GetExposedParameters().AddParameter(TypedSubject);
    Source->GetExposedParameters().AddParameter(Unrelated);
    Source->GetExposedParameters().SetUObject(Marker,Unrelated);
    FString Error;
    if(!TestTrue(TEXT("The source declares the exact USceneComponent object type"),MCVFXGameplayInputs::ValidateSystem(Source,Error)))
    {AddError(Error);return false;}
    auto* C=NewObject<UNiagaraComponent>(Owner);Owner->AddInstanceComponent(C);C->SetAutoActivate(false);C->SetAsset(Source);
    TestFalse(TEXT("No particle activation is used to manufacture context data"),C->IsActive());
    MCVFXGameplayInputs::ApplySubjectFrame(C,Source,Subject,FTransform::Identity);
    const auto& Store=C->GetOverrideParameters();
    TestTrue(TEXT("The exact typed object slot exists before activation"),Store.IndexOf(TypedSubject)!=INDEX_NONE);
    TestEqual(TEXT("Native input addresses the actual Other subject, not the presentation owner"),Store.GetUObject(TypedSubject).Get(),static_cast<UObject*>(Subject));
    TestEqual(TEXT("Generic UObject setter cannot introduce an identically named wrong-type duplicate"),Store.IndexOf(WrongGenericSubject),INDEX_NONE);
    TestEqual(TEXT("An unrelated object parameter keeps its original value"),Store.GetUObject(Unrelated).Get(),Marker);
    TestFalse(TEXT("Reading native context leaves Niagara inactive"),C->IsActive());
    TestNull(TEXT("Applying to an instance does not mutate the source subject default"),Source->GetExposedParameters().GetUObject(TypedSubject).Get());
    MCVFXGameplayInputs::ApplySubjectFrame(C,Source,nullptr,FTransform::Identity);
    TestNull(TEXT("Losing the accepted subject clears its exact slot, never substitutes the owner"),Store.GetUObject(TypedSubject).Get());
    TestEqual(TEXT("Subject cleanup leaves unrelated object state untouched"),Store.GetUObject(Unrelated).Get(),Marker);
    TestEqual(TEXT("Cleanup also cannot add a generic duplicate"),Store.IndexOf(WrongGenericSubject),INDEX_NONE);
    auto* WrongSource=NewObject<UNiagaraSystem>();WrongSource->GetExposedParameters().AddParameter(WrongGenericSubject);
    TestFalse(TEXT("A generic UObject declaration cannot masquerade as the required USceneComponent type"),MCVFXGameplayInputs::ValidateSystem(WrongSource,Error));
    return true;
}
#endif
