#include "MCSecondaryAnimationSolver.h"

#include "MCAnimationComparisonSubsystem.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

#if WITH_MC_STYLIZED_DYNAMICS
#include "StylizedDynamicsPoseSolver.h"
#endif

namespace
{
    constexpr float MaxDisplacement = 2.f;
    constexpr float MaxGapSeconds = .25f;

    FTransform BoundedTransform(const FTransform& Authored, const FTransform& Solved,
        float MaxAngle, float& OutAngle, float& OutTranslation)
    {
        FTransform Result = Authored;
        const FVector Offset = (Solved.GetTranslation() - Authored.GetTranslation()).GetClampedToMaxSize(MaxDisplacement);
        Result.AddToTranslation(Offset);
        const FQuat From = Authored.GetRotation().GetNormalized();
        const FQuat To = Solved.GetRotation().GetNormalized();
        const float Angle = FMath::RadiansToDegrees(float(From.AngularDistance(To)));
        Result.SetRotation(FQuat::Slerp(From, To, Angle > MaxAngle ? MaxAngle / Angle : 1.f).GetNormalized());
        // Squash/stretch is deliberately excluded from this first comparison.
        Result.SetScale3D(Authored.GetScale3D());
        OutAngle = FMath::Min(Angle, MaxAngle);
        OutTranslation = float(Offset.Size());
        return Result;
    }
}

struct FMCSecondaryAnimationSolver::FImpl
{
    EMCSecondaryAnimationMode Mode = EMCSecondaryAnimationMode::Native;
    bool bAllowed = false;
    bool bConfigured = false;
    bool bHasPreviousTransform = false;
    bool bHasPreviousVelocity = false;
    uint16 BoneSerial = 0;
    float PendingDeltaSeconds = 0.f;
    FTransform PreviousComponent = FTransform::Identity;
    FVector PreviousVelocity = FVector::ZeroVector;
    FVector PreviousAngularVelocity = FVector::ZeroVector;
    FVector ExternalAcceleration = FVector::ZeroVector;
    FVector ExternalAngularAcceleration = FVector::ZeroVector;
    TArray<FName> ConfiguredNames;
    TArray<FCompactPoseBoneIndex> Indices;
    TArray<float> MaxAngles;
    TArray<TUniquePtr<FAnimNode_KawaiiPhysics>> KawaiiNodes;
#if WITH_MC_STYLIZED_DYNAMICS
    TArray<FStylizedDynamicsBoneSettings> StylizedBones;
    TArray<FStylizedDynamicsChainSettings> StylizedChains;
    FStylizedDynamicsPoseSolver StylizedSolver;
#endif
    TArray<FTransform> OriginalComponentPose;
    TArray<FTransform> FinalComponentPose;
    TArray<FBoneTransform> SolvedTransforms;
    float LastMaxAngle = 0.f;
    float LastMaxTranslation = 0.f;
    int32 ActiveBoneCount = 0;
    uint64 EvaluationCount = 0;
    uint64 RejectedTransformCount = 0;

    void ResetSimulation()
    {
        PendingDeltaSeconds = 0.f;
        ExternalAcceleration = FVector::ZeroVector;
        ExternalAngularAcceleration = FVector::ZeroVector;
        bHasPreviousTransform = false;
        bHasPreviousVelocity = false;
        LastMaxAngle = LastMaxTranslation = 0.f;
        ActiveBoneCount = 0;
        for (const auto& Node : KawaiiNodes)
            Node->ResetDynamics(ETeleportType::ResetPhysics);
#if WITH_MC_STYLIZED_DYNAMICS
        StylizedSolver.RequestReset();
#endif
    }

    void Configure(UAnimInstance* Instance, FAnimInstanceProxy& Proxy, const TArray<FName>& Names)
    {
        ResetSimulation();
        ConfiguredNames = Names;
        const FBoneContainer& RequiredBones = Proxy.GetRequiredBones();
        BoneSerial = RequiredBones.GetSerialNumber();
        bConfigured = true;
        Indices.Reset();
        MaxAngles.Reset();
        KawaiiNodes.Reset();
#if WITH_MC_STYLIZED_DYNAMICS
        StylizedSolver.Release();
        StylizedBones.Reset();
        StylizedChains.Reset();
#endif
        const FReferenceSkeleton& Reference = RequiredBones.GetReferenceSkeleton();
        for (int32 Role = 0; Role < Names.Num(); ++Role)
        {
            FBoneReference Bone;
            Bone.BoneName = Names[Role];
            Bone.Initialize(RequiredBones);
            if (!Bone.IsValidToEvaluate(RequiredBones)) continue;
            const FCompactPoseBoneIndex Index = Bone.GetCompactPoseIndex(RequiredBones);
            if (Indices.Contains(Index)) continue;
            const bool bBody = Role == 0;
            const float MaxAngle = bBody ? 4.f : 10.f;
            Indices.Add(Index);
            MaxAngles.Add(MaxAngle);

            auto Node = MakeUnique<FAnimNode_KawaiiPhysics>();
            Node->RootBone.BoneName = Bone.BoneName;
            Node->DummyBoneLength = bBody ? 20.f : 12.f;
            Node->BoneForwardAxis = EBoneForwardAxis::Z_Positive;
            Node->PhysicsSettings.Damping = bBody ? .32f : .22f;
            Node->PhysicsSettings.Stiffness = bBody ? .32f : .22f;
            Node->PhysicsSettings.WorldDampingLocation = .9f;
            Node->PhysicsSettings.WorldDampingRotation = .9f;
            Node->PhysicsSettings.Radius = 0.f;
            Node->PhysicsSettings.LimitAngle = MaxAngle;
            Node->Gravity = FVector::ZeroVector;
            Node->bEnableWind = false;
            Node->bAllowWorldCollision = false;
            Node->bUseSimpleWorldCollision = false;
            Node->bUseSharedCollision = false;
            Node->bSharedCollisionSource = false;
            Node->bUseWarmUpWhenResetDynamics = false;
            Node->TargetFramerate = 60;
            Node->TeleportDistanceThreshold = 100.f;
            // One anchor plus one virtual tip per selected bone. Real children
            // stay fully authored, including hands, feet and contact chains.
            const int32 MeshBone = Reference.FindBoneIndex(Bone.BoneName);
            for (int32 Child = MeshBone + 1; Child < Reference.GetNum(); ++Child)
            {
                if (Reference.GetParentIndex(Child) != MeshBone) continue;
                FBoneReference Excluded;
                Excluded.BoneName = Reference.GetBoneName(Child);
                Node->ExcludeBones.Add(Excluded);
            }
            Node->OnInitializeAnimInstance(&Proxy, Instance);
            Node->Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
            Node->CacheBones_AnyThread(FAnimationCacheBonesContext(&Proxy));
            KawaiiNodes.Add(MoveTemp(Node));

#if WITH_MC_STYLIZED_DYNAMICS
            FStylizedDynamicsBoneSettings Settings;
            Settings.Bone = Bone;
            Settings.Translation.Weight = bBody ? .15f : .25f;
            Settings.Translation.Stiffness = bBody ? 250.f : 220.f;
            Settings.Translation.Damping = bBody ? 20.f : 18.f;
            Settings.Translation.MaxOffset = MaxDisplacement;
            Settings.Rotation.Weight = bBody ? .25f : .45f;
            Settings.Rotation.Stiffness = Settings.Translation.Stiffness;
            Settings.Rotation.Damping = Settings.Translation.Damping;
            Settings.Rotation.MaxAngleDegrees = MaxAngle;
            Settings.Scale.Amount = 0.f;
            Settings.Response.Inertia = .35f;
            Settings.Response.VelocityInfluence = .02f;
            Settings.Response.AccelerationInfluence = .001f;
            Settings.Response.GravityScale = 0.f;
            Settings.Style.Exaggeration = 1.f;
            Settings.Style.BlendWeight = 1.f;
            StylizedBones.Add(Settings);
#endif
        }
    }
};

FMCSecondaryAnimationSolver::FMCSecondaryAnimationSolver() : Impl(MakeUnique<FImpl>()) {}
FMCSecondaryAnimationSolver::~FMCSecondaryAnimationSolver() = default;

void FMCSecondaryAnimationSolver::PreUpdate(UAnimInstance* Instance, FAnimInstanceProxy& Proxy,
    float DeltaSeconds, const TArray<FName>& MotionBones, bool bAllowMotion)
{
    EMCSecondaryAnimationMode NewMode = EMCSecondaryAnimationMode::Native;
    if (Instance)
        if (UWorld* World = Instance->GetWorld())
            if (const auto* Comparison = World->GetSubsystem<UMCAnimationComparisonSubsystem>())
                NewMode = Comparison->GetMode();

    if (Impl->Mode != NewMode || Impl->bAllowed != bAllowMotion)
        Impl->ResetSimulation();
    Impl->Mode = NewMode;
    Impl->bAllowed = bAllowMotion;
    if (!bAllowMotion || NewMode == EMCSecondaryAnimationMode::Native)
    {
        Impl->ResetSimulation();
        return;
    }
    const FBoneContainer& RequiredBones = Proxy.GetRequiredBones();
    if (RequiredBones.GetCompactPoseNumBones() <= 0) return;
    if (!Impl->bConfigured || Impl->ConfiguredNames != MotionBones || Impl->BoneSerial != RequiredBones.GetSerialNumber())
        Impl->Configure(Instance, Proxy, MotionBones);

    const float Step = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    const FTransform Component = Instance && Instance->GetSkelMeshComponent()
        ? Instance->GetSkelMeshComponent()->GetComponentTransform() : Proxy.GetComponentTransform();
    const bool bDiscontinuity = Step > MaxGapSeconds || Component.ContainsNaN()
        || (Impl->bHasPreviousTransform &&
            (FVector::DistSquared(Component.GetLocation(), Impl->PreviousComponent.GetLocation()) > 100.f * 100.f
            || Component.GetRotation().AngularDistance(Impl->PreviousComponent.GetRotation()) > PI * .5));
    if (bDiscontinuity || Impl->PendingDeltaSeconds + Step > MaxGapSeconds)
        Impl->ResetSimulation();

    if (!bDiscontinuity && Step > UE_SMALL_NUMBER && Impl->bHasPreviousTransform)
    {
        const FVector Velocity = (Component.GetLocation() - Impl->PreviousComponent.GetLocation()) / Step;
        FQuat Turn = Component.GetRotation() * Impl->PreviousComponent.GetRotation().Inverse();
        if (Turn.W < 0.f) Turn = FQuat(-Turn.X, -Turn.Y, -Turn.Z, -Turn.W);
        FVector Axis;
        double Angle;
        Turn.GetNormalized().ToAxisAndAngle(Axis, Angle);
        const FVector AngularVelocity = Axis * Angle / Step;
        if (Impl->bHasPreviousVelocity)
        {
            Impl->ExternalAcceleration = Component.InverseTransformVectorNoScale(
                -(Velocity - Impl->PreviousVelocity) / Step).GetClampedToMaxSize(3000.f) * .25f;
            Impl->ExternalAngularAcceleration = Component.InverseTransformVectorNoScale(
                -(AngularVelocity - Impl->PreviousAngularVelocity) / Step).GetClampedToMaxSize(15.f) * .4f;
        }
        Impl->PreviousVelocity = Velocity;
        Impl->PreviousAngularVelocity = AngularVelocity;
        Impl->bHasPreviousVelocity = true;
    }
    else
    {
        Impl->PreviousVelocity = FVector::ZeroVector;
        Impl->PreviousAngularVelocity = FVector::ZeroVector;
        Impl->bHasPreviousVelocity = false;
        Impl->ExternalAcceleration = FVector::ZeroVector;
        Impl->ExternalAngularAcceleration = FVector::ZeroVector;
    }
    if (!Component.ContainsNaN())
    {
        Impl->PreviousComponent = Component;
        Impl->bHasPreviousTransform = true;
    }
    Impl->PendingDeltaSeconds += bDiscontinuity ? 0.f : Step;
    if (NewMode == EMCSecondaryAnimationMode::KawaiiPhysics)
        for (const auto& Node : Impl->KawaiiNodes) Node->PreUpdate(Instance);
}

void FMCSecondaryAnimationSolver::Evaluate(FPoseContext& Output)
{
    Impl->LastMaxAngle = Impl->LastMaxTranslation = 0.f;
    Impl->ActiveBoneCount = 0;
    if (!Impl->bAllowed || Impl->Mode == EMCSecondaryAnimationMode::Native || Impl->Indices.IsEmpty()) return;
    const FBoneContainer& RequiredBones = Output.Pose.GetBoneContainer();
    if (RequiredBones.GetSerialNumber() != Impl->BoneSerial) return;

    FComponentSpacePoseContext ComponentOutput(Output.AnimInstanceProxy);
    ComponentOutput.Pose.InitPose(Output.Pose);
    ComponentOutput.Curve = Output.Curve;
    const int32 Count = Output.Pose.GetNumBones();
    Impl->OriginalComponentPose.SetNumUninitialized(Count);
    for (auto Index : Output.Pose.ForEachBoneIndex())
        Impl->OriginalComponentPose[Index.GetInt()] = ComponentOutput.Pose.GetComponentSpaceTransform(Index);
    Impl->FinalComponentPose = Impl->OriginalComponentPose;
    Impl->SolvedTransforms.Reset();
    const float Step = Impl->PendingDeltaSeconds;
    Impl->PendingDeltaSeconds = 0.f;

    if (Impl->Mode == EMCSecondaryAnimationMode::KawaiiPhysics)
    {
        for (const auto& Node : Impl->KawaiiNodes)
        {
            Node->UpdateInternal(FAnimationUpdateContext(Output.AnimInstanceProxy, Step));
            TArray<FBoneTransform> BoneResult;
            Node->EvaluateSkeletalControl_AnyThread(ComponentOutput, BoneResult);
            Impl->SolvedTransforms.Append(BoneResult);
        }
    }
#if WITH_MC_STYLIZED_DYNAMICS
    else if (Impl->Mode == EMCSecondaryAnimationMode::StylizedDynamics)
    {
        Impl->StylizedSolver.InitializeIfNeeded(Impl->StylizedBones, Impl->StylizedChains, RequiredBones);
        FStylizedDynamicsSolveParams Params;
        Params.DeltaTime = Step;
        Params.SimulationFrequency = 60.f;
        Params.GravityInComponentSpace = FVector::ZeroVector;
        Params.ExternalForceInComponentSpace = Impl->ExternalAcceleration;
        Params.ExternalTorqueInComponentSpace = Impl->ExternalAngularAcceleration;
        Impl->StylizedSolver.Solve(ComponentOutput.Pose, &ComponentOutput.Curve, Params, Impl->SolvedTransforms);
    }
#endif

    for (const FBoneTransform& BoneResult : Impl->SolvedTransforms)
    {
        const int32 Role = Impl->Indices.IndexOfByKey(BoneResult.BoneIndex);
        const int32 Index = BoneResult.BoneIndex.GetInt();
        if (Role == INDEX_NONE || !Impl->OriginalComponentPose.IsValidIndex(Index)) continue;
        if (BoneResult.Transform.ContainsNaN() || !BoneResult.Transform.GetRotation().IsNormalized())
        {
            ++Impl->RejectedTransformCount;
            continue;
        }
        float Angle = 0.f, Displacement = 0.f;
        Impl->FinalComponentPose[Index] = BoundedTransform(Impl->OriginalComponentPose[Index],
            BoneResult.Transform, Impl->MaxAngles[Role], Angle, Displacement);
        Impl->LastMaxAngle = FMath::Max(Impl->LastMaxAngle, Angle);
        Impl->LastMaxTranslation = FMath::Max(Impl->LastMaxTranslation, Displacement);
        ++Impl->ActiveBoneCount;
    }
    if (Impl->ActiveBoneCount == 0) return;

    // Rebuild every local transform against the desired component pose. Leaving
    // unselected component bones unchanged also keeps child IK and grips fixed.
    for (auto Index : Output.Pose.ForEachBoneIndex())
    {
        const FCompactPoseBoneIndex Parent = RequiredBones.GetParentBoneIndex(Index);
        Output.Pose[Index] = Parent.GetInt() == INDEX_NONE ? Impl->FinalComponentPose[Index.GetInt()]
            : Impl->FinalComponentPose[Index.GetInt()].GetRelativeTransform(Impl->FinalComponentPose[Parent.GetInt()]);
    }
    Output.Pose.NormalizeRotations();
    ++Impl->EvaluationCount;
}

void FMCSecondaryAnimationSolver::Reset() { Impl->ResetSimulation(); }
EMCSecondaryAnimationMode FMCSecondaryAnimationSolver::GetMode() const { return Impl->Mode; }
float FMCSecondaryAnimationSolver::GetLastMaxAngleDegrees() const { return Impl->LastMaxAngle; }
float FMCSecondaryAnimationSolver::GetLastMaxTranslation() const { return Impl->LastMaxTranslation; }
int32 FMCSecondaryAnimationSolver::GetActiveBoneCount() const { return Impl->ActiveBoneCount; }
uint64 FMCSecondaryAnimationSolver::GetEvaluationCount() const { return Impl->EvaluationCount; }
uint64 FMCSecondaryAnimationSolver::GetRejectedTransformCount() const { return Impl->RejectedTransformCount; }
