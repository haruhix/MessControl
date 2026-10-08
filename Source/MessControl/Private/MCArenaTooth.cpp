#include "MCArenaTooth.h"
#include "MCToothStatusComponent.h"
#include "MCToothCalculusComponent.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

void FMCArenaToothSettings::Sanitize()
{
    auto Safe=[](float V,float Default,float Min,float Max){ return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default; };
    MaxHealth=Safe(MaxHealth,100,1,10000); LooseHealthFraction=Safe(LooseHealthFraction,0.4f,0,1);
    BrushHitDamage=Safe(BrushHitDamage,25,0,10000); HitSquash=Safe(HitSquash,0.18f,0,0.4f);
    WobbleDegrees=Safe(WobbleDegrees,12,0,25); ReactionSeconds=Safe(ReactionSeconds,0.8f,0.2f,3);
    FallAnticipation=Safe(FallAnticipation,0.25f,0,1); FallLift=Safe(FallLift,310,0,1000); FallSpeed=Safe(FallSpeed,220,0,1000);
    PianoPressDepth=Safe(PianoPressDepth,10,0,25); PianoPressSeconds=Safe(PianoPressSeconds,0.36f,0.15f,1);
    InitialCalculusEveryNthTooth=FMath::Clamp(InitialCalculusEveryNthTooth,0,28);
    InitialCalculusPatchCount=FMath::Clamp(InitialCalculusPatchCount,1,6);
}
AMCArenaTooth::AMCArenaTooth()
{
    PrimaryActorTick.bCanEverTick=true; bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true);
    SetNetUpdateFrequency(12); SetMinNetUpdateFrequency(2);
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    Status=CreateDefaultSubobject<UMCToothStatusComponent>(TEXT("ToothStatus"));
    Body=CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicalBody")); SetRootComponent(Body);
    Body->SetBoxExtent(FVector(48,48,78)); Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Body->SetNotifyRigidBodyCollision(true); Body->SetLinearDamping(0.6f); Body->SetAngularDamping(1.5f);
    Body->BodyInstance.bUseCCD=true;
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnimatedEnamel")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    BrushSurface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BrushSurface")); BrushSurface->SetupAttachment(Visual);
    BrushSurface->SetVisibility(false); BrushSurface->SetHiddenInGame(true); BrushSurface->SetCastShadow(false);
    BrushSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly); BrushSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
    BrushSurface->SetGenerateOverlapEvents(false);
    GrimeRelief=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GrimeClumps")); GrimeRelief->SetupAttachment(Visual);
    GrimeRelief->SetCollisionEnabled(ECollisionEnabled::NoCollision); GrimeRelief->SetCastShadow(false);
    GrimeRelief->SetCanEverAffectNavigation(false);
    Calculus=CreateDefaultSubobject<UMCToothCalculusComponent>(TEXT("DentalCalculus"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Art/Meshes/SM_ToothProp"));
    if (Mesh.Succeeded()) { Visual->SetStaticMesh(Mesh.Object); Appearance.Mesh=Mesh.Object; }
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ToothIdentity")); Label->SetupAttachment(Body);
    Label->SetRelativeLocation(FVector(0,0,112)); Label->SetWorldSize(19); Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetTextRenderColor(FColor(135,255,218)); Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<USoundBase> Piano(TEXT("/Game/Audio/S_ToothPianoC4"));
    if (Piano.Succeeded()) PianoSound=Piano.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> PianoC5(TEXT("/Game/Audio/S_ToothPianoC5"));
    static ConstructorHelpers::FObjectFinder<USoundBase> PianoC6(TEXT("/Game/Audio/S_ToothPianoC6"));
    static ConstructorHelpers::FObjectFinder<USoundBase> PianoC7(TEXT("/Game/Audio/S_ToothPianoC7"));
    PianoUpperOctaves={PianoC5.Object,PianoC6.Object,PianoC7.Object};
    PianoAttenuation=CreateDefaultSubobject<USoundAttenuation>(TEXT("PianoAttenuation"));
    PianoAttenuation->Attenuation.bAttenuate=true;
    PianoAttenuation->Attenuation.bSpatialize=true;
    PianoAttenuation->Attenuation.AttenuationShapeExtents=FVector(250,0,0);
    PianoAttenuation->Attenuation.FalloffDistance=2800;
}
void AMCArenaTooth::Initialize(int32 Id,const FMCArenaToothSettings& Defaults)
{
    if (!HasAuthority()) return;
    Settings=Defaults; Settings.Sanitize(); State.ToothId=Id; State.Health=Settings.MaxHealth;
    Status->Initialize(Settings.MaxHealth);
    if (HasActorBegunPlay()) SeedInitialCalculus();
}
void AMCArenaTooth::BeginPlay()
{
    Super::BeginPlay();
    TInlineComponentArray<UPrimitiveComponent*> Colliders(this);
    for (auto* Collider:Colliders) Collider->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    Body->OnComponentHit.AddDynamic(this,&AMCArenaTooth::OnBodyHit);
    Body->SetMassOverrideInKg(NAME_None,14,true);
    ApplyAppearance();
    SeedInitialCalculus();
    // A new/late-joining client should not replay an old note from its initial snapshot.
    PlayedPianoSerial=PianoState.Serial;
}
int32 AMCArenaTooth::PianoMidiNote() const
{
    static constexpr int32 MajorScale[]={0,2,4,5,7,9,11};
    const int32 Key=FMath::Clamp(State.ToothId-1,0,27);
    return 60+12*(Key/7)+MajorScale[Key%7];
}
float AMCArenaTooth::PianoPitchScale() const
{
    // One base sample per octave keeps every note inside the mixer pitch limits.
    return FMath::Pow(2.f,((PianoMidiNote()-60)%12)/12.f);
}
USoundBase* AMCArenaTooth::PianoNoteSound() const
{
    const int32 Octave=(PianoMidiNote()-60)/12;
    return Octave==0?PianoSound.Get():PianoUpperOctaves.IsValidIndex(Octave-1)?PianoUpperOctaves[Octave-1].Get():nullptr;
}
float AMCArenaTooth::PianoOffset() const
{
    if (!Settings.bPianoEnabled || !IsAvailable() || PianoState.Serial==0) return 0;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float T=(Now-PianoState.PressedAt)/FMath::Max(0.15f,Settings.PianoPressSeconds);
    if (T<0 || T>=1) return 0;
    // Quick key-down, a brief bottom stop, then a soft spring return.
    const float Press=T<0.15f?FMath::SmoothStep(0.f,0.15f,T):T<0.3f?1.f:1-FMath::SmoothStep(0.3f,1.f,T);
    return Settings.PianoPressDepth*PianoState.Strength*Press;
}
bool AMCArenaTooth::NotifyPianoLanding(AMCToothCharacter* Worker,const FHitResult& Hit,float DownSpeed)
{
    if (!HasAuthority() || !Worker || !Worker->HasAuthority() || !Settings.bPianoEnabled || !IsAvailable()
        || !Hit.IsValidBlockingHit() || Hit.GetActor()!=this || Hit.GetComponent()!=Body
        || Hit.ImpactNormal.Z<0.5f || !FMath::IsFinite(DownSpeed) || DownSpeed<40) return false;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    // Ignore contact jitter; normal consecutive jumps and neighbouring keys remain independent.
    if (Now-PianoState.PressedAt<0.12) return false;
    PianoState.PressedAt=Now;
    PianoState.Strength=FMath::GetMappedRangeValueClamped(FVector2D(40,700),FVector2D(0.75,1),DownSpeed);
    ++PianoState.Serial;
    OnRep_Piano(); ForceNetUpdate();
    return true;
}
void AMCArenaTooth::OnRep_Piano()
{
    if (!HasActorBegunPlay() || PianoState.Serial==PlayedPianoSerial) return;
    PlayedPianoSerial=PianoState.Serial;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    auto* Sound=PianoNoteSound();
    if (!IsAvailable() || !Settings.bPianoEnabled || !Sound || GetNetMode()==NM_DedicatedServer
        || Now-PianoState.PressedAt>1) return;
    UGameplayStatics::PlaySoundAtLocation(this,Sound,Visual->Bounds.Origin,0.65f*PianoState.Strength,PianoPitchScale(),0,PianoAttenuation);
}
void AMCArenaTooth::SetAppearance(UStaticMesh* Mesh,FVector Scale,UMaterialInterface* GameplayMaterial)
{
    if (!HasAuthority() || !Mesh || Scale.ContainsNaN()) return;
    Appearance.Mesh=Mesh; Appearance.MeshScale=Scale.GetAbs().ComponentMax(FVector(0.01));
    Appearance.GameplayMaterial=GameplayMaterial;
}
void AMCArenaTooth::ApplyAppearance()
{
    if (!Appearance.Mesh) return;
    Visual->SetStaticMesh(Appearance.Mesh);
    BrushSurface->SetStaticMesh(Appearance.Mesh);
    GrimeRelief->ClearAllMeshSections();
    GrimeSamples.Reset();
    bGrimeReliefBuilt=false;
    const FBoxSphereBounds Bounds=Appearance.Mesh->GetBounds();
    MeshBaseScale=Appearance.MeshScale;
    MeshBaseLocation=-Bounds.Origin*MeshBaseScale;
    Visual->SetRelativeScale3D(MeshBaseScale); Visual->SetRelativeLocation(MeshBaseLocation);
    Body->SetBoxExtent(Bounds.BoxExtent*MeshBaseScale*0.9f);
    Label->SetRelativeLocation(FVector(0,0,Bounds.BoxExtent.Z*MeshBaseScale.Z+35));
    UMaterialInterface* Base=Appearance.GameplayMaterial;
    if (!Base) Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_ArenaTooth.M_ArenaTooth"));
    if (Base && (!Material || Material->Parent!=Base)) Material=UMaterialInstanceDynamic::Create(Base,this);
    if (Material)
    {
        for (int32 I=0;I<Visual->GetNumMaterials();++I) Visual->SetMaterial(I,Material);
        Material->SetVectorParameterValue(TEXT("GrimeMin"),FLinearColor(Bounds.Origin-Bounds.BoxExtent));
        Material->SetVectorParameterValue(TEXT("GrimeSize"),FLinearColor(Bounds.BoxExtent*2));
        Material->SetVectorParameterValue(TEXT("GrimeScale"),FLinearColor(MeshBaseScale));
    }
    if (Calculus) Calculus->RebuildForSurface();
}
void AMCArenaTooth::SeedInitialCalculus()
{
    if (!HasAuthority() || bInitialCalculusSeeded || State.ToothId<=0 || !Calculus || !Appearance.Mesh) return;
    bInitialCalculusSeeded=true;
    if (Settings.InitialCalculusEveryNthTooth>0 && State.ToothId%Settings.InitialCalculusEveryNthTooth==0)
        Calculus->GrowCalculus(State.ToothId*941+137,Settings.InitialCalculusPatchCount);
}
bool AMCArenaTooth::ReceiveArenaHit(float Damage,FVector Direction)
{
    if (!HasAuthority() || !IsAvailable() || !FMath::IsFinite(Damage) || Damage<=0 || Direction.ContainsNaN()) return false;
    return Status->Damage(Damage,Direction);
}
bool AMCArenaTooth::IsLoose() const { return IsAvailable() && Status->IsLoose(); }
void AMCArenaTooth::StatusChanged()
{
    if (!HasAuthority()) return;
    // Compatibility snapshot for the original arena demo; Status owns all gameplay values.
    State.Health=Status->State.Health; State.Coffee=Status->CoffeeAmount();
    State.bLost=State.Health<=0;
    if (Status->State.bCareReaction) { ForceNetUpdate(); return; }
    State.HitDirection=Status->LastDamageDirection.GetSafeNormal2D();
    if (State.HitDirection.IsNearlyZero()) State.HitDirection=FVector(0,1,0);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    State.HitTime=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds(); ++State.HitSerial;
    ForceNetUpdate();
}
bool AMCArenaTooth::ConsumeForRespawn()
{
    if (!HasAuthority() || !IsAvailable()) return false;
    State.bConsumed=true; SetActorHiddenInGame(true); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ForceNetUpdate(); return true;
}
void AMCArenaTooth::SetCoffee(float Amount)
{
    if (!HasAuthority() || !IsAvailable() || !FMath::IsFinite(Amount)) return;
    Status->ApplyCoffee(Amount);
}
void AMCArenaTooth::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (State.bConsumed)
    {
        SetActorHiddenInGame(true); Body->SetSimulatePhysics(false); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); return;
    }
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Since=FMath::Max(0.,Now-State.HitTime);
    const float T=Since/Settings.ReactionSeconds;
    const float Envelope=T<1?FMath::Exp(-T*5):0;
    const float Squash=Settings.HitSquash*Envelope*FMath::Cos(T*PI*4);
    const float Wobble=IsLoose()?Settings.WobbleDegrees*FMath::Sin(Now*5.5):0;
    if (!bFallStarted)
    {
        Visual->SetRelativeScale3D(MeshBaseScale*FVector(1+Squash*0.5,1+Squash*0.5,1-Squash));
        const FVector KeyDown=GetActorQuat().UnrotateVector(FVector(0,0,-PianoOffset()));
        Visual->SetRelativeLocation(MeshBaseLocation+FVector(0,0,-Squash*50)+KeyDown);
        const FVector Local=GetActorRotation().UnrotateVector(State.HitDirection);
        const float Kick=Settings.WobbleDegrees*2*Envelope*FMath::Sin(T*PI*5);
        Visual->SetRelativeRotation(FRotator(Kick*Local.X+Wobble*0.25,0,-Kick*Local.Y+Wobble));
        if (State.bLost && Since>=Settings.FallAnticipation)
        {
            bFallStarted=true; Label->SetVisibility(false);
            Visual->SetRelativeScale3D(MeshBaseScale); Visual->SetRelativeLocation(MeshBaseLocation); Visual->SetRelativeRotation(FRotator::ZeroRotator);
            Body->SetSimulatePhysics(true);
            if (HasAuthority())
            {
                // Authored roots sit slightly inside the gum. Extract them before the physics launch.
                SetActorLocation(GetActorLocation()+FVector(0,0,25),false,nullptr,ETeleportType::TeleportPhysics);
                Body->AddImpulse(State.HitDirection*Settings.FallSpeed+FVector(0,0,Settings.FallLift),NAME_None,true);
                Body->AddAngularImpulseInDegrees(FVector(-State.HitDirection.Y,State.HitDirection.X,0.15f)*220,NAME_None,true);
                ForceNetUpdate();
            }
        }
    }
    if (Material)
    {
        Material->SetScalarParameterValue(TEXT("Coffee"),State.Coffee);
        Material->SetScalarParameterValue(TEXT("Damage"),1-State.Health/Settings.MaxHealth);
        Material->SetScalarParameterValue(TEXT("HitFlash"),Since<0.16f?(1-Since/0.16f):0);
    }
    UpdateGrime(DeltaSeconds);
    Label->SetVisibility(bShowCareLabel && !State.bLost);
    Label->SetText(FText::FromString(FString::Printf(TEXT("%02d | HP %.0f\nBRUSH %d/%d | REPAIR %d"),State.ToothId,State.Health,Status->State.CoffeeLeft,Status->State.CoffeeTotal,Status->State.RepairLeft)));
    Label->SetTextRenderColor(IsLoose()?FColor(255,177,69):FColor(135,255,218));
    if (const APlayerController* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCArenaTooth::OnBodyHit(UPrimitiveComponent*,AActor* OtherActor,UPrimitiveComponent* Other,FVector Impulse,const FHitResult& Hit)
{
    if (Cast<AMCFoodActor>(OtherActor)) return; // Food owns its per-target hit cooldown.
    if (!HasAuthority() || !IsAvailable() || !Other || !Other->IsSimulatingPhysics()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    // Normal impulse gives the approaching speed even when Chaos already stopped the other body.
    const float Speed=Impulse.Size()/FMath::Max(1.f,Other->GetMass());
    if (Speed<230 || Now-LastPhysicsHit<0.6) return;
    LastPhysicsHit=Now; ReceiveArenaHit(FMath::Clamp(Speed*0.05f,10.f,50.f),-Hit.ImpactNormal);
}
void AMCArenaTooth::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCArenaTooth,State); DOREPLIFETIME(AMCArenaTooth,Settings);
    DOREPLIFETIME(AMCArenaTooth,Appearance);
    DOREPLIFETIME(AMCArenaTooth,PianoState);
    DOREPLIFETIME(AMCArenaTooth,GrimeMask); DOREPLIFETIME(AMCArenaTooth,GrimeAmount);
    DOREPLIFETIME(AMCArenaTooth,BrushLocal); DOREPLIFETIME(AMCArenaTooth,BrushAt);
}
