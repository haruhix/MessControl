#include "MCNiagaraProceduralSurfaceComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "HAL/PlatformStackWalk.h"
#include "UObject/ObjectKey.h"
#endif

UMCNiagaraProceduralSurfaceComponent::UMCNiagaraProceduralSurfaceComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);SetCastShadow(false);SetVisibility(false);
}

UNiagaraComponent* UMCNiagaraProceduralSurfaceComponent::DirectNiagaraParent()const
{return Cast<UNiagaraComponent>(GetAttachParent());}

bool UMCNiagaraProceduralSurfaceComponent::ReadControl(UNiagaraComponent* Parent,bool& Preparing,int32& Capacity,FString& Error)const
{
    if(!Parent||Parent->GetWorld()!=GetWorld())return false;
    const auto& Store=Parent->GetOverrideParameters();
    const FNiagaraVariable B(FNiagaraTypeDefinition::GetBoolDef(),TEXT("User.SurfacePreparing"));
    const FNiagaraVariable I(FNiagaraTypeDefinition::GetIntDef(),TEXT("User.SurfaceTriangleCapacity"));
    if(Store.IndexOf(B)==INDEX_NONE||Store.IndexOf(I)==INDEX_NONE)
    {Error=TEXT("Surface draw requires exact typed preparation controls on its direct Niagara parent.");return false;}
    const auto* BV=Store.GetParameterData(B);const auto* IV=Store.GetParameterData(I);
    if(!BV||!IV)return false;
    Preparing=reinterpret_cast<const FNiagaraBool*>(BV)->GetValue();
    Capacity=*reinterpret_cast<const int32*>(IV);return true;
}

bool UMCNiagaraProceduralSurfaceComponent::PrepareDraw(FString& Error)
{
    bool Preparing=false;int32 Capacity=0;
    if(bDrawPrepared||!ReadControl(DirectNiagaraParent(),Preparing,Capacity,Error)||!Preparing
        ||SourceID.IsNone()||Capacity<=0||Capacity>MaximumTriangles||MaximumTriangles>131072
        ||NativeOriginalMaterials.IsEmpty()||NativeOriginalMaterials.Num()>8
        ||NativeOriginalMaterials.Num()!=AuthoredMaterials.Num()
        ||ScalarParameters.Num()+VectorParameters.Num()+TextureParameters.Num()>64)
    {if(Error.IsEmpty())Error=TEXT("Only explicit bounded prebarrier surface priming may allocate a draw.");return false;}
    TriangleCapacity=Capacity;VerticesPerSection=FMath::Min(Capacity,TrianglesPerSection)*3;
    Positions.Init(FVector::ZeroVector,VerticesPerSection);Normals.Init(FVector::UpVector,VerticesPerSection);
    UVs.Init(FVector2D::ZeroVector,VerticesPerSection);Colors.Init(FColor::White,VerticesPerSection);
    Tangents.Init(FProcMeshTangent(FVector::ForwardVector,false),VerticesPerSection);
    TArray<int32> Indices;Indices.Reserve(VerticesPerSection);for(int32 V=0;V<VerticesPerSection;++V)Indices.Add(V);
    for(int32 V=0;V<NativeOriginalMaterials.Num();++V) {
        if(!NativeOriginalMaterials[V]||!AuthoredMaterials[V]){Error=TEXT("Surface variants must be explicit paired native/authored material resources.");return false;}
        auto* MID=UMaterialInstanceDynamic::Create(AuthoredMaterials[V],this);
        if(!MID){Error=TEXT("Surface material preparation failed.");return false;}PreparedMaterials.Add(MID);
    }
    const int32 Sections=FMath::DivideAndRoundUp(Capacity,TrianglesPerSection);
    for(int32 S=0;S<Sections;++S) {
        CreateMeshSection(S,Positions,Indices,Normals,UVs,Colors,Tangents,false);
        SetMaterial(S,PreparedMaterials[0]);SetMeshSectionVisible(S,false);
    }
    SetAbsolute(true,true,true);SetWorldTransform(FTransform::Identity);SetVisibility(false);
    for(UMaterialInstanceDynamic* Variant:PreparedMaterials) {
        for(int32 S=0;S<Sections;++S)SetMaterial(S,Variant);
        PrecachePSOs();
    }
    for(int32 S=0;S<Sections;++S)SetMaterial(S,PreparedMaterials[0]);
    bDrawPrepared=true;return true;
}

void UMCNiagaraProceduralSurfaceComponent::OnRegister()
{
    Super::OnRegister();
    // Niagara attaches its actual duplicated template before RegisterComponent.
    bool Preparing=false;int32 Capacity=0;
    if(ReadControl(DirectNiagaraParent(),Preparing,Capacity,SurfaceError)&&Preparing)
        PrepareDraw(SurfaceError);
    if(auto* Parent=DirectNiagaraParent())AddTickPrerequisiteComponent(Parent);
    SetVisibility(false);
}

bool UMCNiagaraProceduralSurfaceComponent::BindPreparedSubject(UProceduralMeshComponent* Subject,FString& Error)
{
    if(!bDrawPrepared||!IsRegistered()||!IsValid(Subject)||Subject->GetClass()!=UProceduralMeshComponent::StaticClass()||Subject==this||Subject->GetWorld()!=GetWorld()
        ||!IsValid(Subject->GetOwner())||Subject->GetOwner()->IsActorBeingDestroyed()
        ||Subject->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)
    {Error=TEXT("Surface lease requires its prepared draw and exact live noncolliding native subject.");return false;}
    if(auto* Previous=NativeSubject.Get();Previous&&Previous!=Subject)RemoveTickPrerequisiteComponent(Previous);
    NativeSubject=Subject;AddTickPrerequisiteComponent(Subject);return UpdateNativeDraw(Error);
}

bool UMCNiagaraProceduralSurfaceComponent::UpdateNativeDraw(FString& Error)
{
    Error.Reset();auto* Subject=NativeSubject.Get();auto* Parent=DirectNiagaraParent();
    if(!CanDrawSubject(Subject,Error)){SetVisibility(false);return false;}
    const FNiagaraVariable Key(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject"));
    const auto& Store=Parent->GetOverrideParameters();
    if(Store.IndexOf(Key)==INDEX_NONE||Store.GetUObject(Key).Get()!=Subject)
    {Error=TEXT("Surface subject differs from the exact typed accepted Niagara object input.");SetVisibility(false);return false;}
    const auto* Section=Subject->GetProcMeshSection(0);auto* Native=Cast<UMaterialInstanceDynamic>(Subject->GetMaterial(0));
    const int32 Variant=Native?NativeOriginalMaterials.IndexOfByKey(Native->Parent.Get()):INDEX_NONE;
    if(!Section||Section->ProcIndexBuffer.Num()%3||Section->ProcIndexBuffer.Num()/3>TriangleCapacity
        ||Variant==INDEX_NONE||!PreparedMaterials.IsValidIndex(Variant))
    {Error=TEXT("Native surface topology/material exceeds its exact prepared lease.");SetVisibility(false);return false;}
    auto* DrawMaterial=PreparedMaterials[Variant].Get();
    for(FName N:ScalarParameters)DrawMaterial->SetScalarParameterValue(N,Native->K2_GetScalarParameterValue(N));
    for(FName N:VectorParameters)DrawMaterial->SetVectorParameterValue(N,Native->K2_GetVectorParameterValue(N));
    for(FName N:TextureParameters)DrawMaterial->SetTextureParameterValue(N,Native->K2_GetTextureParameterValue(N));
    const int32 Triangles=Section->ProcIndexBuffer.Num()/3;
    for(uint32 V:Section->ProcIndexBuffer)if(!Section->ProcVertexBuffer.IsValidIndex(int32(V)))
    {Error=TEXT("Native surface contains an invalid original triangle index.");SetVisibility(false);return false;}
    // Deindexing preserves oriented triangles, interpolated original normals,
    // UVs, handed tangents and the original already-quantized vertex colors.
    const int32 Sections=GetNumSections();
    for(int32 S=0;S<Sections;++S) {
        const int32 First=S*TrianglesPerSection,Active=FMath::Clamp(Triangles-First,0,VerticesPerSection/3);
        if(!Active){SetMeshSectionVisible(S,false);continue;}
        const FVector Padding=Section->ProcVertexBuffer[Section->ProcIndexBuffer[First*3]].Position;
        for(int32 V=0;V<VerticesPerSection;++V) {
            if(V<Active*3) {
                const auto& NativeVertex=Section->ProcVertexBuffer[Section->ProcIndexBuffer[First*3+V]];
                Positions[V]=NativeVertex.Position;Normals[V]=NativeVertex.Normal;UVs[V]=NativeVertex.UV0;
                Colors[V]=NativeVertex.Color;Tangents[V]=NativeVertex.Tangent;
            } else {Positions[V]=Padding;Normals[V]=FVector::UpVector;UVs[V]=FVector2D::ZeroVector;
                Colors[V]=FColor::Transparent;Tangents[V]=FProcMeshTangent(FVector::ForwardVector,false);}
        }
        SetMaterial(S,DrawMaterial);UpdateMeshSection(S,Positions,Normals,UVs,Colors,Tangents);SetMeshSectionVisible(S,true);
    }
    if(auto* Host=GetOwner())Host->SetActorTransform(Subject->GetOwner()->GetActorTransform());
    SetWorldTransform(Subject->GetComponentTransform());UpdateBounds();SetVisibility(Triangles>0&&IsRendererEligible());return true;
}

bool UMCNiagaraProceduralSurfaceComponent::CanDrawSubject(UProceduralMeshComponent* Subject,FString& Error)const
{
    Error.Reset();auto* Parent=DirectNiagaraParent();
    if(!bDrawPrepared||!IsValid(Subject)||NativeSubject.Get()!=Subject||!Parent||Subject->GetWorld()!=GetWorld()
        ||!IsValid(Subject->GetOwner())||Subject->GetOwner()->IsActorBeingDestroyed()||Subject->GetNumSections()!=1)
    {Error=TEXT("The prepared surface subject was retired or changed its section contract.");return false;}
    const auto* Section=Subject->GetProcMeshSection(0);auto* Native=Cast<UMaterialInstanceDynamic>(Subject->GetMaterial(0));
    if(!Section||Section->ProcIndexBuffer.Num()%3||Section->ProcIndexBuffer.Num()/3>TriangleCapacity
        ||!Native||!NativeOriginalMaterials.Contains(Native->Parent.Get()))
    {Error=TEXT("Current native surface is outside the prepared topology/material contract.");return false;}
    for(uint32 V:Section->ProcIndexBuffer)if(!Section->ProcVertexBuffer.IsValidIndex(int32(V)))
    {Error=TEXT("Current native surface contains an invalid triangle index.");return false;}
    return true;
}

void UMCNiagaraProceduralSurfaceComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Delta,TickType,TickFunction);
    if(!IsRendererEligible()||!NativeSubject.IsValid()){SetVisibility(false);return;}
    UpdateNativeDraw(SurfaceError);
}

void UMCNiagaraProceduralSurfaceComponent::ReleasePreparedSubject()
{
    if(auto* Previous=NativeSubject.Get())RemoveTickPrerequisiteComponent(Previous);
    NativeSubject.Reset();bRendererEligible=false;LastRendererFrame=0;SetVisibility(false);for(int32 S=0;S<GetNumSections();++S)SetMeshSectionVisible(S,false);
}
void UMCNiagaraProceduralSurfaceComponent::GetPreparedMaterials(TArray<UMaterialInterface*>& Out)const
{for(UMaterialInstanceDynamic* MID:PreparedMaterials)if(MID)Out.AddUnique(MID);}
void UMCNiagaraProceduralSurfaceComponent::OnUnregister()
{
#if WITH_EDITOR || !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    auto* Parent=DirectNiagaraParent();
    if(IsInGameThread()&&NativeSubject.IsValid()&&Parent&&Parent->IsActive()&&!Parent->IsPaused()
        &&GetOwner()&&!GetOwner()->IsActorBeingDestroyed()) {
        // Normal Release pauses the controller and clears NativeSubject first.
        // Capture only an unexpected live draw unregister, once per instance;
        // the bounded set avoids repeated stacks or permanent diagnostic growth.
        static TSet<FObjectKey> Reported;
        const FObjectKey Instance(this);
        if(Reported.Num()<32&&!Reported.Contains(Instance)) {
            Reported.Add(Instance);
            ANSICHAR Trace[16384]{};FPlatformStackWalk::StackWalkAndDump(Trace,sizeof(Trace),1);
            UE_LOG(LogTemp,Warning,TEXT("MC_SURFACE_UNEXPECTED_UNREGISTER draw=%s source=%s subject=%s parent=%s active=%d paused=%d tick=%d rendering=%d complete=%d lastRender=%.3f real=%.3f game=%.3f capacity=%d\n%s"),
                *GetPathName(),*SourceID.ToString(),*GetPathNameSafe(NativeSubject.Get()),*GetPathNameSafe(Parent),
                Parent->IsActive(),Parent->IsPaused(),Parent->IsComponentTickEnabled(),Parent->GetRenderingEnabled(),Parent->IsComplete(),
                Parent->GetLastRenderTime(),GetWorld()?GetWorld()->GetRealTimeSeconds():-1.,GetWorld()?GetWorld()->GetTimeSeconds():-1.,TriangleCapacity,
                ANSI_TO_TCHAR(Trace));
        }
    }
#endif
    ReleasePreparedSubject();Super::OnUnregister();
}
FBoxSphereBounds UMCNiagaraProceduralSurfaceComponent::CalcBounds(const FTransform& LocalToWorld)const
{
    // Hidden padded capacity must not change ObjectPosition/ObjectBounds shader inputs.
    // The subject is restricted to the original stock component, preventing recursion.
    if(auto* Native=NativeSubject.Get();IsValid(Native)&&Native->GetClass()==UProceduralMeshComponent::StaticClass())
        return Native->CalcBounds(LocalToWorld);
    return Super::CalcBounds(LocalToWorld);
}

void UMCNiagaraProceduralSurfaceComponent::SetRendererEligibility(bool Enabled)
{
    bRendererEligible=Enabled;LastRendererFrame=GFrameCounter;
    if(!Enabled)SetVisibility(false);
}
bool UMCNiagaraProceduralSurfaceComponent::IsRendererEligible()const
{
    const auto* Parent=DirectNiagaraParent();
    // Disabled/removed renderers cannot leave their last visible decision alive.
    // Paused leases stay prepared, but are not accepted until a fresh CPU tick.
    return bRendererEligible&&LastRendererFrame&&GFrameCounter-LastRendererFrame<=1
        &&Parent&&Parent->IsActive()&&!Parent->IsPaused()&&!Parent->IsComplete();
}
