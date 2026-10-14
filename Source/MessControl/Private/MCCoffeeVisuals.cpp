#include "MCCoffeeFlood.h"
#include "MCCataclysmStormMath.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

void AMCCoffeeFlood::UpdatePour(float Time)
{
    if(IsCataclysmStorm())
    {
        // The drink enters along the tongue. The old ceiling jet and radial
        // crown belong only to the legacy pour, never to this incoming stream.
        Jet->SetVisibility(false);Crown->SetVisibility(false);
        const FVector Direction=StormStreamDirection(),Side(-Direction.Y,Direction.X,0);
        const float HalfWidth=FMath::Abs(Side.X)*HalfSize.X+FMath::Abs(Side.Y)*HalfSize.Y;
        auto Noise=[](float N){return FMath::Frac(FMath::Abs(FMath::Sin(N*127.1f)*43758.5453f));};
        TArray<FTransform> Spray;Spray.Reserve(72);
        for(int32 I=0;I<72;++I)
        {
            // Three bounded pools: arrival breakers, continuous entrance spray,
            // and inward-facing foam lifted by turbulence around the throat.
            const int32 Group=I/24,Drop=I%24;
            const float Life=.55f+Noise(I+11)*.35f,Age=FMath::Fmod(Time+Noise(I+91)*Life,Life);
            const float Born=FMath::Max(0.f,Time-Age),Generation=FMath::FloorToFloat((Time+Noise(I+91)*Life)/Life);
            const float Across=FMath::Lerp(-HalfWidth*.8f,HalfWidth*.8f,Noise(Drop*4.7f+Generation*13.1f+37));
            FVector Launch=RiverOrigin+Side*Across;
            FVector Heading=Direction;
            bool SourceVisible=Born>.2f;
            if(Group==0)
            {
                const float Front=StormFrontAt(Launch,Born);
                Launch+=Direction*(Front-145.f);
                SourceVisible&=Front<RiverLength+120.f;
            }
            else if(Group==1) Launch+=Direction*(260.f+Noise(I+23)*220.f);
            else
            {
                const float Angle=Drop*2.399963f+Generation*.7f;
                const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
                Launch=WaterSettings.DrainPoint+Radial*(120.f+Noise(I+31)*250.f);
                Heading=(-Radial+FVector(-Radial.Y,Radial.X,0)*.35f).GetSafeNormal2D();
            }
            SourceVisible&=StormWetAt(Launch,Born)>.3f;
            Launch.Z=StormHeightAt(Launch,Born)+12.f;
            const float Forward=Group==0?RiverSpeed*.38f:MCCataclysmStormMath::OpticalSpeed*(.6f+Noise(I+44)*.25f);
            const float Lateral=(Noise(I+51)-.5f)*180.f,Up=180.f+Noise(I+27)*240.f;
            const FVector Travel=Heading*Forward+Side*Lateral;
            const FVector P=Launch+Travel*Age+FVector(0,0,Up*Age-490.f*Age*Age);
            const FVector Velocity=Travel+FVector(0,0,Up-980.f*Age);
            const bool Inside=FMath::Abs(P.X-ArenaCenter.X)<HalfSize.X && FMath::Abs(P.Y-ArenaCenter.Y)<HalfSize.Y;
            const float Fade=FMath::Min(1.f,Age*16.f)*FMath::Min(1.f,(Life-Age)*8.f);
            const float Size=SourceVisible && Inside && P.Z>StormHeightAt(P,Time)-3.f?(.10f+Noise(I+17)*.12f)*Fade:0.f;
            Spray.Emplace(Velocity.Rotation(),P,FVector(Size*2.f,Size,Size));
        }
        Drops->SetVisibility(true);Drops->BatchUpdateInstancesTransforms(0,Spray,true,true,true);
        FVector Outlet=WaterSettings.DrainPoint;Outlet.Z=StormHeightAt(Outlet,Time)+5.f;
        const float Outflow=StormWetAt(WaterSettings.DrainPoint,Time);
        DrainRibbon->SetVisibility(Outflow>.05f);
        DrainRibbon->SetWorldLocationAndRotation(Outlet,Direction.Rotation());
        // Span the whole mouth lip. The radius of the local sink must not turn
        // the visible outflow into a narrow chute beside the players.
        const float SourceWidth=DrainRibbon->GetStaticMesh()?DrainRibbon->GetStaticMesh()->GetBounds().BoxExtent.Y*2.f:100.f;
        DrainRibbon->SetWorldScale3D(FVector(3.5f,HalfWidth*2.f/FMath::Max(SourceWidth,1.f),2.5f));
        if(DrainMaterial)
        {
            DrainMaterial->SetScalarParameterValue(TEXT("WaterTime"),Time);
            DrainMaterial->SetScalarParameterValue(TEXT("Strength"),Outflow);
        }
        return;
    }
    if (bRiverFlood)
    {
        Jet->SetVisibility(false); Crown->SetVisibility(false);
        const float Local=WaterSettings.CycleTime(Time);
        const FVector Side=FVector::CrossProduct(FVector::UpVector,RiverDirection);
        const float HalfWidth=FMath::Abs(Side.X)*HalfSize.X+FMath::Abs(Side.Y)*HalfSize.Y;
        auto Noise=[](float N){return FMath::Frac(FMath::Abs(FMath::Sin(N*127.1f)*43758.5453f));};
        TArray<FTransform> Spray; Spray.Reserve(72);
        float LaunchFloorZ=InletFloorZ;
        for (int32 I=0;I<72;++I)
        {
            // Nine independently pulsing breakers, rather than a regular row of
            // droplets. Each burst shares its floor probe; all motion is local VFX.
            const int32 Cluster=I/8;
            const float Life=.8f+Noise(Cluster+11)*.3f, Offset=Noise(Cluster+91)*Life;
            const float Age=FMath::Fmod(Local+Offset,Life);
            const float Born=Local-Age, BornTime=FMath::Max(0.f,Time-Age);
            const float Generation=FMath::FloorToFloat((Local+Offset)/Life);
            const float Across=FMath::Lerp(-HalfWidth*.88f,HalfWidth*.88f,Noise(Cluster*4.7f+Generation*13.1f+37));
            FVector Launch=RiverOrigin+Side*Across;
            Launch+=RiverDirection*(RiverFrontAt(Launch,BornTime)-80.f);
            if (I%8==0) LaunchFloorZ=RiverFloorAt(Launch);
            Launch+=Side*(float(I%8)-3.5f)*16.f+RiverDirection*(Noise(I+7)*60.f-30.f);
            Launch.Z=RiverHeightAt(Launch,BornTime,LaunchFloorZ)+22.f;
            const float Forward=RiverSpeed*.62f+Noise(I+44)*230.f;
            const float Lateral=(Noise(I+51)-.5f)*240.f, Up=350.f+Noise(I+27)*440.f;
            const FVector Velocity=RiverDirection*Forward+Side*Lateral+FVector(0,0,Up-980.f*Age);
            FVector P=Launch+(RiverDirection*Forward+Side*Lateral)*Age;
            P.Z+=Up*Age-490.f*Age*Age;
            const bool Inside=FMath::Abs(P.X-ArenaCenter.X)<HalfSize.X && FMath::Abs(P.Y-ArenaCenter.Y)<HalfSize.Y;
            const bool Visible=Inside && Born>.35f && P.Z>LaunchFloorZ+RiverDepth;
            const float Fade=FMath::Min(1.f,Age*16.f)*FMath::Min(1.f,(Life-Age)*8.f);
            const float Size=Visible?(.14f+Noise(I+17)*.18f)*Fade:0;
            const float Streak=FMath::Clamp(Velocity.Size()/400.f,1.4f,2.7f);
            Spray.Emplace(Velocity.Rotation(),P,FVector(Size*Streak,Size,Size));
        }
        Drops->SetVisibility(bActive); Drops->BatchUpdateInstancesTransforms(0,Spray,true,true,true);
        FVector Outlet=WaterSettings.DrainPoint; Outlet.Z=SurfaceHeightAt(Outlet)+4;
        const float Outflow=RiverWeightAt(Outlet);
        DrainRibbon->SetVisibility(Outflow>.05f); DrainRibbon->SetWorldLocationAndRotation(Outlet,RiverDirection.Rotation());
        DrainRibbon->SetWorldScale3D(FVector(3.5f,WaterSettings.DrainRadius/100,1));
        if (DrainMaterial) { DrainMaterial->SetScalarParameterValue(TEXT("WaterTime"),Time); DrainMaterial->SetScalarParameterValue(TEXT("Strength"),Outflow); }
        return;
    }
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
