#include "MCThroat.h"
#include "MCGameDirector.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCVomitBurst.h"
#include "MCThroatVortex.h"
#include "MCTutorialDirector.h"
#include "MCPlayerState.h"
#include "MCFoodCollectionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/TextRenderComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace {
// Measured from the saved /Game/FromBlender4/SK_Exit native export. These are
// the actual inner boundary, not the bounds of the whole back wall.
// Provenance and FBX coordinate conversion: Saved/ApertureProbe/ApertureExact.json.
const FVector ExitApertureCenter(0,369.93134,-397.40539);
const FVector ExitApertureNormal(0,.9851301,.1718100);
const FVector ExitApertureUp(0,-.1718100,.9851301);
const FVector ExitApertureBoundary[]={
    {0,435.66058,-770.38892},{163.29349,426.63367,-750.67853},
    {351.69412,409.12405,-673.91443},{462.85519,407.53976,-585.38525},
    {509.47537,389.65298,-446.42477},{482.81137,357.56491,-297.00253},
    {450.59818,341.57620,-232.62732},{355.08005,324.53574,-124.88197},
    {225.14194,312.29459,-55.54401},{109.68974,301.22644,-32.23042},
    {0,291.62140,-28.93486},{-109.68969,301.22644,-32.23042},
    {-225.14188,312.29465,-55.54401},{-355.07999,324.53580,-124.88197},
    {-450.59811,341.57626,-232.62732},{-482.81131,357.56497,-297.00253},
    {-509.47531,389.65305,-446.42477},{-462.85513,407.53983,-585.38525},
    {-351.69406,409.12411,-673.91443},{-163.29343,426.63367,-750.67853}
};

// The artist's existing Open morph contracts these front rings. Manual intake
// fixes its expansion scalar rather than replacing or rescaling the mesh.
// Measurements from the original saved native export are retained in
// Tools/Unreal/manual_intake_aperture.json; no derived visual asset is needed.
const FVector FrontApertureBasis[]={
    {152.00694275,416.39166260,-727.80169678},
    {0.00003723,425.84802246,-742.89849854},
    {-152.00688171,416.39166260,-727.80169678},
    {-341.01901245,393.97171021,-650.09130859},
    {-449.76574707,377.77029419,-560.92285156},
    {-496.06015015,351.92568970,-443.79141235},
    {-473.40542603,332.47659302,-308.82653809},
    {-444.55258179,312.75143433,-248.34115601},
    {-357.68313599,276.94686890,-138.98298645},
    {-238.20034790,264.52090454,-65.03102112},
    {-126.99697113,257.39025879,-37.32032776},
    {0.00002229,255.02284241,-30.13883972},
    {126.99701691,257.39025879,-37.32032776},
    {238.20040894,264.52084351,-65.03102112},
    {357.68319702,276.94680786,-138.98298645},
    {444.55264282,312.75137329,-248.34115601},
    {473.40548706,332.47653198,-308.82653809},
    {496.06021118,351.92562866,-443.79141235},
    {449.76580811,377.77023315,-560.92285156},
    {341.01907349,393.97164917,-650.09130859},
};
const FVector FrontApertureOpen[]={
    {1.24247742,268.05297852,-425.72698975},
    {-0.00150244,268.13021851,-425.85040283},
    {-1.24241638,268.05297852,-425.72698975},
    {-2.78738403,267.86968994,-425.09179688},
    {-3.67620850,267.73730469,-424.36291504},
    {-4.05462646,267.52606201,-423.40551758},
    {-3.86959839,267.36709595,-422.30230713},
    {-3.63385010,267.20584106,-421.80792236},
    {-2.92355347,266.91314697,-420.91406250},
    {-1.94699097,266.81158447,-420.30957031},
    {-1.03795624,266.75329590,-420.08306885},
    {-0.00150256,266.73394775,-420.02435303},
    {1.03800201,266.75329590,-420.08306885},
    {1.94705200,266.81158447,-420.30957031},
    {2.92361450,266.91314697,-420.91406250},
    {3.63391113,267.20584106,-421.80792236},
    {3.86965942,267.36709595,-422.30230713},
    {4.05465698,267.52606201,-423.40551758},
    {3.67623901,267.73730469,-424.36291504},
    {2.78741455,267.86968994,-425.09179688},
};

struct FIntakeApertureProfile {
    FVector Center,Normal,Up;
    FVector Boundary[UE_ARRAY_COUNT(ExitApertureBoundary)];
    double HalfWidth=0,HalfHeight=0;
    bool bFixedFront=false;
};
FIntakeApertureProfile IntakeProfile(const AMCThroat* Throat)
{
    FIntakeApertureProfile Profile;
    static const FName ArtistExitName(TEXT("SK_Exit"));
    Profile.bFixedFront=Throat->bManualIntake && Throat->AuthoredMouth
        && Throat->AuthoredMouth->GetSkeletalMeshAsset()
        && Throat->AuthoredMouth->GetSkeletalMeshAsset()->GetFName()==ArtistExitName;
    if(Profile.bFixedFront) {
        const float Expansion=FMath::Clamp(Throat->ManualAuthoredExpansion,.05f,1.f)*Throat->OpenAmount();
        const float Morph=Throat->bReverseAuthoredOpen?1-Expansion:Expansion;
        Profile.Center=FMath::Lerp(FVector(.00003081,339.24208692,-386.35745988),
            FVector(.00001988,267.42235677,-422.93604172),Morph);
        Profile.Normal=FVector(0,.9747185974,.2234360218);
        Profile.Up=FVector(0,-.2234360218,.9747185974);
        for(int32 I=0;I<UE_ARRAY_COUNT(Profile.Boundary);++I)
            Profile.Boundary[I]=FMath::Lerp(FrontApertureBasis[I],FrontApertureOpen[I],Morph);
    } else {
        Profile.Center=ExitApertureCenter;Profile.Normal=ExitApertureNormal;Profile.Up=ExitApertureUp;
        for(int32 I=0;I<UE_ARRAY_COUNT(Profile.Boundary);++I) Profile.Boundary[I]=ExitApertureBoundary[I];
    }
    for(const FVector& Vertex:Profile.Boundary) {
        Profile.HalfWidth=FMath::Max(Profile.HalfWidth,FMath::Abs(Vertex.X-Profile.Center.X));
        Profile.HalfHeight=FMath::Max(Profile.HalfHeight,FMath::Abs(FVector::DotProduct(Vertex-Profile.Center,Profile.Up)));
    }
    return Profile;
}

}

bool FMCThroatSuctionState::IsActive(double ServerTime) const
{
    return Duration>0 && ServerTime>=StartedAt && ServerTime<StartedAt+Duration;
}
float FMCThroatSuctionState::Envelope(double ServerTime) const
{
    if(!IsActive(ServerTime)) return 0;
    const float Age=float(ServerTime-StartedAt);
    const float Attack=FMath::Min(.12f,Duration*.2f);
    const float Release=FMath::Min(.35f,Duration*.25f);
    return FMath::SmoothStep(0.f,Attack,Age)*(1-FMath::SmoothStep(Duration-Release,Duration,Age));
}
float FMCThroatSuctionState::StrengthAt(FVector Location,double ServerTime) const
{
    const float Pulse=Envelope(ServerTime);
    if(Pulse<=0 || InfluenceRadius<=0) return 0;
    const float Distance=FVector::Distance(Location,Origin)/InfluenceRadius;
    if(Distance>=1) return 0;
    // A mild background draft reaches the whole room; the last 12% feathers
    // into zero instead of a hard boundary beyond the play space.
    return Pulse*(.18f+.82f*FMath::Pow(1-Distance,1.7f))*(1-FMath::SmoothStep(.88f,1.f,Distance));
}
FVector FMCThroatSuctionState::VelocityAt(FVector Location,double ServerTime) const
{
    const FVector Toward=Origin-Location;
    // Never lift players off the tongue, and stop gently at the inlet.
    const float Arrival=FMath::SmoothStep(35.f,135.f,float(Toward.Size2D()));
    return Toward.GetSafeNormal2D()*FMath::Clamp(MaxPullSpeed,0.f,150.f)*StrengthAt(Location,ServerTime)*Arrival;
}

AMCThroat::AMCThroat()
{
    bAlwaysRelevant=true;
    SetNetUpdateFrequency(20);
    Tissue=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LivingTissue")); Tissue->SetupAttachment(Volume);
    Tissue->SetCollisionEnabled(ECollisionEnabled::NoCollision); Tissue->SetCastShadow(true);
    SculptedTissue=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SculptedTissue")); SculptedTissue->SetupAttachment(Volume);
    SculptedTissue->SetCollisionEnabled(ECollisionEnabled::NoCollision); SculptedTissue->SetCastShadow(true);
    SculptedTissue->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SculptedTissue->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Gameplay/Throat/SK_Throat.SK_Throat")));
    SculptedTissue->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    AuthoredMouth=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AuthoredMouth")); AuthoredMouth->SetupAttachment(Volume);
    AuthoredMouth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AuthoredMouth->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    AuthoredMouth->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    TissueContact=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuthoredTissueContact"));
    TissueContact->SetupAttachment(AuthoredMouth);
    TissueContact->SetCollisionProfileName(TEXT("BlockAll"));
    TissueContact->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    TissueContact->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TissueContact->SetVisibility(false);TissueContact->SetHiddenInGame(true);
    TissueContact->SetCastShadow(false);TissueContact->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ContactMesh(TEXT("/Game/Gameplay/Arena/ManualIntake/SM_ThroatTissueContact.SM_ThroatTissueContact"));
    TissueContact->SetStaticMesh(ContactMesh.Object);
    Uvula=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Uvula")); Uvula->SetupAttachment(Volume);
    Uvula->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UvulaLanding=CreateDefaultSubobject<UBoxComponent>(TEXT("UvulaLanding")); UvulaLanding->SetupAttachment(Volume);
    UvulaLanding->SetBoxExtent(FVector(28,38,16)); UvulaLanding->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UvulaLanding->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore); UvulaLanding->SetCanEverAffectNavigation(false);
    ClosedBarrier=CreateDefaultSubobject<UBoxComponent>(TEXT("ClosedThroat")); ClosedBarrier->SetupAttachment(Volume);
    ClosedBarrier->SetCollisionProfileName(TEXT("BlockAllDynamic")); ClosedBarrier->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    ZoneRing=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FoodZoneRing")); ZoneRing->SetupAttachment(Volume);
    ZoneRing->SetCollisionEnabled(ECollisionEnabled::NoCollision); ZoneRing->SetCastShadow(false);
    Label->SetWorldSize(21); Label->SetRelativeRotation(FRotator(0,180,0));
    TissueMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_LivingThroat.MI_LivingThroat"));
    RingMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_ThroatRing.MI_ThroatRing"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Uvula->SetStaticMesh(Sphere.Object);
}
void AMCThroat::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ManualAuthoredExpansion=FMath::Clamp(ManualAuthoredExpansion,.05f,1.f);
    GateSize.X=FMath::Max(100.f,GateSize.X); GateSize.Y=FMath::Max(100.f,GateSize.Y);
    ZoneRadius=FMath::Max(80.f,ZoneRadius); ZoneHeight=FMath::Max(40.f,ZoneHeight);
    DeliveryStripHalfExtent.X=FMath::Max(80.,DeliveryStripHalfExtent.X);DeliveryStripHalfExtent.Y=FMath::Max(80.,DeliveryStripHalfExtent.Y);
    PressSeconds=FMath::Max(.1f,PressSeconds); AnticipationSeconds=FMath::Max(.1f,AnticipationSeconds);
    SwallowSeconds=FMath::Max(.5f,SwallowSeconds); RecoverySeconds=FMath::Max(.2f,RecoverySeconds);
    VomitSeconds=FMath::Max(2.f,VomitSeconds);
    SuctionRadius=FMath::Max(1000.f,SuctionRadius);SuctionPullSpeed=FMath::Clamp(SuctionPullSpeed,0.f,150.f);
    Tissue->SetMaterial(0,TissueMaterial);
    if(!bUseAuthoredUvula) Uvula->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_MouthPalate.MI_MouthPalate")));
    SculptedTissue->SetRelativeLocation(GateCenter);
    SculptedTissue->SetRelativeScale3D(FVector(1,GateSize.X/660,GateSize.Y/500));
    SculptedTissue->SetMaterial(0,TissueMaterial);
    Tissue->SetVisibility(!SculptedTissue->GetSkeletalMeshAsset());
    ClosedBarrier->SetRelativeLocation(GateCenter+FVector(45,0,250));
    ClosedBarrier->SetBoxExtent(FVector(35,GateSize.X*1.55,GateSize.Y*1.3));
    UpdateTissue(0,0,true); BuildRing(); UpdatePresentation(0);
}
void AMCThroat::BeginPlay()
{
    Super::BeginPlay();
    // Read this frame's completed physics movement against food's PrePhysics
    // position, including the very first frame after release.
    SetTickGroup(TG_PostPhysics);
    if (!bUseAuthoredUvula) if (auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Throat/SM_Uvula.SM_Uvula"))) Uvula->SetStaticMesh(Mesh);
    if (!TissueMaterial) TissueMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_LivingThroat.MI_LivingThroat"));
    if (!RingMaterial) RingMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_ThroatRing.MI_ThroatRing"));
    Tissue->SetMaterial(0,TissueMaterial);
    if(!bUseAuthoredUvula) Uvula->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_MouthPalate.MI_MouthPalate")));
    if(!SculptedTissue->GetSkeletalMeshAsset()) SculptedTissue->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Gameplay/Throat/SK_Throat.SK_Throat")));
    SculptedTissue->SetRelativeLocation(GateCenter); SculptedTissue->SetRelativeScale3D(FVector(1,GateSize.X/660,GateSize.Y/500));
    SculptedTissue->SetMaterial(0,TissueMaterial); Tissue->SetVisibility(!SculptedTissue->GetSkeletalMeshAsset());
    if (RingMaterial) { RingMID=UMaterialInstanceDynamic::Create(RingMaterial,this); ZoneRing->SetMaterial(0,RingMID); }
    BuildRing(); UpdatePresentation(0);
    if(GetNetMode()!=NM_DedicatedServer && AuthoredMouth->GetSkeletalMeshAsset()) {
        // The authored exit is an open mesh. Give its opening a shaded throat
        // interior so inhalation reveals depth rather than the sky outside.
        auto* Depth=NewObject<UProceduralMeshComponent>(this,TEXT("ThroatDepth"));
        Depth->SetupAttachment(AuthoredMouth);Depth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Depth->SetCastShadow(false);AddInstanceComponent(Depth);Depth->RegisterComponent();
        TArray<FVector> V,N;TArray<FVector2D> UV;TArray<int32> Tri;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
        const FIntakeApertureProfile Profile=IntakeProfile(this);
        constexpr int32 Segments=UE_ARRAY_COUNT(ExitApertureBoundary),Rings=7;
        for(int32 Ring=0;Ring<Rings;++Ring) {
            const float T=float(Ring)/(Rings-1),Scale=FMath::Lerp(1.025f,0.f,T);
            for(int32 I=0;I<=Segments;++I) {
                V.Add(Profile.Center+(Profile.Boundary[I%Segments]-Profile.Center)*Scale+Profile.Normal*(12+T*400));
                N.Add(-Profile.Normal);UV.Add(FVector2D(float(I)/Segments,T));
                Colors.Add(FLinearColor::LerpUsingHSV(FLinearColor(.025f,.003f,.006f,1),FLinearColor(.001f,.0001f,.0002f,1),T));
                Tangents.Add(FProcMeshTangent(1,0,0));
                if(Ring>0 && I>0) {const int32 B=(Ring-1)*(Segments+1)+I-1,C=B+Segments+1;Tri.Append({B,C,B+1,B+1,C,C+1});}
            }
        }
        Depth->CreateMeshSection_LinearColor(0,V,Tri,N,UV,Colors,Tangents,false);
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_ReactionSoft.M_ReactionSoft"))) {
            auto* Interior=UMaterialInstanceDynamic::Create(Base,this);
            Interior->SetVectorParameterValue(TEXT("Color"),FLinearColor::White);
            Interior->SetScalarParameterValue(TEXT("Opacity"),1);
            Depth->SetMaterial(0,Interior);
        }
    }
}
void AMCThroat::RebuildAppearance() { OnConstruction(GetActorTransform()); }
void AMCThroat::EndPlay(const EEndPlayReason::Type Reason) { ResetSwallow(); Super::EndPlay(Reason); }
double AMCThroat::ServerNow() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}
bool AMCThroat::ContainsFood(const AMCFoodActor* Food) const
{
    if (!IsValid(Food) || Food->bBrushTool || Food->IsDisposed() || Food->Phase==EMCFoodPhase::Stuck
        || Food->StackCarrier || Food->Phase==EMCFoodPhase::Equipped || Food->Phase==EMCFoodPhase::Swallowing || Food->Phase==EMCFoodPhase::Absorbing || !Food->Holders.IsEmpty()) return false;
    if(bManualIntake) return !Food->IsMouthEntryActive() && IsInsideIntakeAperture(Food->GetActorLocation());
    const FVector P=GetActorTransform().InverseTransformPosition(Food->GetActorLocation())-ZoneCenter;
    bool HasCap=false;const bool InCap=ContainsDeliveryCap(Food->GetActorLocation(),HasCap);
    if(HasCap)
    {
        if(!InCap) return false;
        double FloorZ;
        return DeliverySurfaceFloorZ(Food->GetActorLocation(),FloorZ) && Food->GetActorLocation().Z>=FloorZ-30*FMath::Abs(GetActorScale3D().Z) && P.Z<=ZoneHeight;
    }
    return ContainsDeliveryPosition(Food->GetActorLocation()) && P.Z>=-30 && P.Z<=ZoneHeight;
}
void AMCThroat::GetDeliveryZoneGeometry(FTransform& OutTransform,FVector& OutHalfExtent,bool& bOutCircular) const
{
    if(bManualIntake) {OutTransform=GetActorTransform();OutTransform.SetLocation(VacuumInlet());OutHalfExtent=FVector(45,GateSize.X*.65f,GateSize.Y*.5f);bOutCircular=false;return;}
    OutTransform=GetActorTransform();OutTransform.SetLocation(GetActorTransform().TransformPosition(ZoneCenter));
    OutHalfExtent=bUseDeliveryStrip?FVector(DeliveryStripHalfExtent.X,DeliveryStripHalfExtent.Y,ZoneHeight):FVector(ZoneRadius,ZoneRadius,ZoneHeight);
    bOutCircular=!bUseDeliveryStrip;
}
bool AMCThroat::ContainsDeliveryPosition(FVector Position) const
{
    if(bManualIntake) return IsInsideIntakeAperture(Position);
    if(Position.ContainsNaN()) return false;
    const FVector P=GetActorTransform().InverseTransformPosition(Position)-ZoneCenter;
    bool HasCap=false;const bool InCap=ContainsDeliveryCap(Position,HasCap);
    if(HasCap)
    {
        if(!InCap) return false;
        double FloorZ;
        return DeliverySurfaceFloorZ(Position,FloorZ) && Position.Z>=FloorZ-80*FMath::Abs(GetActorScale3D().Z) && P.Z<=FMath::Max(ZoneHeight,UvulaTop.Z-ZoneCenter.Z+160);
    }
    const bool InFootprint=HasCap?InCap:bUseDeliveryStrip?FMath::Abs(P.X)<=DeliveryStripHalfExtent.X && FMath::Abs(P.Y)<=DeliveryStripHalfExtent.Y:P.SizeSquared2D()<=FMath::Square(ZoneRadius);
    return InFootprint && P.Z>=-80 && P.Z<=FMath::Max(ZoneHeight,UvulaTop.Z-ZoneCenter.Z+160);
}
bool AMCThroat::CanAcceptDelivery(const AMCFoodActor* Food) const
{
    if(bManualIntake) return HasAuthority() && IsValid(Food) && Food->GetWorld()==GetWorld()
        && !Food->bBrushTool && !Food->IsDisposed() && !Food->IsMouthEntryActive() && Food->WasManuallyThrown()
        && !Food->StackCarrier && !Food->EquippedBy && Food->Holders.IsEmpty()
        && (Food->Phase==EMCFoodPhase::Free || Food->Phase==EMCFoodPhase::Falling)
        && (!Food->IsWrongIngredient() || (ThroatPhase!=EMCThroatPhase::Spasm && ThroatPhase!=EMCThroatPhase::Vomiting))
        && IsInsideIntakeAperture(Food->GetActorLocation());
    if(!HasAuthority() || !IsValid(Food) || Food->GetWorld()!=GetWorld() || Food->bBrushTool || Food->IsDisposed()
        || (!Food->bFragment && !Food->Holders.IsEmpty())
        || (Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Falling && !(Food->bFragment && Food->Phase==EMCFoodPhase::Carried))) return false;
    if(const auto* Carrier=Food->StackCarrier.Get()) return !Food->UsesLegacyGrip() && Carrier->CanWork() && ContainsPlayer(Carrier);
    if(Food->bFragment) return ContainsDeliveryPosition(Food->GetActorLocation());
    return ContainsFood(Food);
}
bool AMCThroat::AcceptDelivery(AMCFoodActor* Food)
{
    if(!CanAcceptDelivery(Food)) return false;
    if(bManualIntake) return AcceptThrownDelivery(Food);
    if(Food->bFragment && !Food->IsWrongIngredient()) {
        auto* Carrier=Food->StackCarrier.Get();
        auto* Worker=Food->GetLastHandledBy()?Cast<AMCToothCharacter>(Food->GetLastHandledBy()->GetPawn()):nullptr;
        if(!Worker) Worker=Carrier;
        // Credit each piece, but show success once when this carrier's matching fragments are gone.
        const bool FinalFragment=!Carrier || !Carrier->FoodCollection->Pieces.ContainsByPredicate([this,Food,Carrier](const auto& Piece) {
            return IsValid(Piece) && Piece!=Food && Piece->bFragment && Piece->StackCarrier==Carrier
                && !Piece->IsWrongIngredient() && CanAcceptDelivery(Piece);
        });
        const FVector Point=Food->GetActorLocation();
        // Use the atomic handoff to remove this layer without waking it or spilling neighbours.
        if(Food->StackCarrier && !Food->BeginSwallow()) return false;
        Food->AwardDelivery();Food->Dispose();++FoodSwallowed;
        if(Worker && FinalFragment) Worker->NotifyTaskFeedback(true,Point);
        ForceNetUpdate();return true;
    }
    if (AMCTutorialDirector::IsSafeTutorial(GetWorld()) && AMCTutorialDirector::IsTutorialTarget(Food) && Food->IsWrongIngredient())
    {
        auto* Worker=Food->GetLastHandledBy()?Cast<AMCToothCharacter>(Food->GetLastHandledBy()->GetPawn()):nullptr;
        if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->NotifyIncorrectSort(Worker,Food);
        if (auto* Carrier=Food->StackCarrier.Get()) Carrier->FoodCollection->Stop(false);
        const float InwardEdge=bUseDeliveryStrip?DeliveryStripHalfExtent.X:ZoneRadius;
        Food->SetActorLocation(GetActorTransform().TransformPosition(ZoneCenter+FVector(-InwardEdge-120,0,60)),false,nullptr,ETeleportType::TeleportPhysics);
        Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
        return false;
    }
    if(!Food->BeginSwallow()) return false;
    const int32 Slot=PendingMeal.Num()%6,Column=PendingMeal.Num()/6;
    if(Slot==0) PendingPileHeight=0;
    const float HalfHeight=Food->PrepareHorizontalStackPose(&Food->FoodData.Stack,Slot);
    const FQuat Rotation=Food->StackRestRotation(FRotator(0,GetActorRotation().Yaw,0).Quaternion());
    const FVector Offset=FVector(Food->StackPickup.SlotOffset);
    // Keep delivered stacks on the tongue in small separate piles. Ingredients
    // are now kinematic: no heap contacts or pickup sweeps run during the wait.
    FVector Base=GetActorTransform().TransformPosition(ZoneCenter+FVector(-55+(Column/3)*80,(Column%3-1)*80,0));
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
        FHitResult Hit;if(It->SurfacePoint(Base,Hit)) {Base=Hit.ImpactPoint+FVector(0,0,3);break;}
    }
    PendingPileHeight+=HalfHeight;
    const FVector Position=Base+FVector(0,0,PendingPileHeight)+FRotator(0,GetActorRotation().Yaw,0).RotateVector(Offset);
    PendingPileHeight+=HalfHeight+Food->FoodData.Stack.SafeLayerGap();
    Food->SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    Food->PauseFuse(this);Food->ForceNetUpdate();
    PendingMeal.Add({Food,Position,Rotation});
    QueuedFoodCount=PendingMeal.Num();FoodInZone=QueuedFoodCount+Meal.Num();
    // The deadline belongs to the first delivery. A teammate joining this
    // batch never extends it; deliveries during a gulp wait for the next cycle.
    if(ThroatPhase==EMCThroatPhase::Collecting) SetPhase(EMCThroatPhase::Anticipation,ServerNow());
    ForceNetUpdate();return true;
}
void AMCThroat::CollectCarriedDeliveries()
{
    if(bManualIntake) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->CanWork() && ContainsPlayer(*It)) {
        const auto Pieces=It->FoodCollection->Pieces;
        for(const auto& Piece:Pieces) if(IsValid(Piece)) AcceptDelivery(Piece);
    }
}
void AMCThroat::CollectLooseDeliveries()
{
    // Expelled ingredients must first leave the mouth; the vomit animation
    // must not reserve its own outgoing food into the next batch.
    if(ThroatPhase==EMCThroatPhase::Spasm || ThroatPhase==EMCThroatPhase::Vomiting) return;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if((It->bFragment && ContainsDeliveryPosition(It->GetActorLocation())) || ContainsFood(*It)) AcceptDelivery(*It);
}
void AMCThroat::IntakeFrame(FVector& Center,FVector& Inward,FVector& Right,FVector& Up) const
{
    if(AuthoredMouth && AuthoredMouth->GetSkeletalMeshAsset()) {
        const FTransform Art=AuthoredMouth->GetComponentTransform();
        const auto Profile=IntakeProfile(this);
        Center=Art.TransformPosition(Profile.Center);
        Inward=Art.TransformVectorNoScale(Profile.Normal).GetSafeNormal();
        Right=Art.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
        Up=Art.TransformVectorNoScale(Profile.Up).GetSafeNormal();
    } else {
        Center=GetActorTransform().TransformPosition(GateCenter+FVector(0,0,230));
        Inward=GetActorForwardVector();Right=GetActorRightVector();Up=GetActorUpVector();
    }
}
bool AMCThroat::IsInsideIntakeAperture(FVector Point,float InwardDepth) const
{
    if(Point.ContainsNaN()) return false;
    FVector Center,Inward,Right,Up;IntakeFrame(Center,Inward,Right,Up);
    const double Depth=FVector::DotProduct(Point-Center,Inward)-InwardDepth;
    if(Depth<0 || Depth>500+FMath::Max(0.f,-InwardDepth)) return false;
    if(AuthoredMouth && AuthoredMouth->GetSkeletalMeshAsset()) {
        // The authored lower aperture is buried under the tongue. Only its
        // visible upper opening receives food or players, never a route under the floor.
        if(GetWorld()) for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
            FHitResult Floor;
            if(It->SurfacePoint(GetActorTransform().TransformPosition(ZoneCenter),Floor)) {
                if(Point.Z<Floor.ImpactPoint.Z-10.f) return false;
                break;
            }
        }
        // Use the measured artist opening rather than the former floor-wide cap.
        // This remains valid when the complete mouth is scaled/rotated in the level.
        const FTransform Art=AuthoredMouth->GetComponentTransform();
        const auto Profile=IntakeProfile(this);
        const FVector Local=Art.InverseTransformPosition(Point);
        const FVector2D P(Local.X,FVector::DotProduct(Local-Profile.Center,Profile.Up));
        const FVector& Last=Profile.Boundary[UE_ARRAY_COUNT(Profile.Boundary)-1];
        FVector2D A(Last.X,FVector::DotProduct(Last-Profile.Center,Profile.Up));
        bool Inside=false;
        for(const FVector& Vertex:Profile.Boundary) {
            const FVector2D B(Vertex.X,FVector::DotProduct(Vertex-Profile.Center,Profile.Up));
            if((A.Y>P.Y)!=(B.Y>P.Y) && P.X<(B.X-A.X)*(P.Y-A.Y)/(B.Y-A.Y)+A.X) Inside=!Inside;
            A=B;
        }
        return Inside;
    }
    const FVector Offset=Point-Center;
    const FVector2D HalfSize(GateSize.X*.9f,GateSize.Y*.9f);
    return FMath::Square(FVector::DotProduct(Offset,Right)/FMath::Max(1.,HalfSize.X))
        +FMath::Square(FVector::DotProduct(Offset,Up)/FMath::Max(1.,HalfSize.Y))<=1;
}
bool AMCThroat::CrossesIntakeAperture(FVector From,FVector To) const
{
    FVector Center,Inward,Right,Up;IntakeFrame(Center,Inward,Right,Up);
    const double Before=FVector::DotProduct(From-Center,Inward);
    const double After=FVector::DotProduct(To-Center,Inward);
    if(Before>=0 || After<0 || After-Before<=SMALL_NUMBER) return false;
    // Intersect the swept center with the intake plane so fast throws cannot skip it.
    const FVector Crossing=FMath::Lerp(From,To,-Before/(After-Before));
    return IsInsideIntakeAperture(Crossing+Inward*.01f);
}
bool AMCThroat::GetIntakeAimTarget(FVector& Center,float& Radius) const
{
    Center=VacuumInlet();Radius=FMath::Max(60.f,FMath::Min(GateSize.X,GateSize.Y)*.32f)*FMath::Abs(GetActorScale3D().Z);
    const auto Profile=IntakeProfile(this);
    if(Profile.bFixedFront) Radius=Profile.HalfHeight*.72f*FMath::Abs(AuthoredMouth->GetComponentScale().Z);
    return bManualIntake && OpenAmount()>=.65f && !Center.ContainsNaN();
}
float AMCThroat::IntakeAimStrength(FVector ViewOrigin,FVector ViewDirection) const
{
    FVector Center;float Radius;
    if(!GetIntakeAimTarget(Center,Radius) || ViewOrigin.ContainsNaN() || ViewDirection.ContainsNaN()) return 0;
    const FVector Offset=Center-ViewOrigin,Direction=ViewDirection.GetSafeNormal();
    const float Distance=Offset.Size();
    if(Distance<=SMALL_NUMBER || Direction.IsNearlyZero()) return 0;
    const float Angle=FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Direction,Offset/Distance)),-1.f,1.f));
    const float HoleAngle=FMath::Asin(FMath::Clamp(Radius/Distance,0.f,1.f));
    const float Margin=FMath::DegreesToRadians(FMath::Max(.1f,AimAssistMarginDegrees));
    if(Angle>HoleAngle+Margin) return 0;
    // The world trace below ignores this actor (including decorative throat
    // parts), so explicitly test its physical tissue before revealing the aim.
    if(TissueContact && TissueContact->IsCollisionEnabled()) {
        FHitResult TissueHit;
        const FCollisionQueryParams TissueQuery(SCENE_QUERY_STAT(MCIntakeTissueAim),true);
        if(TissueContact->LineTraceComponent(TissueHit,ViewOrigin,Center,TissueQuery)) return 0;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIntakeAim),false,this);
    if(auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->GetPawn()) {
        Query.AddIgnoredActor(PC->GetPawn());
        if(const auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn())) {
            if(Hero->FoodCollection) for(const auto& Piece:Hero->FoodCollection->Pieces) if(IsValid(Piece)) Query.AddIgnoredActor(Piece.Get());
            if(IsValid(Hero->HeldFood)) Query.AddIgnoredActor(Hero->HeldFood.Get());
        }
    }
    FHitResult Hit;
    if(GetWorld()->LineTraceSingleByChannel(Hit,ViewOrigin,Center,ECC_Visibility,Query) && Hit.Distance<Distance-Radius*.5f) return 0;
    return 1-FMath::SmoothStep(HoleAngle,HoleAngle+Margin,Angle);
}
bool AMCThroat::AcceptThrownDelivery(AMCFoodActor* Food)
{
    if(!HasAuthority() || !IsValid(Food) || Food->GetWorld()!=GetWorld() || Food->IsDisposed() || Food->bBrushTool || Food->IsMouthEntryActive()
        || !Food->WasManuallyThrown()
        || Food->StackCarrier || Food->EquippedBy || !Food->Holders.IsEmpty()
        || (Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Falling)) return false;
    if(!Food->BeginSwallow()) return false;
    Food->PauseFuse(this);
    FVector Inlet,Axis,Right,Up;IntakeFrame(Inlet,Axis,Right,Up);
    const float Seed=(Food->GetUniqueID()%4096)*2.399963f;
    const float Duration=FMath::Clamp(IntakePullSeconds,.5f,2.f)+.18f*FMath::Frac(Seed);
    FVector Aim;float ApertureRadius=80.f;GetIntakeAimTarget(Aim,ApertureRadius);
    Inlet=Aim;
    ManualIntakes.Add({Food,Food->GetActorLocation(),Inlet,Axis,Right,Up,Food->GetActorQuat(),ServerNow(),Duration,Seed,FMath::Clamp(ApertureRadius*.12f,12.f,55.f)});
    QueuedFoodCount=ManualIntakes.Num();FoodInZone=QueuedFoodCount+Meal.Num();
    RefreshManualVortex();ForceNetUpdate();
    return true;
}
void AMCThroat::RefreshManualVortex()
{
    if(ManualIntakes.IsEmpty()) return;
    const auto& First=ManualIntakes[0];
    TArray<AMCFoodActor*> Sources;
    float Remaining=.5f;
    for(const auto& Piece:ManualIntakes) {
        if(auto* Food=Piece.Food.Get();IsValid(Food) && !Food->IsDisposed()) Sources.Add(Food);
        Remaining=FMath::Max(Remaining,float(Piece.StartedAt+Piece.Duration-ServerNow())+.25f);
    }
    // Dedicated servers intentionally disable presentation ticking. Only the
    // authoritative lifetime determines whether this shared effect has ended.
    if(ActiveVortex.IsValid() && ServerNow()>=ActiveVortex->StartedAt+ActiveVortex->Duration) {
        ActiveVortex->Destroy();ActiveVortex.Reset();
    }
    if(!ActiveVortex.IsValid()) {
        ActiveVortex=AMCThroatVortex::Spawn(GetWorld(),First.Inlet,First.Axis,
            FMath::Clamp(First.Radius*2.5f,80.f,150.f),FMath::Clamp(IntakeCaptureDepth,80.f,600.f),Remaining,Sources,180.f,150.f);
    } else {
        auto* Vortex=ActiveVortex.Get();
        Vortex->Duration=float(ServerNow()-Vortex->StartedAt)+Remaining;
        Vortex->FoodSources.Reset();
        for(int32 I=0;I<FMath::Min(24,Sources.Num());++I) Vortex->FoodSources.Add(Sources[I]);
        Vortex->SetLifeSpan(Remaining+.15f);Vortex->ForceNetUpdate();
    }
}
void AMCThroat::UpdateManualIntakes()
{
    const double Now=ServerNow();bool Changed=false;
    for(int32 I=ManualIntakes.Num()-1;I>=0;--I) {
        const auto Piece=ManualIntakes[I];auto* Food=Piece.Food.Get();
        if(!IsValid(Food) || Food->IsDisposed() || Food->Phase!=EMCFoodPhase::Swallowing) {
            if(IsValid(Food)) Food->ResumeFuse(this);
            ManualIntakes.RemoveAt(I);Changed=true;continue;
        }
        const float T=FMath::Clamp(float(Now-Piece.StartedAt)/Piece.Duration,0.f,1.f);
        const float Alpha=FMath::SmoothStep(0.f,1.f,T),Angle=Piece.Seed+Alpha*PI*3.5f;
        const float Curl=FMath::Sin(Alpha*PI)*Piece.Radius;
        const FVector Point=FMath::Lerp(Piece.Start,Piece.Inlet+Piece.Axis*240.f,Alpha)
            +(Piece.Right*FMath::Cos(Angle)+Piece.Up*FMath::Sin(Angle))*Curl;
        Food->SetActorLocationAndRotation(Point,FQuat(Piece.Axis,Alpha*PI*2.5f)*Piece.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
        if(T<1.f) continue;
        auto* Worker=Food->GetLastHandledBy()?Cast<AMCToothCharacter>(Food->GetLastHandledBy()->GetPawn()):nullptr;
        if(Food->IsWrongIngredient()) {
            // The visible intake finishes before the existing gag/vomit response.
            Meal.Add({Food,Food->GetActorLocation(),Food->GetActorQuat()});
            ++SpasmCount;SetPhase(EMCThroatPhase::Spasm,Now);
            if(Worker) {
                if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->NotifyIncorrectSort(Worker,Food);
                Worker->NotifyTaskFeedback(false,Piece.Inlet);
            }
        } else {
            Food->AwardDelivery();Food->Dispose();++FoodSwallowed;++SwallowCount;++MealSequence;
            if(Worker && !AMCTutorialDirector::IsSafeTutorial(GetWorld())) Worker->NotifyTaskFeedback(true,Piece.Inlet);
        }
        ManualIntakes.RemoveAt(I);Changed=true;
    }
    if(Changed) {QueuedFoodCount=ManualIntakes.Num();FoodInZone=QueuedFoodCount+Meal.Num();ForceNetUpdate();}
}
void AMCThroat::CollectThrownDeliveries()
{
    for(auto It=PreviousThrownPositions.CreateIterator();It;++It) {
        const auto* Food=It.Key().Get();
        if(!IsValid(Food) || Food->IsDisposed() || Food->IsMouthEntryActive() || Food->StackCarrier || Food->EquippedBy || !Food->Holders.IsEmpty()) It.RemoveCurrent();
    }
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
        auto* Food=*It;
        if(Food->bBrushTool || Food->IsDisposed() || Food->IsMouthEntryActive() || !Food->WasManuallyThrown()
            || Food->StackCarrier || Food->EquippedBy || !Food->Holders.IsEmpty()
            || (Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Falling)) continue;
        const FVector Position=Food->GetActorLocation();
        if(Food->IsWrongIngredient() && (ThroatPhase==EMCThroatPhase::Spasm || ThroatPhase==EMCThroatPhase::Vomiting)) continue;
        const FVector* Tracked=PreviousThrownPositions.Find(Food);
        const FVector Previous=Tracked?*Tracked:Food->IntakeSweepStart();
        FVector Center,Inward,Right,Up;IntakeFrame(Center,Inward,Right,Up);
        // Use the aperture silhouette, excluding resting food and rim misses.
        const bool Incoming=FVector::DotProduct(Position-Previous,Inward)>.05f;
        const bool FrontCapture=Incoming && FVector::DotProduct(Position-Center,Inward)<0
            && IsInsideIntakeAperture(Position,-FMath::Clamp(IntakeCaptureDepth,80.f,600.f));
        if((FrontCapture || IsInsideIntakeAperture(Position) || CrossesIntakeAperture(Previous,Position)) && AcceptThrownDelivery(Food)) {
            PreviousThrownPositions.Remove(Food);
        } else PreviousThrownPositions.Add(Food,Position);
    }
}
void AMCThroat::ReactToWaterTickle(AMCToothCharacter* Source,FVector Impact)
{
    if(!HasAuthority() || !IsValid(Source) || Source->GetWorld()!=GetWorld() || Impact.ContainsNaN()) return;
    if(bUseAuthoredUvula && !Uvula->Bounds.GetBox().ExpandBy(15).IsInsideOrOn(Impact)) return;
    if(ServerNow()-TickleStartedAt<.6) return;
    TickleStartedAt=ServerNow();ForceNetUpdate();
}

void AMCThroat::CheckOpenThroatPlayers()
{
    if(!HasAuthority() || OpenAmount()<.65f || AMCTutorialDirector::IsSafeTutorial(GetWorld())) return;
    const FVector Inlet=VacuumInlet(),Forward=GetActorForwardVector().GetSafeNormal2D(),Side=GetActorRightVector().GetSafeNormal2D();
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        if(!It->Status || !It->Status->IsAlive() || It->SwallowedBy || It->IsMimicCaptured()) continue;
        const FVector Offset=It->GetActorLocation()-Inlet;
        const float Depth=FVector::DotProduct(Offset,Forward);
        // Crossing the aperture plane is lethal; the green delivery strip in
        // front of it remains usable. Waves and suction can push a player across.
        const bool Entered=bManualIntake?IsInsideIntakeAperture(It->GetActorLocation())
            :Depth>=-15 && Depth<500 && FMath::Abs(FVector::DotProduct(Offset,Side))<GateSize.X*.78f && FMath::Abs(Offset.Z)<GateSize.Y*.8f;
        if(Entered) {
            It->DropFood();
            It->Status->Damage(It->Status->State.MaxHealth*2,Forward,this);
        }
    }
}
bool AMCThroat::ContainsPlayer(const AMCToothCharacter* Hero) const
{
    if(!IsValid(Hero) || !Hero->Status->IsAlive()) return false;
    // A jumping carrier still counts as entering the delivery column.
    return ContainsDeliveryPosition(Hero->GetActorLocation());
}
bool AMCThroat::CanOrderJump(const AMCToothCharacter* Hero) const {return false;}
bool AMCThroat::LaunchToUvula(AMCToothCharacter* Hero) {return false;}
float AMCThroat::UvulaBodyClearance(FVector Center,float Radius,float HalfHeight) const
{
    // Circumscribed bands of the profile authored in refine_uvula_anatomy.py.
    // This also covers the palatal root, which has no gameplay collision mesh.
    const FVector2D Profile[]={{-1.6,380},{-1,180},{-.55,72},{-.20,37},{.16,29},{.40,32},{.60,40},{.78,46},{1,46}};
    const FTransform Shape=Uvula->GetComponentTransform();
    const FVector Scale=Shape.GetScale3D().GetAbs();
    const float Length=FMath::Max(1.f,100*Scale.Z);
    float Clearance=MAX_flt;
    const float Stem=FMath::Max(0.f,HalfHeight-Radius);
    for(int32 I=0;I<=8;++I) {
        const FVector P=Shape.InverseTransformPositionNoScale(Center+FVector(0,0,FMath::Lerp(-Stem,Stem,I/8.f)));
        const float T=FMath::Clamp(float(-P.Z/Length),-1.6f,1.f);
        float R=46;
        for(int32 J=1;J<UE_ARRAY_COUNT(Profile);++J) if(T<=Profile[J].X) { R=FMath::Max(Profile[J-1].Y,Profile[J].Y); break; }
        const float Rx=R*.82f*Scale.X+4,Ry=R*Scale.Y+4;
        const float Radial=(FVector2D(P.X/Rx,P.Y/Ry).Size()-1)*FMath::Min(Rx,Ry);
        const float Axial=FMath::Abs(float(P.Z+T*Length));
        const float Distance=Axial>0?FVector2D(FMath::Max(0.f,Radial),Axial).Size():Radial;
        // Four extra centimetres enclose the gaps between sampled capsule spheres.
        Clearance=FMath::Min(Clearance,Distance-Radius-4);
    }
    return Clearance;
}
bool AMCThroat::OrderVelocity(const AMCToothCharacter* Hero,FVector& Velocity) const
{
    const FVector Goal=UvulaLanding->GetComponentLocation()+FVector(0,0,16+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
    const FVector Start=Hero->GetActorLocation();
    const float Gravity=Hero->GetCharacterMovement()->GetGravityZ();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCOrderJump),false,Hero); Query.AddIgnoredActor(this);
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(ContainsFood(*It)) Query.AddIgnoredActor(*It);
    // Keep the original arc when it fits. A lower palate needs a shorter arc,
    // still descending onto the pad, with the same full-body clearance checks.
    for(float Flight:{1.05f,.9f,.75f,.65f}) {
        const FVector Candidate=(Goal-Start)/Flight+FVector(0,0,FMath::Abs(Gravity)*Flight*.5f);
        if(Candidate.Z+Gravity*Flight>-30) continue;
        bool Clear=true; FVector Previous=Start;
        for(int32 I=1;I<=32;++I) {
            const float T=Flight*I/32; const FVector P=Start+Candidate*T+FVector(0,0,Gravity*T*T*.5f);
            if(UvulaBodyClearance(P,Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+6,
                Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4)<2) {Clear=false;break;}
            FHitResult Hit;
            if(GetWorld()->SweepSingleByChannel(Hit,Previous,P,FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()*.85f,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.95f),Query)) {
                // The moving tongue can rise into the grounded capsule between
                // ticks. An upward takeoff may leave that initial floor contact.
                const bool LeavingFloor=I==1 && Hit.bStartPenetrating && Hit.GetComponent()==Hero->GetMovementBaseObject()
                    && Hit.ImpactNormal.Z>.65f && FVector::DotProduct(Candidate,Hit.ImpactNormal)>0;
                if(!LeavingFloor) {Clear=false;break;}
            }
            Previous=P;
        }
        if(Clear) {Velocity=Candidate;return true;}
    }
    return false;
}
void AMCThroat::UpdateOrderJumps()
{
    for(int32 I=PreparingPlayers.Num()-1;I>=0;--I) {
        auto* Hero=PreparingPlayers[I].Get();
        if(!Hero || Hero->OrderJumpTarget!=this) { PreparingPlayers.RemoveAtSwap(I); continue; }
        bool HasMeal=false;
        for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(ContainsFood(*It)) { HasMeal=true; break; }
        if(!Hero->CanWork() || !Hero->GetCharacterMovement()->IsMovingOnGround()
            || !ContainsPlayer(Hero) || ThroatPhase!=EMCThroatPhase::Collecting || !HasMeal) {
            Hero->ClearOrderJump(); PreparingPlayers.RemoveAtSwap(I); continue;
        }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        const FRotator Facing=(UvulaLanding->GetComponentLocation()-Hero->GetActorLocation()).GetSafeNormal2D().Rotation();
        Hero->SetActorRotation(FMath::RInterpTo(Hero->GetActorRotation(),Facing,GetWorld()->GetDeltaSeconds(),14.f));
        if(ServerNow()-Hero->OrderJumpStartedAt<AMCToothCharacter::OrderPrepareSeconds) continue;
        FVector Velocity;
        // The scene can change during the crouch: recheck the arc at takeoff.
        if(OrderVelocity(Hero,Velocity)) {
            Hero->bOrderJumpLaunched=true;
            Hero->ClientOrderLaunch_Implementation(Velocity);
            if(!Hero->IsLocallyControlled()) Hero->ClientOrderLaunch(Velocity);
            Hero->ForceNetUpdate();
        } else Hero->ClearOrderJump();
        PreparingPlayers.RemoveAtSwap(I);
    }
}
void AMCThroat::NotifyUvulaLanding(AMCToothCharacter* Hero,const FHitResult& Hit,float DownSpeed) {}
void AMCThroat::SetPhase(EMCThroatPhase Phase,double At)
{
    if(Phase!=EMCThroatPhase::Swallowing && (!bManualIntake || ManualIntakes.IsEmpty()) && ActiveVortex.IsValid()) {ActiveVortex->Destroy();ActiveVortex.Reset();}
    if(Phase!=EMCThroatPhase::Swallowing) SuctionState.Duration=0;
    ThroatPhase=Phase; PhaseStartedAt=At; ForceNetUpdate();
}
float AMCThroat::OpenAmount() const
{
    if(bManualIntake && ThroatPhase!=EMCThroatPhase::Spasm) return 1.f;
    const float Age=FMath::Max(0.,ServerNow()-PhaseStartedAt);
    if (ThroatPhase==EMCThroatPhase::Swallowing) return FMath::SmoothStep(0.f,.35f,Age);
    if (ThroatPhase==EMCThroatPhase::Recovering) return 1-FMath::SmoothStep(0.f,RecoverySeconds*.75f,Age);
    if (ThroatPhase==EMCThroatPhase::Spasm) return .32f+.28f*FMath::Square(FMath::Sin(Age*17));
    // Recovery owns closing. Closing here would reopen the aperture at the
    // transition to Recovering, whose initial opening is one.
    if (ThroatPhase==EMCThroatPhase::Vomiting) return .94f;
    if(GetWorld()) for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(It->IsYawnActive())
        return .90f*FMath::Sin(PI*float(ServerNow()-It->YawnStartedAt)/FMath::Max(1.f,It->YawnDuration));
    return 0;
}
FVector AMCThroat::VacuumInlet() const
{
    if(AuthoredMouth && AuthoredMouth->GetSkeletalMeshAsset()) {
        const FTransform Art=AuthoredMouth->GetComponentTransform();
        const auto Profile=IntakeProfile(this);
        const FVector Center=Art.TransformPosition(Profile.Center);
        const FVector Up=Art.TransformVectorNoScale(Profile.Up);
        if(Profile.bFixedFront) {
            float FloorZ=Center.Z-100;
            for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
                FHitResult Hit;
                if(It->SurfacePoint(GetActorTransform().TransformPosition(ZoneCenter),Hit)) {FloorZ=Hit.ImpactPoint.Z;break;}
            }
            // The artist may move/expand this opening below the tongue. Aim
            // through its visible upper part, never at its buried area center.
            if(Center.Z>=FloorZ+40 || Up.Z<.1f) return Center;
            float Top=Center.Z;
            for(const FVector& Vertex:Profile.Boundary) Top=FMath::Max(Top,float(Art.TransformPosition(Vertex).Z));
            const float VisibleZ=(FMath::Max(FloorZ+40, float(Center.Z))+Top)*.5f;
            return Center+Up*((VisibleZ-Center.Z)/Up.Z);
        }
        const float Top=Art.TransformPosition(FVector(0,291.62140,-39.19496)).Z;
        float FloorZ=Center.Z;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {
            FHitResult Hit;
            if(It->SurfacePoint(GetActorTransform().TransformPosition(ZoneCenter),Hit)) {FloorZ=Hit.ImpactPoint.Z;break;}
        }
        // The lower half of the artist opening lies below the tongue. End the
        // vacuum inside its visible upper half, on the measured aperture plane.
        const float VisibleZ=(FMath::Max(FloorZ,Center.Z)+Top)*.5f;
        return Center+Up*((VisibleZ-Center.Z)/FMath::Max(.1f,Up.Z));
    }
    return GetActorTransform().TransformPosition(GateCenter+FVector(0,0,230));
}
bool AMCThroat::IsAmbientSuctionActive() const { return SuctionState.IsActive(ServerNow()); }
float AMCThroat::GetAmbientSuctionStrengthAt(FVector Location) const { return SuctionState.StrengthAt(Location,ServerNow()); }
FVector AMCThroat::GetAmbientSuctionVelocityAt(FVector Location) const { return SuctionState.VelocityAt(Location,ServerNow()); }
bool AMCThroat::FindAmbientSuctionAt(UWorld* World,FVector Location,float& Strength,FVector& Velocity)
{
    Strength=0;Velocity=FVector::ZeroVector;
    if(!World) return false;
    float SpeedLimit=0;
    for(TActorIterator<AMCThroat> It(World);It;++It)
    {
        if(!It->IsAmbientSuctionActive()) continue;
        Strength=FMath::Max(Strength,It->GetAmbientSuctionStrengthAt(Location));
        Velocity+=It->GetAmbientSuctionVelocityAt(Location);
        SpeedLimit=FMath::Max(SpeedLimit,FMath::Clamp(It->SuctionState.MaxPullSpeed,0.f,150.f));
    }
    Velocity=Velocity.GetClampedToMaxSize(SpeedLimit);
    return Strength>0;
}
bool AMCThroat::CaptureMeal()
{
    Meal=MoveTemp(PendingMeal);PendingMeal.Reset();PendingPileHeight=0;QueuedFoodCount=0;
    Meal.RemoveAll([](const FMealPiece& Piece){const auto* Food=Piece.Food.Get();return !IsValid(Food) || Food->IsDisposed() || Food->Phase!=EMCFoodPhase::Swallowing;});
    if(Meal.IsEmpty()) {FoodInZone=0;SetPhase(EMCThroatPhase::Collecting,ServerNow());return false;}
    for(auto& Piece:Meal) if(auto* Food=Piece.Food.Get()) {Piece.Start=Food->GetActorLocation();Piece.Rotation=Food->GetActorQuat();}
    FoodInZone=Meal.Num(); ++SwallowCount; ++MealSequence;
    for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get()) Food->PauseFuse(this);
    // Uvula is decorative. The automatic intake captures ingredients only.
    SwallowedPlayers.Reset();
    FVector Intake=GetActorTransform().TransformPosition(ZoneCenter);
    if(!Meal.IsEmpty()) {Intake=FVector::ZeroVector;for(const auto& Piece:Meal) Intake+=Piece.Start;Intake/=Meal.Num();}
    SwallowInlet=VacuumInlet();
    const FVector Flow=SwallowInlet-(Intake+FVector(0,0,45));
    SwallowAxis=Flow.GetSafeNormal();if(SwallowAxis.IsNearlyZero()) SwallowAxis=GetActorForwardVector();
    // The air field spans the mouth, independently of where this meal was delivered.
    const FVector AirAxis=GetActorForwardVector().GetSafeNormal2D();
    const FVector AirSide=FVector::CrossProduct(FVector::UpVector,AirAxis);
    float AirLength=FMath::Max(float(Flow.Size()+100),ZoneRadius+100);
    float AirHalfWidth=ZoneRadius;
    float FieldRadius=FMath::Max(1000.f,SuctionRadius);
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
    {
        if(!It->Surface || !It->Surface->IsRegistered()) continue;
        const FBox Bounds=It->Surface->Bounds.GetBox();
        if(!Bounds.IsValid) continue;
        for(int32 Corner=0;Corner<8;++Corner)
        {
            const FVector Point(Corner&1?Bounds.Max.X:Bounds.Min.X,
                Corner&2?Bounds.Max.Y:Bounds.Min.Y,Corner&4?Bounds.Max.Z:Bounds.Min.Z);
            const FVector Offset=Point-SwallowInlet;
            FieldRadius=FMath::Max(FieldRadius,float(Offset.Size())*1.15f+200.f);
            AirLength=FMath::Max(AirLength,float(-FVector::DotProduct(Offset,AirAxis))+40.f);
            AirHalfWidth=FMath::Max(AirHalfWidth,float(FMath::Abs(FVector::DotProduct(Offset,AirSide))));
        }
    }
    TArray<AMCFoodActor*> Sources;Sources.Reserve(Meal.Num());
    for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get()) Sources.Add(Food);
    SuctionState.Origin=SwallowInlet;SuctionState.StartedAt=PhaseStartedAt;
    SuctionState.Duration=FMath::Max(.5f,SwallowSeconds);SuctionState.InfluenceRadius=FieldRadius;
    SuctionState.MaxPullSpeed=FMath::Clamp(SuctionPullSpeed,0.f,150.f);
    ActiveVortex=AMCThroatVortex::Spawn(GetWorld(),SwallowInlet,AirAxis,ZoneRadius*.9f,AirLength,SwallowSeconds,Sources,AirHalfWidth,260.f);
    ForceNetUpdate();
    return true;
}
void AMCThroat::SpitOut(bool Reset)
{
    const FVector Forward=GetActorForwardVector();
    int32 I=0;
    for(const auto& Entry:SwallowedPlayers) if(auto* Hero=Entry.Hero.Get()) {
        const FVector Exit=Reset?Entry.Start:GetActorTransform().TransformPosition(VomitOrigin+FVector(-25,(I++%4-1.5f)*90,0));
        Hero->SetActorLocation(Exit,false,nullptr,ETeleportType::TeleportPhysics); Hero->SetThroatCapture(nullptr);
        if(!Reset) Hero->LaunchCharacter(-Forward*1050+FVector(0,0,470),true,true);
        if(!Hero->IsLocallyControlled()) Hero->ClientThroatExit(Exit,Reset?FVector::ZeroVector:-Forward*1050+FVector(0,0,470));
    }
    SwallowedPlayers.Reset();
    for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get(); IsValid(Food) && !Food->IsDisposed()) {
        Food->SetActorLocation(Reset?Piece.Start:GetActorTransform().TransformPosition(VomitOrigin+FVector(-45,(I++%5-2)*55,35)),false,nullptr,ETeleportType::TeleportPhysics);
        Food->CancelSwallow(); Food->ResumeFuse(this); if(!Reset) Food->Body->SetPhysicsLinearVelocity(-Forward*780+FVector(0,0,400));
    }
    Meal.Reset();
}
void AMCThroat::BeginVomit()
{
    if(auto* Burst=GetWorld()->SpawnActor<AMCVomitBurst>()) { Burst->Configure(this); ActiveVomit=Burst; }
    SpitOut(); ++VomitCount; SetPhase(EMCThroatPhase::Vomiting,ServerNow());
}
void AMCThroat::ResetSwallow()
{
    if (!HasAuthority()) return;
    for(const auto& Piece:ManualIntakes) if(auto* Food=Piece.Food.Get();IsValid(Food) && !Food->IsDisposed()) {
        Food->SetActorLocation(Piece.Start,false,nullptr,ETeleportType::TeleportPhysics);
        Food->CancelSwallow();Food->ResumeFuse(this);
    }
    ManualIntakes.Reset();
    if(ActiveVomit.IsValid()) ActiveVomit->Destroy(); ActiveVomit.Reset();
    SpitOut(true);
    for(const auto& Piece:PendingMeal) if(auto* Food=Piece.Food.Get();IsValid(Food) && !Food->IsDisposed()) {Food->CancelSwallow();Food->ResumeFuse(this);}
    PendingMeal.Reset();PendingPileHeight=0;QueuedFoodCount=FoodInZone=0;NextIntakeAt=0;PreviousThrownPositions.Reset();
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) It->ResumeFuse(this);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->OrderJumpTarget==this) It->ClearOrderJump();
    PreparingPlayers.Reset();
    Meal.Reset(); LandedPlayers.Reset(); PressTime=Weight=0; bPressConsumed=false;
    SetPhase(EMCThroatPhase::Collecting,ServerNow());
}
void AMCThroat::Tick(float Dt)
{
    // AMCFoodDisposal's legacy tick deletes immediately; this actor owns the full cycle.
    AActor::Tick(Dt);
    if(bManualIntake) {
        if(HasAuthority()) {
            const double Now=ServerNow();
            CheckOpenThroatPlayers();CollectThrownDeliveries();UpdateManualIntakes();
            if(ThroatPhase==EMCThroatPhase::Spasm && Now-PhaseStartedAt>=SpasmSeconds) BeginVomit();
            if(ThroatPhase==EMCThroatPhase::Vomiting && Now-PhaseStartedAt>=FMath::Max(2.f,VomitSeconds)) SetPhase(EMCThroatPhase::Recovering,Now);
            if(ThroatPhase==EMCThroatPhase::Recovering && Now-PhaseStartedAt>=RecoverySeconds) SetPhase(EMCThroatPhase::Collecting,Now);
        }
        UpdatePresentation(Dt);return;
    }
    if (HasAuthority())
    {
        const double Now=ServerNow();
        CheckOpenThroatPlayers();
        Weight=0;
        PendingMeal.RemoveAll([this](const FMealPiece& Piece) {
            auto* Food=Piece.Food.Get();
            const bool Remove=!IsValid(Food) || Food->IsDisposed() || Food->Phase!=EMCFoodPhase::Swallowing;
            if(Remove && IsValid(Food)) Food->ResumeFuse(this);
            return Remove;
        });
        QueuedFoodCount=PendingMeal.Num();FoodInZone=QueuedFoodCount+Meal.Num();
        // Carriers are few and need a full gathering window, including its last
        // frame. Expensive loose-food scans retain their independent 10 Hz rate.
        const bool CarriersChecked=ThroatPhase==EMCThroatPhase::Anticipation && Now-PhaseStartedAt<AnticipationSeconds;
        if(CarriersChecked) CollectCarriedDeliveries();
        // Freeze the batch before checking new arrivals on the deadline tick.
        // A slow frame therefore cannot add a delivery after the shared window.
        if(ThroatPhase==EMCThroatPhase::Anticipation) {
            if(PendingMeal.IsEmpty()) SetPhase(EMCThroatPhase::Collecting,Now);
            else if(Now-PhaseStartedAt>=AnticipationSeconds) {
                const auto* Director=AMCGameDirector::Find(GetWorld());
                // Accepted food stays reserved; postpone only the next air event.
                if(!Director || !Director->IsManagingEvents() || Director->CanStartSwallow()) {
                    SetPhase(EMCThroatPhase::Swallowing,Now);CaptureMeal();
                }
            }
        }
        if (ThroatPhase==EMCThroatPhase::Swallowing)
        {
            const float T=FMath::Clamp(float(Now-PhaseStartedAt)/SwallowSeconds,0.f,1.f);
            const FVector End=SwallowInlet+SwallowAxis*140;
            const float Alpha=FMath::SmoothStep(.02f,1.f,T);
            FVector Side=FVector::CrossProduct(FVector::UpVector,SwallowAxis).GetSafeNormal();
            if(Side.IsNearlyZero()) Side=GetActorRightVector();
            const FVector CurlUp=FVector::CrossProduct(SwallowAxis,Side).GetSafeNormal();
            for (int32 I=0;I<Meal.Num();++I) if (auto* Food=Meal[I].Food.Get(); IsValid(Food) && !Food->IsDisposed())
            {
                const auto& Piece=Meal[I];
                const float Angle=Alpha*PI*3+I*2.399963f;
                const float Envelope=FMath::Sin(Alpha*PI);
                const float Radius=Envelope*FMath::Min(70.f,ZoneRadius*.24f);
                FVector P=FMath::Lerp(Piece.Start,End,Alpha)+(Side*FMath::Cos(Angle)+CurlUp*FMath::Sin(Angle))*Radius;
                P.Z+=Envelope*85;
                Food->SetActorLocationAndRotation(P,FQuat(SwallowAxis,Alpha*PI*3)*Piece.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
            }
            for(const auto& Entry:SwallowedPlayers) if(auto* Hero=Entry.Hero.Get()) {
                const float PlayerAlpha=FMath::SmoothStep(0.f,.72f,T);
                FVector P=FMath::Lerp(Entry.Start,End,PlayerAlpha); P.Z+=FMath::Sin(PlayerAlpha*PI)*100;
                Hero->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
            }
            bool Wrong=!SwallowedPlayers.IsEmpty();
            for(const auto& Piece:Meal) if(const auto* Food=Piece.Food.Get()) Wrong|=Food->IsWrongIngredient();
            if (Wrong && T>=.78f) { ++SpasmCount; SetPhase(EMCThroatPhase::Spasm,Now); }
            else if (T>=1) {
                for(const auto& Piece:Meal) if(auto* Food=Piece.Food.Get(); IsValid(Food) && !Food->IsDisposed()) { Food->AwardDelivery(); Food->Dispose(); ++FoodSwallowed; }
                AMCToothCharacter* Worker=nullptr;float Distance=FLT_MAX;
                const FVector Point=GetActorTransform().TransformPosition(ZoneCenter);
                for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->Status->IsAlive()) {const float D=FVector::DistSquared(It->GetActorLocation(),Point);if(D<Distance) {Worker=*It;Distance=D;}}
                if(Worker) Worker->NotifyTaskFeedback(true,Point);
                Meal.Reset();FoodInZone=QueuedFoodCount;SetPhase(EMCThroatPhase::Recovering,Now);
            }
        }
        if(ThroatPhase==EMCThroatPhase::Spasm && Now-PhaseStartedAt>=SpasmSeconds) BeginVomit();
        if(ThroatPhase==EMCThroatPhase::Vomiting && Now-PhaseStartedAt>=FMath::Max(2.f,VomitSeconds)) SetPhase(EMCThroatPhase::Recovering,Now);
        if (ThroatPhase==EMCThroatPhase::Recovering && Now-PhaseStartedAt>=RecoverySeconds)
            SetPhase(PendingMeal.IsEmpty()?EMCThroatPhase::Collecting:EMCThroatPhase::Anticipation,Now);
        // At/after the deadline the meal was already frozen above, so this
        // delivery enters the next queue rather than extending the old batch.
        if(!CarriersChecked) CollectCarriedDeliveries();
        if(Now>=NextIntakeAt) {NextIntakeAt=Now+.1;CollectLooseDeliveries();}
    }
    UpdatePresentation(Dt);
    RingElapsed+=Dt; if (RingElapsed>.2f) { RingElapsed=0; BuildRing(); }
}
void AMCThroat::UpdatePresentation(float Dt)
{
    // This derivative keeps the authored glands/walls and the real open hole
    // at the saved manual expansion (.75, reversed Open=.25). Re-bake it when
    // changing that fixed pose; the short gag/vomit morphs are visual only.
    // The source skeletal model and the food capture volume remain independent.
    const bool ContactActive=bManualIntake && AuthoredMouth->GetSkeletalMeshAsset()
        && AuthoredMouth->GetSkeletalMeshAsset()->GetFName()==FName(TEXT("SK_Exit")) && TissueContact->GetStaticMesh();
    TissueContact->SetCollisionEnabled(ContactActive?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    const float Time=ServerNow(),Open=OpenAmount();
    bool Yawning=false;
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) Yawning|=It->IsYawnActive();
    ZoneRing->SetVisibility(!bManualIntake && !Yawning);
    if(bManualIntake) Label->SetVisibility(false);
    if(AuthoredMouth->GetSkeletalMeshAsset()) {
        const float Breath=.015f+.012f*FMath::Sin(Time*1.35f);
        const float Aperture=bManualIntake?FMath::Clamp(ManualAuthoredExpansion,.05f,1.f)*Open:FMath::Clamp(Open+Breath,0.f,1.f);
        AuthoredMouth->SetMorphTarget(TEXT("Open"),bReverseAuthoredOpen?1-Aperture:Aperture);
        const float Age=FMath::Max(0.f,float(Time-PhaseStartedAt));
        const float Gag=ThroatPhase==EMCThroatPhase::Spasm?FMath::SmoothStep(0.f,.25f,Age)*(.65f+.35f*FMath::Square(FMath::Sin(Age*12))):0;
        const float Expel=ThroatPhase==EMCThroatPhase::Vomiting?(1-FMath::SmoothStep(VomitSeconds-.4f,VomitSeconds,Age))*(.82f+.18f*FMath::Square(FMath::Sin(Age*14))):0;
        AuthoredMouth->SetMorphTarget(TEXT("vomit"),FMath::Max(Gag,Expel));
        SculptedTissue->SetVisibility(false); Tissue->SetVisibility(false);
    }
    const float Target=Weight>0?1.f:0.f;
    VisualWeight=Dt>0?FMath::Lerp(VisualWeight,Target,1-FMath::Exp(-8.f*Dt)):Target;
    const float Pull=VisualWeight*28;
    const float Length=UvulaLength+Pull;
    // The authored uvula is 100 cm long, top-pivoted. The sphere is only a missing-asset fallback.
    const bool Authored=Uvula->GetStaticMesh() && Uvula->GetStaticMesh()->GetName()==TEXT("SM_Uvula");
    if(!bUseAuthoredUvula) {
        Uvula->SetRelativeLocation(Authored?UvulaTop:UvulaTop-FVector(0,0,Length*.5f));
        Uvula->SetRelativeScale3D(FVector(.7f,.85f,Length/100.f));
    }
    UvulaLanding->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const float TickleAge=Time-TickleStartedAt;
    const float Tickle=TickleAge>=0 && TickleAge<1.2f?FMath::Sin(TickleAge*32)*18*FMath::Sin(TickleAge/1.2f*PI):0;
    const float Sway=Tickle+FMath::Sin(Time*1.8f)*1.2f*(1-VisualWeight)+(ThroatPhase==EMCThroatPhase::Spasm?FMath::Sin(Time*28)*12:
        ThroatPhase==EMCThroatPhase::Vomiting?FMath::Sin(float(Time-PhaseStartedAt)*24)*8*FMath::Exp(-float(Time-PhaseStartedAt)):0);
    if(bUseAuthoredUvula) {
        // Imported MiniTong is center-pivoted. Rotate around its real upper root
        // so the attachment stays fixed while the hanging tip reacts to water.
        FTransform Shape=AuthoredUvulaRestTransform;
        if(const UStaticMesh* Mesh=Uvula->GetStaticMesh()) {
            const FVector LocalPivot(0,0,Mesh->GetBoundingBox().Max.Z);
            const FVector Pivot=Shape.TransformPosition(LocalPivot);
            const FQuat Rotation=Shape.GetRotation()*FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Sway));
            Shape.SetRotation(Rotation);
            Shape.SetLocation(Pivot-Rotation.RotateVector(Shape.GetScale3D()*LocalPivot));
        }
        Uvula->SetRelativeTransform(Shape);
    } else Uvula->SetRelativeRotation(FRotator(0,0,Sway));
    const bool ShowUvula=!bManualIntake || bUseAuthoredUvula;
    Uvula->SetVisibility(ShowUvula);Uvula->SetHiddenInGame(!ShowUvula);
    if(Dt<=0) {
        Uvula->SetCollisionEnabled(bUseAuthoredUvula?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        Uvula->SetCollisionObjectType(ECC_WorldDynamic);Uvula->SetCollisionResponseToAllChannels(ECR_Ignore);
        Uvula->SetCollisionResponseToChannel(ECC_Visibility,bUseAuthoredUvula?ECR_Block:ECR_Ignore);
        Uvula->SetGenerateOverlapEvents(false);Uvula->SetCanEverAffectNavigation(false);
    }
    // The character braces against the front of the bulb. A pad on its central
    // axis let the visible stalk pass through the body despite a valid landing.
    UvulaLanding->SetRelativeLocation(UvulaTop+FRotator(0,0,Sway).RotateVector(FVector(-86,0,-Length*.81f))-FVector(0,0,16));
    // A closed aperture blocks passage; the open aperture exposes the death zone.
    ClosedBarrier->SetCollisionEnabled(Open>=.65f?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    const FLinearColor Color=ThroatPhase==EMCThroatPhase::Collecting?FLinearColor(.05f,1.f,.31f):
        ThroatPhase==EMCThroatPhase::Anticipation?FLinearColor(.08f,.72f,.60f)*(1+.12f*FMath::Sin(Time*3)):
        ThroatPhase==EMCThroatPhase::Spasm || ThroatPhase==EMCThroatPhase::Vomiting?FLinearColor(1.f,.035f,.01f)*(1+.45f*FMath::Sin(Time*14)):FLinearColor(1.f,.52f,.045f);
    if (RingMID) RingMID->SetVectorParameterValue(TEXT("ZoneColor"),Color);
    Label->SetRelativeLocation(ZoneCenter+FVector(-30,0,72));
    Label->SetTextRenderColor(Color.ToFColorSRGB());
    LabelElapsed+=Dt;
    if(Dt<=0 || LabelElapsed>=.1f) {
        LabelElapsed=0;
        const FString Text=ThroatPhase==EMCThroatPhase::Collecting?TEXT("WALK IN WITH FOOD\nAUTOMATIC DELIVERY"):
            ThroatPhase==EMCThroatPhase::Anticipation?FString::Printf(TEXT("%d FOOD  /  BRING MORE!  %.1f"),QueuedFoodCount,FMath::Max(0.f,AnticipationSeconds-(Time-float(PhaseStartedAt)))):
            ThroatPhase==EMCThroatPhase::Spasm?TEXT("WRONG INGREDIENT!  BLEURGH!"):
            ThroatPhase==EMCThroatPhase::Vomiting?TEXT("BLEURGH!"):
            ThroatPhase==EMCThroatPhase::Swallowing?FString::Printf(TEXT("GULP!  /  %d NEXT"),QueuedFoodCount):FString::Printf(TEXT("CLOSING...  /  %d NEXT"),QueuedFoodCount);
        if(Text!=LastLabel) {LastLabel=Text;Label->SetText(FText::FromString(Text));}
    }
    GeometryElapsed+=Dt;
    if(SculptedTissue->GetSkeletalMeshAsset())
    {
        SculptedTissue->SetMorphTarget(TEXT("SwallowOpen"),Open);
        SculptedTissue->SetMorphTarget(TEXT("Breath"),.5f+.5f*FMath::Sin(Time*1.35f));
        const float GulpAge=FMath::Clamp(float(ServerNow()-PhaseStartedAt)/SwallowSeconds,0.f,1.f);
        SculptedTissue->SetMorphTarget(TEXT("Peristalsis"),ThroatPhase==EMCThroatPhase::Swallowing?FMath::Sin(GulpAge*PI):ThroatPhase==EMCThroatPhase::Spasm?.5f+.5f*FMath::Sin(Time*22):ThroatPhase==EMCThroatPhase::Vomiting?.65f+.35f*FMath::Sin(float(Time-PhaseStartedAt)*18):0.f);
    }
    else if (GetNetMode()!=NM_DedicatedServer && (Dt<=0 || GeometryElapsed>=1.f/30)) { GeometryElapsed=0; UpdateTissue(Open,Time); }
}
void AMCThroat::UpdateTissue(float Open,float Time,bool Rebuild)
{
    constexpr int32 Around=96,Rows=20;
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<int32> Tri; TArray<FProcMeshTangent> Tangents; TArray<FLinearColor> C;
    for (int32 R=0;R<=Rows;++R) for (int32 I=0;I<=Around;++I)
    {
        const float T=float(R)/Rows,A=2*PI*I/Around;
        const float Breathe=FMath::Sin(Time*1.35f)*3*FMath::Sin(T*PI);
        const float Y=FMath::Lerp(1.f,FMath::Lerp(.28f,.68f,Open),T)*GateSize.X*FMath::Cos(A);
        const float Z=FMath::Lerp(1.f,Open*.74f,T)*GateSize.Y*FMath::Sin(A);
        const float Fold=FMath::Sin(A*9+.5f)*FMath::Sin(T*PI)*8;
        const float X=T*(180+Open*120)-FMath::Sin(T*3*PI)*20+Fold+Breathe;
        V.Add(GateCenter+FVector(X,Y,Z)); UV.Add(FVector2D(float(I)/Around,T)); C.Add(FLinearColor::White);
        if (R<Rows && I<Around) { const int32 B=R*(Around+1)+I; Tri.Append({B,B+Around+1,B+1,B+1,B+Around+1,B+Around+2}); }
    }
    UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V,Tri,UV,N,Tangents);
    if (Rebuild || !Tissue->GetProcMeshSection(0)) Tissue->CreateMeshSection_LinearColor(0,V,Tri,N,UV,C,Tangents,false);
    else Tissue->UpdateMeshSection_LinearColor(0,V,N,UV,C,Tangents);
}
void AMCThroat::BuildRing()
{
    if(bManualIntake) {ZoneRing->ClearAllMeshSections();ZoneRing->SetVisibility(false);return;}
    AMCTongue* Tongue=nullptr; if(GetWorld()) for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    auto Point=[&](FVector Local) {
        FHitResult Hit; if(Tongue && Tongue->SurfacePoint(GetActorTransform().TransformPosition(Local),Hit))
            return GetActorTransform().InverseTransformPosition(Hit.ImpactPoint+Hit.ImpactNormal*2.5f);
        return Local;
    };
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<int32> Tri; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;
    constexpr int32 S=64;
    for (int32 I=0;I<=S;++I) for(int32 Edge=0;Edge<2;++Edge)
    {
        const float A=2*PI*I/S,Radius=ZoneRadius+(Edge?5:-5);
        V.Add(Point(ZoneCenter+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,5))); N.Add(FVector::UpVector);
        UV.Add(FVector2D(float(I)/S,Edge)); C.Add(FLinearColor::White); T.Add(FProcMeshTangent(1,0,0));
        if(I<S && !Edge) { const int32 B=I*2; Tri.Append({B,B+1,B+2,B+1,B+3,B+2}); }
    }
    if(ZoneRing->GetProcMeshSection(0) && ZoneRing->GetProcMeshSection(0)->ProcVertexBuffer.Num()==V.Num()) ZoneRing->UpdateMeshSection_LinearColor(0,V,N,UV,C,T);
    else ZoneRing->CreateMeshSection_LinearColor(0,V,Tri,N,UV,C,T,false);
    ZoneRing->SetMaterial(0,RingMID?RingMID.Get():RingMaterial.Get());
    V.Reset(); N.Reset(); UV.Reset(); Tri.Reset(); C.Reset(); T.Reset();
    V.Add(Point(ZoneCenter)); N.Add(FVector::UpVector); UV.Add(FVector2D(.5,.5)); C.Add(FLinearColor(1,1,1,.025f)); T.Add(FProcMeshTangent(1,0,0));
    for(int32 I=0;I<=S;++I) {
        const float A=2*PI*I/S; V.Add(Point(ZoneCenter+FVector(FMath::Cos(A),FMath::Sin(A),0)*(ZoneRadius-6)));
        N.Add(FVector::UpVector); UV.Add(FVector2D(.5+.5*FMath::Cos(A),.5+.5*FMath::Sin(A))); C.Add(FLinearColor(1,1,1,.09f)); T.Add(FProcMeshTangent(1,0,0));
        if(I<S) Tri.Append({0,I+1,I+2});
    }
    if(ZoneRing->GetProcMeshSection(1) && ZoneRing->GetProcMeshSection(1)->ProcVertexBuffer.Num()==V.Num()) ZoneRing->UpdateMeshSection_LinearColor(1,V,N,UV,C,T);
    else ZoneRing->CreateMeshSection_LinearColor(1,V,Tri,N,UV,C,T,false);
    ZoneRing->SetMaterial(1,RingMID?RingMID.Get():RingMaterial.Get());
}
void AMCThroat::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCThroat,ThroatPhase); DOREPLIFETIME(AMCThroat,PhaseStartedAt); DOREPLIFETIME(AMCThroat,Weight);
    DOREPLIFETIME(AMCThroat,FoodInZone); DOREPLIFETIME(AMCThroat,SwallowCount); DOREPLIFETIME(AMCThroat,FoodSwallowed);
    DOREPLIFETIME(AMCThroat,QueuedFoodCount);
    DOREPLIFETIME(AMCThroat,TickleStartedAt);
    DOREPLIFETIME(AMCThroat,SuctionState);
    DOREPLIFETIME(AMCThroat,SpasmCount);
    DOREPLIFETIME(AMCThroat,VomitCount); DOREPLIFETIME(AMCThroat,MealSequence);
}
