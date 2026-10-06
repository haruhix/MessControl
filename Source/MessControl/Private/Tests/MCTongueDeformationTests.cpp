#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCHazardWave.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCTongueCachedDeformationTest,"MessControl.Tongue.CachedDeformationMatchesCollisionSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCTongueCachedDeformationTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
    auto* Tongue=World->SpawnActor<AMCTongue>();
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/SM_TongueSurface.SM_TongueSurface"));
#if WITH_EDITOR
    if (Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
    Tongue->SourceMesh=Mesh; Tongue->RebuildSurface(); Tongue->Settings.Sanitize();
    if (!TestTrue(TEXT("Uses the authored collision surface"),Tongue->Rest.Num()>100))
    { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false; }
    TArray<FMCTongueMotionState> Pulses;
    FMCTongueMotionState Pulse; Pulse.Settings.Shape=EMCTongueShape::RadialWave; Pulse.Settings.Height=27;
    Pulse.Settings.Redness=.7f; Pulse.StartedAt=1; Pulse.Origin=Tongue->RestBounds.GetCenter(); Pulses.Add(Pulse);
    Tongue->YawnStartedAt=.2; Tongue->YawnDuration=4;
    float MaxHeightError=0,MaxColorError=0;
    const EMCTongueShape Shapes[]={EMCTongueShape::FrontBend,EMCTongueShape::RadialWave,EMCTongueShape::LocalLift,EMCTongueShape::DirectionalWave};
    const float Times[]={-.1f,.1f,.7f,1.5f,3.f,8.f,300.f};
    Tongue->CurrentWorldVertices();
    for (int32 Pass=0;Pass<2;++Pass)
    {
        Tongue->SetActorTransform(Pass?FTransform(FRotator(12,35,-7),FVector(200,-340,70),FVector(1.2,.8,1.4)):FTransform::Identity);
        const auto& WorldVertices=Tongue->CurrentWorldVertices();
        bool SnapshotMatches=true;
        for(int32 I=0;I<WorldVertices.Num();++I)
        {
            const FVector Expected=Tongue->Surface->GetComponentTransform().TransformPosition(Tongue->CurrentVertices()[I]);
            SnapshotMatches &= FMemory::Memcmp(&Expected,&WorldVertices[I],sizeof(FVector))==0;
        }
        TestTrue(TEXT("Shared world vertices preserve every transformed surface vertex exactly"),SnapshotMatches);
        Tongue->RefreshDeformationMotion();
        bool TransformRefreshed=true;
        for (int32 I=0;I<Tongue->DeformationSamples.Num();I+=107)
        {
            const auto& Sample=Tongue->DeformationSamples[I];
            TransformRefreshed &= FMath::IsNearlyEqual(Sample.Distance,Tongue->MotionDistance(Sample.Point),.001f)
                && FMath::IsNearlyEqual(Sample.MotionMask,Tongue->MotionWeight(Sample.Point),.00001f);
        }
        TestTrue(TEXT("Moving/scaling the tongue invalidates samples without a new motion serial"),TransformRefreshed);
        for (EMCTongueShape Shape:Shapes)
        {
            Tongue->Motion.Settings.Shape=Shape; Tongue->Motion.Settings.Redness=.8f;
            Tongue->Motion.Origin=Tongue->RestBounds.GetCenter()+FVector(25,-40,0);
            Tongue->Motion.Direction=FVector(.6,.8,0); Tongue->Motion.StartedAt=.1; ++Tongue->Motion.Serial;
            Tongue->RefreshDeformationMotion();
            for (float Time:Times)
            {
                const float Idle=Time*2*PI/Tongue->Settings.IdlePeriod;
                const float Envelope=Tongue->Motion.Settings.Envelope(Time-Tongue->Motion.StartedAt);
                const float Yawn=Time>=Tongue->YawnStartedAt && Time<Tongue->YawnStartedAt+Tongue->YawnDuration
                    ?45*FMath::Sin(PI*(Time-Tongue->YawnStartedAt)/FMath::Max(1.f,Tongue->YawnDuration)):0;
                for (int32 I=0;I<Tongue->DeformationSamples.Num();I+=17)
                {
                    const auto& Sample=Tongue->DeformationSamples[I]; float A=0,B=0;
                    const float Expected=Tongue->Offset(Sample.Point,Time,A,Pulses);
                    const float Actual=Tongue->SampleOffset(Sample,Time,Idle,Envelope,Yawn,B,Pulses);
                    MaxHeightError=FMath::Max(MaxHeightError,FMath::Abs(Expected-Actual));
                    MaxColorError=FMath::Max(MaxColorError,FMath::Abs(A-B));
                }
            }
        }
    }
    TestTrue(TEXT("Every displacement/normal sample preserves motion, yawn and overlapping ulcers"),MaxHeightError<.001f);
    TestTrue(TEXT("The pressure/event material mask is preserved"),MaxColorError<.00001f);
    // A replicated reset+start can reuse a serial without the client observing
    // the neutral state. Its geometry still has to refresh immediately.
    Tongue->Motion.Origin+=FVector(130,-280,0);Tongue->Motion.Direction=FVector(0,-1,0);
    Tongue->Motion.Settings.Shape=EMCTongueShape::LocalLift;Tongue->Motion.Settings.Radius=370;
    Tongue->RefreshDeformationMotion();
    bool ReusedSerialRefreshed=true;
    for (int32 I=0;I<Tongue->DeformationSamples.Num();I+=107)
    {
        const auto& Sample=Tongue->DeformationSamples[I];
        ReusedSerialRefreshed &= FMath::IsNearlyEqual(Sample.Distance,Tongue->MotionDistance(Sample.Point),.001f)
            && FMath::IsNearlyEqual(Sample.MotionMask,Tongue->MotionWeight(Sample.Point),.00001f);
    }
    TestTrue(TEXT("A reused motion serial refreshes the origin, shape, radius and direction"),ReusedSerialRefreshed);
    Tongue->Motion=FMCTongueMotionState(); Tongue->RefreshDeformationMotion(); Pulses.Reset();
    float Sum=0,Red=0;
    const double ReferenceStart=FPlatformTime::Seconds();
    for (int32 Repeat=0;Repeat<4;++Repeat) for (const auto& Sample:Tongue->DeformationSamples)
        Sum+=Tongue->Offset(Sample.Point,8,Red,Pulses);
    const double ReferenceMs=(FPlatformTime::Seconds()-ReferenceStart)*250;
    const double CachedStart=FPlatformTime::Seconds();
    const float Idle=8.f*2*PI/Tongue->Settings.IdlePeriod;
    for (int32 Repeat=0;Repeat<4;++Repeat) for (const auto& Sample:Tongue->DeformationSamples)
        Sum+=Tongue->SampleOffset(Sample,8,Idle,0,0,Red,Pulses);
    const double CachedMs=(FPlatformTime::Seconds()-CachedStart)*250;
    AddInfo(FString::Printf(TEXT("Authored tongue %d samples; displacement reference %.3f ms, cached %.3f ms; max height error %.8f; checksum %.3f"),Tongue->DeformationSamples.Num(),ReferenceMs,CachedMs,MaxHeightError,Sum));
    Tongue->Deform(8.f);
    const auto& DeformedWorldVertices=Tongue->CurrentWorldVertices();
    bool RevisionRefreshed=true;
    for(int32 I=0;I<DeformedWorldVertices.Num();++I)
    {
        const FVector Expected=Tongue->Surface->GetComponentTransform().TransformPosition(Tongue->CurrentVertices()[I]);
        RevisionRefreshed &= FMemory::Memcmp(&Expected,&DeformedWorldVertices[I],sizeof(FVector))==0;
    }
    TestTrue(TEXT("Deformation invalidates the shared world snapshot"),RevisionRefreshed);
    IConsoleVariable* ParallelVertices=IConsoleManager::Get().FindConsoleVariable(TEXT("mc.TongueParallelVertices"));
    if(TestNotNull(TEXT("The tongue vertex parallelism switch is registered"),ParallelVertices))
    {
        struct FRestoreParallelSetting
        {
            IConsoleVariable* Variable; int32 Previous;
            ~FRestoreParallelSetting() { Variable->SetWithCurrentPriority(Previous); }
        } RestoreParallelSetting{ParallelVertices,ParallelVertices->GetInt()};
        TestTrue(TEXT("The authored tongue exceeds the parallel worker selection threshold"),Tongue->Rest.Num()>1024);
        for(int32 I=0;I<Tongue->Rest.Num();++I)
        {
            Tongue->IndentDepth[I]=(I%19)*.375f;
            Tongue->IndentGradient[I]=FVector((I%7-3)*.015f,(I%11-5)*.01f,(I%5-2)*.0125f);
        }
        bool PositionsExact=true,NormalsExact=true,ColorsExact=true,TangentsExact=true,RevisionsExact=true;
        bool ReferencePositionsExact=true,ReferenceNormalsExact=true,ReferenceColorsExact=true,ReferenceTangentsExact=true;
        struct FOriginalVertexMatch
        {
            bool Positions=true,Normals=true,Colors=true,Tangents=true;
        };
        const auto CheckOriginalVertexFormula=[&](float Time,TConstArrayView<FMCTongueMotionState> ReferencePulses,int32 ExtraIndex=INDEX_NONE)
        {
            FOriginalVertexMatch Match;
            // Retain the original actor SampleOffset path independently of the new kernel.
            const float ReferenceIdle=Time*2*PI/Tongue->Settings.IdlePeriod;
            const float ReferenceEnvelope=Tongue->Motion.Settings.IsWave()?0:Tongue->Motion.Settings.Envelope(Time-Tongue->Motion.StartedAt);
            const float ReferenceYawn=Tongue->YawnStartedAt>=0 && Time>=Tongue->YawnStartedAt && Time<Tongue->YawnStartedAt+Tongue->YawnDuration
                ?45*FMath::Sin(PI*(Time-Tongue->YawnStartedAt)/FMath::Max(1.f,Tongue->YawnDuration)):0;
            const float ReferenceScaleZ=FMath::Abs(Tongue->GetActorScale3D().Z);
            for(int32 SampleIndex=0;SampleIndex<=64+(ExtraIndex!=INDEX_NONE);++SampleIndex)
            {
                const int32 I=SampleIndex==65?ExtraIndex:int32(int64(SampleIndex)*(Tongue->Rest.Num()-1)/64);
                const FVector P=Tongue->Rest[I]; float ReferenceRed=0,Dummy=0;
                const auto* Samples=&Tongue->DeformationSamples[I*7];
                auto OffsetAt=[&](int32 J,float& R) { return Tongue->SampleOffset(Samples[J],Time,ReferenceIdle,ReferenceEnvelope,ReferenceYawn,R,ReferencePulses); };
                const float PhysicalHeight=OffsetAt(0,ReferenceRed),Height=PhysicalHeight-Tongue->IndentDepth[I],Anchor=Tongue->AnchorWeights[I];
                const FVector ExpectedPosition=P+FVector(0,0,Height*Anchor); ReferenceRed*=Anchor;
                const float Dx=((OffsetAt(1,Dummy)-OffsetAt(2,Dummy))*.5f-Tongue->IndentGradient[I].X)*Anchor+Height*Tongue->AnchorGradients[I].X;
                const float Dy=((OffsetAt(3,Dummy)-OffsetAt(4,Dummy))*.5f-Tongue->IndentGradient[I].Y)*Anchor+Height*Tongue->AnchorGradients[I].Y;
                const float Dz=((OffsetAt(5,Dummy)-OffsetAt(6,Dummy))*.5f-Tongue->IndentGradient[I].Z)*Anchor+Height*Tongue->AnchorGradients[I].Z;
                const FVector N=Tongue->RestNormals[I],T=Tongue->RestTangents[I].TangentX;
                const float Nz=N.Z/FMath::Max(.5f,1+Dz);
                const FVector ExpectedNormal=FVector(N.X-Dx*Nz,N.Y-Dy*Nz,Nz).GetSafeNormal();
                const FProcMeshTangent ExpectedTangent((T+FVector(0,0,Dx*T.X+Dy*T.Y+Dz*T.Z)).GetSafeNormal(),Tongue->RestTangents[I].bFlipTangentY);
                const float Depth=Tongue->IndentDepth[I]*Anchor*ReferenceScaleZ;
                const float Mask=FMath::Clamp(Depth/FMath::Max(1.f,Tongue->PressureSettings.MaxDepth),0.f,1.f);
                const float Rim=FMath::Clamp(float((Tongue->IndentGradient[I]*Anchor+Tongue->IndentDepth[I]*Tongue->AnchorGradients[I]).Size2D())*4,0.f,1.f);
                const FColor ExpectedColor(FMath::RoundToInt(FMath::Clamp(ReferenceRed,0.f,1.f)*255),FMath::RoundToInt(Mask*255),FMath::RoundToInt(Rim*255),255);
                Match.Positions &= FMemory::Memcmp(&ExpectedPosition,&Tongue->Positions[I],sizeof(FVector))==0;
                Match.Normals &= FMemory::Memcmp(&ExpectedNormal,&Tongue->Normals[I],sizeof(FVector))==0;
                Match.Colors &= FMemory::Memcmp(&ExpectedColor,&Tongue->Colors[I],sizeof(FColor))==0;
                Match.Tangents &= FMemory::Memcmp(&ExpectedTangent.TangentX,&Tongue->Tangents[I].TangentX,sizeof(FVector))==0
                    && ExpectedTangent.bFlipTangentY==Tongue->Tangents[I].bFlipTangentY;
            }
            return Match;
        };
        const float ParallelTimes[]={-.1f,.7f,1.5f};
        for(int32 Pass=0;Pass<2;++Pass)
        {
            Tongue->SetActorTransform(Pass?FTransform(FRotator(12,35,-7),FVector(200,-340,70),FVector(1.2,.8,1.4)):FTransform::Identity);
            for(EMCTongueShape Shape:Shapes)
            {
                Tongue->Motion.Settings.Shape=Shape; Tongue->Motion.Settings.Redness=.8f;
                Tongue->Motion.Origin=Tongue->RestBounds.GetCenter()+FVector(25,-40,0);
                Tongue->Motion.Direction=FVector(.6,.8,0); Tongue->Motion.StartedAt=.1; ++Tongue->Motion.Serial;
                for(float Time:ParallelTimes)
                {
                    ParallelVertices->SetWithCurrentPriority(0);
                    const uint64 SingleRevision=Tongue->SurfaceRevision();
                    Tongue->Deform(Time);
                    RevisionsExact &= Tongue->SurfaceRevision()==SingleRevision+1;
                    const TArray<FVector> SinglePositions=Tongue->Positions,SingleNormals=Tongue->Normals;
                    const TArray<FColor> SingleColors=Tongue->Colors;
                    const TArray<FProcMeshTangent> SingleTangents=Tongue->Tangents;
                    ParallelVertices->SetWithCurrentPriority(1);
                    const uint64 ParallelRevision=Tongue->SurfaceRevision();
                    Tongue->Deform(Time);
                    RevisionsExact &= Tongue->SurfaceRevision()==ParallelRevision+1;
                    PositionsExact &= FMemory::Memcmp(SinglePositions.GetData(),Tongue->Positions.GetData(),SinglePositions.Num()*sizeof(FVector))==0;
                    NormalsExact &= FMemory::Memcmp(SingleNormals.GetData(),Tongue->Normals.GetData(),SingleNormals.Num()*sizeof(FVector))==0;
                    ColorsExact &= FMemory::Memcmp(SingleColors.GetData(),Tongue->Colors.GetData(),SingleColors.Num()*sizeof(FColor))==0;
                    // FProcMeshTangent has unspecified padding after its bool.
                    // Compare every stored field, including signed-zero bits.
                    for(int32 I=0;I<SingleTangents.Num();++I)
                        TangentsExact &= FMemory::Memcmp(&SingleTangents[I].TangentX,&Tongue->Tangents[I].TangentX,sizeof(FVector))==0
                            && SingleTangents[I].bFlipTangentY==Tongue->Tangents[I].bFlipTangentY;
                    const auto Reference=CheckOriginalVertexFormula(Time,Pulses);
                    ReferencePositionsExact &= Reference.Positions; ReferenceNormalsExact &= Reference.Normals;
                    ReferenceColorsExact &= Reference.Colors; ReferenceTangentsExact &= Reference.Tangents;
                }
            }
        }
        TestTrue(TEXT("Single-thread and parallel positions are bit-identical at the same time"),PositionsExact);
        TestTrue(TEXT("Single-thread and parallel normals are bit-identical at the same time"),NormalsExact);
        TestTrue(TEXT("Single-thread and parallel material masks are bit-identical at the same time"),ColorsExact);
        TestTrue(TEXT("Single-thread and parallel tangent fields are bit-identical at the same time"),TangentsExact);
        TestTrue(TEXT("Parallel positions preserve the original SampleOffset vertex formula bit-for-bit"),ReferencePositionsExact);
        TestTrue(TEXT("Parallel normals preserve the original SampleOffset vertex formula bit-for-bit"),ReferenceNormalsExact);
        TestTrue(TEXT("Parallel material masks preserve the original SampleOffset vertex formula bit-for-bit"),ReferenceColorsExact);
        TestTrue(TEXT("Parallel tangent fields preserve the original SampleOffset vertex formula bit-for-bit"),ReferenceTangentsExact);
        TestTrue(TEXT("Each single-thread or parallel surface write advances its revision exactly once"),RevisionsExact);
        // Construct real actors through the same ulcer-to-tongue binding and
        // snapshotted wave setup as gameplay. No world tick advances either pose.
        Tongue->Motion=FMCTongueMotionState();
        FHitResult PulseFloor;
        if(TestTrue(TEXT("The live pulse fixture has a tongue surface"),Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(Tongue->RestBounds.GetCenter()),PulseFloor)))
        {
            const FTransform UlcerTransform(PulseFloor.ImpactPoint+FVector(0,0,5));
            auto* Ulcer=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),UlcerTransform);
            if(TestNotNull(TEXT("The pulse fixture ulcer is created"),Ulcer))
            {
                Ulcer->bUlcer=true; Ulcer->bRandomizeLiquidSize=false; Ulcer->FinishSpawning(UlcerTransform); Ulcer->SetActorTickEnabled(false);
                if(TestEqual(TEXT("The live ulcer binds to the authored tongue"),Ulcer->GetTongue(),Tongue))
                {
                    int32 CreatedWaves=0;
                    for(int32 I=0;I<2;++I)
                    {
                        const FTransform WaveTransform(Ulcer->GetActorLocation()+FVector(I*45,0,0));
                        auto* Wave=World->SpawnActorDeferred<AMCHazardWave>(AMCHazardWave::StaticClass(),WaveTransform);
                        if(TestNotNull(TEXT("The live pulse wave is created"),Wave))
                        {
                            Wave->Source=Ulcer; Wave->MaxRadius=260; Wave->WarningSeconds=.6f; Wave->TravelSeconds=1.1f;
                            Wave->FinishSpawning(WaveTransform); Wave->SetActorTickEnabled(false); Wave->StartedAt=I*.125;
                            ++CreatedWaves;
                        }
                    }
                    if(CreatedWaves==2) for(float Time:{.85f,1.25f})
                    {
                        TArray<FMCTongueMotionState,TInlineAllocator<6>> LivePulses;
                        for(TActorIterator<AMCHazardWave> It(World);It;++It)
                        {
                            FMCTongueMotionState LivePulse;
                            if(It->SurfaceMotion(Tongue,Time,LivePulse)) LivePulses.Add(LivePulse);
                        }
                        TestEqual(TEXT("Both live pulses enter the same ordered gather as Deform"),LivePulses.Num(),2);
                        ParallelVertices->SetWithCurrentPriority(0);
                        const uint64 LiveSingleRevision=Tongue->SurfaceRevision(); Tongue->Deform(Time);
                        TestEqual(TEXT("A serial live pulse writes one surface revision"),Tongue->SurfaceRevision(),LiveSingleRevision+1);
                        float PeakContribution=0; int32 PeakIndex=INDEX_NONE;
                        const float LiveIdle=Time*2*PI/Tongue->Settings.IdlePeriod;
                        const float LiveYawn=Tongue->YawnStartedAt>=0 && Time>=Tongue->YawnStartedAt && Time<Tongue->YawnStartedAt+Tongue->YawnDuration
                            ?45*FMath::Sin(PI*(Time-Tongue->YawnStartedAt)/FMath::Max(1.f,Tongue->YawnDuration)):0;
                        for(int32 I=0;I<Tongue->Rest.Num();++I)
                        {
                            float NoPulseRed=0;
                            const float NeutralHeight=Tongue->SampleOffset(Tongue->DeformationSamples[I*7],Time,LiveIdle,0,LiveYawn,NoPulseRed,Pulses);
                            const FVector NoPulsePosition=Tongue->Rest[I]+FVector(0,0,(NeutralHeight-Tongue->IndentDepth[I])*Tongue->AnchorWeights[I]);
                            const float Contribution=Tongue->Positions[I].Z-NoPulsePosition.Z;
                            if(Contribution>PeakContribution) { PeakContribution=Contribution; PeakIndex=I; }
                        }
                        TestTrue(TEXT("Live gathered pulses produce a nonzero visible crest"),PeakContribution>1 && PeakIndex!=INDEX_NONE);
                        if(PeakIndex!=INDEX_NONE) TestTrue(TEXT("Live gathered pulses produce a nonzero redness mask"),Tongue->Colors[PeakIndex].R>0);
                        const auto SerialReference=CheckOriginalVertexFormula(Time,LivePulses,PeakIndex);
                        TestTrue(TEXT("Serial live pulses preserve original position, normal, color and tangent bits"),SerialReference.Positions && SerialReference.Normals && SerialReference.Colors && SerialReference.Tangents);
                        const TArray<FVector> LivePositions=Tongue->Positions,LiveNormals=Tongue->Normals;
                        const TArray<FColor> LiveColors=Tongue->Colors;
                        const TArray<FProcMeshTangent> LiveTangents=Tongue->Tangents;
                        ParallelVertices->SetWithCurrentPriority(1);
                        const uint64 LiveParallelRevision=Tongue->SurfaceRevision(); Tongue->Deform(Time);
                        TestEqual(TEXT("A parallel live pulse writes one surface revision"),Tongue->SurfaceRevision(),LiveParallelRevision+1);
                        const auto ParallelReference=CheckOriginalVertexFormula(Time,LivePulses,PeakIndex);
                        TestTrue(TEXT("Parallel live pulses preserve original position, normal, color and tangent bits"),ParallelReference.Positions && ParallelReference.Normals && ParallelReference.Colors && ParallelReference.Tangents);
                        bool LiveFieldsExact=FMemory::Memcmp(LivePositions.GetData(),Tongue->Positions.GetData(),LivePositions.Num()*sizeof(FVector))==0
                            && FMemory::Memcmp(LiveNormals.GetData(),Tongue->Normals.GetData(),LiveNormals.Num()*sizeof(FVector))==0
                            && FMemory::Memcmp(LiveColors.GetData(),Tongue->Colors.GetData(),LiveColors.Num()*sizeof(FColor))==0;
                        for(int32 I=0;I<LiveTangents.Num();++I)
                            LiveFieldsExact &= FMemory::Memcmp(&LiveTangents[I].TangentX,&Tongue->Tangents[I].TangentX,sizeof(FVector))==0
                                && LiveTangents[I].bFlipTangentY==Tongue->Tangents[I].bFlipTangentY;
                        TestTrue(TEXT("Overlapping live pulses keep every serial and parallel vertex field bit-identical"),LiveFieldsExact);
                    }
                }
            }
        }
    }
    World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
