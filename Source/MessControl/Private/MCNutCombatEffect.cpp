#include "MCNutCombatEffect.h"
#include "MCNutCombatVisuals.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCTongue.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#if WITH_EDITOR
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#endif

namespace
{
float Bounded(float Value,float Default,float Min,float Max) {return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Min,Max);}
UMaterialInstanceDynamic* Material(UPrimitiveComponent* Part,const TCHAR* Path)
{
    auto* Parent=LoadObject<UMaterialInterface>(nullptr,Path);
    return Parent?Part->CreateDynamicMaterialInstance(0,Parent):nullptr;
}
float MeshRadius(UStaticMesh* Mesh) {return Mesh?FMath::Max(.1f,Mesh->GetBounds().BoxExtent.GetMax()):1.f;}
void ReleaseBurst(UNiagaraComponent* Component)
{
    if(!IsValid(Component)) return;
    // A world without Niagara pooling can return an ordinary component for a manual-pool request.
    if(Component->PoolingMethod==ENCPoolMethod::ManualRelease) Component->ReleaseToPool();
    else Component->DestroyComponent();
}
}

AMCNutCombatEffect::AMCNutCombatEffect()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(false);SetNetUpdateFrequency(10);
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=1.f/30;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("CueRoot"));SetRootComponent(Root);
    Warning=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundTelegraph"));Warning->SetupAttachment(Root);
    DropWarnings=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DropTelegraphs"));DropWarnings->SetupAttachment(Root);
    DropShadows=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SoftLandingShadows"));DropShadows->SetupAttachment(Root);
    Flames=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MagicFlames"));Flames->SetupAttachment(Root);
    RainNuts=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FallingWalnuts"));RainNuts->SetupAttachment(Root);
    Debris=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ShellFragments"));Debris->SetupAttachment(Root);
    UPrimitiveComponent* Parts[]={Warning.Get(),DropWarnings.Get(),DropShadows.Get(),Flames.Get(),RainNuts.Get(),Debris.Get()};
    for(auto* Part:Parts) MCNutCombatVisuals::Configure(Part);
    Warning->SetTranslucentSortPriority(3);DropWarnings->SetTranslucentSortPriority(4);DropShadows->SetTranslucentSortPriority(2);
}

double AMCNutCombatEffect::Now() const
{const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}

FVector AMCNutCombatEffect::GetNutRainDropPoint(FVector Center,float Radius,int32 Seed,int32 Index)
{
    if(Center.ContainsNaN()) return FVector::ZeroVector;
    FRandomStream Random{int32(HashCombineFast(uint32(Seed),uint32(Index)))};
    const float Angle=Random.FRand()*2*PI,Distance=FMath::Sqrt(Random.FRand())*Bounded(Radius,300,20,1200)*.86f;
    return Center+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
}

AMCNutCombatEffect* AMCNutCombatEffect::Spawn(AActor* Source,AMCTongue* Surface,EMCNutCombatCue Type,
    FVector Origin,FVector Target,float Radius,float Windup,float Active,int32 Seed,float DetailRadius,int32 DropCount,float DropCadence)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=Source->GetWorld()
        || Origin.ContainsNaN() || Target.ContainsNaN() || uint8(Type)>uint8(EMCNutCombatCue::EntranceImpact)) return nullptr;
    const FTransform Pose(Target);
    auto* Effect=Source->GetWorld()->SpawnActorDeferred<AMCNutCombatEffect>(StaticClass(),Pose,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Effect) return nullptr;
    Effect->SourceActor=Source;Effect->Tongue=Surface;
    Effect->Cue.Type=Type;Effect->Cue.Role=Type==EMCNutCombatCue::FireCast || Type==EMCNutCombatCue::FireImpact
        || Type==EMCNutCombatCue::NutRain || Type==EMCNutCombatCue::SummonTell?EMCNutBossRole::Mage:EMCNutBossRole::Tank;
    if(const auto* Boss=Cast<AMCNutBoss>(Source)) Effect->Cue.Role=Boss->BossRole;
    Effect->Cue.Origin=Origin;Effect->Cue.Target=Target;Effect->Cue.Radius=Bounded(Radius,100,15,1200);
    Effect->Cue.DetailRadius=Bounded(DetailRadius,90,15,250);Effect->Cue.WindupSeconds=Bounded(Windup,1,0,12);
    Effect->Cue.ActiveSeconds=Bounded(Active,1,0,16);Effect->Cue.Seed=Seed;
    Effect->Cue.DropCount=FMath::Clamp(DropCount,1,MaxRainDrops);Effect->Cue.DropCadence=Bounded(DropCadence,.5f,.12f,2);
    Effect->Cue.StartedAt=Effect->Now();Effect->FinishSpawning(Pose);return Effect;
}

void AMCNutCombatEffect::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority()) SetLifeSpan(FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds)+1.f);
    if(Tongue) AddTickPrerequisiteActor(Tongue);
    if(GetNetMode()==NM_DedicatedServer) return;
    WarningMID=Material(Warning,TEXT("/Game/Gameplay/VFX/NutCombat/M_NutTelegraph.M_NutTelegraph"));
    SmallWarningMID=Material(DropWarnings,TEXT("/Game/Gameplay/VFX/NutCombat/M_NutTelegraph.M_NutTelegraph"));
    ShadowMID=Material(DropShadows,TEXT("/Game/Gameplay/VFX/NutCombat/M_NutSoftShadow.M_NutSoftShadow"));
    FireMID=Material(Flames,TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"));
    RainNuts->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
}

void AMCNutCombatEffect::Burst(FVector Point,bool bFire,float Scale)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    const TCHAR* Path=bFire?TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutFireImpact.NS_NutFireImpact")
        :TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutDustImpact.NS_NutDustImpact");
    if(auto* System=LoadObject<UNiagaraSystem>(nullptr,Path)) {
        auto* Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Point,FRotator::ZeroRotator,FVector(FMath::Clamp(Scale,.3f,2.f)),
            false,true,ENCPoolMethod::ManualRelease,true);
        if(Component) {Bursts.Add(Component);Component->OnSystemFinished.AddDynamic(this,&AMCNutCombatEffect::FinishedBurst);}
    }
}

void AMCNutCombatEffect::FinishedBurst(UNiagaraComponent* Component)
{
    if(!IsValid(Component) || !Bursts.Contains(Component)) return;
    Component->OnSystemFinished.RemoveDynamic(this,&AMCNutCombatEffect::FinishedBurst);
    Bursts.Remove(Component);ReleaseBurst(Component);
}

void AMCNutCombatEffect::Present(float Age)
{
    using namespace MCNutCombatVisuals;
    const bool Impact=Cue.Type==EMCNutCombatCue::SlamImpact || Cue.Type==EMCNutCombatCue::FireImpact || Cue.Type==EMCNutCombatCue::EntranceImpact;
    const bool Rain=Cue.Type==EMCNutCombatCue::NutRain,Line=Cue.Type==EMCNutCombatCue::ChargeTell;
    const float Total=FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds),T=FMath::Clamp(Age/Total,0.f,1.f);
    const float Fade=Impact?1-FMath::SmoothStep(.25f,1.f,T):1-FMath::SmoothStep(.88f,1.f,T);
    FHitResult Ground;const FVector Center=Line?(Cue.Origin+Cue.Target)*.5f:Cue.Target;
    if(!Tongue->SurfacePoint(Center,Ground)) {Warning->SetVisibility(false);return;}
    const FVector Up=Ground.ImpactNormal,Right=FVector::VectorPlaneProject(Line?Cue.Target-Cue.Origin:FVector::ForwardVector,Up).GetSafeNormal();
    const FVector Side=FVector::CrossProduct(Up,Right).GetSafeNormal();
    FMesh Area;
    const float Size=Impact?Cue.Radius*FMath::Lerp(.18f,1.25f,T):Cue.Radius;
    Area.Quad(Ground.ImpactPoint-GetActorLocation()+Up*5,Right,Side,Line?FMath::Max(Cue.Radius,FVector::Dist(Cue.Origin,Cue.Target)*.5f+Cue.Radius):Size,Line?Cue.Radius:Size,FLinearColor(1,1,1,Fade));
    Area.Upload(Warning);Warning->SetVisibility(Age<=Total && WarningMID);
    if(WarningMID) {
        WarningMID->SetScalarParameterValue(TEXT("Age"),Age);WarningMID->SetScalarParameterValue(TEXT("Progress"),T);
        WarningMID->SetScalarParameterValue(TEXT("IsLine"),Line?1:0);WarningMID->SetScalarParameterValue(TEXT("Impact"),Impact?1:0);
        WarningMID->SetVectorParameterValue(TEXT("Tint"),Cue.Role==EMCNutBossRole::Mage?FLinearColor(1,.27f,.035f):FLinearColor(1,.56f,.10f));
    }
    const bool Fire=Cue.Type==EMCNutCombatCue::FireCast || Cue.Type==EMCNutCombatCue::FireImpact;
    Flames->SetVisibility(Fire && Age<=Total && FireMID);
    if(Fire && FireMID) {
        Flames->SetRelativeLocation(Cue.Type==EMCNutCombatCue::FireCast?Cue.Origin-GetActorLocation():Ground.ImpactPoint-GetActorLocation());
        FireMID->SetScalarParameterValue(TEXT("FireAge"),Age);
        RenderFlames(Flames,FMath::Min(Cue.Radius*.45f,65.f),Age,Fade);
    }
    if(Impact && !bBurstPresented) {
        bBurstPresented=true;
        // A late joiner observes the remaining ring rather than replaying an old burst.
        if(Age<.25f) Burst(Ground.ImpactPoint+Up*10,Fire,FMath::Clamp(Cue.Radius/160.f,.5f,1.5f));
        Debris->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Fire?TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Kernel.SM_Walnut_Kernel")
            :TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
        for(int32 I=0;I<6;++I) Debris->AddInstance(FTransform(FVector::ZeroVector));
    }
    Debris->SetVisibility(Impact && Age<=Total);
    if(Impact) for(int32 I=0;I<Debris->GetInstanceCount();++I) {
        const float A=I*2.39996f+Cue.Seed*.01f;
        const FVector P=Ground.ImpactPoint-GetActorLocation()+(Right*FMath::Cos(A)+Side*FMath::Sin(A))*Cue.Radius*.8f*T+Up*(8+FMath::Sin(T*PI)*65);
        const float Scale=Cue.Radius*.08f/MeshRadius(Debris->GetStaticMesh())*(1-T);
        Debris->UpdateInstanceTransform(I,FTransform(FRotator(Age*160+I*31,Age*90+I*60,I*37),P,FVector(Scale)),false,I==Debris->GetInstanceCount()-1,true);
    }
    DropWarnings->SetVisibility(Rain && Age<Total && SmallWarningMID);DropShadows->SetVisibility(Rain && Age<Total && ShadowMID);
    RainNuts->SetVisibility(Rain && Age<Total);
    if(!Rain) return;
    const int32 Count=FMath::Clamp(Cue.DropCount,1,MaxRainDrops);
    if(BuiltDropCount!=Count) {
        RainNuts->ClearInstances();for(int32 I=0;I<Count;++I) RainNuts->AddInstance(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector::ZeroVector));
        BuiltDropCount=Count;
    }
    FMesh Small,Shadows;
    for(int32 I=0;I<Count;++I) {
        const float ImpactAt=Cue.WindupSeconds+I*Cue.DropCadence,FlightAge=Age-(ImpactAt-DropFlightSeconds);
        const float P=FMath::Clamp(FlightAge/DropFlightSeconds,0.f,1.f);
        FHitResult Landing;const FVector Candidate=GetNutRainDropPoint(Cue.Target,Cue.Radius,Cue.Seed,I);
        const bool Supported=Tongue->InteriorSurfacePoint(Candidate,Cue.DetailRadius,Landing);
        const bool Falling=Supported && FlightAge>=0 && Age<ImpactAt && ImpactAt<=Total;
        const FVector Location=Supported?Landing.ImpactPoint:GetActorLocation();
        const FVector N=Supported?Landing.ImpactNormal:FVector::UpVector;
        const FVector R=FVector::VectorPlaneProject(FVector::ForwardVector,N).GetSafeNormal(),S=FVector::CrossProduct(N,R).GetSafeNormal();
        Small.Quad(Location-GetActorLocation()+N*7,R,S,Cue.DetailRadius,Cue.DetailRadius,FLinearColor(1,1,1,Falling?.40f+.60f*P:0));
        Shadows.Quad(Location-GetActorLocation()+N*4,R,S,42*(1.25f-.25f*P),42*(1.25f-.25f*P),FLinearColor(1,1,1,Falling?.35f+.65f*P:0));
        const float NutScale=Falling?25.f/MeshRadius(RainNuts->GetStaticMesh()):0;
        const FVector Air=Location-GetActorLocation()+FVector(0,0,660*(1-P*P)+25);
        RainNuts->UpdateInstanceTransform(I,FTransform(FRotator(FlightAge*150+I*37,FlightAge*95+I*61,I*43),Air,FVector(NutScale)),false,I==Count-1,true);
        if(Supported && Age>=ImpactAt && (ImpactMask&(1u<<I))==0) {
            ImpactMask|=1u<<I;
            if(Age-ImpactAt<.2f && ImpactAt<=Total) Burst(Location+N*7,false,.55f);
        }
    }
    Small.Upload(DropWarnings);Shadows.Upload(DropShadows);
    if(SmallWarningMID) {SmallWarningMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.35f,.055f));SmallWarningMID->SetScalarParameterValue(TEXT("Age"),Age);}
}

void AMCNutCombatEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority()) {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if(!IsValid(SourceActor) || SourceActor->IsActorBeingDestroyed() || !IsValid(Tongue)
            || (GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))) {Destroy();return;}
    }
    if(GetNetMode()!=NM_DedicatedServer && IsValid(Tongue) && Cue.StartedAt>=0) Present(FMath::Max(0.f,float(Now()-Cue.StartedAt)));
}

void AMCNutCombatEffect::Cancel() {if(HasAuthority()) Destroy();}

void AMCNutCombatEffect::EndPlay(const EEndPlayReason::Type Reason)
{
    for(UNiagaraComponent* Component:Bursts) if(IsValid(Component)) {
        Component->OnSystemFinished.RemoveDynamic(this,&AMCNutCombatEffect::FinishedBurst);
        Component->DeactivateImmediate();ReleaseBurst(Component);
    }
    Bursts.Reset();Super::EndPlay(Reason);
}

void AMCNutCombatEffect::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCNutCombatEffect,Cue);
    DOREPLIFETIME(AMCNutCombatEffect,Tongue);DOREPLIFETIME(AMCNutCombatEffect,SourceActor);
}

bool AMCNutCombatEffect::AuthorNiagaraAssets()
{
#if WITH_EDITOR
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    if(!Template) return false;
    for(int32 Kind=0;Kind<3;++Kind) {
        const FString Name=Kind==0?TEXT("NS_NutDustImpact"):Kind==1?TEXT("NS_NutFireImpact"):TEXT("NS_NutEmberTrail");
        const FString Path=TEXT("/Game/Gameplay/VFX/NutCombat/")+Name;
        if(FPackageName::DoesPackageExist(Path)) continue;
        auto* Parent=LoadObject<UMaterial>(nullptr,Kind==0?TEXT("/Game/Gameplay/VFX/NutCombat/M_NutDust.M_NutDust")
            :TEXT("/Game/Gameplay/VFX/NutCombat/M_NutEmber.M_NutEmber"));
        if(!Parent) return false;
        auto* Package=CreatePackage(*Path);auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,*Name);
        System->SetFlags(RF_Public|RF_Standalone);System->ClearFlags(RF_Transient);
        auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter();if(!Emitter) return false;
        const auto* StateProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("EmitterState"));
        if(!StateProperty) return false;
        auto* EmitterState=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);
        EmitterState->LoopBehavior=Kind==2?ENiagaraLoopBehavior::Infinite:ENiagaraLoopBehavior::Once;
        EmitterState->LoopDurationMode=ENiagaraLoopDurationMode::Fixed;
        EmitterState->LoopDuration=FNiagaraDistributionRangeFloat(1.f);
        auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
        auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
        auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
        auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()));
        if(!Init || !Velocity || !Shape || !Gravity) return false;
        for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {
            auto* Spawn=Emitter->GetSpawnInfoByIndex(I);Spawn->bEnabled=I==0;Spawn->SpawnTime=0;Spawn->bSpawnProbabilityEnabled=false;
            Spawn->Type=Kind==2?ENiagaraStatelessSpawnInfoType::Rate:ENiagaraStatelessSpawnInfoType::Burst;
            Spawn->Amount=FNiagaraDistributionRangeInt(Kind==0?24:32);Spawn->Rate=FNiagaraDistributionRangeFloat(36);
            Spawn->bLoopCountLimitEnabled=Kind!=2;Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1);
        }
        Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(Kind==0?.45f:.18f,Kind==0?.85f:Kind==1?.55f:.38f);
        Init->SpriteSizeDistribution.InitConstant(FVector2f(Kind==0?30:Kind==1?9:7));
        Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor::White);
        Shape->SetIsModuleEnabled(true);Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere;Shape->SphereRadius=FNiagaraDistributionRangeFloat(0,Kind==2?12:5);
        Velocity->SetIsModuleEnabled(true);Velocity->VelocityType=ENSM_VelocityType::InCone;
        Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
        Velocity->CoordinateSpace=ENiagaraCoordinateSpace::World;Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(Kind==0?50:30,Kind==0?160:Kind==1?280:70);Velocity->ConeAngle=75;
        Gravity->SetIsModuleEnabled(true);Gravity->GravityDistribution.InitConstant(FVector3f(0,0,Kind==0?-80:Kind==1?-450:40));
        if(auto* Scale=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass()))) {
            Scale->SetIsModuleEnabled(true);Scale->ScaleDistribution.InitConstant(FVector2f(1.f));
        }
        if(auto* Scale=Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass())) Scale->SetIsModuleEnabled(false);
        if(auto* Curl=Emitter->GetModule(UNiagaraStatelessModule_CurlNoiseForce::StaticClass())) Curl->SetIsModuleEnabled(false);
        if(auto* Drag=Cast<UNiagaraStatelessModule_Drag>(Emitter->GetModule(UNiagaraStatelessModule_Drag::StaticClass()))) {Drag->SetIsModuleEnabled(true);Drag->DragDistribution=FNiagaraDistributionRangeFloat(Kind==0?2.f:.8f);}
        const auto Old=Emitter->GetRenderers();for(auto* Renderer:Old) Emitter->RemoveRenderer(Renderer,FGuid());
        auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(Emitter,NAME_None,RF_Transactional);Renderer->Material=Parent;Emitter->AddRenderer(Renderer,FGuid());
        System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-350),FVector(350)));
        Emitter->PostEditChange();System->PostEditChange();System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
        FAssetRegistryModule::AssetCreated(System);System->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;Save.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return false;
    }
    return true;
#else
    return false;
#endif
}
