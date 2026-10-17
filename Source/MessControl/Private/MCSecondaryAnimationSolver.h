#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
struct FAnimInstanceProxy;
struct FPoseContext;
enum class EMCSecondaryAnimationMode : uint8;

// Owned by a native animation proxy. Gameplay state is read only in PreUpdate;
// Evaluate consumes cached inputs and changes only the requested component bones.
class FMCSecondaryAnimationSolver
{
public:
    FMCSecondaryAnimationSolver();
    ~FMCSecondaryAnimationSolver();

    FMCSecondaryAnimationSolver(const FMCSecondaryAnimationSolver&) = delete;
    FMCSecondaryAnimationSolver& operator=(const FMCSecondaryAnimationSolver&) = delete;

    void PreUpdate(UAnimInstance* Instance, FAnimInstanceProxy& Proxy, float DeltaSeconds,
        const TArray<FName>& MotionBones, bool bAllowMotion = true);
    void Evaluate(FPoseContext& Output);
    void Reset();

    EMCSecondaryAnimationMode GetMode() const;
    float GetLastMaxAngleDegrees() const;
    float GetLastMaxTranslation() const;
    int32 GetActiveBoneCount() const;
    uint64 GetEvaluationCount() const;
    uint64 GetRejectedTransformCount() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
