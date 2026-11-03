// E-only staged test; NOT built/run. Parent chooses the shared build/test scope.
#include "Misc/AutomationTest.h"
#include "MCFreezeNativeMotion.h"
#include "MCFreezeDeathSnapshot.h"
#include "MCFreezeCapturedGeometry.h"
#include "MCVFXGameplayInputs.h"
#include "MCVFXNativeInputs.h"
#include "MCVFXComposition.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeMotionParityOnce,
    "MessControl.VFX.FreezeCapture.NativeMotionParityAndSingleIssue",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeMotionParityOnce::RunTest(const FString&)
{
    const FBox Bounds(FVector(-41,-32,-58),FVector(46,29,103));FString Error;
    FMCFreezeNativeMotion Producer;
    TestTrue(TEXT("One real native episode is accepted"),Producer.IssueOnce(Bounds,Error));
    const auto Samples=Producer.GetShards();if(!TestEqual(TEXT("All36 native samples"),Samples.Num(),36))return false;
    // Independent literal oracle retained from original AMCFreezeDeathVFX::Capture,
    // same platform/compiler evaluation order; not a second runtime RNG path.
    constexpr int32 Across=3,High=4;
    const float HalfSize=FMath::Max(70.f,float(Bounds.GetExtent().GetMax()+15.f));
    const FVector Center=Bounds.GetCenter(),CubeCenter(Center.X,Center.Y,Bounds.Min.Z+HalfSize-4.f);
    const FVector Cell(HalfSize*2/Across,HalfSize*2/Across,HalfSize*2/High);FRandomStream Random(9173);
    for(int32 Z=0;Z<High;++Z)for(int32 Y=0;Y<Across;++Y)for(int32 X=0;X<Across;++X) {
        const int32 I=X+Across*(Y+Across*Z);
        const FVector Home=CubeCenter-FVector(HalfSize)+Cell*FVector(X+.5f,Y+.5f,Z+.5f);
        const FVector Direction=(Home-CubeCenter).GetSafeNormal();
        const FVector Velocity=Direction*Random.FRandRange(105.f,225.f)+FVector(Random.FRandRange(-25,25),Random.FRandRange(-25,25),Random.FRandRange(110,230));
        const FVector Spin=FVector(Random.FRandRange(-130,130),Random.FRandRange(-160,160),Random.FRandRange(-120,120));
        TestEqual(*FString::Printf(TEXT("Home%d"),I),Samples[I].Home,Home);
        TestEqual(*FString::Printf(TEXT("Velocity%d"),I),Samples[I].Velocity,Velocity);
        TestEqual(*FString::Printf(TEXT("Spin%d"),I),Samples[I].Spin,Spin);
        TestEqual(*FString::Printf(TEXT("Floor%d"),I),Samples[I].Floor,float(Bounds.Min.Z+Cell.GetMin()*.32f));
    }
    const auto Last=Samples[35];const auto* Address=Samples.GetData();
    TestFalse(TEXT("Fallback cannot issue another episode"),Producer.IssueOnce(FBox(FVector(-2),FVector(2)),Error));
    TestTrue(TEXT("Snapshot address remains retained"),Producer.GetShards().GetData()==Address);
    TestEqual(TEXT("Second issue did not replace final spin"),Producer.GetShards()[35].Spin,Last.Spin);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeCellLocalMaterialUV,
    "MessControl.VFX.FreezeCapture.CellLocalMaterialUVAndImmutableSnapshot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeCellLocalMaterialUV::RunTest(const FString&)
{
    using namespace MCFreezeDeathSnapshot;
    FBuffer Snapshot;FString Error;const FTriangleIndex Topology[]={ {0,1,2,1} };
    TestTrue(TEXT("Bounded material partition prepared"),Snapshot.Prepare(3,2,MakeArrayView(Topology),Error));
    FFinalSkinVertex Pose[3]{};
    Pose[0].Position=FVector3f(0,0,0);Pose[1].Position=FVector3f(3,0,0);Pose[2].Position=FVector3f(0,3,0);
    Pose[0].TextureCoordinates[0]=FVector2D(.1,.2);Pose[1].TextureCoordinates[0]=FVector2D(.3,.4);Pose[2].TextureCoordinates[0]=FVector2D(.5,.6);
    for(auto& V:Pose)V.TangentZ=FVector3f(0,0,1);
    const FTransform Frame(FRotator(0,37,0),FVector(1000000000,2000000000,3000000000));
    FMCFreezeNativeMotion Motion;const FBox Bounds(FVector(0),FVector(3,3,0));
    TestTrue(TEXT("One native motion set"),Motion.IssueOnce(Bounds,Error));
    TestTrue(TEXT("Same actual pose seals immutable packet"),Snapshot.Seal(MakeArrayView(Pose),Frame,Frame,12.5,Motion.GetShards(),Error));
    const auto Triangles=Snapshot.GetTriangles();if(!TestEqual(TEXT("Original triangle count"),Triangles.Num(),1))return false;
    const auto& T=Triangles[0];const FVector Home=Motion.GetShards()[T.Cell].Home;
    TestTrue(TEXT("Cell-local frozen PreSkinned semantics preserve captured A"),(FVector(T.A)+Home).Equals(FVector(Pose[0].Position),.001));
    TestEqual(TEXT("Original material partition"),int32(T.Slot),1);
    TestTrue(TEXT("Original UV A"),FVector2D(T.UA).Equals(Pose[0].TextureCoordinates[0],1.e-6));
    TestTrue(TEXT("Original UV C"),FVector2D(T.UC).Equals(Pose[2].TextureCoordinates[0],1.e-6));
    TestEqual(TEXT("Accepted timestamp remains native"),Snapshot.GetAcceptedAt(),12.5);
    TestFalse(TEXT("Cannot replace immutable pose"),Snapshot.Seal(MakeArrayView(Pose),Frame,Frame,15,Motion.GetShards(),Error));
    TestEqual(TEXT("Rejected replacement preserves native timestamp"),Snapshot.GetAcceptedAt(),12.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeMetadataVersusAdmission,
    "MessControl.VFX.FreezeCapture.DeclaredMetadataDoesNotAuthorizeActivation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeMetadataVersusAdmission::RunTest(const FString&)
{
    auto* System=NewObject<UNiagaraSystem>();auto& Store=System->GetExposedParameters();FString Error;
    auto Declare=[&](FString Suffix) {
        const FName Name(*(TEXT("User.Native.FreezeCapture.")+Suffix));FNiagaraTypeDefinition Type;
        if(!FMCPreparedFreezeGeometryLayer::FindType(Name,Type)){AddError(TEXT("Test declaration is not recognized: ")+Suffix);return;}
        Store.AddParameter(FNiagaraVariable(Type,Name));
    };
    for(int32 I=0;I<6;++I)Declare(FString::Printf(TEXT("Row%d"),I));
    for(int32 I=0;I<3;++I)Declare(FString::Printf(TEXT("Motion%d"),I));
    for(const TCHAR* Name:{TEXT("CellSlot"),TEXT("Ready"),TEXT("Version"),TEXT("Age"),TEXT("HalfSize"),TEXT("FrameOrigin"),TEXT("FrameRotation"),TEXT("FrameScale"),TEXT("CubeCenter"),TEXT("Glass")})Declare(Name);
    for(int32 I=0;I<8;++I){Declare(FString::Printf(TEXT("Count%d"),I));Declare(FString::Printf(TEXT("Offset%d"),I));Declare(FString::Printf(TEXT("Material%d"),I));}
    TestTrue(TEXT("Typed metadata can prepare/discover before a fatal resource exists"),MCVFXGameplayInputs::ValidateSystem(System,Error));
    FMCVFXNativeInputs Payload;
    TestFalse(TEXT("The same metadata alone cannot authorize runtime payload activation"),Payload.ValidateForSystem(System,Error));
    FMCVFXCompositionLayer Layer;FMCVFXBoolOverride Ready;Ready.Name=TEXT("User.Native.FreezeCapture.Ready");Ready.bOverride=true;Ready.Value=true;Layer.Bools.Add(Ready);
    TestFalse(TEXT("Artist override cannot fabricate native capture readiness"),UMCVFXComposition::ValidateParameters(Layer,System,Error));
    const FName Row0(TEXT("User.Native.FreezeCapture.Row0"));FNiagaraTypeDefinition ArrayType;
    FMCPreparedFreezeGeometryLayer::FindType(Row0,ArrayType);Store.RemoveParameter(FNiagaraVariable(ArrayType,Row0));
    Store.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),Row0));
    TestFalse(TEXT("Metadata rejects wrong stock DI type without silently accepting it"),MCVFXGameplayInputs::ValidateSystem(System,Error));
    return true;
}
#endif
