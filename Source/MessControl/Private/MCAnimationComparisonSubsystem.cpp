#include "MCAnimationComparisonSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
    // A process-local choice survives travel and respawn. Separate game clients choose independently.
    TAutoConsoleVariable<int32> CVarSecondaryAnimationMode(
        TEXT("mc.Animation.SecondaryMode"),0,
        TEXT("Local secondary animation: 0 Native, 1 StylizedDynamics, 2 KawaiiPhysics."),ECVF_Default);

    const TCHAR* ModeName(EMCSecondaryAnimationMode Mode)
    {
        switch(Mode)
        {
        case EMCSecondaryAnimationMode::StylizedDynamics: return TEXT("StylizedDynamics");
        case EMCSecondaryAnimationMode::KawaiiPhysics: return TEXT("KawaiiPhysics");
        default: return TEXT("Native");
        }
    }
}

EMCSecondaryAnimationMode UMCAnimationComparisonSubsystem::GetMode() const
{
    const auto Mode=static_cast<EMCSecondaryAnimationMode>(FMath::Clamp(CVarSecondaryAnimationMode.GetValueOnGameThread(),0,2));
    return IsModeAvailable(Mode)?Mode:EMCSecondaryAnimationMode::Native;
}

void UMCAnimationComparisonSubsystem::SetMode(EMCSecondaryAnimationMode Mode)
{
    if(IsModeAvailable(Mode)) CVarSecondaryAnimationMode->Set(static_cast<int32>(Mode),ECVF_SetByConsole);
}

bool UMCAnimationComparisonSubsystem::IsModeAvailable(EMCSecondaryAnimationMode Mode) const
{
    switch(Mode)
    {
    case EMCSecondaryAnimationMode::Native:
    case EMCSecondaryAnimationMode::KawaiiPhysics: return true;
    case EMCSecondaryAnimationMode::StylizedDynamics: return WITH_MC_STYLIZED_DYNAMICS != 0;
    default: return false;
    }
}

void UMCAnimationComparisonSubsystem::RecordSample(USkeletalMeshComponent* Mesh,EMCSecondaryAnimationMode Mode,int32 Bones,int64 Evaluations,float MaxAngle,float MaxTranslation)
{
    if(!Mesh || !GetWorld()) return;
    FEntry& Entry=Samples.FindOrAdd(Mesh);
    if(Entry.Sample.MeshName.IsEmpty()) Entry.Sample.MeshName=Mesh->GetPathName();
    Entry.Sample.Mode=Mode;
    Entry.Sample.ActiveBones=Bones;
    Entry.Sample.Evaluations=Evaluations;
    Entry.Sample.MaxAngleDegrees=MaxAngle;
    Entry.Sample.MaxTranslation=MaxTranslation;
    Entry.LastUpdate=GetWorld()->GetTimeSeconds();
    // Weak keys cannot retain despawned bots or a previous level's objects.
    if(Entry.LastUpdate>=NextPruneAt)
    {
        NextPruneAt=Entry.LastUpdate+1.0;
        for(auto It=Samples.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    }
}

TArray<FMCAnimationComparisonSample> UMCAnimationComparisonSubsystem::GetSamples() const
{
    TArray<FMCAnimationComparisonSample> Result;
    const double Now=GetWorld()?GetWorld()->GetTimeSeconds():0;
    for(const auto& Pair:Samples)
        if(Pair.Key.IsValid() && Now-Pair.Value.LastUpdate<1.0) Result.Add(Pair.Value.Sample);
    return Result;
}

FString UMCAnimationComparisonSubsystem::GetStatusText() const
{
    const auto Mode=GetMode();
    return FString::Printf(TEXT("%s | meshes: %d"),ModeName(Mode),GetSamples().Num());
}
