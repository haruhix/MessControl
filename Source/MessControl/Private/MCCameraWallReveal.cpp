#include "MCToothCharacter.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

namespace
{
    constexpr int32 FocusDataIndex=20,SettingsDataIndex=24;

    bool CanReveal(const UMeshComponent* Wall)
    {
        if (!Wall || !Wall->IsRegistered() || !Wall->IsQueryCollisionEnabled() || Wall->GetNumMaterials()==0) return false;
        // Every slot must support the aperture before bypassing camera collision.
        for (int32 Slot=0; Slot<Wall->GetNumMaterials(); ++Slot) {
            const auto* Material=Wall->GetMaterial(Slot); float Enabled=0;
            if (!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("CameraRevealEnabled")),Enabled) || Enabled<.5f) return false;
        }
        return Wall->GetCustomPrimitiveDataIndexForVectorParameter(TEXT("CameraRevealFocus"))==FocusDataIndex
            && Wall->GetCustomPrimitiveDataIndexForVectorParameter(TEXT("CameraRevealSettings"))==SettingsDataIndex;
    }
}

void AMCToothCharacter::ClearCameraWallReveal()
{
    for (const auto& Entry:CameraRevealWalls) if (auto* Wall=Entry.Key.Get()) {
        Wall->SetCustomPrimitiveDataFloat(SettingsDataIndex,0);
        Wall->SetCollisionResponseToChannel(ECC_Camera,Entry.Value.CameraResponse);
    }
    CameraRevealWalls.Empty(); NextCameraWallScan=0;
}

void AMCToothCharacter::UpdateCameraWallReveal(float Dt,const FVector& Eye,const FVector& Focus)
{
    if (!bCameraWallReveal || !IsLocallyControlled()) { ClearCameraWallReveal(); return; }
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now>=NextCameraWallScan) {
        NextCameraWallScan=Now+.5;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It) {
            if (*It==this) continue;
            TInlineComponentArray<UMeshComponent*> Components; It->GetComponents(Components);
            for (auto* Wall:Components) {
                if (CameraRevealWalls.Contains(Wall) || Wall->GetCollisionResponseToChannel(ECC_Camera)!=ECR_Block || !CanReveal(Wall)) continue;
                auto& State=CameraRevealWalls.Add(Wall); State.CameraResponse=Wall->GetCollisionResponseToChannel(ECC_Camera);
                // Only this local world's camera channel changes; pawn/physics collision remains authored.
                Wall->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
            }
        }
    }

    const float Blend=1-FMath::Exp(-10.f*FMath::Max(0.f,Dt));
    for (auto It=CameraRevealWalls.CreateIterator(); It; ++It) {
        auto* Wall=It.Key().Get();
        if (!Wall) { It.RemoveCurrent(); continue; }
        if (!CanReveal(Wall)) {
            Wall->SetCustomPrimitiveDataFloat(SettingsDataIndex,0);
            Wall->SetCollisionResponseToChannel(ECC_Camera,It.Value().CameraResponse);
            It.RemoveCurrent(); continue;
        }
        FHitResult Hit;
        // Direct component queries still see a wall after its camera response is bypassed.
        const bool Occluded=Wall->SweepComponent(Hit,Eye,Focus,FQuat::Identity,FCollisionShape::MakeSphere(45),true);
        auto& State=It.Value(); State.Amount=FMath::Lerp(State.Amount,Occluded?1.f:0.f,Blend);
        if (State.Amount<.001f) State.Amount=0;
        Wall->SetCustomPrimitiveDataVector4(FocusDataIndex,FVector4(Focus,0));
        Wall->SetCustomPrimitiveDataVector4(SettingsDataIndex,FVector4(State.Amount,FMath::Clamp(CameraWallRevealRadius,60.f,500.f),FMath::Clamp(CameraWallRevealFeather,.02f,.8f),0));
    }
}
