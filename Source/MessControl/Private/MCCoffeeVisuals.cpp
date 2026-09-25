#include "MCCoffeeFlood.h"
#include "Materials/MaterialInstanceDynamic.h"

void AMCCoffeeFlood::UpdatePour(float Time)
{
    const float Local=WaterSettings.CycleTime(Time), JetAlpha=WaterSettings.JetAmount(Time);
    const float DrainAlpha=WaterSettings.DrainAmount(Time);
    FVector Impact=WaterSettings.Inlet; Impact.Z=FMath::Max(InletFloorZ,SurfaceHeightAt(Impact))+2;
    // The tip travels down before the impact front begins, instead of popping in full-height.
    const float Bottom=FMath::Lerp(WaterSettings.Inlet.Z,Impact.Z,FMath::Clamp(Local/.25f,0.f,1.f));
    Jet->SetVisibility(JetAlpha>.001f);
    Jet->SetWorldLocation(FVector(Impact.X,Impact.Y,Bottom));
    Jet->SetWorldScale3D(FVector(WaterSettings.JetRadius/50,WaterSettings.JetRadius/50,FMath::Max(1.f,WaterSettings.Inlet.Z-Bottom)/100));
    Crown->SetVisibility(JetAlpha>.001f && Local>.25f);
    Crown->SetWorldLocation(Impact);
    Crown->SetWorldScale3D(FVector(WaterSettings.JetRadius/65,WaterSettings.JetRadius/65,.7f+.3f*JetAlpha));

    // Authored sheet bends down through the throat; it is separate from the water plane.
    const FVector Direction=(WaterSettings.DrainPoint-WaterSettings.Inlet).GetSafeNormal2D();
    FVector Outlet=WaterSettings.DrainPoint; Outlet.Z=SurfaceHeightAt(Outlet)+4;
    DrainRibbon->SetVisibility(DrainAlpha>.001f && WaterSettings.FillAmount(Time)>.005f);
    DrainRibbon->SetWorldLocationAndRotation(Outlet,Direction.Rotation());
    DrainRibbon->SetWorldScale3D(FVector(3.5f,WaterSettings.DrainRadius/100,2.5f));
    for (UMaterialInstanceDynamic* Mat:{JetMaterial.Get(),CrownMaterial.Get(),DrainMaterial.Get()})
        if (Mat) Mat->SetScalarParameterValue(TEXT("WaterTime"),Time);
    if (JetMaterial) JetMaterial->SetScalarParameterValue(TEXT("Strength"),JetAlpha);
    if (CrownMaterial) CrownMaterial->SetScalarParameterValue(TEXT("Strength"),JetAlpha);
    if (DrainMaterial) DrainMaterial->SetScalarParameterValue(TEXT("Strength"),DrainAlpha*FMath::Clamp(WaterSettings.FillAmount(Time)*5,0.f,1.f));

    // A small deterministic ballistic pool: no fluid solver, collision bodies or per-drop RPCs.
    TArray<FTransform> Transforms; Transforms.Reserve(72);
    auto Noise=[](float N){return FMath::Frac(FMath::Abs(FMath::Sin(N*127.1f)*43758.5453f));};
    for (int32 I=0;I<72;++I)
    {
        const float Life=.9f+Noise(I+1)*.5f, Offset=Noise(I+91)*Life;
        const float Age=FMath::Fmod(FMath::Max(0.f,Local-.25f)+Offset,Life);
        const float Born=Local-Age;
        const bool Visible=Local>.25f && Born>=.25f && Born<WaterSettings.FillSeconds-.12f;
        const float Angle=I*2.399963f+FMath::FloorToFloat((Local+Offset)/Life)*.65f;
        const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const float Horizontal=190+Noise(I+44)*260, Up=320+Noise(I+27)*250;
        FVector P=WaterSettings.Inlet+Radial*(WaterSettings.JetRadius*.7f+Age*Horizontal);
        P.Z=FMath::Max(InletFloorZ,BaseHeight(FMath::Max(0.f,Time-Age)))+25+Up*Age-400*Age*Age;
        const float Size=Visible && P.Z>SurfaceHeightAt(P)?(.07f+Noise(I+17)*.09f)*FMath::Min(1.f,(Life-Age)*10):0;
        const FVector Velocity=Radial*Horizontal+FVector(0,0,Up-800*Age);
        Transforms.Emplace(Velocity.Rotation(),P,FVector(Size*1.55f,Size,Size));
    }
    Drops->SetVisibility(Local>0.25f && Local<WaterSettings.FillSeconds+1.4f);
    Drops->BatchUpdateInstancesTransforms(0,Transforms,true,true,true);
}
