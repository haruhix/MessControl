#include "MCNutCombatEffect.h"
#include "MCVFXPresentationComponent.h"
#include "MCNutCombatVisuals.h"
#include "MCVFXNativeArtControls.h"
#include "MCNutShieldCrackLayout.h"
#include "MCNutShellShardVisuals.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCNutKamikaze.h"
#include "MCNutLandingShadow.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
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
USceneComponent* NutSourceVisual(AActor* Source)
{
    if(auto* Boss=Cast<AMCNutBoss>(Source))return Boss->BossRole==EMCNutBossRole::Mage
        ?static_cast<USceneComponent*>(Boss->MageModel.Get()):static_cast<USceneComponent*>(Boss->TankModel.Get());
    if(auto* Summon=Cast<AMCNutKamikaze>(Source))return Summon->SummonModel;
    if(auto* Landing=Cast<AMCNutLandingShadow>(Source);Landing && IsValid(Landing->TrackedFood.Get()))return Landing->TrackedFood->Visual;
    if(auto* Nut=Cast<AMCNutEnemy>(Source))return Nut->Visual;
    return nullptr;
}

float Bounded(float Value,float Default,float Min,float Max) {return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Min,Max);}
UMaterialInstanceDynamic* Material(UPrimitiveComponent* Part,UMaterialInterface* Parent)
{
    return Parent?Part->CreateDynamicMaterialInstance(0,Parent):nullptr;
}
float MeshRadius(UStaticMesh* Mesh) {return Mesh?FMath::Max(.1f,Mesh->GetBounds().BoxExtent.GetMax()):1.f;}
FName ShieldBone(const AMCNutBoss* Boss)
{
    if(Boss && Boss->TankModel && Boss->TankModel->GetSkeletalMeshAsset())
        for(const FName Name:{FName(TEXT("hand.l")),FName(TEXT("hand_l"))})
            if(Boss->TankModel->GetBoneIndex(Name)!=INDEX_NONE) return Name;
    return NAME_None;
}
void CrackSegment(MCNutCombatVisuals::FMesh& Mesh,FVector A,FVector B,FVector Normal,float Width,float Alpha)
{
    const FVector Along=(B-A).GetSafeNormal(),Across=FVector::CrossProduct(Normal,Along).GetSafeNormal();
    if(Along.IsNearlyZero() || Across.IsNearlyZero()) return;
    const int32 First=Mesh.Points.Num();
    for(const FVector Point:{A-Across*Width,A+Across*Width,B+Across*Width,B-Across*Width}) {
        Mesh.Points.Add(Point);Mesh.Normals.Add(Normal);Mesh.UV.Add(FVector2D(.5f,.5f));
        Mesh.Colors.Add(FLinearColor(1,1,1,Alpha));Mesh.Tangents.Add(FProcMeshTangent(Along,false));
    }
    Mesh.Indices.Append({First,First+1,First+2,First,First+2,First+3});
}
void ShieldFissure(MCNutCombatVisuals::FMesh& Mesh,const MCNutShieldCrackLayout::FPath& Path,
    float Damage,bool Authored,float FallbackRadius,const FMCVFXNativeCracksShape* Art=nullptr)
{
    using namespace MCNutShieldCrackLayout;
    const float Growth=Path.Rim?1.f:FMath::Clamp((Damage-Path.RevealAt)/FMath::Max(.01f,Path.FullAt-Path.RevealAt),0.f,1.f);
    if(Growth<=0) return;
    const float Last=Growth*(Path.Count-1);
    const int32 Rows=FMath::Min(Path.Count,FMath::CeilToInt(Last)+1);
    if(Rows<2) return;
    const int32 First=Mesh.Points.Num();
    const FLinearColor Core=Art?Art->CoreColor:FLinearColor(.009f,.004f,.002f);
    const FLinearColor Lip=Art?Art->LipColor:FLinearColor(.31f,.16f,.066f),Rim=Art?Art->RimColor:FLinearColor(.48f,.20f,.035f);
    auto PointAt=[&](int32 R,int32 C){return Art?Art->Samples[R].Points[C]:Samples[R].Points[C];};
    auto NormalAt=[&](int32 R,int32 C){return Art?Art->Samples[R].Normals[C]:Samples[R].Normals[C];};
    const FVector Side=FVector::CrossProduct(Normal,FVector::UpVector).GetSafeNormal();
    const FVector Up=FVector::CrossProduct(Side,Normal).GetSafeNormal();
    for(int32 Row=0;Row<Rows;++Row) {
        const float At=FMath::Min(float(Row),Last);
        const int32 A=FMath::Min(FMath::FloorToInt(At),Path.Count-1),B=FMath::Min(A+1,Path.Count-1);
        const float Blend=At-A;
        const int32 Before=Path.First+A,After=Path.First+B;
        const FVector Middle=FMath::Lerp(PointAt(Before,2),PointAt(After,2),Blend);
        // A growing split ends in a fine point. Each five-column joined strip
        // has a dark interior and a much narrower rough shell lip.
        const float Tip=Path.Rim?1.f:FMath::Clamp((Last-At)/2.5f,0.f,1.f);
        const float Width=(Art?Art->WidthScale:1.f)*(Path.Rim?1.f:(.40f+.60f*Damage)*FMath::SmoothStep(0.f,1.f,Tip));
        for(int32 Column=0;Column<5;++Column) {
            FVector Point=FMath::Lerp(PointAt(Before,Column),PointAt(After,Column),Blend);
            FVector VertexNormal=FMath::Lerp(NormalAt(Before,Column),NormalAt(After,Column),Blend).GetSafeNormal();
            Point=Middle+(Point-Middle)*Width;
            // Raised raw lips catch scene light; the interior remains a dark gap.
            if(!Path.Rim && (Column==0 || Column==4)) Point+=VertexNormal*(Art?Art->LipLift:.18f)*Width;
            if(!Authored) {
                const FVector Relative=Point-Center;
                Point=FVector(FallbackRadius*1.08f,FVector::DotProduct(Relative,Side),FVector::DotProduct(Relative,Up));
                VertexNormal=FVector::ForwardVector;
            }
            Mesh.Points.Add(Point);Mesh.Normals.Add(VertexNormal);Mesh.UV.Add(FVector2D(Column*.25f,At/FMath::Max(1,Path.Count-1)));
            Mesh.Colors.Add(Path.Rim?Rim:Column==0 || Column==4?Lip:Core);
            Mesh.Tangents.Add(FProcMeshTangent(FVector::CrossProduct(VertexNormal,FVector::UpVector).GetSafeNormal(),false));
        }
        if(Row>0) for(int32 Column=0;Column<4;++Column) {
            const int32 V=First+(Row-1)*5+Column;
            Mesh.Indices.Append({V,V+1,V+6,V,V+6,V+5});
        }
    }
}
void ReleaseBurst(UNiagaraComponent* Component)
{
    if(!IsValid(Component)) return;
    // A world without Niagara pooling can return an ordinary component for a manual-pool request.
    if(Component->PoolingMethod==ENCPoolMethod::ManualRelease) Component->ReleaseToPool();
    else Component->DestroyComponent();
}
void ImpactTriangle(MCNutCombatVisuals::FMesh& Mesh,FVector Center,FVector A,FVector B,FVector Normal,float Alpha)
{
    const int32 First=Mesh.Points.Num();
    for(const FVector Point:{Center,A,B}) {
        Mesh.Points.Add(Point);Mesh.Normals.Add(Normal);
        // Sample the warm stream core of the existing material. Geometry supplies
        // the sharp silhouette; no new material or texture needs authoring.
        Mesh.UV.Add(FVector2D(.75f,.5f));Mesh.Colors.Add(FLinearColor(1,1,1,Alpha));
        Mesh.Tangents.Add(FProcMeshTangent((A-Center).GetSafeNormal(),false));
    }
    Mesh.Indices.Append({First,First+2,First+1});
}
}

AMCNutCombatEffect::AMCNutCombatEffect()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(false);SetNetUpdateFrequency(10);
    // Ribbons and debris follow continuous server time on every rendered frame.
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=0;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("CueRoot"));SetRootComponent(Root);
    Warning=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundTelegraph"));Warning->SetupAttachment(Root);
    DropWarnings=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DropTelegraphs"));DropWarnings->SetupAttachment(Root);
    DropShadows=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SoftLandingShadows"));DropShadows->SetupAttachment(Root);
    Flames=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MagicFlames"));Flames->SetupAttachment(Root);
    Magic=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ArcaneRibbons"));Magic->SetupAttachment(Root);
    Storm=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StormCloudAndStreaks"));Storm->SetupAttachment(Root);
    SustainedParticles=CreateDefaultSubobject<UNiagaraComponent>(TEXT("SustainedCueParticles"));SustainedParticles->SetupAttachment(Root);
    SustainedParticles->SetAutoActivate(false);
    Presentation=CreateDefaultSubobject<UMCVFXPresentationComponent>(TEXT("NutCuePresentation"));
    RainNuts=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FallingWalnuts"));RainNuts->SetupAttachment(Root);
    Debris=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ShellFragments"));Debris->SetupAttachment(Root);
    UPrimitiveComponent* Parts[]={Warning.Get(),DropWarnings.Get(),DropShadows.Get(),Flames.Get(),Magic.Get(),Storm.Get(),SustainedParticles.Get(),RainNuts.Get(),Debris.Get()};
    for(auto* Part:Parts) MCNutCombatVisuals::Configure(Part);
    Warning->SetTranslucentSortPriority(3);DropWarnings->SetTranslucentSortPriority(4);DropShadows->SetTranslucentSortPriority(2);
    Magic->SetTranslucentSortPriority(5);
    TelegraphMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutTelegraph.M_NutTelegraph"));
    ShadowMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutSoftShadow.M_NutSoftShadow"));
    FlameMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"));
    MagicMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutArcane.M_NutArcane"));
    ShieldFractureMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutShieldFracture.M_NutShieldFracture"));
    StormMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutStorm.M_NutStorm"));
    DustImpactSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutDustImpact.NS_NutDustImpact"));
    FireImpactSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutFireImpact.NS_NutFireImpact"));
    ArcaneBurstSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutArcaneBurst.NS_NutArcaneBurst"));
    CastChargeSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutCastCharge.NS_NutCastCharge"));
    MotionDustSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutMotionDust.NS_NutMotionDust"));
    StormMotesSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutStormMotes.NS_NutStormMotes"));
    ShieldHitSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutShieldHit.NS_NutShieldHit"));
}

double AMCNutCombatEffect::Now() const
{const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}

FVector AMCNutCombatEffect::GetShieldContactOrigin(const AMCNutBoss* Boss)
{
    if(!IsValid(Boss)) return FVector::ZeroVector;
    if(const FName Bone=ShieldBone(Boss);!Bone.IsNone())
        return Boss->TankModel->GetSocketTransform(Bone).TransformPosition(MCNutShieldCrackLayout::Center);
    if(Boss->Shield && Boss->Shield->GetStaticMesh())
        return Boss->Shield->Bounds.Origin+Boss->GetActorForwardVector()*Boss->Settings.BodyRadius*.25f;
    return Boss->GetActorLocation()+Boss->GetActorForwardVector()*Boss->Settings.BodyRadius;
}

FTransform AMCNutCombatEffect::GetShieldContactFrame(const AMCNutBoss* Boss)
{
    if(!IsValid(Boss))return FTransform::Identity;
    const FVector Contact=GetShieldContactOrigin(Boss);
    FVector Normal=Boss->GetActorForwardVector();
    if(const FName Bone=ShieldBone(Boss);!Bone.IsNone()) {
        Normal=Boss->TankModel->GetSocketTransform(Bone).TransformVectorNoScale(MCNutShieldCrackLayout::Normal)
            .GetSafeNormal(SMALL_NUMBER,Normal);
        if(FVector::DotProduct(Normal,Contact-Boss->GetActorLocation())<0)Normal=-Normal;
    }
    return FTransform(Normal.Rotation(),Contact);
}

void AMCNutCombatEffect::UpdateShieldDamageVisual(AMCNutBoss* Boss)
{
    if(!IsValid(Boss) || Boss->GetNetMode()==NM_DedicatedServer) return;
    const auto* Style=Boss->ShieldPresentation?Boss->ShieldPresentation->GetPreparedNativeSource(TEXT("Boss.NutKnight.Shield")):nullptr;
    const auto* Art=Style?Style->FindPart(NAME_None,TEXT("ShieldCracks")):nullptr;
    const uint64 StyleHash=Style?Style->Reference.SettingsHash:0;
    const bool StyleChanged=Boss->LastShieldNativePresentationHash!=StyleHash;
    Boss->LastShieldNativePresentationHash=StyleHash;
    static const FName Name(TEXT("ShieldDamageCracks"));
    auto* Cracks=FindObjectFast<UProceduralMeshComponent>(Boss,Name);
    const FName Bone=ShieldBone(Boss);
    const bool Authored=!Bone.IsNone();
    const bool Visible=MCNativeArtControls::Enabled(Art) && Boss->BossRole==EMCNutBossRole::Tank && !Boss->bDefeated && Boss->ShieldHealth>0
        && Boss->State!=EMCNutBossState::Falling
        && (Authored?Boss->TankModel->IsVisible():Boss->Shield && Boss->Shield->IsVisible());
    if(Boss->ShieldPresentation) {
        const float Damage=FMath::Clamp(1-Boss->ShieldHealth/FMath::Max(1.f,Boss->BossSettings.ShieldMaxHealth),0.f,1.f);
        const double Clock=Boss->GetWorld()->GetGameState()?Boss->GetWorld()->GetGameState()->GetServerWorldTimeSeconds():Boss->GetWorld()->GetTimeSeconds();
        if(Boss->ShieldVFXShown<0 || StyleChanged)Boss->ShieldVFXShown=Boss->ShieldVFXFrom=Boss->ShieldVFXTarget=Damage;
        if(!FMath::IsNearlyEqual(Boss->ShieldVFXTarget,Damage)) {
            Boss->ShieldVFXFrom=Boss->ShieldVFXShown;Boss->ShieldVFXTarget=Damage;Boss->ShieldVFXChangedAt=Clock;
        }
        Boss->ShieldVFXShown=FMath::Lerp(Boss->ShieldVFXFrom,Damage,FMath::SmoothStep(0.f,Art?Art->Cracks.BlendSeconds:.14f,float(Clock-Boss->ShieldVFXChangedAt)));
        FMCPerkVFXGameplayValues Values;Values.Fill=Boss->ShieldVFXShown;Values.Radius=Boss->Settings.BodyRadius;
        FMCVFXNativeInput ShieldFrame;ShieldFrame.Name=TEXT("User.Native.bAuthoredShield");
        ShieldFrame.Type=EMCVFXNativeInputType::Bool;ShieldFrame.BoolValue=Authored;Values.NativeInputs.Entries.Add(ShieldFrame);
        Values.VisualSubject=Authored?static_cast<USceneComponent*>(Boss->TankModel.Get()):Boss->Body.Get();
        Values.FrameComponent=Authored?static_cast<USceneComponent*>(Boss->TankModel.Get()):static_cast<USceneComponent*>(Boss->Body.Get());
        Values.FrameSocket=Authored?Bone:NAME_None;
        const FTransform Frame=Authored?Boss->TankModel->GetSocketTransform(Bone):Boss->GetActorTransform();
        FString Error;
        Values.LocalStateAgeSeconds=-1.f; // Static artwork still needs an advancing scheduling clock.
        const bool Updated=Boss->ShieldPresentation->UpdateLocalSourceState(TEXT("Boss.NutKnight.Shield"),TEXT("Durability"),Visible,Frame,Values,
            Authored?static_cast<UActorComponent*>(Boss->TankModel.Get()):nullptr,Error);
        EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
        Boss->ShieldPresentation->GetPreparedEventMode(TEXT("Boss.NutKnight.Shield"),TEXT("Durability"),Mode);
        if(Visible && Updated && Mode!=EMCPerkVFXSlotMode::UseDefault) {
            if(IsValid(Cracks))Cracks->SetVisibility(false);return;
        }
    }
    if(!Visible) {if(IsValid(Cracks)) Cracks->SetVisibility(false);return;}
    if(!Cracks) {
        Cracks=NewObject<UProceduralMeshComponent>(Boss,Name,RF_Transient);
        MCNutCombatVisuals::Configure(Cracks);Cracks->SetTranslucentSortPriority(6);
        Cracks->SetupAttachment(Authored?static_cast<USceneComponent*>(Boss->TankModel.Get()):Boss->Body.Get(),Bone);
        Cracks->RegisterComponent();
        const auto* Defaults=GetDefault<AMCNutCombatEffect>();
        Cracks->SetCustomPrimitiveDataFloat(0,-1.f);
        if(auto* Material=Art?Art->Material.Get():Defaults->ShieldFractureMaterial.LoadSynchronous()) Cracks->SetMaterial(0,Material);
    }
    if(StyleChanged) {
        const auto* Defaults=GetDefault<AMCNutCombatEffect>();
        Cracks->SetMaterial(0,Art?Art->Material.Get():Defaults->ShieldFractureMaterial.LoadSynchronous());
    }
    if(!Cracks->GetMaterial(0)) {Cracks->SetVisibility(false);return;}
    const float Damage=FMath::Clamp(1-Boss->ShieldHealth/FMath::Max(1.f,Boss->BossSettings.ShieldMaxHealth),0.f,1.f);
    // Bone attachment updates transforms. CPD keeps peer-local growth state;
    // a brief transition lets the split propagate instead of popping on a hit.
    const FName Socket=Bone;
    USceneComponent* Parent=Authored?static_cast<USceneComponent*>(Boss->TankModel.Get()):Boss->Body.Get();
    const bool AttachmentChanged=Cracks->GetAttachParent()!=Parent || Cracks->GetAttachSocketName()!=Socket;
    if(AttachmentChanged) Cracks->AttachToComponent(Parent,FAttachmentTransformRules::SnapToTargetIncludingScale,Socket);
    const auto& Cached=Cracks->GetCustomPrimitiveData().Data;
    const float Time=float(Boss->GetWorld()->GetGameState()?Boss->GetWorld()->GetGameState()->GetServerWorldTimeSeconds():Boss->GetWorld()->GetTimeSeconds());
    const bool Reset=StyleChanged || !Cracks->GetProcMeshSection(0) || AttachmentChanged || Cached.Num()<4;
    const float LastShown=Reset?Damage:Cached[0];
    const bool NewHit=!Reset && FMath::Abs(Cached[2]-Damage)>.0001f;
    const float From=Reset?Damage:NewHit?LastShown:Cached[1];
    const float Started=Reset || NewHit?Time:Cached[3];
    const float Shown=FMath::Lerp(From,Damage,FMath::SmoothStep(0.f,Art?Art->Cracks.BlendSeconds:.14f,Time-Started));
    if(Reset || NewHit) {
        Cracks->SetCustomPrimitiveDataFloat(1,From);
        Cracks->SetCustomPrimitiveDataFloat(2,Damage);
        Cracks->SetCustomPrimitiveDataFloat(3,Started);
    }
    if(Reset || FMath::Abs(LastShown-Shown)>.00001f) {
        using namespace MCNutCombatVisuals;
        FMesh Detail;
        if(Art) {
            for(const auto& Path:Art->Cracks.Paths) {
                const MCNutShieldCrackLayout::FPath NativePath{Path.First,Path.Count,Path.RevealAt,Path.FullAt,Path.bRim};
                ShieldFissure(Detail,NativePath,Shown,Authored,Boss->Settings.BodyRadius,&Art->Cracks);
            }
        } else for(const auto& Path:MCNutShieldCrackLayout::Paths)
            ShieldFissure(Detail,Path,Shown,Authored,Boss->Settings.BodyRadius);
        Detail.Upload(Cracks);Cracks->SetCustomPrimitiveDataFloat(0,Shown);
    }
    Cracks->SetVisibility(true);
}

FVector AMCNutCombatEffect::GetNutRainDropPoint(FVector Center,float Radius,int32 Seed,int32 Index)
{
    if(Center.ContainsNaN()) return FVector::ZeroVector;
    FRandomStream Random{int32(HashCombineFast(uint32(Seed),uint32(Index)))};
    const float Angle=Random.FRand()*2*PI,Distance=FMath::Sqrt(Random.FRand())*Bounded(Radius,300,20,1200)*.86f;
    return Center+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
}

AMCNutCombatEffect* AMCNutCombatEffect::Spawn(AActor* Source,AMCTongue* Surface,EMCNutCombatCue Type,
    FVector Origin,FVector Target,float Radius,float Windup,float Active,int32 Seed,float DetailRadius,int32 DropCount,float DropCadence,
    bool bSuppressCosmeticAccents,FGuid PresentationEpisodeID,
    const FMCVFXPreparedNativePresentation* NativePresentation,FName NativeEvent,const FMCNutCuePresentation& Context)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=Source->GetWorld()
        || Origin.ContainsNaN() || Target.ContainsNaN() || uint8(Type)>uint8(EMCNutCombatCue::WizardSpell)) return nullptr;
    const FTransform Pose(Target);
    auto* Effect=Source->GetWorld()->SpawnActorDeferred<AMCNutCombatEffect>(StaticClass(),Pose,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Effect) return nullptr;
    Effect->SourceActor=Source;Effect->Tongue=Surface;
    Effect->Cue.Type=Type;Effect->Cue.Role=Type==EMCNutCombatCue::FireCast || Type==EMCNutCombatCue::FireImpact
        || Type==EMCNutCombatCue::NutRain || Type==EMCNutCombatCue::SummonTell || Type==EMCNutCombatCue::CastCharge
        || Type==EMCNutCombatCue::CastRelease || Type==EMCNutCombatCue::RitualCast?EMCNutBossRole::Mage:EMCNutBossRole::Tank;
    if(const auto* Boss=Cast<AMCNutBoss>(Source)) Effect->Cue.Role=Boss->BossRole;
    Effect->Cue.Origin=Origin;Effect->Cue.Target=Target;Effect->Cue.Radius=Bounded(Radius,100,15,1200);
    Effect->Cue.DetailRadius=Bounded(DetailRadius,90,15,250);Effect->Cue.WindupSeconds=Bounded(Windup,1,0,12);
    Effect->Cue.ActiveSeconds=Bounded(Active,1,0,24);Effect->Cue.Seed=Seed;
    Effect->Cue.DropCount=FMath::Clamp(DropCount,1,MaxRainDrops);Effect->Cue.DropCadence=Bounded(DropCadence,.5f,.12f,2);
    // Only the migrated fireball family has a separate accent channel. This
    // cannot suppress unrelated native attack or danger geometry.
    Effect->Cue.bSuppressCosmeticAccents=bSuppressCosmeticAccents &&
        (Type==EMCNutCombatCue::CastCharge || Type==EMCNutCombatCue::CastRelease || Type==EMCNutCombatCue::FireImpact);
    Effect->Cue.PresentationEpisodeID=PresentationEpisodeID;
    Effect->Cue.PresentationSource=Context.Source;Effect->Cue.PresentationEvent=Context.Event;Effect->Cue.AfterWindupEvent=Context.AfterWindupEvent;
    if(const auto* Contract=MCNutVFX::Find(Context.Source)) {
        Effect->Presentation->SourceID=Contract->ID;Effect->Presentation->Events=Contract->Events;
        if(auto* VisualSubject=NutSourceVisual(Source)) {FString Error;Effect->Presentation->BindSourceAudioSocket(Context.Source,VisualSubject,Error);}
        TInlineComponentArray<UMCVFXPresentationComponent*> Owners(Source);
        for(const auto* Owner:Owners)if(IsValid(Owner) && Owner->IsSourcePrepared(Context.Source)) {
            FString Error;if(Owner->CopyPreparedSourceTo(Context.Source,Effect->Presentation,Error))break;
        }
        if(Context.Source==MCNutVFX::GravityWavesSource()) {
            FString Error;
            MCNutVFX::Prepare(Effect->Presentation,Context.Source,Error,false);
        }
    }
    if(NativePresentation && NativePresentation->Snapshot) {
        Effect->NativeArt=*NativePresentation;Effect->Cue.NativeStyle=NativePresentation->Reference;
        Effect->Cue.NativeEvent=NativeEvent;
    }
    Effect->Cue.StartedAt=Effect->Now();Effect->FinishSpawning(Pose);return Effect;
}

void AMCNutCombatEffect::BeginPlay()
{
    Super::BeginPlay();
    if(const auto* Contract=MCNutVFX::Find(Cue.PresentationSource)) {
        Presentation->SourceID=Contract->ID;Presentation->Events=Contract->Events;
        FString Error;
        if(auto* Subject=NutSourceVisual(SourceActor))Presentation->BindSourceAudioSocket(Contract->ID,Subject,Error);
        MCNutVFX::Prepare(Presentation,Contract->ID,Error,false);
    }
    if(HasAuthority()) SetLifeSpan(FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds)+1.f);
    if(Tongue) AddTickPrerequisiteActor(Tongue);
    if(IsValid(SourceActor))AddTickPrerequisiteActor(SourceActor);
    if(GetNetMode()==NM_DedicatedServer) return;
    if(!NativeArt.Snapshot && !Cue.NativeStyle.Source.IsNull()) {
        const bool Shield=Cue.Type==EMCNutCombatCue::ShieldHit;
        const FName Source=Shield?FName(TEXT("Boss.NutKnight.Shield")):FName(TEXT("Boss.NutMage.Fireball"));
        const TArray<FName> Events=Shield?TArray<FName>{TEXT("Hit"),TEXT("Break")}:
            TArray<FName>{TEXT("Charge"),TEXT("Release"),TEXT("Flight"),TEXT("Impact")};
        FString Error;if(!FMCVFXPreparedNativePresentation::ResolveResidentReference(Cue.NativeStyle,Source,Events,this,NativeArt,Error))
            UE_LOG(LogTemp,Warning,TEXT("Native combat cue uses its legacy appearance: %s"),*Error);
    }
    if(const auto* Boss=Cast<AMCNutBoss>(SourceActor))
        if(Boss->BossRole==EMCNutBossRole::Mage && Boss->MageModel) {
            AddTickPrerequisiteComponent(Boss->MageModel);
            if(Cue.Type==EMCNutCombatCue::CastCharge && Cue.PresentationEpisodeID.IsValid()) {
                FString Error;Boss->FireballPresentation->BindSourceEpisodeTickSource(
                    Boss->FireballPresentation->SourceID,Cue.PresentationEpisodeID,this,Error);
            }
        }
    InitializeMaterials();
}

void AMCNutCombatEffect::InitializeMaterials()
{
    if(WarningMID || Cue.Type==EMCNutCombatCue::GravityHand || Cue.Type==EMCNutCombatCue::WizardSpell) return;
    WarningMID=Material(Warning,TelegraphMaterial.LoadSynchronous());
    SmallWarningMID=Material(DropWarnings,TelegraphMaterial.LoadSynchronous());
    ShadowMID=Material(DropShadows,ShadowMaterial.LoadSynchronous());
    const FName FireRole=Cue.NativeEvent==TEXT("Charge")?FName(TEXT("ChargeFlames")):
        Cue.NativeEvent==TEXT("Break")?FName(TEXT("BrokenShell")):FName(TEXT("ImpactFlames"));
    if(NativePart(FireRole)) {FireMID=NativeArt.CreatePrivateMaterial(Cue.NativeEvent,FireRole,this);Flames->SetMaterial(0,FireMID);}
    else FireMID=Material(Flames,FlameMaterial.LoadSynchronous());
    const FName MagicRole=Cue.NativeEvent==TEXT("Charge")?FName(TEXT("ChargeFocusMagic")):
        Cue.NativeEvent==TEXT("Release")?FName(TEXT("ReleaseMagic")):FName(TEXT("ShieldContactMagic"));
    if(NativePart(MagicRole)) {MagicMID=NativeArt.CreatePrivateMaterial(Cue.NativeEvent,MagicRole,this);Magic->SetMaterial(0,MagicMID);}
    else MagicMID=Material(Magic,MagicMaterial.LoadSynchronous());
    StormMID=Material(Storm,StormMaterial.LoadSynchronous());
    RainNuts->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
}

void AMCNutCombatEffect::PreviewAtAge(float Age)
{
#if WITH_EDITOR
    if(GetWorld() && !GetWorld()->IsGameWorld() && IsValid(Tongue) && FMath::IsFinite(Age)) {
        LastCueAge=FMath::Max(0.f,Age);InitializeMaterials();Present(LastCueAge);
    }
#endif
}

void AMCNutCombatEffect::Burst(FVector Point,bool bFire,float Scale)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    const auto* Art=NativePart(bFire?FName(TEXT("ImpactSparks")):FName(TEXT("FloorDust")));
    PlayBurst(MCNativeArtControls::System(Art,bFire?FireImpactSystem:DustImpactSystem),Point,FVector::UpVector,
        Art && Cue.NativeEvent!=TEXT("Impact")?Art->SystemScale:Scale);
}

void AMCNutCombatEffect::PlayBurst(UNiagaraSystem* System,FVector Point,FVector Direction,float Scale,bool bKeepWithComposition)
{
    if(bComposedDecorations&&!bKeepWithComposition)return;
    if(GetNetMode()!=NM_DedicatedServer && System && Bursts.Num()<8) {
        auto* Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Point,Direction.Rotation(),FVector(FMath::Clamp(Scale,.3f,2.f)),
            false,true,ENCPoolMethod::ManualRelease,true);
        if(Component) {
            if(bKeepWithComposition)Component->ComponentTags.AddUnique(TEXT("MC.Nut.AcceptedExtraBurst"));
            Bursts.Add(Component);Component->OnSystemFinished.AddDynamic(this,&AMCNutCombatEffect::FinishedBurst);
        }
    }
}

void AMCNutCombatEffect::GetAcceptedBurstEvents(TArray<FName>& Out) const
{for(const auto& Burst:AcceptedBursts)Out.AddUnique(Burst.Event);}

void AMCNutCombatEffect::PresentBurstEvent(FName Event,UNiagaraSystem* System,FVector Point,FVector Direction,float Scale,float StartedAtCueAge)
{
    // Called only by the original real burst sites and their original late-cue
    // admission guards. Per-peer local state avoids another replicated/audio cue.
    if(Event.IsNone()||AcceptedBursts.ContainsByPredicate([&](const auto& B){return B.Event==Event;}))return;
    FMCNutAcceptedBurst Burst;Burst.Event=Event;Burst.StartedAtCueAge=StartedAtCueAge;
    Burst.Frame=FTransform(Direction.Rotation(),Point,FVector(FMath::Clamp(Scale,.3f,2.f)));
    AcceptedBursts.Add(Burst);
    UpdateAcceptedBursts(LastCueAge);
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    const bool Prepared=Presentation&&!Cue.PresentationSource.IsNone()
        &&Presentation->GetPreparedEventMode(Cue.PresentationSource,Event,Mode);
    const auto* Native=Prepared?Presentation->GetPreparedNativeSource(Cue.PresentationSource):nullptr;
    const auto* Accepted=AcceptedBursts.FindByPredicate([&](const auto& B){return B.Event==Event;});
    const bool KeepNative=!Accepted||!Accepted->bPresentationAccepted||Mode==EMCPerkVFXSlotMode::UseDefault
        ||(Native&&Native->ShouldRenderNativeAccents(Event,Mode));
    if(!Prepared||KeepNative)PlayBurst(System,Point,Direction,Scale,true);
}

void AMCNutCombatEffect::UpdateAcceptedBursts(float Age)
{
    LastCueAge=Age;
    if(!Presentation||Cue.PresentationSource.IsNone())return;
    FString Error;
    if(!MCNutVFX::Prepare(Presentation,Cue.PresentationSource,Error,false))return;
    for(auto& Burst:AcceptedBursts) {
        if(Burst.bRejected)continue;
        FMCPerkVFXGameplayValues Values;Values.NativeInputs=LastCueNativeInputs;
        Values.Radius=Cue.Radius;Values.Fill=0;Values.Direction=Burst.Frame.GetRotation().GetForwardVector();
        Values.Duration=FMath::Max(.01f,Cue.WindupSeconds+Cue.ActiveSeconds-Burst.StartedAtCueAge);
        Values.LocalStateAgeSeconds=FMath::Max(0.f,Age-Burst.StartedAtCueAge);
        Values.bHasEndpoints=true;Values.SourcePosition=Cue.Origin;Values.TargetPosition=Cue.Target;
        Values.bHasSeed=true;Values.Seed=uint32(Cue.Seed);Values.VisualSubject=NutSourceVisual(SourceActor);
        // Original bursts are world-fixed at acceptance, not reattached to a
        // moving palm/body. SourceSocket audio still uses the explicit mesh map.
        Burst.bPresentationAccepted=Presentation->UpdateLocalSourceState(Cue.PresentationSource,Burst.Event,true,Burst.Frame,Values,
            Values.VisualSubject.Get(),Error);
        if(!Burst.bPresentationAccepted)Burst.bRejected=true;
        if(!Burst.bPresentationAccepted&&!Error.IsEmpty()&&LastNativeInputError!=Error) {
            LastNativeInputError=Error;
            UE_LOG(LogTemp,Warning,TEXT("Nut VFX %s.%s kept its native burst: %s"),
                *Cue.PresentationSource.ToString(),*Burst.Event.ToString(),*Error);
        }
    }
}

void AMCNutCombatEffect::PresentMagic(float Age,FVector Floor,FVector Normal,FVector Right,FVector Side,float Fade)
{
    using namespace MCNutCombatVisuals;
    const bool Charge=Cue.Type==EMCNutCombatCue::CastCharge || Cue.Type==EMCNutCombatCue::FireCast;
    const bool Release=Cue.Type==EMCNutCombatCue::CastRelease;
    const auto* FocusArt=NativePart(Release?FName(TEXT("ReleaseMagic")):FName(TEXT("ChargeFocusMagic")));
    const auto* CastSparks=NativePart(TEXT("CastSparks"));
    const auto* ReleaseSparks=NativePart(TEXT("ReleaseSparks"));
    const bool Ritual=Cue.Type==EMCNutCombatCue::RitualCast || Cue.Type==EMCNutCombatCue::SummonTell;
    const bool Transform=Cue.Type==EMCNutCombatCue::Transform;
    const bool Roll=Cue.Type==EMCNutCombatCue::RollTell;
    const bool Shield=Cue.Type==EMCNutCombatCue::ShieldHit;
    const bool Death=Cue.Type==EMCNutCombatCue::DeathBurst;
    const bool Rain=Cue.Type==EMCNutCombatCue::NutRain;
    const bool Motion=Cue.Type==EMCNutCombatCue::ChargeTell && Age>=Cue.WindupSeconds;
    const float Total=FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds);
    const float Progress=FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f);
    const auto* Boss=Cast<AMCNutBoss>(SourceActor);
    const FVector Caster=Boss?Boss->GetSpellFocusOrigin():Cue.Origin;
    if(Cue.Type==EMCNutCombatCue::CastCharge && Boss && Cue.PresentationEpisodeID.IsValid()
        && Age<Cue.WindupSeconds) {
        // This existing cue ticks after the evaluated mage model on every
        // rendering peer. Native animation supplies the hand frame, not an
        // authority-only transform update or another network stream.
        FString Error;Boss->FireballPresentation->BindSourceEpisodeTickSource(
            Boss->FireballPresentation->SourceID,Cue.PresentationEpisodeID,this,Error);
        FMCPerkVFXGameplayValues Values;Values.EpisodeID=Cue.PresentationEpisodeID;
        Values.Direction=(Boss->LockedTarget-Caster).GetSafeNormal();Values.Radius=Cue.Radius;
        Values.Duration=Cue.WindupSeconds;Values.Fill=Progress;
        Values.bHasEndpoints=true;Values.SourcePosition=Caster;Values.TargetPosition=Boss->LockedTarget;
        Values.bHasSeed=true;Values.Seed=uint32(Cue.Seed);
        Boss->FireballPresentation->UpdateSourceEpisode(Boss->FireballPresentation->SourceID,
            Cue.PresentationEpisodeID,FTransform(Values.Direction.Rotation(),Caster),Values);
    }
    if(Cue.bSuppressCosmeticAccents && (Charge || Release || Cue.Type==EMCNutCombatCue::FireImpact)) {
        Magic->SetVisibility(false);Storm->SetVisibility(false);SustainedParticles->Deactivate();return;
    }
    const FVector Source=IsValid(SourceActor)?SourceActor->GetActorLocation():Cue.Origin;
    const FVector Direction=(Cue.Target-Cue.Origin).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FVector Forward=Boss && (Roll || Motion)?Boss->AttackForward:Direction;
    const FLinearColor Tint=Cue.Role==EMCNutBossRole::Mage?FLinearColor(.56f,.19f,1.f):FLinearColor(1.f,.53f,.09f);
    FMesh Detail,Cloud;
    if(MagicMID) {
        MagicMID->SetScalarParameterValue(TEXT("Age"),Age);
        MagicMID->SetScalarParameterValue(TEXT("Progress"),Progress);
        MagicMID->SetVectorParameterValue(TEXT("Tint"),FocusArt?FocusArt->Tint:Tint);
        MagicMID->SetScalarParameterValue(TEXT("Beam"),0);
    }
    const FVector LocalFloor=Floor-GetActorLocation();
    if((Charge && MCNativeArtControls::Enabled(FocusArt)) || Cue.Type==EMCNutCombatCue::RitualCast) {
        const FVector Center=Caster-GetActorLocation();
        const float Radius=FMath::Lerp(FocusArt?FocusArt->Cast.MinFocusRadius:14.f,FocusArt?FocusArt->Cast.MaxFocusRadius:40.f,Progress)
            *(1.f+.035f*FMath::Sin(Age*19))*MCNativeArtControls::Expansion(FocusArt,Age,Cue.WindupSeconds);
        // Three crossed planes form a compact focus visible from every player camera.
        Detail.Quad(Center,FVector::ForwardVector,FVector::UpVector,Radius,Radius,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade)));
        Detail.Quad(Center,FVector::RightVector,FVector::UpVector,Radius,Radius,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade)));
        Detail.Quad(Center,FVector::ForwardVector,FVector::RightVector,Radius,Radius,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade)));
        for(int32 Arm=0;Arm<(FocusArt?FocusArt->Cast.Arms:2);++Arm) for(int32 Segment=0;Segment<(FocusArt?FocusArt->Cast.Segments:12);++Segment) {
            const float Phase=Arm*PI+Segment*.36f-Age*(FocusArt?FocusArt->Cast.SpinSpeed:3.2f);
            const float Distance=Radius*(1.65f-Segment*.045f);
            const FVector P=Center+FVector(FMath::Cos(Phase)*Distance,FMath::Sin(Phase)*Distance,Segment*3.f-18.f);
            Detail.Quad(P,FVector::UpVector,FVector(-FMath::Sin(Phase),FMath::Cos(Phase),0),2.2f,4.2f,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade*.65f)));
        }
    }
    if(Ritual) {
        const float Radius=Cue.Type==EMCNutCombatCue::SummonTell?Cue.Radius*1.18f:55.f;
        const FVector Base=Cue.Type==EMCNutCombatCue::SummonTell?LocalFloor:Source-GetActorLocation();
        Detail.Quad(Base+Normal*9,Right,Side,Radius,Radius,FLinearColor(1,1,1,Fade*.82f));
        const float Rise=30+Progress*85;
        for(int32 Arm=0;Arm<3;++Arm) for(int32 Segment=0;Segment<12;++Segment) {
            const float Fraction=Segment/11.f,Phase=Arm*2*PI/3+Fraction*4.5f-Age*2.2f;
            const FVector P=Base+(Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase))*Radius*.8f+Normal*(12+Fraction*Rise);
            Detail.Quad(P,Normal,Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase),2.2f,4.5f,FLinearColor(1,1,1,Fade*(1-Fraction*.55f)));
        }
        if(Age>=Cue.WindupSeconds && !bReleasePresented) {
            bReleasePresented=true;
            if(Age-Cue.WindupSeconds<.25f) PresentBurstEvent(TEXT("ReleaseBurst"),ArcaneBurstSystem.Get(),Floor+Normal*16,Normal,.65f,Cue.WindupSeconds);
        }
    }
    if(Release) {
        if(MCNativeArtControls::Enabled(FocusArt)) {
        const FVector BeamSide=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
        const FVector Base=Cue.Origin-GetActorLocation();
        const float Length=FMath::Lerp(FocusArt?FocusArt->Cast.ReleaseMinLength:35.f,FocusArt?FocusArt->Cast.ReleaseMaxLength:110.f,FMath::Clamp(Age/Total,0.f,1.f));
        Detail.Quad(Base+Direction*Length*.5f,Direction,BeamSide,Length*.5f,(FocusArt?FocusArt->Cast.ReleaseWidth:12.f)*Fade,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade)));
        Detail.Quad(Base+Direction*Length*.5f,Direction,FVector::UpVector,Length*.5f,(FocusArt?FocusArt->Cast.ReleaseWidth:12.f)*Fade,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade)));
        if(MagicMID) MagicMID->SetScalarParameterValue(TEXT("Beam"),1);
        }
        if(!bReleasePresented) {bReleasePresented=true;if(Age<.2f) PlayBurst(
            MCNativeArtControls::System(ReleaseSparks,ArcaneBurstSystem),Cue.Origin,Direction,MCNativeArtControls::SystemScale(ReleaseSparks,.65f));}
    }
    if(Transform || Roll || Motion || Shield || Death) {
        const FVector Base=Shield?Cue.Origin-GetActorLocation():Source-GetActorLocation();
        const float Expansion=Death?FMath::Lerp(.4f,1.65f,FMath::Clamp(Age/Total,0.f,1.f)):1.f;
        const float Radius=FMath::Clamp(Cue.Radius,35.f,300.f)*Expansion;
        if(Shield) {
            const FVector Facing=(Cue.Origin-Cue.Target).GetSafeNormal(SMALL_NUMBER,Forward);
            const FVector Horizontal=FVector::CrossProduct(Facing,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
            Detail.Quad(Base,Horizontal,FVector::UpVector,Radius,Radius*.85f,FLinearColor(1,1,1,MCNativeArtControls::Opacity(FocusArt,Age,Total,Fade)));
            if(!bReleasePresented) {bReleasePresented=true;if(Age<.2f) PlayBurst(ShieldHitSystem.LoadSynchronous(),Cue.Origin,Facing,.8f);}
        } else {
            for(int32 Arm=0;Arm<3;++Arm) for(int32 Segment=0;Segment<14;++Segment) {
                const float Fraction=Segment/13.f,Phase=Arm*2*PI/3+Fraction*5.4f-Age*5;
                const float R=Radius*(.65f+.35f*FMath::Sin(Fraction*PI));
                const FVector P=Base+(Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase))*R+Normal*(Fraction*Radius*1.8f-Radius*.6f);
                Detail.Quad(P,Normal,Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase),2.5f,5.2f,FLinearColor(1,1,1,Fade*.7f));
            }
            if(Transform && !bReleasePresented) {
                bReleasePresented=true;
                if(Age<.2f) PresentBurstEvent(TEXT("TransformBurst"),ShieldHitSystem.Get(),Source,Normal,.7f);
            }
            if(Death && !bReleasePresented) {
                bReleasePresented=true;
                if(Age<.25f) {
                    Burst(Floor+Normal*15,false,1.25f);
                    PresentBurstEvent(TEXT("BodyBurst"),Cue.Role==EMCNutBossRole::Mage?ArcaneBurstSystem.Get():ShieldHitSystem.Get(),Source,Normal,1.2f);
                }
            }
        }
    }
    if(Cue.Type==EMCNutCombatCue::SlamImpact || Cue.Type==EMCNutCombatCue::EntranceImpact) {
        const float Phase=FMath::Clamp(Age/Total,0.f,1.f);
        for(int32 Ring=0;Ring<2;++Ring) {
            const float Radius=Cue.Radius*FMath::Lerp(.22f,1.1f,Phase)*(1.f-Ring*.24f);
            Detail.Quad(LocalFloor+Normal*(7+Ring*2),Right,Side,Radius,Radius,FLinearColor(1,1,1,Fade*(.72f-Ring*.22f)));
        }
    }
    if(Rain && StormMID) {
        // A shallow cloud lid stays above the fight. The actual falling nut
        // streaks below use the same points and schedule as server damage.
        const FVector CloudCenter=LocalFloor+FVector(0,0,610);
        for(int32 Panel=0;Panel<5;++Panel) {
            const float Angle=Panel*2.39996f+Cue.Seed*.001f;
            const FVector Offset=FVector(FMath::Cos(Angle),FMath::Sin(Angle),.08f*Panel)*Cue.Radius*.32f;
            Cloud.Quad(CloudCenter+Offset,FVector::ForwardVector,FVector::RightVector,Cue.Radius*.52f,Cue.Radius*.48f,FLinearColor(1,1,1,Fade*.28f));
        }
        StormMID->SetScalarParameterValue(TEXT("Age"),Age);
    }
    if(!Detail.Points.IsEmpty()) Detail.Upload(Magic);
    Magic->SetVisibility(!Detail.Points.IsEmpty() && Age<=Total && MagicMID);
    if(!Cloud.Points.IsEmpty()) Cloud.Upload(Storm);
    Storm->SetVisibility(Rain && Age<=Total && StormMID);
    UNiagaraSystem* Sustained=nullptr;
    FVector ParticlePoint=Source;
    if(Charge || Cue.Type==EMCNutCombatCue::RitualCast) {Sustained=MCNativeArtControls::System(CastSparks,CastChargeSystem);ParticlePoint=Caster;}
    else if(Rain) {Sustained=StormMotesSystem.LoadSynchronous();ParticlePoint=Floor+FVector(0,0,480);}
    else if(Transform || Roll || Motion) {Sustained=MotionDustSystem.LoadSynchronous();ParticlePoint=Source-Forward*Cue.Radius*.6f-FVector(0,0,Cue.Radius*.3f);}
    if(Sustained && Age<Total) {
        if(!bSustainedStarted) {bSustainedStarted=true;SustainedParticles->SetAsset(Sustained);SustainedParticles->Activate(true);}
        SustainedParticles->SetWorldLocation(ParticlePoint);SustainedParticles->SetWorldRotation(Forward.Rotation());
    } else if(bSustainedStarted) SustainedParticles->Deactivate();
}

void AMCNutCombatEffect::FinishedBurst(UNiagaraComponent* Component)
{
    if(!IsValid(Component) || !Bursts.Contains(Component)) return;
    Component->OnSystemFinished.RemoveDynamic(this,&AMCNutCombatEffect::FinishedBurst);
    Bursts.Remove(Component);ReleaseBurst(Component);
}

void AMCNutCombatEffect::PresentTankImpact(float Age)
{
    using namespace MCNutCombatVisuals;
    const bool Shield=Cue.Type==EMCNutCombatCue::ShieldHit;
    // The existing breaking-hit cue has a longer tail than ordinary shield hits.
    const bool Broken=Shield && Cue.ActiveSeconds>.45f;
    const auto* FlashArt=NativePart(TEXT("ShieldContactMagic"));
    const auto* DebrisArt=NativePart(TEXT("ShellDebris"));
    const auto* ShardArt=NativePart(TEXT("BrokenShell"));
    const auto* DustArt=NativePart(TEXT("FloorDust"));
    const auto* SparksArt=NativePart(TEXT("ShieldSparks"));
    const float Total=FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds);
    const bool Visible=Age<Total;
    const FVector Forward=(Shield?Cue.Origin-Cue.Target:Cue.Target-Cue.Origin).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FVector Horizontal=FVector::CrossProduct(Forward,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    const FVector Vertical=FVector::CrossProduct(Horizontal,Forward).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    const FVector Contact=Cue.Origin-GetActorLocation();
    Warning->SetVisibility(false);DropWarnings->SetVisibility(false);DropShadows->SetVisibility(false);
    Flames->SetVisibility(false);Storm->SetVisibility(false);RainNuts->SetVisibility(false);
    if(MagicMID) {
        MagicMID->SetScalarParameterValue(TEXT("Age"),0);
        MagicMID->SetScalarParameterValue(TEXT("Progress"),1);
        MagicMID->SetScalarParameterValue(TEXT("Beam"),1);
        MagicMID->SetVectorParameterValue(TEXT("Tint"),FlashArt?FlashArt->Tint:FLinearColor(.65f,.31f,.085f));
    }
    FMesh Detail;
    if(Shield) {
        if(MCNativeArtControls::Enabled(FlashArt)) {
        const float FlashEnd=FlashArt?FlashArt->Contact.FlashEndSeconds:Broken?.15f:.12f;
        const float Flash=1-FMath::SmoothStep(FlashArt?FlashArt->Contact.FlashBeginSeconds:.025f,FlashEnd,Age);
        const float Radius=FMath::Clamp(Cue.Radius*(FlashArt?FlashArt->Contact.OuterRadiusFraction:Broken?.27f:.20f),FlashArt?FlashArt->Contact.MinRadius:16.f,FlashArt?FlashArt->Contact.MaxRadius:42.f)
            *FMath::Lerp(.65f,1.f,FMath::Clamp(Age/(FlashArt?FlashArt->Contact.ExpandSeconds:.035f),0.f,1.f))*MCNativeArtControls::Expansion(FlashArt,Age,Total);
        const FVector Center=Contact+Forward*(FlashArt?FlashArt->Contact.DepthOffset:3.f);
        const int32 Corners=FlashArt?FlashArt->Contact.Corners:12;
        for(int32 I=0;I<Corners;++I) {
            const float A=I*2*PI/Corners+.15f,B=(I+1)*2*PI/Corners+.15f;
            const float RA=Radius*(I%2==0?1.f:(FlashArt?FlashArt->Contact.InnerRadiusRatio:.32f)),RB=Radius*(I%2==0?(FlashArt?FlashArt->Contact.InnerRadiusRatio:.32f):1.f);
            ImpactTriangle(Detail,Center,Center+(Horizontal*FMath::Cos(A)+Vertical*FMath::Sin(A))*RA,
                Center+(Horizontal*FMath::Cos(B)+Vertical*FMath::Sin(B))*RB,Forward,MCNativeArtControls::Opacity(FlashArt,Age,Total,Flash*.88f));
        }
        if(FlashArt?FlashArt->Contact.SplinterBranches>0:Broken) {
            // Short outward splinters accent the physical shell fragments.
            const float Spread=FMath::Clamp(Age/.18f,0.f,1.f);
            const float Fade=1-FMath::SmoothStep(.035f,.18f,Age);
            const float Reach=FMath::Clamp(Cue.Radius*.21f,18.f,55.f);
            for(int32 Branch=0;Branch<(FlashArt?FlashArt->Contact.SplinterBranches:5);++Branch) {
                const float Angle=Branch*2.39996f+.25f;
                const FVector Radial=Horizontal*FMath::Cos(Angle)+Vertical*FMath::Sin(Angle);
                const FVector Tangent=FVector::CrossProduct(Forward,Radial).GetSafeNormal();
                const FVector A=Center+Radial*Reach*(.12f+Spread*.3f)+Forward*Age*40;
                const FVector B=Center+Radial*Reach*(.50f+Spread*.55f)+Tangent*Reach*.12f+Forward*Age*40;
                const FVector C=Center+Radial*Reach*(.88f+Spread*.72f)+Forward*Age*40;
                CrackSegment(Detail,A,B,Forward,(FlashArt?FlashArt->Contact.SplinterWidthA:1.4f)*(1-Spread*.65f),MCNativeArtControls::Opacity(FlashArt,Age,Total,Fade*.35f));
                CrackSegment(Detail,B,C,Forward,(FlashArt?FlashArt->Contact.SplinterWidthB:.8f)*(1-Spread*.65f),MCNativeArtControls::Opacity(FlashArt,Age,Total,Fade*.25f));
            }
        }
        }
    } else {
        // This is the attack's directional swipe, not a claim that a player was hit.
        const float Sweep=FMath::Clamp(Age/.13f,0.f,1.f),Fade=1-FMath::SmoothStep(.10f,Total,Age);
        const float Radius=Cue.Radius*.76f;
        constexpr int32 Segments=16;
        for(int32 I=0;I<Segments;++I) {
            const float Fraction=(I+.5f)/Segments,Angle=FMath::DegreesToRadians(-65.f+Fraction*130.f);
            const FVector Radial=Forward*FMath::Cos(Angle)+Horizontal*FMath::Sin(Angle);
            const FVector Tangent=-Forward*FMath::Sin(Angle)+Horizontal*FMath::Cos(Angle);
            const float Tail=FMath::Clamp(1-FMath::Abs(Fraction-Sweep)*5.f,0.f,1.f);
            Detail.Quad(Contact+Radial*Radius,Radial,Tangent,4.f+10.f*Tail,Radius*.075f,FLinearColor(1,1,1,Fade*Tail*.82f));
        }
    }
    FHitResult Ground;
    const FVector GroundPoint=Shield?Cue.Origin:Cue.Origin+Forward*Cue.Radius*.48f;
    const bool HasGround=Tongue->SurfacePoint(GroundPoint,Ground);
    const FVector Up=HasGround?Ground.ImpactNormal:FVector::UpVector;
    const FVector Right=FVector::VectorPlaneProject(Forward,Up).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FVector Side=FVector::CrossProduct(Up,Right).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    if(HasGround && !Shield) {
        const float RingEnd=Broken?.36f:Shield?.24f:Total;
        const float Phase=FMath::Clamp(Age/RingEnd,0.f,1.f),Fade=1-FMath::SmoothStep(.08f,RingEnd,Age);
        const float Radius=FMath::Clamp(Cue.Radius*(Broken?.42f:.27f),35.f,115.f)*FMath::Lerp(.40f,1.15f,Phase);
        const FVector Base=Ground.ImpactPoint-GetActorLocation()+Up*6;
        constexpr int32 Segments=24;
        for(int32 I=0;I<Segments;++I) {
            const float Angle=(I+.5f)*2*PI/Segments;
            const FVector Radial=Right*FMath::Cos(Angle)+Side*FMath::Sin(Angle),Tangent=-Right*FMath::Sin(Angle)+Side*FMath::Cos(Angle);
            Detail.Quad(Base+Radial*Radius,Tangent,Radial,Radius*PI/Segments*1.04f,FMath::Lerp(2.8f,.6f,Phase),FLinearColor(1,1,1,Fade*.38f));
        }
    }
    Detail.Upload(Magic);Magic->SetVisibility(Visible && Age<(Broken?.42f:Shield?.24f:Total) && MagicMID);
    if(!bBurstPresented) {
        bBurstPresented=true;
        Debris->SetStaticMesh(MCNativeArtControls::Mesh(DebrisArt,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
        const int32 Count=!MCNativeArtControls::Enabled(DebrisArt)?0:DebrisArt?DebrisArt->Debris.Count:Broken?12:Shield?4:5;
        for(int32 I=0;I<Count;++I) Debris->AddInstance(FTransform(FVector::ZeroVector));
        // Late replication reconstructs the tail without replaying an old burst.
        if(HasGround && Age<.12f) PlayBurst(MCNativeArtControls::System(DustArt,DustImpactSystem),Ground.ImpactPoint+Up*8,Up,MCNativeArtControls::SystemScale(DustArt,Broken?1.2f:.35f));
        if(Shield && Age<.12f) PlayBurst(MCNativeArtControls::System(SparksArt,ShieldHitSystem),Cue.Origin,Forward,MCNativeArtControls::SystemScale(SparksArt,Broken?.6f:.3f));
        if(Broken) Flames->SetMaterial(0,ShardArt?ShardArt->Material.Get():ShieldFractureMaterial.LoadSynchronous());
    }
    const float Tail=1-FMath::SmoothStep(Total*(DebrisArt?DebrisArt->Debris.FadeStartFraction:.55f),Total,Age);
    const FVector Start=(Shield?Cue.Origin:HasGround?Ground.ImpactPoint+Up*14:GroundPoint)-GetActorLocation();
    FRandomStream Random(Cue.Seed+137);
    for(int32 I=0;I<Debris->GetInstanceCount();++I) {
        const float Angle=I*2.39996f+Random.FRandRange(-.2f,.2f),Speed=Random.FRandRange(DebrisArt?DebrisArt->Debris.MinSpeed:Broken?135.f:85.f,DebrisArt?DebrisArt->Debris.MaxSpeed:Broken?310.f:160.f);
        const FVector Radial=Horizontal*FMath::Cos(Angle)+Vertical*FMath::Sin(Angle);
        const FVector Velocity=Shield?Forward*Speed+Radial*Speed*(DebrisArt?DebrisArt->Debris.RadialSpread:Broken?1.f:.6f)+Up*(DebrisArt?DebrisArt->Debris.UpSpeed:Broken?110.f:65.f):(Right*FMath::Cos(Angle)+Side*FMath::Sin(Angle))*Speed+Up*Random.FRandRange(80.f,135.f);
        FVector Point=Start+Velocity*Age-Up*((DebrisArt?DebrisArt->Debris.Gravity:380.f)*Age*Age);
        if(HasGround) {
            const float Height=FVector::DotProduct(Point-(Ground.ImpactPoint-GetActorLocation()),Up);
            if(Height<3) Point+=Up*(3-Height);
        }
        const float Size=Random.FRandRange(DebrisArt?DebrisArt->Debris.MinSize:Broken?1.4f:3.f,DebrisArt?DebrisArt->Debris.MaxSize:Broken?3.2f:4.8f)/MeshRadius(Debris->GetStaticMesh())*Tail;
        Debris->UpdateInstanceTransform(I,FTransform(FRotator(I*37+Age*260,I*71+Age*190,I*53),Point,FVector(Size,Size*.65f,Size)),false,I==Debris->GetInstanceCount()-1,true);
    }
    Debris->SetVisibility(Visible && Debris->GetStaticMesh());
    if(Broken && MCNativeArtControls::Enabled(ShardArt) && Flames->GetMaterial(0)) {
        FMesh Shell;
        FRandomStream Pieces(Cue.Seed+913);
        const float Damping=ShardArt?ShardArt->Debris.Damping:2.8f;
        const float Drift=Damping>.001f?(1-FMath::Exp(-Age*Damping))/Damping:Age;
        const float Fade=1-FMath::SmoothStep(Total*(ShardArt?ShardArt->Debris.FadeStartFraction:.76f),Total,Age);
        for(int32 I=0;I<(ShardArt?ShardArt->Debris.Count:5);++I) {
            const float Angle=I*2.39996f+Pieces.FRandRange(-.25f,.25f);
            const FVector Radial=Horizontal*FMath::Cos(Angle)+Vertical*FMath::Sin(Angle);
            const float Speed=Pieces.FRandRange(ShardArt?ShardArt->Debris.MinSpeed:155.f,ShardArt?ShardArt->Debris.MaxSpeed:265.f);
            FVector Point=Start+Radial*Cue.Radius*.06f+(Forward*Speed*(ShardArt?ShardArt->Debris.ForwardFraction:.65f)+Radial*Speed+Up*(ShardArt?ShardArt->Debris.UpSpeed:115.f))*Drift-Up*((ShardArt?ShardArt->Debris.Gravity:270.f)*Age*Age);
            if(HasGround) {
                const float Height=FVector::DotProduct(Point-(Ground.ImpactPoint-GetActorLocation()),Up);
                if(Height<4) Point+=Up*(4-Height);
            }
            const FQuat Orientation=FRotationMatrix::MakeFromZ(Forward).ToQuat()
                *FRotator(I*51+Age*(I%2==0?225.f:-170.f),I*73+Age*130.f,I*41).Quaternion();
            const float Size=FMath::Clamp(Cue.Radius*Pieces.FRandRange(ShardArt?ShardArt->Debris.RadiusFractionMin:.12f,ShardArt?ShardArt->Debris.RadiusFractionMax:.23f),ShardArt?ShardArt->Debris.SizeClampMin:11.f,ShardArt?ShardArt->Debris.SizeClampMax:32.f);
            const int32 First=Shell.Points.Num();
            MCNutShellShardVisuals::Shard(Shell,FTransform(Orientation,Point),Size,Cue.Seed+I*131,Fade);
            if(HasGround) {
                float Lowest=MAX_flt;
                for(int32 V=First;V<Shell.Points.Num();++V)
                    Lowest=FMath::Min(Lowest,float(FVector::DotProduct(Shell.Points[V]-(Ground.ImpactPoint-GetActorLocation()),Up)));
                if(Lowest<1.5f) for(int32 V=First;V<Shell.Points.Num();++V) Shell.Points[V]+=Up*(1.5f-Lowest);
            }
        }
        Shell.Upload(Flames);Flames->SetVisibility(Visible);
    }
}

void AMCNutCombatEffect::PresentKnightHeadSplit(float Age)
{
    using namespace MCNutCombatVisuals;
    const float Total=Cue.WindupSeconds+Cue.ActiveSeconds;
    const bool Visible=Age>=Cue.WindupSeconds && Age<Total;
    Warning->SetVisibility(false);DropWarnings->SetVisibility(false);DropShadows->SetVisibility(false);
    Storm->SetVisibility(false);Flames->SetVisibility(false);RainNuts->SetVisibility(false);
    const float Open=FMath::SmoothStep(Cue.WindupSeconds,1.25f,Age);
    const float Fade=1-FMath::SmoothStep(1.85f,Total,Age);
    const FVector Center=Cue.Origin-GetActorLocation();
    const FVector Side=IsValid(SourceActor)?SourceActor->GetActorRightVector():FVector::RightVector;
    if(!bBurstPresented) {
        if(!Visible) return;
        bBurstPresented=true;
        Debris->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
        Debris->AddInstance(FTransform::Identity);Debris->AddInstance(FTransform::Identity);
        if(Age<Cue.WindupSeconds+.2f) {
            PlayBurst(ArcaneBurstSystem.LoadSynchronous(),Cue.Origin,FVector::UpVector,1.35f);
            PresentBurstEvent(TEXT("HeadDust"),DustImpactSystem.Get(),Cue.Origin,FVector::UpVector,.65f,Cue.WindupSeconds);
        }
    }
    const UStaticMesh* Shell=Debris->GetStaticMesh();
    if(Shell) for(int32 I=0;I<2;++I) {
        const float Sign=I==0?-1.f:1.f;
        const float Scale=Cue.Radius*.95f/FMath::Max(1.,Shell->GetBounds().BoxExtent.GetMax())*Fade;
        const FQuat Rotation=FRotator(Sign*(14+Open*58),I*180.f,Sign*Open*42).Quaternion();
        const FVector Point=Center+Side*(Sign*Open*Cue.Radius*1.1f)+FVector(0,0,Open*25-Open*Open*90);
        Debris->UpdateInstanceTransform(I,FTransform(Rotation,Point-Rotation.RotateVector(Shell->GetBounds().Origin*Scale),FVector(Scale)),false,I==1,true);
    }
    Debris->SetVisibility(Visible && Shell);
    FMesh Arcane;
    if(MagicMID) {
        MagicMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.55f,.16f,1.f));
        MagicMID->SetScalarParameterValue(TEXT("Age"),Age);
        MagicMID->SetScalarParameterValue(TEXT("Progress"),Open);
        constexpr int32 Segments=32;
        const float Radius=Cue.Radius*(.25f+.55f*Open);
        for(int32 I=0;I<Segments;++I) {
            const float Angle=(I+.5f)*2*PI/Segments+Age;
            const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0),Tangent(-Radial.Y,Radial.X,0);
            Arcane.Quad(Center+Radial*Radius,Tangent,FVector::UpVector,Radius*PI/Segments*1.08f,
                Cue.Radius*.6f*FMath::Sin(Open*PI),FLinearColor(1,1,1,Fade*.8f));
        }
    }
    Arcane.Upload(Magic);Magic->SetVisibility(Visible && MagicMID);
}

void AMCNutCombatEffect::Present(float Age)
{
    using namespace MCNutCombatVisuals;
    if(Cue.Type==EMCNutCombatCue::GravityHand || Cue.Type==EMCNutCombatCue::WizardSpell) return;
    if(Cue.Type==EMCNutCombatCue::KnightHeadSplit) {PresentKnightHeadSplit(Age);return;}
    if(Cue.Role==EMCNutBossRole::Tank && (Cue.Type==EMCNutCombatCue::ShieldHit || Cue.Type==EMCNutCombatCue::MeleeSlash)) {
        PresentTankImpact(Age);return;
    }
    const bool Impact=Cue.Type==EMCNutCombatCue::SlamImpact || Cue.Type==EMCNutCombatCue::FireImpact || Cue.Type==EMCNutCombatCue::EntranceImpact
        || Cue.Type==EMCNutCombatCue::DeathBurst;
    const bool Rain=Cue.Type==EMCNutCombatCue::NutRain,Line=Cue.Type==EMCNutCombatCue::ChargeTell;
    const bool Roll=Cue.Type==EMCNutCombatCue::RollTell,Rolling=Roll && Age>=Cue.WindupSeconds;
    const float Total=FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds),T=FMath::Clamp(Age/Total,0.f,1.f);
    const bool Melee=Cue.Type==EMCNutCombatCue::MeleeTell || Cue.Type==EMCNutCombatCue::MeleeSlash;
    const bool Teleport=Cue.Type==EMCNutCombatCue::TeleportTell || Cue.Type==EMCNutCombatCue::TeleportBurst;
    if(Melee || Teleport) {
        const bool Release=Cue.Type==EMCNutCombatCue::MeleeSlash || Cue.Type==EMCNutCombatCue::TeleportBurst;
        const float Fade=1-FMath::SmoothStep(Release?.12f:.88f,1.f,T);
        FMesh Detail,Area;
        Warning->SetVisibility(false);Flames->SetVisibility(false);Storm->SetVisibility(false);
        DropWarnings->SetVisibility(false);DropShadows->SetVisibility(false);RainNuts->SetVisibility(false);Debris->SetVisibility(false);
        if(MagicMID) {
            MagicMID->SetScalarParameterValue(TEXT("Age"),Age);MagicMID->SetScalarParameterValue(TEXT("Beam"),Melee?1:0);
            MagicMID->SetVectorParameterValue(TEXT("Tint"),Melee?FLinearColor(1,.60f,.12f):FLinearColor(.55f,.24f,1.f));
        }
        FHitResult Floor;
        if(Melee && Tongue->SurfacePoint(Cue.Origin,Floor)) {
            const FVector Normal=Floor.ImpactNormal;
            const FVector Forward=FVector::VectorPlaneProject(Cue.Target-Cue.Origin,Normal).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
            const FVector Side=FVector::CrossProduct(Normal,Forward).GetSafeNormal();
            const FVector Center=(Release?Cue.Origin:Floor.ImpactPoint+Normal*7)-GetActorLocation();
            constexpr int32 Segments=20;
            for(int32 I=0;I<Segments;++I) {
                const float Fraction=(I+.5f)/Segments;
                const float Angle=FMath::DegreesToRadians(-65.f+Fraction*130.f);
                const FVector Radial=Forward*FMath::Cos(Angle)+Side*FMath::Sin(Angle);
                const FVector Tangent=-Forward*FMath::Sin(Angle)+Side*FMath::Cos(Angle);
                const float Sweep=Release?FMath::Clamp(1-FMath::Abs(Fraction-T)*3,0.f,1.f):1;
                Detail.Quad(Center+Radial*Cue.Radius*.82f,Radial,Tangent,Release?12.f:3.5f,Cue.Radius*.06f,FLinearColor(1,1,1,Fade*Sweep));
            }
            if(!Release) {
                // A textured warm footprint and curved leading edge distinguish the close-range tell from spell circles.
                Area.Quad(Center+Forward*Cue.Radius*.44f,Forward,Side,Cue.Radius*.53f,Cue.Radius*.58f,FLinearColor(1,1,1,Fade*.55f));
                Area.Upload(Warning);Warning->SetVisibility(Age<=Total && WarningMID);
                if(WarningMID) {
                    WarningMID->SetScalarParameterValue(TEXT("Age"),Age);WarningMID->SetScalarParameterValue(TEXT("Progress"),FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f));
                    WarningMID->SetScalarParameterValue(TEXT("IsLine"),1);WarningMID->SetScalarParameterValue(TEXT("Impact"),0);
                    WarningMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.58f,.12f));
                }
            }
        }
        if(Teleport) {
            for(const FVector Point:{Cue.Origin,Cue.Target}) if(Tongue->SurfacePoint(Point,Floor)) {
                const FVector Normal=Floor.ImpactNormal;
                const FVector Right=FVector::VectorPlaneProject(FVector::ForwardVector,Normal).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
                const FVector Side=FVector::CrossProduct(Normal,Right).GetSafeNormal();
                const FVector Base=Floor.ImpactPoint-GetActorLocation()+Normal*7;
                const float Radius=Cue.Radius*(Release?FMath::Lerp(.45f,1.35f,T):1.f);
                const float Charge=Release?1.f:FMath::SmoothStep(0.f,1.f,T);
                // Counter-rotating portal bands gather around the silhouette;
                // arrival rings expand into a short burst at both endpoints.
                for(int32 Ring=0;Ring<3;++Ring) for(int32 Segment=0;Segment<32;++Segment) {
                    const float Angle=(Segment+.5f)*2*PI/32+Age*(Ring%2?-2.2f:1.6f)+Ring*.7f;
                    const FVector Radial=Right*FMath::Cos(Angle)+Side*FMath::Sin(Angle);
                    const FVector Tangent=-Right*FMath::Sin(Angle)+Side*FMath::Cos(Angle);
                    const float Height=(Ring*.62f+(Release?T*.5f:Charge*.25f))*Cue.Radius;
                    const float R=Radius*(.72f+Ring*.11f)*(Release?1.f:1.12f-Charge*.2f);
                    const float Pulse=.7f+.3f*FMath::Sin(Angle*3+Age*6);
                    Detail.Quad(Base+Radial*R+Normal*Height,Tangent,Normal,
                        R*PI/32*1.04f,Release?6.f:3.5f,FLinearColor(1,1,1,Fade*Pulse*(.3f+.7f*Charge)));
                }
                for(int32 Spark=0;Spark<16;++Spark) {
                    const float Phase=FMath::Frac(Spark*.618034f+Age*(Release?1.8f:.7f));
                    const float Angle=Spark*2.399963f+Age*.8f;
                    const FVector Radial=Right*FMath::Cos(Angle)+Side*FMath::Sin(Angle);
                    const FVector Tangent=-Right*FMath::Sin(Angle)+Side*FMath::Cos(Angle);
                    const FVector P=Base+Radial*Radius*(Release?.3f+Phase:1.2f-Phase*.55f)+Normal*(Phase*Cue.Radius*1.9f);
                    Detail.Quad(P,Tangent,Normal,2.f,7.f+Phase*13.f,FLinearColor(1,1,1,Fade*FMath::Sin(Phase*PI)));
                }
                for(int32 I=0;I<24;++I) {
                    const float Angle=I*2*PI/24+Age*1.7f;
                    const FVector Radial=Right*FMath::Cos(Angle)+Side*FMath::Sin(Angle);
                    Detail.Quad(Base+Radial*Radius,Right,Normal,4.f,5.f,FLinearColor(1,1,1,Fade*.8f));
                    if(I%4==0) Detail.Quad(Base+Radial*Radius*.8f+Normal*(20+FMath::Frac(Age*.9f+I/24.f)*Radius*1.5f),Right,Normal,3.f,7.f,FLinearColor(1,1,1,Fade));
                }
            }
            // Sparse blue motes connect immutable endpoints; this is a movement cue, not a damage zone.
            for(int32 I=0;I<8;++I) {
                const float U=FMath::Frac(I/8.f+Age*.65f);
                const FVector Point=FMath::Lerp(Cue.Origin,Cue.Target,U)-GetActorLocation()+FVector(0,0,FMath::Sin(U*PI)*65);
                Detail.Quad(Point,FVector::RightVector,FVector::UpVector,3.f,5.f,FLinearColor(1,1,1,Fade*.65f));
            }
            if(Release && !bReleasePresented) {
                bReleasePresented=true;
                if(Age<.2f) {
                    PlayBurst(ArcaneBurstSystem.Get(),Cue.Origin,FVector::UpVector,.7f);
                    PresentBurstEvent(TEXT("ArrivalBurst"),ArcaneBurstSystem.Get(),Cue.Target,FVector::UpVector,.7f);
                }
            }
        }
        Detail.Upload(Magic);Magic->SetVisibility(Age<=Total && MagicMID);
        return;
    }
    const float Fade=Impact || Cue.Type==EMCNutCombatCue::ShieldHit || Cue.Type==EMCNutCombatCue::CastRelease
        ?1-FMath::SmoothStep(.15f,1.f,T):1-FMath::SmoothStep(.88f,1.f,T);
    FHitResult Ground;const FVector Center=(Roll || Cue.Type==EMCNutCombatCue::Transform || Cue.Type==EMCNutCombatCue::DeathBurst) && IsValid(SourceActor)
        ?SourceActor->GetActorLocation():Line?(Cue.Origin+Cue.Target)*.5f:Cue.Target;
    const bool HandCast=Cue.Type==EMCNutCombatCue::FireCast || Cue.Type==EMCNutCombatCue::CastCharge || Cue.Type==EMCNutCombatCue::CastRelease;
    const auto* FlameArt=NativePart(HandCast?FName(TEXT("ChargeFlames")):FName(TEXT("ImpactFlames")));
    const auto* ImpactArt=NativePart(TEXT("ImpactDebris"));
    if(HandCast) {
        // Charging and release belong to the caster, with no destination mark
        // or dependency on there being a tongue surface below the target.
        Ground.ImpactPoint=Cue.Origin;Ground.ImpactNormal=FVector::UpVector;
    } else if(Cue.Type==EMCNutCombatCue::FireImpact) {
        Ground.ImpactPoint=Cue.Target;
        Ground.ImpactNormal=(Cue.Origin-Cue.Target).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    } else if(!Tongue->SurfacePoint(Center,Ground)) {Warning->SetVisibility(false);Magic->SetVisibility(false);Storm->SetVisibility(false);return;}
    FVector Facing=Line || Roll?Cue.Target-Cue.Origin:FVector::ForwardVector;
    if(Rolling) if(const auto* Boss=Cast<AMCNutBoss>(SourceActor)) Facing=Boss->AttackForward;
    const FVector Up=Ground.ImpactNormal;
    FVector Right=FVector::VectorPlaneProject(Facing,Up).GetSafeNormal();
    if(Right.IsNearlyZero()) Right=FVector::VectorPlaneProject(FVector::RightVector,Up).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    const FVector Side=FVector::CrossProduct(Up,Right).GetSafeNormal();
    FMesh Area;
    const float Size=Impact?Cue.Radius*FMath::Lerp(.18f,1.25f,T):Cue.Radius;
    Area.Quad(Ground.ImpactPoint-GetActorLocation()+Up*5,Right,Side,Line?FMath::Max(Cue.Radius,FVector::Dist(Cue.Origin,Cue.Target)*.5f+Cue.Radius):Size,Line?Cue.Radius:Size,FLinearColor(1,1,1,Fade));
    const bool GroundCue=!HandCast && (uint8(Cue.Type)<=uint8(EMCNutCombatCue::RollTell) || Cue.Type==EMCNutCombatCue::DeathBurst);
    Area.Upload(Warning);Warning->SetVisibility(GroundCue && Age<=Total && WarningMID);
    if(WarningMID) {
        WarningMID->SetScalarParameterValue(TEXT("Age"),Age);
        WarningMID->SetScalarParameterValue(TEXT("Progress"),Roll?FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f):T);
        WarningMID->SetScalarParameterValue(TEXT("IsLine"),Line?1:0);WarningMID->SetScalarParameterValue(TEXT("Impact"),Impact?1:0);
        const bool Arcane=Cue.Type==EMCNutCombatCue::NutRain || Cue.Type==EMCNutCombatCue::SummonTell || Cue.Type==EMCNutCombatCue::DeathBurst;
        WarningMID->SetVectorParameterValue(TEXT("Tint"),Cue.Role==EMCNutBossRole::Mage && Arcane?FLinearColor(.58f,.18f,1.f):FLinearColor(1,.56f,.10f));
    }
    const bool Fire=!Cue.bSuppressCosmeticAccents && (Cue.Type==EMCNutCombatCue::FireCast || Cue.Type==EMCNutCombatCue::FireImpact || Cue.Type==EMCNutCombatCue::CastCharge);
    Flames->SetVisibility(Fire && MCNativeArtControls::Enabled(FlameArt) && Age<=Total && FireMID);
    if(Fire && FireMID && MCNativeArtControls::Enabled(FlameArt)) {
        const auto* Caster=Cast<AMCNutBoss>(SourceActor);
        const bool Charging=Cue.Type==EMCNutCombatCue::FireCast || Cue.Type==EMCNutCombatCue::CastCharge;
        Flames->SetRelativeLocation(Charging?(Caster?Caster->GetSpellFocusOrigin():Cue.Origin)-GetActorLocation():Ground.ImpactPoint-GetActorLocation());
        FireMID->SetScalarParameterValue(TEXT("FireAge"),Age);
        const float Charge=FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f);
        const float FlameRadius=Charging
            ?FMath::Lerp(FlameArt?FlameArt->BurstFlame.MinChargeRadius:8.f,FlameArt?FlameArt->BurstFlame.MaxChargeRadius:24.f,Charge)
            :FMath::Min(Cue.Radius*(FlameArt?FlameArt->BurstFlame.ImpactRadiusFraction:.45f),FlameArt?FlameArt->BurstFlame.MaxImpactRadius:65.f);
        MCNativeArtControls::RenderBurstFlames(Flames,FlameArt,FlameRadius,Age,Total,Fade);
    }
    PresentMagic(Age,Ground.ImpactPoint,Up,Right,Side,Fade);
    if(Impact && !Cue.bSuppressCosmeticAccents && !bBurstPresented) {
        bBurstPresented=true;
        // A late joiner observes the remaining ring rather than replaying an old burst.
        if(Age<.25f && Cue.Type!=EMCNutCombatCue::DeathBurst) Burst(Ground.ImpactPoint+Up*10,Fire,FMath::Clamp(Cue.Radius/160.f,.5f,1.5f));
        Debris->SetStaticMesh(MCNativeArtControls::Mesh(ImpactArt,Fire?TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Kernel.SM_Walnut_Kernel")
            :TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
        for(int32 I=0;I<(MCNativeArtControls::Enabled(ImpactArt)?ImpactArt?ImpactArt->ImpactDebris.Count:6:0);++I) Debris->AddInstance(FTransform(FVector::ZeroVector));
    }
    Debris->SetVisibility(Impact && !Cue.bSuppressCosmeticAccents && Age<=Total);
    if(Impact) for(int32 I=0;I<Debris->GetInstanceCount();++I) {
        const float A=I*2.39996f+Cue.Seed*.01f;
        const FVector P=Ground.ImpactPoint-GetActorLocation()+(Right*FMath::Cos(A)+Side*FMath::Sin(A))*Cue.Radius*(ImpactArt?ImpactArt->ImpactDebris.SpreadFraction:.8f)*T+Up*(8+FMath::Sin(T*PI)*(ImpactArt?ImpactArt->ImpactDebris.HopHeight:65.f));
        const float Scale=Cue.Radius*(ImpactArt?ImpactArt->ImpactDebris.SizeFraction:.08f)/MeshRadius(Debris->GetStaticMesh())*(1-T);
        Debris->UpdateInstanceTransform(I,FTransform(FRotator(Age*(ImpactArt?ImpactArt->ImpactDebris.PitchRate:160.f)+I*31,Age*(ImpactArt?ImpactArt->ImpactDebris.YawRate:90.f)+I*60,I*37),P,FVector(Scale)),false,I==Debris->GetInstanceCount()-1,true);
    }
    if(Roll) {
        FMesh Arrow;
        Arrow.Quad(Ground.ImpactPoint-GetActorLocation()+Up*8+Right*Cue.Radius*1.15f,
            Right,Side,Cue.Radius*.9f,Cue.Radius*.3f,FLinearColor(1,1,1,Fade));
        Arrow.Upload(DropWarnings);DropWarnings->SetVisibility(Age<=Total && SmallWarningMID);
        if(SmallWarningMID) {
            SmallWarningMID->SetScalarParameterValue(TEXT("IsLine"),1);
            SmallWarningMID->SetScalarParameterValue(TEXT("Age"),Age);
            SmallWarningMID->SetScalarParameterValue(TEXT("Progress"),T);
            SmallWarningMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.56f,.10f));
        }
        // One sustained emitter supplies dust throughout the roll; no repeated
        // actor/component spawns are needed for movement presentation.
        DropShadows->SetVisibility(false);RainNuts->SetVisibility(false);
        return;
    }
    DropWarnings->SetVisibility(Rain && Age<Total && SmallWarningMID);DropShadows->SetVisibility(Rain && Age<Total && ShadowMID);
    RainNuts->SetVisibility(Rain && Age<Total);
    if(!Rain) return;
    const int32 Count=FMath::Clamp(Cue.DropCount,1,MaxRainDrops);
    if(BuiltDropCount!=Count) {
        RainNuts->ClearInstances();for(int32 I=0;I<Count;++I) RainNuts->AddInstance(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector::ZeroVector));
        BuiltDropCount=Count;
    }
    FMesh Small,Shadows,Streaks;
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
        if(Falling) {
            const float Length=FMath::Lerp(18.f,65.f,P);
            Streaks.Quad(Air+FVector(0,0,Length),FVector::RightVector,FVector::UpVector,2.5f,Length,FLinearColor(1,1,1,.40f+.30f*P));
            Streaks.Quad(Air+FVector(0,0,Length),FVector::ForwardVector,FVector::UpVector,2.5f,Length,FLinearColor(1,1,1,.40f+.30f*P));
        }
        if(Supported && Age>=ImpactAt && (ImpactMask&(1u<<I))==0) {
            ImpactMask|=1u<<I;
            if(Age-ImpactAt<.2f && ImpactAt<=Total) Burst(Location+N*7,false,.55f);
        }
    }
    Small.Upload(DropWarnings);Shadows.Upload(DropShadows);
    if(!Streaks.Points.IsEmpty()) Streaks.Upload(Magic);
    Magic->SetVisibility(!Streaks.Points.IsEmpty() && Age<Total && MagicMID);
    if(MagicMID) {MagicMID->SetScalarParameterValue(TEXT("Beam"),1);MagicMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.65f,.30f,1.f));}
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
    if(GetNetMode()!=NM_DedicatedServer && IsValid(Tongue) && Cue.StartedAt>=0) {
        const float Age=FMath::Max(0.f,float(Now()-Cue.StartedAt));
        LastCueAge=Age;bComposedDecorations=UpdateComposedPresentation(Age);
        Present(Age);
        UpdateAcceptedBursts(Age);
        if(bComposedDecorations)SuppressNativeDecorations();
    }
}

bool AMCNutCombatEffect::UpdateComposedPresentation(float Age)
{
    if(!Presentation || Cue.PresentationSource.IsNone() || Cue.PresentationEvent.IsNone())return false;
    FString Error;
    if(auto* Subject=NutSourceVisual(SourceActor))Presentation->BindSourceAudioSocket(Cue.PresentationSource,Subject,Error);
    if(!MCNutVFX::Prepare(Presentation,Cue.PresentationSource,Error,false))return false;
    const float Total=FMath::Max(.01f,Cue.WindupSeconds+Cue.ActiveSeconds);
    const bool DelayedHeadSplit=Cue.Type==EMCNutCombatCue::KnightHeadSplit;
    const bool Active=Age<Total && (!DelayedHeadSplit || Age>=Cue.WindupSeconds);
    const bool Second=!Cue.AfterWindupEvent.IsNone() && Age>=Cue.WindupSeconds;
    const bool Follow=(Second && (Cue.Type==EMCNutCombatCue::RollTell || Cue.Type==EMCNutCombatCue::ChargeTell))
        || Cue.Type==EMCNutCombatCue::Transform || Cue.Type==EMCNutCombatCue::DeathBurst;
    FVector Point=Follow && IsValid(SourceActor)?SourceActor->GetActorLocation():Cue.Origin;
    FVector Direction=(Cue.Target-Cue.Origin).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    UActorComponent* Prerequisite=nullptr;
    FMCPerkVFXGameplayValues Values;Values.Radius=Cue.Radius;Values.Direction=Direction;Values.VisualSubject=NutSourceVisual(SourceActor);
    Values.bHasEndpoints=true;Values.SourcePosition=Cue.Origin;Values.TargetPosition=Cue.Target;
    Values.bHasSeed=true;Values.Seed=uint32(Cue.Seed);
    const float Duration=DelayedHeadSplit?FMath::Max(.01f,Cue.ActiveSeconds):Cue.AfterWindupEvent.IsNone()?Total:Second?FMath::Max(.01f,Cue.ActiveSeconds):FMath::Max(.01f,Cue.WindupSeconds);
    Values.Duration=Duration;Values.LocalStateAgeSeconds=FMath::Max(0.f,Age-((Second||DelayedHeadSplit)?Cue.WindupSeconds:0.f));
    Values.Fill=FMath::Clamp(Values.LocalStateAgeSeconds/Duration,0.f,1.f);
    if(Cue.Type==EMCNutCombatCue::WizardSpell) {
        const bool bLeft=Cue.PresentationEvent==TEXT("ChargeLeft"),bRight=Cue.PresentationEvent==TEXT("ChargeRight");
        if(bLeft || bRight) {
            auto* Boss=Cast<AMCNutBoss>(SourceActor);
            if(!Boss || !Boss->MageModel) return false;
            const FVector Hand=Boss->GetGravityHandOrigin(bRight);
            const float Transfer=FMath::SmoothStep(.72f,1.f,Values.Fill);
            Point=FMath::Lerp(Hand,Cue.Target,Transfer);
            Direction=(Cue.Target-Point).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
            Values.SourcePosition=Point;
            Values.Direction=Direction;
            Prerequisite=Boss->MageModel.Get();
        }
        return Presentation->UpdateLocalSourceState(Cue.PresentationSource,Cue.PresentationEvent,Active,
            FTransform(Direction.Rotation(),Point),Values,Prerequisite,Error);
    }
    if(Cue.Type==EMCNutCombatCue::GravityHand) {
        auto* Boss=Cast<AMCNutBoss>(SourceActor);
        if(!Boss || !Boss->MageModel)return false;
        const bool bRight=Cue.PresentationEvent==TEXT("LiftRight") || Cue.PresentationEvent==TEXT("DropRight");
        const FVector Hand=Boss->GetGravityHandOrigin(bRight);
        const FVector HandDirection=Cue.Target-Cue.Origin;
        Values.VisualSubject=Boss->MageModel.Get();
        Values.SourcePosition=Hand;
        Values.TargetPosition=Hand+HandDirection;
        Values.Direction=HandDirection.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        return Presentation->UpdateLocalSourceState(Cue.PresentationSource,Cue.PresentationEvent,Active,
            FTransform(Values.Direction.Rotation(),Hand),Values,Boss->MageModel.Get(),Error);
    }
    if(auto* Boss=Cast<AMCNutBoss>(SourceActor)) {
        Values.VisualSubject=Boss->BossRole==EMCNutBossRole::Mage?static_cast<USceneComponent*>(Boss->MageModel.Get()):static_cast<USceneComponent*>(Boss->TankModel.Get());
        Prerequisite=Values.VisualSubject.Get();
        if(Cue.Type==EMCNutCombatCue::RitualCast)Point=Boss->GetSpellFocusOrigin();
        if(Second&&(Cue.Type==EMCNutCombatCue::RollTell||Cue.Type==EMCNutCombatCue::ChargeTell))Values.Direction=Boss->AttackForward.GetSafeNormal(SMALL_NUMBER,Values.Direction);
    }
    // Accepted native state only. The typed namespace is never an artist override.
    const auto AddNativeFloat=[&](const TCHAR* Name,float Value) {
        FMCVFXNativeInput Input;Input.Name=FName(Name);Input.Type=EMCVFXNativeInputType::Float;
        Input.FloatValue=Value;Values.NativeInputs.Entries.Add(MoveTemp(Input));
    };
    const auto AddNativeBool=[&](const TCHAR* Name,bool Value) {
        FMCVFXNativeInput Input;Input.Name=FName(Name);Input.Type=EMCVFXNativeInputType::Bool;
        Input.BoolValue=Value;Values.NativeInputs.Entries.Add(MoveTemp(Input));
    };
    const auto AddNativeVector=[&](const TCHAR* Name,const FVector& Value,bool bPosition) {
        FMCVFXNativeInput Input;Input.Name=FName(Name);
        Input.Type=bPosition?EMCVFXNativeInputType::Position:EMCVFXNativeInputType::Vector;
        if(bPosition)Input.PositionValue=Value;else Input.VectorValue=Value;
        Values.NativeInputs.Entries.Add(MoveTemp(Input));
    };
    AddNativeFloat(TEXT("User.Native.CueAge"),Age);
    AddNativeFloat(TEXT("User.Native.WindupSeconds"),Cue.WindupSeconds);
    AddNativeFloat(TEXT("User.Native.ActiveSeconds"),Cue.ActiveSeconds);
    // These fallbacks are the same accepted body/focus inputs used by PresentMagic,
    // not a newly invented ground or another gameplay/target/RNG decision.
    const FVector ActorPoint=IsValid(SourceActor)?SourceActor->GetActorLocation():Cue.Origin;
    AddNativeVector(TEXT("User.Native.ActorPosition"),ActorPoint,true);
    if(IsValid(SourceActor)) {
        AddNativeVector(TEXT("User.Native.ActorForward"),SourceActor->GetActorForwardVector(),false);
        AddNativeVector(TEXT("User.Native.ActorRight"),SourceActor->GetActorRightVector(),false);
    }
    const auto* Boss=Cast<AMCNutBoss>(SourceActor);
    AddNativeVector(TEXT("User.Native.SpellFocusPosition"),Boss?Boss->GetSpellFocusOrigin():Cue.Origin,true);
    const bool Roll=Cue.Type==EMCNutCombatCue::RollTell;
    const bool Line=Cue.Type==EMCNutCombatCue::ChargeTell;
    const bool Motion=Line&&Age>=Cue.WindupSeconds;
    const bool Palm=Cue.Type==EMCNutCombatCue::RitualCast;
    const bool Melee=Cue.Type==EMCNutCombatCue::MeleeTell||Cue.Type==EMCNutCombatCue::MeleeSlash;
    const bool Teleport=Cue.Type==EMCNutCombatCue::TeleportTell||Cue.Type==EMCNutCombatCue::TeleportBurst;
    const FVector SurfaceCenter=Melee||Teleport?Cue.Origin:
        (Roll||Cue.Type==EMCNutCombatCue::Transform||Cue.Type==EMCNutCombatCue::DeathBurst)&&IsValid(SourceActor)
        ?ActorPoint:Line?(Cue.Origin+Cue.Target)*.5f:Cue.Target;
    FHitResult Floor;bool HasGround=false;
    if(Cue.Type==EMCNutCombatCue::FireImpact) {
        // Present() already uses this accepted impact plane, including the
        // Kamikaze's body-centre blast. Do not silently move it to a new floor.
        Floor.ImpactPoint=Cue.Target;
        Floor.ImpactNormal=(Cue.Origin-Cue.Target).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        HasGround=true;
    } else HasGround=Tongue&&Tongue->SurfacePoint(SurfaceCenter,Floor);
    AddNativeBool(TEXT("User.Native.bHasGround"),HasGround);
    if(HasGround) {
        AddNativeVector(TEXT("User.Native.GroundPosition"),Floor.ImpactPoint,true);
        AddNativeVector(TEXT("User.Native.GroundNormal"),Floor.ImpactNormal,false);
        FVector Facing=Line||Roll?Cue.Target-Cue.Origin:FVector::ForwardVector;
        if(Roll&&Age>=Cue.WindupSeconds&&Boss)Facing=Boss->AttackForward;
        FVector Right=FVector::VectorPlaneProject(Facing,Floor.ImpactNormal).GetSafeNormal();
        if(Right.IsNearlyZero())Right=FVector::VectorPlaneProject(FVector::RightVector,Floor.ImpactNormal).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        AddNativeVector(TEXT("User.Native.SurfaceRight"),Right,false);
        AddNativeVector(TEXT("User.Native.SurfaceSide"),FVector::CrossProduct(Floor.ImpactNormal,Right).GetSafeNormal(),false);
    }
    FHitResult TargetFloor;const bool HasTargetGround=Tongue&&Tongue->SurfacePoint(Cue.Target,TargetFloor);
    AddNativeBool(TEXT("User.Native.bHasTargetGround"),HasTargetGround);
    if(HasTargetGround) {
        AddNativeVector(TEXT("User.Native.TargetGroundPosition"),TargetFloor.ImpactPoint,true);
        AddNativeVector(TEXT("User.Native.TargetGroundNormal"),TargetFloor.ImpactNormal,false);
    }
    // Knight melee's secondary floor ring samples in front of the contact. It is
    // not interchangeable with the general cue's tongue sample.
    FHitResult ContactGround;bool HasContactGround=false;
    if(Melee&&Cue.Role==EMCNutBossRole::Tank) {
        const FVector Forward=(Cue.Target-Cue.Origin).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
        HasContactGround=Tongue&&Tongue->SurfacePoint(Cue.Origin+Forward*Cue.Radius*.48f,ContactGround);
        AddNativeBool(TEXT("User.Native.bHasContactGround"),HasContactGround);
        if(HasContactGround) {
            const FVector Right=FVector::VectorPlaneProject(Forward,ContactGround.ImpactNormal).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
            AddNativeVector(TEXT("User.Native.ContactGroundPosition"),ContactGround.ImpactPoint,true);
            AddNativeVector(TEXT("User.Native.ContactGroundNormal"),ContactGround.ImpactNormal,false);
            AddNativeVector(TEXT("User.Native.ContactGroundRight"),Right,false);
            AddNativeVector(TEXT("User.Native.ContactGroundSide"),FVector::CrossProduct(ContactGround.ImpactNormal,Right).GetSafeNormal(SMALL_NUMBER,FVector::RightVector),false);
        }
    }
    // Preserve each actual native anchor. Principal shape geometry reads its
    // own typed points; original motion dust keeps its precise source frame.
    if((Roll||Motion||Cue.Type==EMCNutCombatCue::Transform)&&IsValid(SourceActor)) {
        const FVector Forward=Boss&&(Roll||Motion)?Boss->AttackForward:Values.Direction;
        Point=ActorPoint-Forward*Cue.Radius*.6f-FVector(0,0,Cue.Radius*.3f);Direction=Forward;
    } else if(!Palm&&!DelayedHeadSplit&&!Melee&&!Teleport&&HasGround) {
        Point=Floor.ImpactPoint;Direction=Floor.ImpactNormal;
    }
    // Missing ground values are intentionally OMITTED. NativeInputs validation
    // rejects a custom system that requires them; no made-up plane is supplied.
    FVector NativeDonorScale=FVector::OneVector;
    if(Cue.Type==EMCNutCombatCue::SlamImpact||Cue.Type==EMCNutCombatCue::EntranceImpact||Cue.Type==EMCNutCombatCue::FireImpact) {
        if(HasGround) {Point=Floor.ImpactPoint+Floor.ImpactNormal*10.f;Direction=Floor.ImpactNormal;}
        NativeDonorScale=FVector(FMath::Clamp(Cue.Radius/160.f,.5f,1.5f));
    } else if(Cue.Type==EMCNutCombatCue::MeleeSlash&&Cue.Role==EMCNutBossRole::Tank&&HasContactGround) {
        Point=ContactGround.ImpactPoint+ContactGround.ImpactNormal*8.f;Direction=ContactGround.ImpactNormal;
        NativeDonorScale=FVector(.35f);
    } else if(Cue.Type==EMCNutCombatCue::DeathBurst&&HasGround) {
        Point=Floor.ImpactPoint+Floor.ImpactNormal*15.f;Direction=Floor.ImpactNormal;
        NativeDonorScale=FVector(1.25f);
    } else if(Cue.Type==EMCNutCombatCue::SummonTell&&Second&&HasGround) {
        Point=Floor.ImpactPoint+Floor.ImpactNormal*16.f;Direction=Floor.ImpactNormal;
        NativeDonorScale=FVector(.65f);
    } else if(DelayedHeadSplit) {
        Point=Cue.Origin;Direction=FVector::UpVector;NativeDonorScale=FVector(1.35f);
    } else if(Cue.Type==EMCNutCombatCue::TeleportBurst) {
        Point=Cue.Origin;Direction=FVector::UpVector;NativeDonorScale=FVector(.7f);
    } else if(Cue.Type==EMCNutCombatCue::NutRain&&HasGround) {
        Point=Floor.ImpactPoint+FVector(0,0,480);Direction=Values.Direction;
    }
    const FTransform Frame(Direction.Rotation(),Point,NativeDonorScale);
    LastCueNativeInputs=Values.NativeInputs;
    FString FirstError,SecondError;
    const auto A=Presentation->UpdateLocalSourceState(Cue.PresentationSource,Cue.PresentationEvent,Active&&!Second,Frame,Values,Prerequisite,FirstError);
    bool Result=A;Error=FirstError;
    if(!Cue.AfterWindupEvent.IsNone()) {
        const auto B=Presentation->UpdateLocalSourceState(Cue.PresentationSource,Cue.AfterWindupEvent,Active&&Second,Frame,Values,Prerequisite,SecondError);
        if(Second){Result=B;Error=SecondError;}
    }
    if(Active&&!Result&&!Error.IsEmpty()&&LastNativeInputError!=Error) {
        LastNativeInputError=Error;
        UE_LOG(LogTemp,Warning,TEXT("Nut VFX %s.%s kept native presentation: %s"),
            *Cue.PresentationSource.ToString(),*(Second?Cue.AfterWindupEvent:Cue.PresentationEvent).ToString(),*Error);
    } else if(Result) LastNativeInputError.Reset();
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    const FName Event=Second?Cue.AfterWindupEvent:Cue.PresentationEvent;
    Presentation->GetPreparedEventMode(Cue.PresentationSource,Event,Mode);
    const auto* Native=Presentation->GetPreparedNativeSource(Cue.PresentationSource);
    return Active && Result && Mode!=EMCPerkVFXSlotMode::UseDefault
        && !(Native&&Native->ShouldRenderNativeAccents(Event,Mode));
}

void AMCNutCombatEffect::SuppressNativeDecorations()
{
    // Gameplay-owned telegraphs/shadows keep their actual shape, clock and footprint.
    Magic->SetVisibility(false);Flames->SetVisibility(false);Storm->SetVisibility(false);
    Debris->SetVisibility(false);RainNuts->SetVisibility(false);SustainedParticles->Deactivate();
    for(UNiagaraComponent* Burst:Bursts)
        if(IsValid(Burst)&&!Burst->ComponentHasTag(TEXT("MC.Nut.AcceptedExtraBurst")))Burst->DeactivateImmediate();
}

void AMCNutCombatEffect::Cancel() {if(HasAuthority()) Destroy();}

void AMCNutCombatEffect::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Presentation&&!Cue.PresentationSource.IsNone()) {
        FString Error;FMCPerkVFXGameplayValues Values;
        if(!Cue.PresentationEvent.IsNone())Presentation->UpdateLocalSourceState(Cue.PresentationSource,Cue.PresentationEvent,false,FTransform::Identity,Values,nullptr,Error);
        if(!Cue.AfterWindupEvent.IsNone())Presentation->UpdateLocalSourceState(Cue.PresentationSource,Cue.AfterWindupEvent,false,FTransform::Identity,Values,nullptr,Error);
        Presentation->StopLocalSourceStates(Cue.PresentationSource);
    }
    SustainedParticles->DeactivateImmediate();
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
    struct FEffect
    {
        const TCHAR* Name;
        const TCHAR* MaterialName;
        bool bRate;
        int32 Amount;
        float LifetimeMin,LifetimeMax,Size,SpeedMin,SpeedMax,Gravity,Radius,Angle;
        FVector3f Direction;
        bool bLocalVelocity;
    };
    const FEffect Effects[]={
        {TEXT("NS_NutDustImpact"),TEXT("M_NutDust"),false,24,.45f,.85f,30,50,160,-80,5,75,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutFireImpact"),TEXT("M_NutEmber"),false,32,.18f,.55f,9,30,280,-450,5,75,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutEmberTrail"),TEXT("M_NutEmber"),true,36,.18f,.38f,7,30,70,40,12,75,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutArcaneBurst"),TEXT("M_NutArcaneSpark"),false,28,.22f,.65f,10,80,240,0,8,28,FVector3f::XAxisVector,true},
        {TEXT("NS_NutCastCharge"),TEXT("M_NutArcaneSpark"),true,24,.25f,.55f,5,8,24,0,32,120,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutMotionDust"),TEXT("M_NutDust"),true,28,.25f,.65f,22,20,70,15,12,45,-FVector3f::XAxisVector,true},
        {TEXT("NS_NutStormMotes"),TEXT("M_NutArcaneSpark"),true,24,.35f,.85f,3,150,250,-180,120,16,-FVector3f::ZAxisVector,false},
        {TEXT("NS_NutShieldHit"),TEXT("M_NutEmber"),false,18,.15f,.50f,5,100,230,-180,6,70,FVector3f::XAxisVector,true}
    };
    for(const FEffect& Effect:Effects) {
        const FString Name=Effect.Name;
        const FString Path=TEXT("/Game/Gameplay/VFX/NutCombat/")+Name;
        if(FPackageName::DoesPackageExist(Path)) continue;
        const FString MaterialPath=FString(TEXT("/Game/Gameplay/VFX/NutCombat/"))+Effect.MaterialName+TEXT(".")+Effect.MaterialName;
        auto* Parent=LoadObject<UMaterial>(nullptr,*MaterialPath);
        if(!Parent) return false;
        auto* Package=CreatePackage(*Path);auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,*Name);
        System->SetFlags(RF_Public|RF_Standalone);System->ClearFlags(RF_Transient);
        auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter();if(!Emitter) return false;
        const auto* StateProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("EmitterState"));
        if(!StateProperty) return false;
        auto* EmitterState=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);
        EmitterState->LoopBehavior=Effect.bRate?ENiagaraLoopBehavior::Infinite:ENiagaraLoopBehavior::Once;
        EmitterState->LoopDurationMode=ENiagaraLoopDurationMode::Fixed;
        EmitterState->LoopDuration=FNiagaraDistributionRangeFloat(1.f);
        auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
        auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
        auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
        auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()));
        if(!Init || !Velocity || !Shape || !Gravity) return false;
        for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {
            auto* Spawn=Emitter->GetSpawnInfoByIndex(I);Spawn->bEnabled=I==0;Spawn->SpawnTime=0;Spawn->bSpawnProbabilityEnabled=false;
            Spawn->Type=Effect.bRate?ENiagaraStatelessSpawnInfoType::Rate:ENiagaraStatelessSpawnInfoType::Burst;
            Spawn->Amount=FNiagaraDistributionRangeInt(Effect.Amount);Spawn->Rate=FNiagaraDistributionRangeFloat(float(Effect.Amount));
            Spawn->bLoopCountLimitEnabled=!Effect.bRate;Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1);
        }
        Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(Effect.LifetimeMin,Effect.LifetimeMax);
        Init->SpriteSizeDistribution.InitConstant(FVector2f(Effect.Size));
        Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360);
        Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor::White);
        Shape->SetIsModuleEnabled(true);Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere;Shape->SphereRadius=FNiagaraDistributionRangeFloat(0,Effect.Radius);
        Velocity->SetIsModuleEnabled(true);Velocity->VelocityType=ENSM_VelocityType::InCone;
        Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;Velocity->ConeDirection.InitConstant(Effect.Direction);
        Velocity->CoordinateSpace=Effect.bLocalVelocity?ENiagaraCoordinateSpace::Local:ENiagaraCoordinateSpace::World;
        Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(Effect.SpeedMin,Effect.SpeedMax);Velocity->ConeAngle=Effect.Angle;
        Gravity->SetIsModuleEnabled(true);Gravity->GravityDistribution.InitConstant(FVector3f(0,0,Effect.Gravity));
        if(auto* Scale=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass()))) {
            Scale->SetIsModuleEnabled(true);Scale->ScaleDistribution.InitConstant(FVector2f(1.f));
        }
        if(auto* Scale=Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass())) Scale->SetIsModuleEnabled(false);
        if(auto* Curl=Emitter->GetModule(UNiagaraStatelessModule_CurlNoiseForce::StaticClass())) Curl->SetIsModuleEnabled(false);
        if(auto* Drag=Cast<UNiagaraStatelessModule_Drag>(Emitter->GetModule(UNiagaraStatelessModule_Drag::StaticClass()))) {
            Drag->SetIsModuleEnabled(true);Drag->DragDistribution=FNiagaraDistributionRangeFloat(FCString::Strcmp(Effect.MaterialName,TEXT("M_NutDust"))==0?2.f:.8f);
        }
        const auto Old=Emitter->GetRenderers();for(auto* Renderer:Old) Emitter->RemoveRenderer(Renderer,FGuid());
        auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(Emitter,NAME_None,RF_Transactional);Renderer->Material=Parent;Emitter->AddRenderer(Renderer,FGuid());
        System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-350),FVector(350)));
        System->MaxPoolSize=16;System->PoolPrimeSize=0;
        Emitter->PostEditChange();System->PostEditChange();System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
        FAssetRegistryModule::AssetCreated(System);System->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;Save.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return false;
    }
    return true;
#else
    return false;
#endif
}
