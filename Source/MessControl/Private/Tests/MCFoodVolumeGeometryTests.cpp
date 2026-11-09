#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodCutGeometry.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "UObject/StrongObjectPtr.h"

namespace MCFoodVolumeGeometryTests
{
using namespace MCFoodCutGeometry;

TStrongObjectPtr<UProceduralMeshComponent> Mesh()
{
    return TStrongObjectPtr<UProceduralMeshComponent>(NewObject<UProceduralMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient));
}

void Shape(UProceduralMeshComponent* Target,const TArray<FVector>& Vertices,const TArray<int32>& Indices)
{
    FProcMeshSection Section;Section.bEnableCollision=false;
    for(const FVector& P:Vertices) {auto& V=Section.ProcVertexBuffer.AddDefaulted_GetRef();V.Position=P;Section.SectionLocalBox+=P;}
    for(int32 Index:Indices)Section.ProcIndexBuffer.Add(uint32(Index));
    Target->SetProcMeshSection(0,Section);
}

void ConcavePrism(UProceduralMeshComponent* Target,bool Open)
{
    // L-shaped cross section: area 5, depth 2, exact volume 10. Its convex
    // envelope has area 7 and volume 14, exposing a hull-only implementation.
    TArray<FVector> V;
    for(double Z:{0.,2.})for(const FVector2D P:{FVector2D(0,0),FVector2D(3,0),FVector2D(3,1),FVector2D(1,1),FVector2D(1,3),FVector2D(0,3)})
        V.Emplace(P.X,P.Y,Z);
    const int32 Cap[12]={0,1,3,1,2,3,0,3,5,3,4,5};TArray<int32> T;
    for(int32 I=0;I<12;I+=3) {
        T.Append({Cap[I+2],Cap[I+1],Cap[I]});
        if(!Open)T.Append({Cap[I]+6,Cap[I+1]+6,Cap[I+2]+6});
    }
    for(int32 A=0;A<6;++A) {const int32 B=(A+1)%6;T.Append({A,B,B+6,A,B+6,A+6});}
    Shape(Target,V,T);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodClosedVolumeGeometryTest,"MessControl.Food.VolumeGeometry.ClosedAndTransforms",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodClosedVolumeGeometryTest::RunTest(const FString&)
{
    using namespace MCFoodVolumeGeometryTests;
    auto Target=Mesh();TArray<FVector> V,N;TArray<int32> T;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;
    UKismetProceduralMeshLibrary::GenerateBoxMesh(FVector(300,2,1),V,T,N,UV,Tangents);
    for(FVector& P:V)P+=FVector(100000,-200000,300000);
    V.Add(FVector(1000000)); // A stale unreferenced buffer vertex is not food.
    Shape(Target.Get(),V,T);
    TArray<FIsland> Islands;FString Error;
    if(!TestTrue(TEXT("Long thin closed box can be measured"),FindIslands(Target.Get(),Islands,&Error)))return false;
    if(!TestEqual(TEXT("One connected solid"),Islands.Num(),1))return false;
    TestTrue(TEXT("Triangle volume is preferred"),Islands[0].VolumeMethod==EVolumeMethod::ClosedMesh);
    TestTrue(TEXT("600 x 4 x 2 cm is 4800 cubic cm"),FMath::IsNearlyEqual(Islands[0].Volume,4800.,1.e-6));
    TestTrue(TEXT("Unused buffer vertices do not enlarge referenced bounds"),FMath::IsNearlyEqual(Islands[0].Bounds.GetSize().X,600.,1.e-6));

    const FTransform Measurement(FRotator(31,57,13),FVector(1.e9),FVector(-2,3,.5));
    if(!TestTrue(TEXT("Mirrored nonuniform scale and rotation remain measurable"),FindIslands(Target.Get(),Islands,&Error,256,Measurement)))return false;
    TestTrue(TEXT("Solid volume follows the absolute scale determinant"),FMath::IsNearlyEqual(Islands[0].Volume,14400.,1.e-5));
    TestTrue(TEXT("Measurement leaves replay bounds mesh-local"),FMath::IsNearlyEqual(Islands[0].Bounds.GetSize().X,600.,1.e-6));

    // A real stock-generated/sliced solid also exercises seam welding and
    // cap edges whose long segments omit intermediate collinear skin points.
    UKismetProceduralMeshLibrary::GenerateBoxMesh(FVector(3,2,1),V,T,N,UV,Tangents);
    Shape(Target.Get(),V,T);
    if(!TestTrue(TEXT("Cut creates a capped half"),SliceAndCap(Target.Get(),FVector::ZeroVector,FVector::ForwardVector,nullptr)))return false;
    if(!TestTrue(TEXT("Capped half can be measured"),FindIslands(Target.Get(),Islands,&Error)))return false;
    TestEqual(TEXT("Capped half remains one island"),Islands.Num(),1);
    if(Islands.Num()==1) {
        TestTrue(TEXT("Capped half is a coherent closed surface"),Islands[0].VolumeMethod==EVolumeMethod::ClosedMesh);
        TestTrue(TEXT("Capped half keeps exactly half of 48 cubic cm"),FMath::IsNearlyEqual(Islands[0].Volume,24.,1.e-5));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodConcaveVolumeGeometryTest,"MessControl.Food.VolumeGeometry.ConcaveAndOpenFallback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodConcaveVolumeGeometryTest::RunTest(const FString&)
{
    using namespace MCFoodVolumeGeometryTests;
    auto Target=Mesh();ConcavePrism(Target.Get(),false);TArray<FIsland> Islands;FString Error;
    if(!TestTrue(TEXT("Closed concave prism is measurable"),FindIslands(Target.Get(),Islands,&Error)))return false;
    if(!TestEqual(TEXT("One concave part"),Islands.Num(),1))return false;
    TestTrue(TEXT("Concavity uses actual closed triangles"),Islands[0].VolumeMethod==EVolumeMethod::ClosedMesh);
    TestTrue(TEXT("Concave volume excludes empty hull space"),FMath::IsNearlyEqual(Islands[0].Volume,10.,1.e-6));

    ConcavePrism(Target.Get(),true);
    if(!TestTrue(TEXT("Open 3D artwork has a conservative measure"),FindIslands(Target.Get(),Islands,&Error)))return false;
    TestTrue(TEXT("Open surface cannot use a cancelling signed sum"),Islands[0].VolumeMethod==EVolumeMethod::ConvexHull);
    TestTrue(TEXT("Open surface falls back to the 14 cubic cm envelope"),FMath::IsNearlyEqual(Islands[0].Volume,14.,1.e-4));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodPlanarVolumeGeometryTest,"MessControl.Food.VolumeGeometry.PlanarAndInvalid",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodPlanarVolumeGeometryTest::RunTest(const FString&)
{
    using namespace MCFoodVolumeGeometryTests;
    auto Target=Mesh();const FVector A(0,0,0),B(600,0,0),C(0,4,4);
    Shape(Target.Get(),{A,B,C},{0,1,2});TArray<FIsland> Islands;FString Error;
    const FTransform Measurement(FRotator(17,29,43),FVector(1.e9),FVector(-2,3,.5));
    if(!TestTrue(TEXT("A thin one-sided ornament is measured explicitly"),FindIslands(Target.Get(),Islands,&Error,256,Measurement)))return false;
    if(!TestEqual(TEXT("One planar island"),Islands.Num(),1))return false;
    const double Area=FVector::CrossProduct(Measurement.TransformVector(B-A),Measurement.TransformVector(C-A)).Size()*.5;
    TestTrue(TEXT("Open planar skin has an explicit metric status"),Islands[0].VolumeMethod==EVolumeMethod::PlanarSurface);
    TestTrue(TEXT("Oblique surface area respects nonuniform scale"),FMath::IsNearlyEqual(Islands[0].SurfaceArea,Area,1.e-6));
    TestTrue(TEXT("Planar volume uses 0.1 cm nominal thickness, not zero"),FMath::IsNearlyEqual(Islands[0].Volume,Area*.1,1.e-6));

    const FTransform Singular(FQuat::Identity,FVector::ZeroVector,FVector(1,0,1));
    TestFalse(TEXT("Singular measurement cannot become an edible zero"),FindIslands(Target.Get(),Islands,&Error,256,Singular));
    TestTrue(TEXT("Failed measurement is atomic"),Islands.IsEmpty());
    Shape(Target.Get(),{A,B,C},{0,1,999});
    TestFalse(TEXT("Broken triangle indices retain the original transaction"),FindIslands(Target.Get(),Islands,&Error));
    TestTrue(TEXT("Invalid topology returns no accepted islands"),Islands.IsEmpty());
    return true;
}
#endif
