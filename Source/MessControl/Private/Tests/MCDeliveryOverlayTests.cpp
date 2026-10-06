#if WITH_DEV_AUTOMATION_TESTS
#include "MCDeliveryZoneSurface.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FCompactSetting
{
    IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("mc.DeliveryCompactVertices"));
    int32 Previous=Variable?Variable->GetInt():1;
    explicit FCompactSetting(int32 Enabled) { if(Variable) Variable->SetWithCurrentPriority(Enabled); }
    ~FCompactSetting() { if(Variable) Variable->SetWithCurrentPriority(Previous); }
};

bool IdenticalBinding(const MCDeliveryGuide::FSurfaceVertex& A,const MCDeliveryGuide::FSurfaceVertex& B)
{
    return A.Source==B.Source
        && FMemory::Memcmp(&A.Weights.X,&B.Weights.X,sizeof(A.Weights.X))==0
        && FMemory::Memcmp(&A.Weights.Y,&B.Weights.Y,sizeof(A.Weights.Y))==0
        && FMemory::Memcmp(&A.Weights.Z,&B.Weights.Z,sizeof(A.Weights.Z))==0
        && FMemory::Memcmp(&A.Alpha,&B.Alpha,sizeof(A.Alpha))==0
        && FMemory::Memcmp(&A.Lift,&B.Lift,sizeof(A.Lift))==0
        && FMemory::Memcmp(&A.UV.X,&B.UV.X,sizeof(A.UV.X))==0
        && FMemory::Memcmp(&A.UV.Y,&B.UV.Y,sizeof(A.UV.Y))==0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDeliveryOverlayCompact,"MessControl.DeliveryOverlay.ExactCompaction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDeliveryOverlayCompact::RunTest(const FString&)
{
    FCompactSetting Enabled(1);
    using namespace MCDeliveryGuide;
    const FSurfaceVertex A{FIntVector(0,1,2),FVector(.25,.5,.25),.3f,2.f,FVector2D(1,2)};
    const FSurfaceVertex B{FIntVector(0,1,2),FVector(.5,.25,.25),.4f,3.f,FVector2D(2,3)};
    const FSurfaceVertex C{FIntVector(0,1,2),FVector(.25,.25,.5),.5f,6.f,FVector2D(3,4)};
    FSurfaceOverlay Overlay;Overlay.Vertices={A,B,C,A,B,C};Overlay.Indices={0,1,2,5,4,3};
    const FSurfaceOverlay Original=Overlay;
    TestEqual(TEXT("Only three identical duplicate bindings are removed"),Overlay.Compact(),3);
    TestEqual(TEXT("Unique vertex count"),Overlay.Vertices.Num(),3);
    TestEqual(TEXT("Index count and triangle order are retained"),Overlay.Indices.Num(),Original.Indices.Num());
    const FVector Sources[]={FVector(12,23,5),FVector(-4,72,16),FVector(3,-2,95)};
    const FTransform Destination(FRotator(17,29,-11),FVector(32,67,-9),FVector(1.2,.8,2));
    for(int32 I=0;I<Original.Indices.Num();++I)
    {
        const auto& Before=Original.Vertices[Original.Indices[I]];
        const auto& After=Overlay.Vertices[Overlay.Indices[I]];
        TestTrue(TEXT("Expanded index stream retains exact source, barycentrics and render attributes"),IdenticalBinding(Before,After));
        auto Position=[&](const FSurfaceVertex& V)
        {
            const FVector P=Sources[V.Source.X]*V.Weights.X+Sources[V.Source.Y]*V.Weights.Y+Sources[V.Source.Z]*V.Weights.Z;
            return Destination.InverseTransformPosition(P+FVector(0,0,V.Lift));
        };
        const FVector BeforePosition=Position(Before),AfterPosition=Position(After);
        TestTrue(TEXT("Deformed indexed position remains bit-identical"),FMemory::Memcmp(&BeforePosition,&AfterPosition,sizeof(FVector))==0);
    }
    TestEqual(TEXT("Compaction is idempotent"),Overlay.Compact(),0);
    TestTrue(TEXT("Original winding is retained after remap"),Overlay.Indices==TArray<int32>({0,1,2,2,1,0}));

    // Coincident current positions can separate after deformation. Different
    // source bindings, seams and lifts must remain distinct despite proximity.
    FSurfaceOverlay Seams;Seams.Vertices={A,A};
    FSurfaceVertex Variant=A;Variant.Weights.X+=1.e-12;Seams.Vertices.Add(Variant);
    Variant=A;Variant.Source.X=3;Seams.Vertices.Add(Variant);
    Variant=A;Variant.Alpha+=.01f;Seams.Vertices.Add(Variant);
    Variant=A;Variant.Lift+=.01f;Seams.Vertices.Add(Variant);
    Variant=A;Variant.UV.X+=1.e-12;Seams.Vertices.Add(Variant);
    Variant=A;Variant.UV.Y+=1.e-12;Seams.Vertices.Add(Variant);
    for(int32 I=0;I<Seams.Vertices.Num();++I) Seams.Indices.Add(I);
    TestEqual(TEXT("Only the identical duplicate merges; near-equal weights and attribute seams survive"),Seams.Compact(),1);
    TestEqual(TEXT("All six differing bindings retain their own vertices"),Seams.Vertices.Num(),7);

    FSurfaceOverlay Disabled=Original;
    { FCompactSetting Off(0);TestEqual(TEXT("A/B switch removes no vertices"),Disabled.Compact(),0); }
    TestEqual(TEXT("Disabled vertex count"),Disabled.Vertices.Num(),Original.Vertices.Num());
    TestTrue(TEXT("Disabled index layout is unchanged"),Disabled.Indices==Original.Indices);

    FSurfaceOverlay Malformed=Original;Malformed.Indices.Last()=99;
    const TArray<int32> InvalidIndices=Malformed.Indices;
    TestEqual(TEXT("Invalid indices refuse compaction"),Malformed.Compact(),0);
    TestEqual(TEXT("Invalid input retains the original vertices"),Malformed.Vertices.Num(),Original.Vertices.Num());
    TestTrue(TEXT("Invalid input cannot leave a partially remapped index stream"),Malformed.Indices==InvalidIndices);
    return true;
}
#endif
