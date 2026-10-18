#include "MCCataclysmJelly.h"

#include "MCCataclysmEvent.h"
#include "MCCombatFeedbackComponent.h"
#include "MCDirtPlacement.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCCataclysmJelly::AMCCataclysmJelly()
{
    PrimaryActorTick.TickInterval=.025f;
    Settings.MaxHealth=360.f;
    Settings.MoveSpeed=190.f;
    Settings.BodyRadius=72.f;
    Settings.AttackRange=140.f;
    Settings.AttackDamage=0.f;
    Settings.HopHeight=6.f;
    Health=Settings.MaxHealth;
    MeshScale=FVector(1.68f,1.58f,1.30f);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    NutMesh=Sphere.Object;
    Visual->SetBoundsScale(1.2f);
    JellyMesh=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CoffeeJellyMesh"));
    JellyMesh->SetupAttachment(Body);
    JellyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    JellyMesh->SetCanEverAffectNavigation(false);
    JellyMesh->SetVisibility(false);
    // The authored pre-rupture balloon extends beyond the rest-pose bounds.
    JellyMesh->SetBoundsScale(3.f);
    JellyMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    JellyMesh->AddTickPrerequisiteActor(this);
    CoffeeMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Cataclysm/Jelly/Coffee/SK_CoffeeSlime.SK_CoffeeSlime")));
    const auto CoffeeClip=[](const TCHAR* Name)
    {
        return TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FString::Printf(
            TEXT("/Game/Cataclysm/Jelly/Coffee/Animations/A_CoffeeSlime_%s.A_CoffeeSlime_%s"),Name,Name)));
    };
    CoffeeIdle=CoffeeClip(TEXT("Idle"));
    CoffeeHop=CoffeeClip(TEXT("Hop"));
    CoffeeHurt=CoffeeClip(TEXT("Hurt"));
    CoffeePreBurst=CoffeeClip(TEXT("PreBurst"));
    CoffeeBurst=CoffeeClip(TEXT("Burst"));
    GroundBurst=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JellyGroundBurst"));
    GroundBurst->SetupAttachment(Body);
    GroundBurst->SetStaticMesh(Sphere.Object);
    GroundBurst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundBurst->SetCanEverAffectNavigation(false);
    GroundBurst->SetCastShadow(false);
    GroundBurst->SetVisibility(false);
    for(int32 Index=0;Index<14;++Index)
    {
        const FName Name(*FString::Printf(TEXT("JellyDroplet_%02d"),Index));
        auto* Droplet=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Droplet->SetupAttachment(Body);
        Droplet->SetStaticMesh(Sphere.Object);
        Droplet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Droplet->SetCanEverAffectNavigation(false);
        Droplet->SetCastShadow(false);
        Droplet->SetVisibility(false);
        Droplets.Add(Droplet);
    }
}

double AMCCataclysmJelly::ServerTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}

void AMCCataclysmJelly::BeginPlay()
{
    if(GetNetMode()!=NM_DedicatedServer)
        GelMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Cataclysm/Jelly/M_CataclysmJellyGel.M_CataclysmJellyGel"));
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer) LoadCoffeePresentation();
    if(HasAuthority() && HopStartedAt<-99.) HopStartedAt=ServerTime();
    if(HasAuthority()) ResetWandering(ServerTime());
    RefreshPresentation();
}

float AMCCataclysmJelly::PresentationScale() const
{
    return JellySize==EMCCataclysmJellySize::Large?1.25f:JellySize==EMCCataclysmJellySize::Medium?.8f:.5f;
}

void AMCCataclysmJelly::ConfigureJelly(AMCTongue* OnTongue,float InMaxHealth,EMCCataclysmJellySize InSize)
{
    if(!HasAuthority() || !IsValid(OnTongue)) return;
    JellySize=InSize;
    const auto* Defaults=GetClass()->GetDefaultObject<AMCCataclysmJelly>();
    const float Scale=PresentationScale();
    FMCNutEnemySettings JellySettings=Defaults->Settings;
    JellySettings.MaxHealth=FMath::IsFinite(InMaxHealth)?FMath::Clamp(InMaxHealth,40.f,1000.f):360.f;
    JellySettings.AttackDamage=0.f;
    JellySettings.BodyRadius=72.f*Scale;
    JellySettings.MoveSpeed=JellySize==EMCCataclysmJellySize::Large?175.f:JellySize==EMCCataclysmJellySize::Medium?205.f:235.f;
    TrailRadius=FMath::Max(24.f,Defaults->TrailRadius*Scale);
    RuptureRadius=FMath::Max(160.f,Defaults->RuptureRadius*Scale);
    bPendingRupture=false;
    DeathStartedAt=RupturedAt=-100.;
    HopStartedAt=ServerTime();
    ConfigureEnemy(OnTongue,NutMesh,Defaults->MeshScale*Scale,JellySettings);
    ResetWandering(HopStartedAt);
    ForceNetUpdate();
}

bool AMCCataclysmJelly::IsEncounterAlive() const
{
    return !bDefeated && !IsActorBeingDestroyed() && (Health>0.f || bPendingRupture);
}

bool AMCCataclysmJelly::CanReceiveToolHit() const
{
    return !bPendingRupture && !bDefeated && Health>0.f && !IsActorBeingDestroyed();
}

float AMCCataclysmJelly::ReceiveToolDamage(float Damage,AMCToothCharacter* Source)
{
    const float Applied=Super::ReceiveToolDamage(Damage,Source);
    // The shared damage resolver remembers the attacker for normal nut enemies.
    // Painting gel never acquires a pursuit target, including after a tool hit.
    if(HasAuthority()) Target=nullptr;
    return Applied;
}

void AMCCataclysmJelly::RefreshPresentation()
{
    Super::RefreshPresentation();
    Body->SetCollisionEnabled(CanReceiveToolHit()?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    if(GetNetMode()==NM_DedicatedServer) return;
    if(!bCoffeePresentationReady && GelMaterial && !GelMID) GelMID=Visual->CreateDynamicMaterialInstance(0,GelMaterial);
    if(GelMaterial && !SplatterMID)
    {
        SplatterMID=UMaterialInstanceDynamic::Create(GelMaterial,this);
        SplatterMID->SetScalarParameterValue(TEXT("JellyWobble"),.15f);
        SplatterMID->SetVectorParameterValue(TEXT("JellyColor"),FLinearColor(.15f,.14f,.03f));
        SplatterMID->SetVectorParameterValue(TEXT("JellyRimColor"),FLinearColor(.37f,.41f,.07f));
    }
    if(!bCoffeePresentationReady && GelMID)
    {
        Visual->SetMaterial(0,GelMID);
    }
    if(SplatterMID)
    {
        GroundBurst->SetMaterial(0,SplatterMID);
        for(UStaticMeshComponent* Droplet:Droplets) if(Droplet) Droplet->SetMaterial(0,SplatterMID);
    }
    Label->SetText(bPendingRupture?FText::FromString(TEXT("!")):FText::AsNumber(FMath::CeilToInt(Health)));
    Label->SetTextRenderColor(bPendingRupture?FColor(255,181,48):FColor(203,224,93));
    Label->SetVisibility(!bDefeated);
    LeftEye->SetVisibility(!bCoffeePresentationReady && !bDefeated);
    RightEye->SetVisibility(!bCoffeePresentationReady && !bDefeated);
    if(bCoffeePresentationReady)
    {
        Visual->SetVisibility(false);
        Feedback->SetVisualTarget(JellyMesh);
        TickCoffeePresentation(ServerTime());
    }
}

void AMCCataclysmJelly::LoadCoffeePresentation()
{
    // Soft CDO references retain the authored assets for cooking. A complete
    // matching set is required before replacing the existing primitive path.
    USkeletalMesh* Mesh=CoffeeMesh.LoadSynchronous();
    LoadedCoffeeIdle=CoffeeIdle.LoadSynchronous();
    LoadedCoffeeHop=CoffeeHop.LoadSynchronous();
    LoadedCoffeeHurt=CoffeeHurt.LoadSynchronous();
    LoadedCoffeePreBurst=CoffeePreBurst.LoadSynchronous();
    LoadedCoffeeBurst=CoffeeBurst.LoadSynchronous();
    const UAnimSequence* Clips[]={LoadedCoffeeIdle.Get(),LoadedCoffeeHop.Get(),LoadedCoffeeHurt.Get(),
        LoadedCoffeePreBurst.Get(),LoadedCoffeeBurst.Get()};
    bool Valid=Mesh && Mesh->GetSkeleton();
    for(const UAnimSequence* Clip:Clips)
    {
        Valid=Valid && Clip && Clip->GetSkeleton()==Mesh->GetSkeleton() && !Clip->IsValidAdditive()
            && FMath::IsFinite(Clip->GetPlayLength()) && Clip->GetPlayLength()>KINDA_SMALL_NUMBER;
    }
    if(!Valid)
    {
        LoadedCoffeeIdle=LoadedCoffeeHop=LoadedCoffeeHurt=LoadedCoffeePreBurst=LoadedCoffeeBurst=nullptr;
        UE_LOG(LogTemp,Verbose,TEXT("%s: coffee mesh/clip set missing or incompatible; keeping primitive jelly presentation."),*GetName());
        return;
    }
    JellyMesh->SetSkeletalMeshAsset(Mesh);
    JellyMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    JellyMesh->SetRelativeScale3D(FVector(PresentationScale()));
    JellyMesh->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f));
    if(UAnimSingleNodeInstance* Animation=JellyMesh->GetSingleNodeInstance())
    {
        Animation->SetRootMotionMode(ERootMotionMode::NoRootMotionExtraction);
        Animation->SetPlaying(false);
        bCoffeePresentationReady=true;
    }
}

void AMCCataclysmJelly::TickCoffeePresentation(double Now)
{
    UAnimSingleNodeInstance* Animation=JellyMesh->GetSingleNodeInstance();
    if(!Animation) return;
    UAnimSequence* Clip=LoadedCoffeeHop;
    float Duration=HopSeconds;
    float Age=float(FMath::Fmod(FMath::Max(0.,Now-HopStartedAt),double(HopSeconds)));
    bool Visible=true;
    if(bDefeated)
    {
        Clip=LoadedCoffeeBurst;
        Duration=.18f;
        Age=RupturedAt>-99.?float(FMath::Max(0.,Now-RupturedAt)):Duration;
        Visible=RupturedAt>-99. && Age<Duration;
    }
    else if(bPendingRupture)
    {
        Clip=LoadedCoffeePreBurst;
        Duration=RuptureWarningSeconds;
        Age=DeathStartedAt>-99.?float(FMath::Max(0.,Now-DeathStartedAt)):0.f;
    }
    else if(Feedback && Feedback->LastHit.Serial>0 && !Feedback->LastHit.bFatal && Feedback->LastHit.HealthDamage>0.f)
    {
        const double HitAge=Now-Feedback->LastHit.ServerTime;
        if(HitAge>=0. && HitAge<.28)
        {
            Clip=LoadedCoffeeHurt;
            Duration=.28f;
            Age=float(HitAge);
        }
    }
    if(Animation->GetAnimationAsset()!=Clip) Animation->SetAnimationAsset(Clip,false,0.f);
    Animation->SetPlaying(false);
    Animation->SetPosition(FMath::Clamp(Age/Duration,0.f,1.f)*Clip->GetPlayLength(),false);
    JellyMesh->SetRelativeScale3D(FVector(PresentationScale()));
    JellyMesh->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f));
    JellyMesh->SetVisibility(Visible);
    // The authored foam bone carries body lift/squash; the health label follows
    // that presentation without moving the collision sphere or paint contact.
    Label->SetWorldLocation(JellyMesh->GetSocketLocation(TEXT("foam_tip"))+FVector(0,0,28.f));
}

void AMCCataclysmJelly::TickGelPresentation(double Now)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    const float BurstAge=RupturedAt>-99.?float(Now-RupturedAt):-1.f;
    if(bCoffeePresentationReady) TickCoffeePresentation(Now);
    else
    {
        const float Phase=FMath::Fmod(FMath::Max(0.,Now-HopStartedAt),double(HopSeconds))/HopSeconds;
        const float Air=FMath::Clamp((Phase-.18f)/.60f,0.f,1.f);
        const float Hop=(Phase>.18f && Phase<.78f)?FMath::Sin(Air*PI):0.f;
        const float Low=FMath::Clamp(1.f-Health/FMath::Max(1.f,Settings.MaxHealth)*3.f,0.f,1.f);
        const float Warning=bPendingRupture?FMath::Clamp(float(Now-DeathStartedAt)/RuptureWarningSeconds,0.f,1.f):0.f;
        const float Hit=Feedback && CanReceiveToolHit()?Feedback->GetHitPulse():0.f;
        const float Shiver=FMath::Sin(float(Now)*53.f)*(Low*.035f+Warning*.08f);
        float Squash=1.f;
        if(Phase<.18f) Squash=1.f-.12f*FMath::Sin(Phase/.18f*PI);
        else if(Phase<.78f) Squash=1.f+.08f*Hop;
        else Squash=1.f-.10f*FMath::Sin((Phase-.78f)/.22f*PI);
        if(bPendingRupture) Squash=.94f+Warning*.16f;
        Squash=FMath::Max(.6f,Squash-.18f*Hit);
        const float Puff=1.f+Low*.13f+FMath::SmoothStep(0.f,1.f,Warning)*.70f;
        const float Collapse=bDefeated?FMath::Clamp(1.f-BurstAge/.18f,0.f,1.f):1.f;
        const FVector Shape=MeshScale*FVector(Puff/FMath::Sqrt(Squash)+Shiver,Puff/FMath::Sqrt(Squash)-Shiver,Puff*Squash)*Collapse;
        Visual->SetRelativeScale3D(Shape);
        // A small bounce keeps the gel close to its wet contact point while it draws.
        const float Lift=bPendingRupture || bDefeated?0.f:Hop*Settings.HopHeight;
        const FVector Recoil=Feedback?GetActorTransform().InverseTransformVectorNoScale(Feedback->LastHit.Direction).GetSafeNormal2D()*Hit*12.f:FVector::ZeroVector;
        Visual->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f+Shape.Z*50.f+Lift)+Recoil);
        Visual->SetVisibility(Collapse>.005f);
        Label->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f+Shape.Z*100.f+Lift+28.f));
        for(UStaticMeshComponent* Eye:{LeftEye.Get(),RightEye.Get()})
        {
            const float Side=Eye==LeftEye.Get()?-1.f:1.f;
            Eye->SetRelativeLocation(FVector(Shape.X*43.f,Side*Shape.Y*17.f,-Settings.BodyRadius-4.f+Shape.Z*62.f+Lift)+Recoil);
            Eye->SetRelativeScale3D(FVector(.14f,.10f,bPendingRupture?.05f:.09f));
        }
        if(GelMID)
        {
            GelMID->SetScalarParameterValue(TEXT("JellyWarning"),FMath::Max(Low*.6f,Warning));
            GelMID->SetScalarParameterValue(TEXT("JellyWobble"),2.f+Low*3.f+Warning*5.f);
            GelMID->SetScalarParameterValue(TEXT("JellyHit"),Hit);
        }
    }
    const bool ContactVisible=!bPendingRupture && !bDefeated;
    // Death liquid is the real thin, tongue-bound paint canvas. Expanding a
    // flattened sphere here made an elevated shiny disc above that coating.
    // Keep only the live contact footprint and the bounded ballistic droplets.
    GroundBurst->SetVisibility(ContactVisible);
    if(ContactVisible)
    {
        GroundBurst->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-3.f));
        GroundBurst->SetRelativeScale3D(FVector(TrailRadius*.02f,TrailRadius*.018f,.018f));
    }
    for(int32 Index=0;Index<Droplets.Num();++Index)
    {
        UStaticMeshComponent* Droplet=Droplets[Index];
        if(!Droplet) continue;
        const float FlightSeconds=.66f+.07f*(Index%4);
        const bool Visible=BurstAge>=0.f && BurstAge<FlightSeconds;
        Droplet->SetVisibility(Visible);
        if(!Visible) continue;
        const float T=FMath::Clamp(BurstAge/FlightSeconds,0.f,1.f);
        const float Angle=Index*2.f*PI/Droplets.Num()+.13f*(Index%3);
        const float Travel=1.f-FMath::Square(1.f-T);
        const float Distance=(.65f+.09f*(Index%4))*RuptureRadius*Travel;
        const float Z=Settings.BodyRadius*.65f*(1.f-T)+FMath::Sin(T*PI)*(65.f+Index%3*35.f)*PresentationScale();
        Droplet->SetRelativeLocation(FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,Z-Settings.BodyRadius));
        Droplet->SetRelativeRotation(FRotator(FMath::Lerp(35.f,-25.f,T),FMath::RadiansToDegrees(Angle),0.f));
        const float Fade=FMath::Clamp((1.f-T)/.25f,0.f,1.f);
        Droplet->SetRelativeScale3D(FVector(.34f*(1.f-T*.35f),.13f,.18f*(1.f-T*.75f))*Fade*PresentationScale());
    }
}

void AMCCataclysmJelly::ContaminateNearby(float Radius,float Amount)
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        AMCToothCharacter* Hero=*It;
        if(!IsPlayerTarget(Hero)) continue;
        const FVector Delta=Hero->GetActorLocation()-GetActorLocation();
        if(Delta.SizeSquared2D()>FMath::Square(Radius) || FMath::Abs(Delta.Z)>160.f) continue;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCJellyGrime),false,this);
        Query.AddIgnoredActor(Hero);
        if(GetOwner()) Query.AddIgnoredActor(GetOwner());
        if(GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        const float Before=Hero->Status->CoffeeAmount();
        const float After=FMath::Clamp(Before+Amount,0.f,1.f);
        if(After>Before+KINDA_SMALL_NUMBER) Hero->Status->ApplyCoffee(After);
    }
}

void AMCCataclysmJelly::PushRupturePlayers()
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        AMCToothCharacter* Hero=*It;
        if(!IsPlayerTarget(Hero)) continue;
        const FVector Offset=Hero->GetActorLocation()-GetActorLocation();
        if(Offset.SizeSquared2D()>FMath::Square(RuptureRadius) || FMath::Abs(Offset.Z)>190.f) continue;
        FHitResult Block;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCJellyRupturePush),false,this);
        Query.AddIgnoredActor(Hero);
        if(GetOwner()) Query.AddIgnoredActor(GetOwner());
        if(GetWorld()->LineTraceSingleByChannel(Block,GetActorLocation(),Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        const float Falloff=FMath::Lerp(1.f,.65f,FMath::Clamp(float(Offset.Size2D())/RuptureRadius,0.f,1.f));
        const float Speed=(JellySize==EMCCataclysmJellySize::Large?510.f:JellySize==EMCCataclysmJellySize::Medium?425.f:340.f)*Falloff;
        const FVector Away=Offset.GetSafeNormal2D(SMALL_NUMBER,GetActorForwardVector());
        const FVector Velocity=Away*Speed+FVector::UpVector*95.f;
        // ApplyHit can start ragdoll/recovery. A paint pop only pushes and never
        // changes the body state, stun timers, health or the player's input.
        if(Hero->ToothPhysics && Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll)
            Hero->GetMesh()->AddImpulseToAllBodiesBelow(Velocity,Hero->RigBone(TEXT("body")),true,true);
        else Hero->LaunchCharacter(Velocity,false,false);
        Hero->NotifyCameraImpact(Speed*.35f,Away);
        ++RupturePlayersPushed;
    }
}

void AMCCataclysmJelly::SpawnChildren()
{
    auto* Event=Cast<AMCCataclysmEvent>(GetOwner());
    if(JellySize==EMCCataclysmJellySize::Small || !IsValid(Tongue) || !Event || !Event->IsRunning()) return;
    const EMCCataclysmJellySize ChildSize=JellySize==EMCCataclysmJellySize::Large?EMCCataclysmJellySize::Medium:EMCCataclysmJellySize::Small;
    const float ChildRadius=72.f*(ChildSize==EMCCataclysmJellySize::Medium?.8f:.5f);
    const float StartAngle=WanderRandom.FRandRange(-180.f,180.f);
    TArray<FVector,TInlineAllocator<3>> Placed;
    for(int32 Index=0;Index<3;++Index)
    {
        FHitResult Floor;
        bool Supported=false;
        for(int32 Attempt=0;Attempt<24 && !Supported;++Attempt)
        {
            const float Angle=StartAngle+Index*120.f+(Attempt%8)*45.f;
            const float Distance=(Settings.BodyRadius+ChildRadius+18.f)*(1.f+.35f*(Attempt/8));
            const FVector Candidate=GetActorLocation()+FVector::ForwardVector.RotateAngleAxis(Angle,FVector::UpVector)*Distance;
            if(!MCDirtPlacement::FindSurface(Tongue,Candidate,ChildRadius+12.f,Floor)) continue;
            Supported=true;
            for(const FVector& Other:Placed) if(FVector::DistSquared2D(Other,Floor.ImpactPoint)<FMath::Square(ChildRadius*2.f+8.f)) Supported=false;
        }
        // The parent occupied the same central, brush-accessible work area with
        // a larger body. Never rescue a child spawn by placing it under a gum.
        if(!Supported) Supported=MCDirtPlacement::FindSurface(Tongue,GetActorLocation(),ChildRadius+8.f,Floor);
        if(!Supported) continue;
        const FTransform Pose(FRotator(0,StartAngle+Index*120.f,0),Floor.ImpactPoint+FVector::UpVector*(ChildRadius+4.f));
        auto* Child=GetWorld()->SpawnActorDeferred<AMCCataclysmJelly>(GetClass(),Pose,Event);
        if(!Child) continue;
        Child->ConfigureJelly(Tongue,Settings.MaxHealth*.5f,ChildSize);
        Child->OnDirtyPatchRequested.AddUObject(Event,&AMCCataclysmEvent::RegisterDirtyPatch);
        Child->FinishSpawning(Pose);
        Event->Jellies.Add(Child);
        Placed.Add(Floor.ImpactPoint);
        ++ChildrenSpawned;
    }
    Event->ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_JELLY SPLIT name=%s tier=%d children=%d"),*GetName(),int32(JellySize),ChildrenSpawned);
}

void AMCCataclysmJelly::RequestDirtyPatch(float Radius,bool bDeathBurst)
{
    if(!HasAuthority() || !IsValid(Tongue)) return;
    FHitResult Floor;
    if(Tongue->InteriorSurfacePoint(GetActorLocation(),Settings.BodyRadius+5.f,Floor))
        OnDirtyPatchRequested.Broadcast(Floor.ImpactPoint+Floor.ImpactNormal*2.f,Radius,bDeathBurst);
}

bool AMCCataclysmJelly::IsPlayerTarget(const AMCToothCharacter* Hero) const
{
    const auto* Identity=Hero?Hero->GetPlayerState():nullptr;
    return IsLiveTarget(Hero) && Identity && !Identity->IsOnlyASpectator() && !Identity->IsSpectator();
}

void AMCCataclysmJelly::ResetWandering(double Now)
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const FVector Position=GetActorLocation();
    const uint32 LocationSeed=HashCombine(GetTypeHash(FMath::RoundToInt(Position.X)),GetTypeHash(FMath::RoundToInt(Position.Y)));
    WanderRandom.Initialize(int32(HashCombine(GS?GS->RunSeed:1,LocationSeed)));
    Target=nullptr;
    bHasWanderGoal=bHasTrailAnchor=false;
    WanderDistance=0.f;
    WanderGoalChanges=PaintStrokeRequests=PaintFloorRejections=TrailDiscontinuities=RuptureCount=ChildrenSpawned=RupturePlayersPushed=0;
    LastTrailLocation=LastObservedLocation=Position;
    LastTrailSampleAt=-100.;
    NextContactAt=NextWanderAt=NextTrailAt=NextMoveAt=LastMoveAt=Now;
}

void AMCCataclysmJelly::PickWanderGoal(double Now)
{
    bHasWanderGoal=false;
    const FVector Position=GetActorLocation();
    const float Margin=FMath::Max(Settings.BodyRadius+10.f,TrailRadius+10.f);
    FHitResult Floor;
    // Local turns make scribbles; occasional work-area goals keep a painter
    // from circling one patch. Neither choice reads a player's position.
    const FBox Bounds=MCDirtPlacement::WorkBounds(Tongue,Margin+65.f);
    if(!Bounds.IsValid) {NextWanderAt=Now+.75;return;}
    for(int32 Attempt=0;Attempt<8 && !bHasWanderGoal;++Attempt)
    {
        const float Turn=WanderRandom.FRandRange(-155.f,155.f);
        const FVector Direction=GetActorForwardVector().RotateAngleAxis(Turn,FVector::UpVector);
        FVector Candidate=Position+Direction*WanderRandom.FRandRange(180.f,520.f);
        // Probe wide goals directly: navigation must not add fake spawns to
        // RandomGameplaySpawnPoint's recent food-placement history.
        if(WanderGoalChanges%4==3 && Attempt<6)
            Candidate=Bounds.GetCenter()+FVector(WanderRandom.FRandRange(-Bounds.GetExtent().X,Bounds.GetExtent().X),
                WanderRandom.FRandRange(-Bounds.GetExtent().Y,Bounds.GetExtent().Y),0.f);
        if(!MCDirtPlacement::FindSurface(Tongue,Candidate,Margin,Floor)) continue;
        WanderGoal=Floor.ImpactPoint;
        bHasWanderGoal=true;
    }
    if(bHasWanderGoal) ++WanderGoalChanges;
    NextWanderAt=Now+WanderRandom.FRandRange(.75f,1.65f);
}

void AMCCataclysmJelly::WanderOnTongue(double Now)
{
    if(Now<NextMoveAt) return;
    const float MoveSeconds=FMath::Clamp(float(Now-LastMoveAt),0.f,.1f);
    LastMoveAt=Now;
    NextMoveAt=Now+MoveInterval;
    const FVector Position=GetActorLocation();
    if(Now>=NextWanderAt || (bHasWanderGoal && FVector::DistSquared2D(Position,WanderGoal)<FMath::Square(65.f))) PickWanderGoal(Now);
    if(!bHasWanderGoal || MoveSeconds<=0.f) return;
    const FVector Direction=(WanderGoal-Position).GetSafeNormal2D();
    FHitResult Ahead;
    const float Margin=FMath::Max(Settings.BodyRadius+10.f,TrailRadius+10.f);
    if(!MCDirtPlacement::FindSurface(Tongue,Position+Direction*Settings.MoveSpeed*MoveSeconds,Margin,Ahead))
    {
        bHasWanderGoal=false;
        NextWanderAt=Now+.18;
        return;
    }
    MoveOnTongue(Direction,MoveSeconds);
    const FVector Moved=GetActorLocation()-Position;
    // The shared swept mover may deflect around an obstacle. Check a deflected
    // step too, so it cannot enter a delivery lane or a forbidden outer strip.
    if(!Moved.IsNearlyZero() && FVector::DotProduct(Moved.GetSafeNormal2D(),Direction)<.995f)
    {
        FHitResult Deflected;
        if(!MCDirtPlacement::FindSurface(Tongue,GetActorLocation(),Margin,Deflected))
        {
            SetActorLocation(Position,false);
            bHasWanderGoal=false;
            NextWanderAt=Now+.18;
            BreakTrail();
            return;
        }
    }
    const float Distance=FVector::Dist2D(Position,GetActorLocation());
    if(Distance<.5f) { bHasWanderGoal=false; NextWanderAt=Now+.18; }
    else
    {
        WanderDistance+=Distance;
        SetActorRotation(Moved.GetSafeNormal2D().Rotation());
    }
}

void AMCCataclysmJelly::BreakTrail()
{
    if(bHasTrailAnchor) ++TrailDiscontinuities;
    bHasTrailAnchor=false;
}

void AMCCataclysmJelly::PaintTrail(double Now,float DeltaSeconds,bool bFlush)
{
    auto* Event=Cast<AMCCataclysmEvent>(GetOwner());
    if(!HasAuthority() || !Event || !Event->IsRunning() || !IsValid(Tongue)) { BreakTrail(); return; }
    const FVector Position=GetActorLocation();
    const float MaxTickTravel=FMath::Max(40.f,Settings.MoveSpeed*FMath::Min(FMath::Max(DeltaSeconds,0.f),.1f)*2.f);
    if(LastTrailSampleAt>-99. && (Now-LastTrailSampleAt>.35 || FVector::DistSquared2D(Position,LastObservedLocation)>FMath::Square(MaxTickTravel))) BreakTrail();
    LastObservedLocation=Position;
    LastTrailSampleAt=Now;
    // Movement is observed each tick to catch teleports; expensive contact
    // probes are needed only when a new 10 Hz paint sample is due.
    if(!bFlush && Now<NextTrailAt) return;
    NextTrailAt=Now+PaintInterval;
    FHitResult Floor;
    if(!MCDirtPlacement::IsInWorkArea(Tongue,Position,TrailRadius+8.f+65.f)
        || !Tongue->InteriorSurfacePoint(Position,TrailRadius+8.f,Floor))
    {
        ++PaintFloorRejections;
        BreakTrail();
        return;
    }
    const FVector Point=Floor.ImpactPoint+Floor.ImpactNormal*2.f;
    if(!bHasTrailAnchor)
    {
        LastTrailLocation=Point;
        bHasTrailAnchor=true;
        Event->RegisterJellyPaintStroke(this,Point,Point,TrailRadius);
        ++PaintStrokeRequests;
        NextTrailAt=Now+PaintInterval;
        return;
    }
    const float Distance=FVector::Dist2D(LastTrailLocation,Point);
    if(Distance<4.f) return;
    // A late frame or external teleport starts a fresh dot; never draw a bridge
    // across a floor gap. At normal speed one update spans about 19 cm.
    if(Distance>96.f) { BreakTrail(); return; }
    const int32 Segments=FMath::Clamp(FMath::CeilToInt(Distance/24.f),1,4);
    FVector From=LastTrailLocation;
    for(int32 Index=1;Index<=Segments;++Index)
    {
        FHitResult Sample;
        const FVector Candidate=FMath::Lerp(LastTrailLocation,Point,float(Index)/Segments);
        if(!MCDirtPlacement::IsInWorkArea(Tongue,Candidate,TrailRadius+8.f+65.f)
            || !Tongue->InteriorSurfacePoint(Candidate,TrailRadius+8.f,Sample)
            || FMath::Abs(Sample.ImpactPoint.Z-From.Z)>45.f)
        {
            BreakTrail();
            return;
        }
        const FVector To=Sample.ImpactPoint+Sample.ImpactNormal*2.f;
        Event->RegisterJellyPaintStroke(this,From,To,TrailRadius);
        ++PaintStrokeRequests;
        From=To;
    }
    LastTrailLocation=Point;
}

void AMCCataclysmJelly::Defeat()
{
    if(!HasAuthority() || bPendingRupture || bDefeated) return;
    PaintTrail(ServerTime(),0.f,true);
    Health=0.f;
    Target=nullptr;
    AttackStartedAt=-100.;
    bPendingRupture=true;
    DeathStartedAt=ServerTime();
    RefreshPresentation();
    ForceNetUpdate();
}

void AMCCataclysmJelly::Rupture(double Now)
{
    if(!HasAuthority() || bDefeated) return;
    // Commit gameplay once, before the short cosmetic lifetime.
    bPendingRupture=false;
    bDefeated=true;
    RupturedAt=Now;
    ++RuptureCount;
    ContaminateNearby(RuptureRadius,1.f);
    RequestDirtyPatch(RuptureRadius,true);
    PushRupturePlayers();
    SpawnChildren();
    RefreshPresentation();
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_JELLY RUPTURE name=%s tier=%d travel=%.1f goals=%d strokes=%d breaks=%d ruptures=%d children=%d pushes=%d"),
        *GetName(),int32(JellySize),WanderDistance,WanderGoalChanges,PaintStrokeRequests,TrailDiscontinuities,RuptureCount,ChildrenSpawned,RupturePlayersPushed);
    SetLifeSpan(1.1f);
}

void AMCCataclysmJelly::Tick(float DeltaSeconds)
{
    // Do not call AMCNutEnemy::Tick: its bite scheduler is intentionally unused.
    AActor::Tick(DeltaSeconds);
    const double Now=ServerTime();
    TickGelPresentation(Now);
    if(!HasAuthority() || bDefeated) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) return;
    if(bPendingRupture)
    {
        if(Now>=DeathStartedAt+RuptureWarningSeconds) Rupture(Now);
        return;
    }
    if(!IsValid(Tongue) || Tongue->IsActorBeingDestroyed()) { Destroy(); return; }
    Target=nullptr;
    WanderOnTongue(Now);
    PaintTrail(Now,DeltaSeconds);
    if(Now>=NextContactAt) { ContaminateNearby(Settings.BodyRadius+48.f,.25f); NextContactAt=Now+.8; }
}

void AMCCataclysmJelly::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    OnDirtyPatchRequested.Clear();
    Super::EndPlay(EndPlayReason);
}

void AMCCataclysmJelly::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCataclysmJelly,HopStartedAt);
    DOREPLIFETIME(AMCCataclysmJelly,DeathStartedAt);
    DOREPLIFETIME(AMCCataclysmJelly,RupturedAt);
    DOREPLIFETIME(AMCCataclysmJelly,bPendingRupture);
    DOREPLIFETIME(AMCCataclysmJelly,JellySize);
    DOREPLIFETIME(AMCCataclysmJelly,TrailRadius);
    DOREPLIFETIME(AMCCataclysmJelly,RuptureRadius);
}
