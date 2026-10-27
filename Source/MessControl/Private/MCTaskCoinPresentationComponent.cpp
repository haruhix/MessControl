#include "MCTaskCoinPresentationComponent.h"
#include "MCGameUserSettings.h"
#include "MCPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PrimitiveComponentUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundBase.h"

namespace MCTaskCoinPresentation
{
constexpr int32 Segments=20;
constexpr float Radius=9.f,Life=1.55f,DropEnd=.52f,BounceEnd=.78f;
float Smooth(float Value) {return FMath::SmoothStep(0.f,1.f,FMath::Clamp(Value,0.f,1.f));}
FVector Bezier(FVector A,FVector B,FVector C,FVector D,float T)
{const float U=1-T;return A*(U*U*U)+B*(3*U*U*T)+C*(3*U*T*T)+D*(T*T*T);}
}

UMCTaskCoinPresentationComponent::UMCTaskCoinPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;SetIsReplicatedByDefault(false);
    CoinMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Rewards/M_TaskCoin.M_TaskCoin")));
    CoinSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins")));
}

void UMCTaskCoinPresentationComponent::BeginPlay()
{Super::BeginPlay();PreparePresentation();}

bool UMCTaskCoinPresentationComponent::HasPreparedPresentation() const
{
    const auto* PC=Cast<AMCPlayerController>(GetOwner());
    return PC && PC->HasLocalPresentationOwner() && PreparedWorld.Get()==GetWorld()
        && IsValid(CoinMesh) && CoinMesh->IsRegistered() && IsValid(RetainedMaterial)
        && CoinMesh->GetNumSections()==1 && !Template.IsEmpty();
}

void UMCTaskCoinPresentationComponent::PreparePresentation()
{
    auto* PC=Cast<AMCPlayerController>(GetOwner());auto* World=GetWorld();
    if(!PC || !PC->HasLocalPresentationOwner() || !World || World->bIsTearingDown
        || GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender()) return;
    if(HasPreparedPresentation()) return;
    ReleasePreparedResources();
    RetainedMaterial=CoinMaterial.LoadSynchronous();
    if(!RetainedMaterial) return; // No first-use fallback or flat substitute.
    TArray<int32> Indices;BuildCoinGeometry(Indices);
    const int32 VerticesPerCoin=Template.Num();
    Positions.SetNumZeroed(VerticesPerCoin*PoolSize);Normals.SetNumUninitialized(Positions.Num());
    UV.SetNumUninitialized(Positions.Num());Colors.SetNumUninitialized(Positions.Num());Tangents.SetNumUninitialized(Positions.Num());
    TArray<int32> PoolIndices;PoolIndices.Reserve(Indices.Num()*PoolSize);
    for(int32 Coin=0;Coin<PoolSize;++Coin) {
        const int32 Offset=Coin*VerticesPerCoin;
        for(int32 I:Indices) PoolIndices.Add(Offset+I);
        for(int32 V=0;V<VerticesPerCoin;++V) {
            const auto& Base=Template[V];const int32 Index=Offset+V;
            // Retain valid closed geometry for render/PSO preparation while hidden.
            Positions[Index]=Base.Position+FVector(Coin*24,0,0);Normals[Index]=Base.Normal;
            UV[Index]=Base.UV;Colors[Index]=Base.Color;Tangents[Index]=FProcMeshTangent(Base.Tangent,false);
        }
    }
    // AController is hidden by default. Keep that controller state untouched:
    // a single local, transient host is prepared now, never on PresentCoins.
    FActorSpawnParameters HostParameters;HostParameters.Owner=PC;HostParameters.ObjectFlags|=RF_Transient;
    HostParameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    VisualHost=World->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,HostParameters);
    if(!VisualHost) return;
    VisualHost->SetReplicates(false);VisualHost->SetActorTickEnabled(false);VisualHost->SetActorEnableCollision(false);
    CoinMesh=NewObject<UProceduralMeshComponent>(VisualHost,TEXT("TaskCoinPresentationMesh"));
    VisualHost->SetRootComponent(CoinMesh);
    CoinMesh->SetAbsolute(true,true,true);CoinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CoinMesh->SetGenerateOverlapEvents(false);CoinMesh->SetCanEverAffectNavigation(false);
    CoinMesh->SetCastShadow(false);CoinMesh->SetOnlyOwnerSee(true);CoinMesh->SetCullDistance(3000.f);
    CoinMesh->SetMaterial(0,RetainedMaterial);VisualHost->AddInstanceComponent(CoinMesh);CoinMesh->RegisterComponent();
    CoinMesh->CreateMeshSection(0,Positions,PoolIndices,Normals,UV,{}, {}, {},Colors,Tangents,false);
    if(APawn* Pawn=PC->GetPawn()) {
        // The view actor is the pawn, not its controller. A PC-owned primitive
        // needs this explicit visibility owner for OnlyOwnerSee in UE 5.8.
        UPrimitiveComponentUtilities::AddVisibilityOwner(CoinMesh,Pawn);VisibilityPawn=Pawn;
    }
    CoinMesh->SetVisibility(false);PreparedWorld=GetWorld();
    // Optional audio is loaded only while preparing. An absent cue never hides coins.
    if(!CoinSound.IsNull() && (CoinSound.IsValid()
        || FPackageName::DoesPackageExist(CoinSound.ToSoftObjectPath().GetLongPackageName())))
        RetainedSound=CoinSound.LoadSynchronous();
    if(RetainedSound) {
        CoinAudio=NewObject<UAudioComponent>(PC,TEXT("TaskCoinPresentationAudio"));
        CoinAudio->bAutoActivate=false;CoinAudio->bAutoDestroy=false;CoinAudio->bAllowSpatialization=false;
        CoinAudio->bIsUISound=true;
        // Settings already apply master*SFX to this class. Do not multiply that
        // gain again on the component, which would square the user's volume.
        CoinAudio->SoundClassOverride=GetDefault<UAudioSettings>()->GetDefaultSoundClass();
        CoinAudio->SetSound(RetainedSound);CoinAudio->SetVolumeMultiplier(.65f);
        PC->AddInstanceComponent(CoinAudio);CoinAudio->RegisterComponent();
    }
}

bool UMCTaskCoinPresentationComponent::CanPresent(AMCPlayerController* PC,APawn* Pawn) const
{
    return PC && PC->HasLocalPresentationOwner() && HasPreparedPresentation()
        && IsValid(Pawn) && Pawn->GetWorld()==GetWorld() && !Pawn->IsActorBeingDestroyed()
        && PC->GetViewTarget()==Pawn && PC->IsWorldPresentationReady() && !PC->IsPaused()
        && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored()
        && !PC->IsWardrobeOpen() && !PC->IsHubShopOpen() && !PC->IsHubPartyPanelOpen()
        && !PC->IsRewardInteractionActive() && !PC->IsBossIntroPlaying();
}

void UMCTaskCoinPresentationComponent::PresentCoins(int32 Amount,int32 WalletAfter)
{
    auto* PC=Cast<AMCPlayerController>(GetOwner());APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(Amount<=0 || WalletAfter<0 || !CanPresent(PC,Pawn)) {ResetPresentation();return;}
    // A burst is a cosmetic acknowledgement, not one actor per credited unit.
    // Concurrent task receipts keep the current bounded payoff instead of
    // restarting it forever or stacking sounds and thousands of extra objects.
    if(ActiveCount>0) return;
    if(VisibilityPawn.Get()!=Pawn) {
        // Rebind only after possession/respawn. Geometry, material and sections
        // stay retained; this is a visibility owner update, not a new visual.
        if(auto* Previous=VisibilityPawn.Get()) UPrimitiveComponentUtilities::RemoveVisibilityOwner(CoinMesh,Previous);
        UPrimitiveComponentUtilities::AddVisibilityOwner(CoinMesh,Pawn);VisibilityPawn=Pawn;
    }
    ActivePawn=Pawn;BurstOrigin=Pawn->GetActorLocation();Age=0;
    ActiveCount=Amount<10?3:Amount<25?4:5;
    const FRotator View=PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation():PC->GetControlRotation();
    FVector Right=View.RotateVector(FVector::RightVector).GetSafeNormal2D();
    if(Right.IsNearlyZero()) Right=FVector::RightVector;
    const FVector Forward=FVector::CrossProduct(Right,FVector::UpVector).GetSafeNormal();
    const FVector CameraNormal=-View.Vector();
    float HalfHeight=55.f;
    if(const auto* Character=Cast<ACharacter>(Pawn);Character && Character->GetCapsuleComponent())
        HalfHeight=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Feet=BurstOrigin-FVector::UpVector*HalfHeight;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCTaskCoinLanding),false,Pawn);Query.AddIgnoredActor(PC);
    Query.AddIgnoredActor(VisualHost);
    // Currency feedback must land on scenery, never a teammate's head/capsule.
    for(TActorIterator<APawn> It(GetWorld());It;++It) Query.AddIgnoredActor(*It);
    for(int32 I=0;I<ActiveCount;++I) {
        auto& Coin=Coins[I];Coin=FCoinMotion();Coin.Delay=I*.06f;Coin.Spin=(I%2?1.f:-1.f)*(1.1f+I*.13f);
        const float Spread=(I-(ActiveCount-1)*.5f);
        Coin.Start=BurstOrigin+Right*(Spread*14)+Forward*24+FVector::UpVector*12;
        Coin.InitialRotation=FQuat::FindBetweenNormals(FVector::UpVector,CameraNormal);
        const FVector FloorCandidate=Feet+Right*(Spread*32)+Forward*(52+(I%2)*16);
        FHitResult Floor;
        // At most five read-only traces per receipt; never collision/Chaos or
        // another trace during animation. Moving rigid surfaces retain an anchor.
        Coin.bContact=GetWorld()->LineTraceSingleByChannel(Floor,FloorCandidate+FVector(0,0,90),
            FloorCandidate-FVector(0,0,140),ECC_Visibility,Query) && Floor.ImpactNormal.Z>.55f;
        if(Coin.bContact) {
            Coin.LandingNormal=Floor.ImpactNormal.GetSafeNormal();
            Coin.Landing=Floor.ImpactPoint+Coin.LandingNormal*1.7f;Coin.Surface=Floor.GetComponent();
            if(auto* Surface=Coin.Surface.Get()) {
                Coin.LandingLocal=Surface->GetComponentTransform().InverseTransformPosition(Coin.Landing);
                Coin.NormalLocal=Surface->GetComponentTransform().InverseTransformVectorNoScale(Coin.LandingNormal);
            }
        } else {
            // Unsupported scenery gets a free arc, never an invented floor bounce.
            Coin.Landing=FloorCandidate+FVector::UpVector*24;
        }
    }
    CoinMesh->SetWorldLocation(BurstOrigin);SetComponentTickEnabled(true);UpdateMesh(0);
    const auto* Preferences=UMCGameUserSettings::Get();
    if(CoinAudio && RetainedSound && (!Preferences || Preferences->GetEffectiveSFXVolume()>KINDA_SMALL_NUMBER)) CoinAudio->Play();
}

void UMCTaskCoinPresentationComponent::UpdateMesh(float Elapsed)
{
    using namespace MCTaskCoinPresentation;
    const auto* Pawn=ActivePawn.Get();const auto* PC=Cast<AMCPlayerController>(GetOwner());
    if(!Pawn || !PC || !CoinMesh) return;
    const FRotator View=PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation():PC->GetControlRotation();
    const FQuat Facing=FQuat::FindBetweenNormals(FVector::UpVector,-View.Vector());
    const FVector Collect=Pawn->GetActorLocation()+FVector(0,0,15);
    bool Visible=false;
    for(int32 I=0;I<PoolSize;++I) {
        auto& Coin=Coins[I];const float T=Elapsed-Coin.Delay;
        float Scale=0;FVector Position=BurstOrigin;FQuat Rotation=FQuat::Identity;
        if(I<ActiveCount && T>=0 && T<Life) {
            Visible=true;Scale=FMath::Lerp(.55f,1.f,Smooth(T/.12f));
            if(auto* Surface=Coin.Surface.Get();Coin.bContact && Surface && Surface->GetWorld()==GetWorld()) {
                Coin.Landing=Surface->GetComponentTransform().TransformPosition(Coin.LandingLocal);
                Coin.LandingNormal=Surface->GetComponentTransform().TransformVectorNoScale(Coin.NormalLocal).GetSafeNormal();
            }
            const FQuat Flat=FQuat::FindBetweenNormals(FVector::UpVector,Coin.LandingNormal)
                *FQuat(FVector::UpVector,Coin.Spin*.7f);
            if(T<DropEnd) {
                const float U=T/DropEnd;
                Position=FMath::Lerp(Coin.Start,Coin.Landing,U)+FVector::UpVector*(4*82.f*U*(1-U));
                const FQuat Turn=Coin.InitialRotation*FQuat(FVector::UpVector,Coin.Spin*U*PI)
                    *FQuat(FVector::RightVector,U*1.4f);
                Rotation=FQuat::Slerp(Turn,Flat,Smooth((U-.55f)/.45f));
            } else if(T<BounceEnd) {
                const float U=(T-DropEnd)/(BounceEnd-DropEnd);
                Position=Coin.Landing+Coin.LandingNormal*(Coin.bContact?FMath::Sin(U*PI)*15.f:0.f);
                Rotation=Flat*FQuat(FVector::RightVector,FMath::Sin(U*PI)*.18f);
            } else {
                const float U=(T-BounceEnd)/(Life-BounceEnd),Blend=Smooth(U);
                Position=Bezier(Coin.Landing,Coin.Landing+FVector::UpVector*75,
                    Collect+FVector::UpVector*35,Collect,Blend);
                Rotation=FQuat::Slerp(Flat,Facing,Smooth(U/.45f))*FQuat(FVector::UpVector,Coin.Spin*U*2*PI);
                Scale*=1-Smooth((U-.58f)/.42f);
            }
        }
        const int32 Offset=I*Template.Num();
        for(int32 V=0;V<Template.Num();++V) {
            const auto& Base=Template[V];const int32 Index=Offset+V;
            Positions[Index]=Position-BurstOrigin+Rotation.RotateVector(Base.Position)*Scale;
            Normals[Index]=Rotation.RotateVector(Base.Normal);Tangents[Index].TangentX=Rotation.RotateVector(Base.Tangent);
        }
    }
    // Fixed topology, section, UVs and packed colours. No per-frame linear-colour
    // conversion or arrays, material variants, timers, actors or collision bodies.
    CoinMesh->UpdateMeshSection(0,Positions,Normals,UV,{}, {}, {},Colors,Tangents);
    CoinMesh->SetVisibility(Visible);
}

void UMCTaskCoinPresentationComponent::TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Tick,Function);
    auto* PC=Cast<AMCPlayerController>(GetOwner());auto* Pawn=ActivePawn.Get();
    if(!CanPresent(PC,Pawn) || PC->GetPawn()!=Pawn) {ResetPresentation();return;}
    Age+=FMath::Max(0.f,Dt);
    if(Age>=MCTaskCoinPresentation::Life+(ActiveCount-1)*.06f) {ResetPresentation();return;}
    UpdateMesh(Age);
}

void UMCTaskCoinPresentationComponent::ResetPresentation()
{
    ActiveCount=0;Age=0;ActivePawn.Reset();SetComponentTickEnabled(false);
    if(CoinMesh) CoinMesh->SetVisibility(false);
    if(CoinAudio) CoinAudio->Stop();
}

void UMCTaskCoinPresentationComponent::ReleasePreparedResources()
{
    ResetPresentation();
    if(CoinAudio) {CoinAudio->DestroyComponent();CoinAudio=nullptr;}
    if(CoinMesh) {CoinMesh->DestroyComponent();CoinMesh=nullptr;}
    if(VisualHost) {VisualHost->Destroy();VisualHost=nullptr;}
    RetainedSound=nullptr;RetainedMaterial=nullptr;PreparedWorld.Reset();VisibilityPawn.Reset();
    Template.Reset();Positions.Reset();Normals.Reset();UV.Reset();Colors.Reset();Tangents.Reset();
}

void UMCTaskCoinPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{ReleasePreparedResources();Super::EndPlay(Reason);}

void UMCTaskCoinPresentationComponent::BuildCoinGeometry(TArray<int32>& Indices)
{
    using namespace MCTaskCoinPresentation;
    Template.Reset();Indices.Reset();Template.Reserve(1000);Indices.Reserve(1800);
    const FColor Gold=FLinearColor(.95f,.64f,.12f).ToFColor(false);
    const FColor Rim=FLinearColor(1.f,.79f,.25f).ToFColor(false);
    const FColor Side=FLinearColor(.66f,.36f,.065f).ToFColor(false);
    const FColor Emboss=FLinearColor(1.f,.84f,.34f).ToFColor(false);
    auto Vertex=[&](FVector Point,FVector Normal,FColor Color) {
        const FVector Tangent=FVector::VectorPlaneProject(FVector::ForwardVector,Normal).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
        return Template.Add({Point,Normal,Tangent,FVector2D(.5+Point.X/(Radius*2),.5+Point.Y/(Radius*2)),Color});
    };
    auto Triangle=[&](int32 A,int32 B,int32 C,FVector Normal) {
        if(FVector::DotProduct(FVector::CrossProduct(Template[B].Position-Template[A].Position,Template[C].Position-Template[A].Position),Normal)<0)
            Swap(B,C);
        Indices.Append({A,B,C});
    };
    auto Quad=[&](FVector A,FVector B,FVector C,FVector D,FVector NA,FVector NB,FColor Color) {
        const int32 VA=Vertex(A,NA,Color),VB=Vertex(B,NB,Color),VC=Vertex(C,NB,Color),VD=Vertex(D,NA,Color);
        const FVector Out=(NA+NB).GetSafeNormal();Triangle(VA,VB,VC,Out);Triangle(VA,VC,VD,Out);
    };
    // Closed cylinder profile: recessed face -> raised rim -> bevel -> real
    // thickness -> back bevel/rim/face. Separate profile normals keep the rim crisp.
    const FVector2D Profile[]={{6.95, .90},{7.25,1.25},{8.05,1.25},{Radius,.55},
        {Radius,-.55},{8.05,-1.25},{7.25,-1.25},{6.95,-.90}};
    for(int32 Ring=0;Ring<UE_ARRAY_COUNT(Profile)-1;++Ring) {
        const FVector2D A=Profile[Ring],B=Profile[Ring+1],Delta=B-A;
        const FVector2D N(-Delta.Y,Delta.X); // outward radial/Z profile normal
        const FVector2D Unit=N.GetSafeNormal();
        for(int32 I=0;I<Segments;++I) {
            const float AngleA=2*PI*I/Segments,AngleB=2*PI*(I+1)/Segments;
            const FVector RA(FMath::Cos(AngleA),FMath::Sin(AngleA),0),RB(FMath::Cos(AngleB),FMath::Sin(AngleB),0);
            Quad(RA*A.X+FVector(0,0,A.Y),RB*A.X+FVector(0,0,A.Y),RB*B.X+FVector(0,0,B.Y),RA*B.X+FVector(0,0,B.Y),
                RA*Unit.X+FVector(0,0,Unit.Y),RB*Unit.X+FVector(0,0,Unit.Y),Ring==3?Side:Rim);
        }
    }
    const FVector2D Tooth[]={{-3,2.6},{-1.4,3.5},{0,2.8},{1.4,3.5},{3,2.6},{3.2,.2},
        {2.3,-3.1},{1,-3.4},{0,-1.3},{-1,-3.4},{-2.3,-3.1},{-3.2,.2}};
    for(float Sign:{1.f,-1.f}) {
        const FVector FaceNormal(0,0,Sign);
        for(int32 I=0;I<Segments;++I) {
            const float A=2*PI*I/Segments,B=2*PI*(I+1)/Segments;
            const int32 Center=Vertex(FVector(0,0,Sign*.90f),FaceNormal,Gold);
            const int32 Left=Vertex(FVector(FMath::Cos(A)*6.95f,FMath::Sin(A)*6.95f,Sign*.90f),FaceNormal,Gold);
            const int32 Right=Vertex(FVector(FMath::Cos(B)*6.95f,FMath::Sin(B)*6.95f,Sign*.90f),FaceNormal,Gold);
            Triangle(Center,Left,Right,FaceNormal);
        }
        // Raised filled tooth emblem on both faces; its bevel and sidewalls
        // are geometry, not an alpha decal or emissive painted mark.
        for(int32 I=0;I<UE_ARRAY_COUNT(Tooth);++I) {
            const FVector2D A=Tooth[I],B=Tooth[(I+1)%UE_ARRAY_COUNT(Tooth)];
            const FVector PA(A.X,A.Y,Sign*.90f),PB(B.X,B.Y,Sign*.90f);
            const FVector TA(A.X*.88,A.Y*.88,Sign*1.50f),TB(B.X*.88,B.Y*.88,Sign*1.50f);
            const FVector Edge=(PB-PA).GetSafeNormal();FVector Out=FVector::CrossProduct(Edge,FaceNormal).GetSafeNormal();
            if(FVector::DotProduct(Out,(PA+PB)*.5f)<0) Out=-Out;
            Quad(PA,PB,TB,TA,(Out+FaceNormal*.45f).GetSafeNormal(),(Out+FaceNormal*.45f).GetSafeNormal(),Rim);
            const int32 Center=Vertex(FVector(0,0,Sign*1.50f),FaceNormal,Emboss);
            const int32 Left=Vertex(TA,FaceNormal,Emboss),Right=Vertex(TB,FaceNormal,Emboss);
            Triangle(Center,Left,Right,FaceNormal);
        }
    }
}
