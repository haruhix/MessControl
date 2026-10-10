#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCBossCharacter.h"
#include "MCRewardChest.h"
#include "Components/BoxComponent.h"
#include "MCColdCola.h"
#include "MCIceEvent.h"
#include "MCFreezeDeathVFX.h"
#include "MCFogBrawlEvent.h"
#include "MCMouthSurface.h"
#include "MCThroat.h"
#include "MCBrushContactComponent.h"
#include "MCOrbitSpringArmComponent.h"
#include "MCPlayerCameraComponent.h"
#include "MCPlayerNameComponent.h"
#include "MCArenaTooth.h"
#include "MCToothCalculusComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCGameMode.h"
#include "MCTutorialDirector.h"
#include "MCToothpick.h"
#include "MCNutEnemy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCTaskActor.h"
#include "MCGameState.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothAnimInstance.h"
#include "MCToothMovementComponent.h"
#include "MCToothMeshComponent.h"
#include "MCGazeComponent.h"
#include "MCGripComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCReactionVFX.h"
#include "MCExpressionComponent.h"
#include "MCExperimentalAudioComponent.h"
#include "PhysicsControlComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/ConstructorHelpers.h"

AMCToothCharacter::AMCToothCharacter(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer.SetDefaultSubobjectClass<UMCToothMovementComponent>(ACharacter::CharacterMovementComponentName).SetDefaultSubobjectClass<UMCToothMeshComponent>(ACharacter::MeshComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 58.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    // Food movement is driven by grip strength, not CharacterMovement's large automatic push force.
    GetCharacterMovement()->bEnablePhysicsInteraction=false;
    GetCharacterMovement()->MaxWalkSpeed = 440.f;
    GetCharacterMovement()->MaxAcceleration = 950.f;
    GetCharacterMovement()->GroundFriction = 2.2f;
    GetCharacterMovement()->BrakingFrictionFactor = 1.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 600.f;
    GetCharacterMovement()->JumpZVelocity = 500.f*FMath::Sqrt(1.28f);
    GetCharacterMovement()->GravityScale = 1.6f;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0, 300, 0);
    bUseControllerRotationYaw = false;
    GetMesh()->SetRelativeLocation(FVector(0,0,-58));
    GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
    GetMesh()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetMesh()->bEnableUpdateRateOptimizations=false;
    GetMesh()->SetAnimInstanceClass(UMCToothAnimInstance::StaticClass());
    Muscles=CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("Muscles"));
    // Recovery removes the mesh bodies from Chaos. Rebind muscle constraints when
    // collision recreates those bodies, instead of keeping dead control handles.
    Muscles->bAttemptToRecreateDisabledControls=true;
    ToothPhysics=CreateDefaultSubobject<UMCToothPhysicsComponent>(TEXT("ToothPhysics"));
    Status=CreateDefaultSubobject<UMCToothStatusComponent>(TEXT("ToothStatus"));
    BrushContact=CreateDefaultSubobject<UMCBrushContactComponent>(TEXT("BrushContact"));
    Gaze=CreateDefaultSubobject<UMCGazeComponent>(TEXT("Gaze"));
    Grip=CreateDefaultSubobject<UMCGripComponent>(TEXT("Grip"));
    FoodCollection=CreateDefaultSubobject<UMCFoodCollectionComponent>(TEXT("FoodCollection"));
    Expression=CreateDefaultSubobject<UMCExpressionComponent>(TEXT("Expression"));
    Inventory=CreateDefaultSubobject<UMCInventoryComponent>(TEXT("Inventory"));
    ExperimentalAudio=CreateDefaultSubobject<UMCExperimentalAudioComponent>(TEXT("ExperimentalAudio"));
    BrushPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BrushPivot"));
    BrushPivot->SetupAttachment(GetMesh(),TEXT("hand_r"));
    Brush = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MiniBrush"));
    Brush->SetupAttachment(BrushPivot); Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CameraBoom = CreateDefaultSubobject<UMCOrbitSpringArmComponent>(TEXT("ArenaCamera"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->SetUsingAbsoluteRotation(true); CameraBoom->SetRelativeRotation(FRotator(-16,0,0));
    CameraBoom->TargetArmLength = 1350; CameraBoom->TargetOffset = FVector(120,0,130);
    CameraBoom->bUsePawnControlRotation = false; CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 7; CameraBoom->CameraLagMaxDistance = 80;
    CameraBoom->bDoCollisionTest = true; CameraBoom->ProbeSize=24; CameraBoom->ProbeChannel=ECC_Camera;
    CameraBoom->AddTickPrerequisiteActor(this);
    Camera = CreateDefaultSubobject<UMCPlayerCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(CameraBoom);
    Camera->SetUsingAbsoluteRotation(true);
    Camera->FieldOfView = FollowFOV;
    PlayerNameLabel=CreateDefaultSubobject<UMCPlayerNameComponent>(TEXT("PlayerName"));
    PlayerNameLabel->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> ToothAsset(TEXT("/Game/Art/Rig/SK_ToothHero"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BrushAsset(TEXT("/Game/Art/Meshes/SM_Brush"));
    static ConstructorHelpers::FObjectFinder<UMCAnimationProfile> AnimAsset(TEXT("/Game/Data/DA_ToothAnimation"));
    static ConstructorHelpers::FObjectFinder<UMCSoundPalette> SoundAsset(TEXT("/Game/Data/DA_MouthSounds"));
    static ConstructorHelpers::FObjectFinder<UMCPlayerAppearance> AppearanceAsset(TEXT("/Game/Data/DA_PlayerAppearance"));
    if (ToothAsset.Succeeded()) GetMesh()->SetSkeletalMesh(ToothAsset.Object);
    if (BrushAsset.Succeeded()) Brush->SetStaticMesh(BrushAsset.Object);
    if (AnimAsset.Succeeded()) AnimationProfile = AnimAsset.Object;
    if (SoundAsset.Succeeded()) SoundPalette = SoundAsset.Object;
    if (AppearanceAsset.Succeeded()) Appearance=AppearanceAsset.Object;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegIceMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> LegIceMaterial(TEXT("/Game/Art/Materials/ice/MI_Ice.MI_Ice"));
    for(int32 Side=0;Side<2;++Side)
    {
        auto* Part=CreateDefaultSubobject<UStaticMeshComponent>(Side==0?TEXT("IceLowerLegL"):TEXT("IceLowerLegR"));
        Part->SetupAttachment(GetMesh());
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false); Part->SetCanEverAffectNavigation(false);
        Part->SetVisibility(false); Part->SetCastShadow(false);
        if(LegIceMesh.Succeeded()) Part->SetStaticMesh(LegIceMesh.Object);
        if(LegIceMaterial.Succeeded()) Part->SetMaterial(0,LegIceMaterial.Object);
        IceLegParts.Add(Part);
    }
}
FName AMCToothCharacter::RigBone(FName BoneRole) const
{
    return Appearance && Appearance->SkeletalMesh==GetMesh()->GetSkeletalMeshAsset()?Appearance->Bone(BoneRole):BoneRole;
}
FTransform AMCToothCharacter::StandingMeshTransform() const
{
    return Appearance && Appearance->SkeletalMesh==GetMesh()->GetSkeletalMeshAsset()?Appearance->MeshTransform:FTransform(FVector(0,0,-58));
}
void AMCToothCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform); ApplyAppearance();
}
void AMCToothCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents(); ApplyAppearance();
}
void AMCToothCharacter::ApplyAppearance()
{
    if (!Appearance || !Appearance->SkeletalMesh) return;
    GetMesh()->SetSkeletalMesh(Appearance->SkeletalMesh);
    if (Appearance->PhysicsAsset) GetMesh()->SetPhysicsAsset(Appearance->PhysicsAsset);
    GetMesh()->SetRelativeTransform(StandingMeshTransform());
    if (Appearance->Material) GetMesh()->SetMaterial(0,Appearance->Material);
    if (Appearance->BrushMesh) { Brush->EmptyOverrideMaterials(); Brush->SetStaticMesh(Appearance->BrushMesh); }
    BrushPivot->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,RigBone(TEXT("hand_r")));
    const auto& Ref=GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    FTransform Hand=FTransform::Identity;
    for (int32 I=Ref.FindBoneIndex(RigBone(TEXT("hand_r")));I>=0;I=Ref.GetParentIndex(I)) Hand=Hand*Ref.GetRefBonePose()[I];
    FTransform BrushGrip=Appearance->BrushTransform; BrushGrip.AddToTranslation(Hand.GetLocation());
    BrushPivot->SetRelativeTransform(BrushGrip.GetRelativeTransform(Hand));
}
void AMCToothCharacter::BeginPlay()
{
    Super::BeginPlay();
    // Include Blueprint colliders and the skeletal bodies used by ragdolls.
    TInlineComponentArray<UPrimitiveComponent*> Colliders(this);
    for (auto* Collider:Colliders) Collider->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    // Choose once on the server; owning clients, proxies and late joiners share it.
    if (HasAuthority())
        if (const auto* Identity=GetPlayerState<AMCPlayerState>()) ApplyPlayerColor(Identity->PlayerColor);
    if (AnimationProfile) AnimationSettings = AnimationProfile->Settings;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("MCRagdollCapture")))
        for (int32 I=0;I<GetMesh()->GetNumMaterials();++I) UE_LOG(LogTemp,Display,TEXT("MC_SKIN_MATERIAL %d %s"),I,*GetNameSafe(GetMesh()->GetMaterial(I)));
#endif
    GetMesh()->SetNotifyRigidBodyCollision(true);
    GetMesh()->OnComponentHit.AddDynamic(this,&AMCToothCharacter::OnBodyHit);
    GetCapsuleComponent()->OnComponentHit.AddDynamic(this,&AMCToothCharacter::OnBodyHit);
    if (!Appearance || !Appearance->SkeletalMesh)
    {
        const auto& Ref=GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
        FTransform Hand=FTransform::Identity;
        for (int32 I=Ref.FindBoneIndex(TEXT("hand_r"));I>=0;I=Ref.GetParentIndex(I)) Hand=Hand*Ref.GetRefBonePose()[I];
        BrushPivot->SetRelativeRotation(Hand.GetRotation().Inverse());
    }
    UMaterialInterface* BagBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/CharacterCurrent/Materials/MI_Bag.MI_Bag"));
    UMaterialInterface* Base=Appearance && Appearance->Material?Appearance->Material.Get():LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_ArenaTooth.M_ArenaTooth"));
    if (Base)
    {
        StatusMaterial=UMaterialInstanceDynamic::Create(Base,this);
        for (int32 I=0;I<GetMesh()->GetNumMaterials();++I)
        {
            const FString Name=GetNameSafe(GetMesh()->GetMaterial(I));
            if (Appearance?I==0:Name.Contains(TEXT("Enamel")) || Name.Contains(TEXT("Tooth"))) GetMesh()->SetMaterial(I,StatusMaterial);
            else if (Appearance && I>0)
            {
                if (auto* FaceMaterial=GetMesh()->CreateDynamicMaterialInstance(I))
                {
                    FaceMaterials.Add(FaceMaterial);
                    if (BagBase && FaceMaterial->IsChildOf(BagBase)) BagMaterial=FaceMaterial;
                }
            }
        }
    }
    if(GetNetMode()!=NM_DedicatedServer)
        if(auto* FootIce=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Cold/VFX/SM_IceShard.SM_IceShard")))
            for(UStaticMeshComponent* Part:IceLegParts) if(Part) Part->SetStaticMesh(FootIce);
    OnRep_BagColor();
    OnRep_IceLegHealth();
}
void AMCToothCharacter::FreezeLegs(float Health)
{
    if(!HasAuthority() || !FMath::IsFinite(Health) || Health<=0 || !Status->IsAlive()
        || IsMimicCaptured() || SwallowedBy || !ToothPhysics->CanAct()) return;
    // A second gust cannot replace partially broken ice with less health.
    IceLegHealth=FMath::Max(IceLegHealth,FMath::Clamp(Health,1.f,10000.f));
    ClearOrderJump(); ClingTooth=nullptr; bWantsCling=false;
    OnRep_IceLegHealth(); ForceNetUpdate();
}
void AMCToothCharacter::ClearFrozenLegs()
{
    if(!HasAuthority() || IceLegHealth<=0) return;
    IceLegHealth=0.f; OnRep_IceLegHealth(); ForceNetUpdate();
}
bool AMCToothCharacter::HitFrozenLegsWithPickaxe(AMCToothCharacter* Worker,float Damage)
{
    if(!HasAuthority() || !HasFrozenLegs() || !Status->IsAlive() || !IsValid(Worker)
        || Worker->GetWorld()!=GetWorld() || !Worker->CanWork() || !Worker->Inventory
        || Worker->Inventory->Selected!=EMCToolSlot::Pickaxe || !FMath::IsFinite(Damage) || Damage<=0) return false;
    // The drill has to reach the frozen feet; ordinary pickaxe self rescue
    // retains its swing without requiring a downward camera.
    if(Worker->Inventory->HasUpgrade(EMCToolUpgrade::Buffer))
    {
        const FVector Point=GetActorLocation()-FVector(0,0,35);
        if(!Worker->Inventory->CanBufferContact(this,Point)) return false;
    }
    else if(Worker!=this)
    {
        const FVector Offset=GetActorLocation()-Worker->GetActorLocation();
        if(Offset.SizeSquared()>FMath::Square(180.f)
            || FVector::DotProduct(Offset.GetSafeNormal2D(),Worker->GetActorForwardVector())<.25f) return false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceLegRescue),false,Worker); Query.AddIgnoredActor(this);
        FHitResult Block;
        if(GetWorld()->LineTraceSingleByChannel(Block,Worker->GetActorLocation(),GetActorLocation(),ECC_Visibility,Query)) return false;
    }
    IceLegHealth=FMath::Max(0.f,IceLegHealth-Damage); OnRep_IceLegHealth(); ForceNetUpdate();
    if(!HasFrozenLegs()) Worker->NotifyTaskFeedback(true,GetActorLocation()-FVector(0,0,35));
    return true;
}
void AMCToothCharacter::OnRep_IceLegHealth()
{
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    Move->SetFrozenLegs(HasFrozenLegs() && !IsMimicCaptured() && !SwallowedBy && Status->IsAlive());
    if(HasFrozenLegs())
    {
        CancelSprintInput(); StopJumping(); ConsumeMovementInputVector();
        LocalPaddle=FVector2D::ZeroVector; PaddleInput=FVector2D::ZeroVector; SwimIntent=FVector::ZeroVector;
    }
    UpdateIceLegVisuals();
    if(IsFreezingToDeath()) GetCharacterMovement()->DisableMovement();
}
void AMCToothCharacter::UpdateIceLegVisuals()
{
    const bool Visible=HasFrozenLegs() && Status->IsAlive() && !IsMimicCaptured() && !SwallowedBy;
    for(int32 Side=0;Side<IceLegParts.Num();++Side)
    {
        auto* Part=IceLegParts[Side].Get(); if(!Part || !Part->GetStaticMesh()) continue;
        Part->SetVisibility(Visible);
        if(!Visible) continue;
        const FName FootBone=RigBone(Side==0?TEXT("foot_l"):TEXT("foot_r"));
        const FName KneeBone=RigBone(Side==0?TEXT("knee_l"):TEXT("knee_r"));
        FVector Foot=GetActorLocation()+GetActorRightVector()*(Side==0?-16.f:16.f)-FVector(0,0,48);
        FVector Knee=Foot+FVector(0,0,32);
        if(GetMesh()->GetBoneIndex(FootBone)!=INDEX_NONE) Foot=GetMesh()->GetSocketLocation(FootBone);
        if(GetMesh()->GetBoneIndex(KneeBone)!=INDEX_NONE) Knee=GetMesh()->GetSocketLocation(KneeBone);
        const FVector Axis=Knee-Foot;
        const FQuat Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Axis.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector));
        const FBox Bounds=Part->GetStaticMesh()->GetBoundingBox();
        const FVector Size=Bounds.GetSize();
        // Locked feet should read as solid ice cuffs from gameplay distance,
        // with a wider base and a visible rim above the knee-to-foot span.
        const FVector Scale(66.f/FMath::Max(1.,Size.X),62.f/FMath::Max(1.,Size.Y),
            FMath::Clamp(float(Axis.Size())+42.f,55.f,98.f)/FMath::Max(1.,Size.Z));
        Part->SetWorldLocationAndRotation((Foot+Knee)*.5f-Rotation.RotateVector(Bounds.GetCenter()*Scale),Rotation);
        Part->SetWorldScale3D(Scale);
    }
}
void AMCToothCharacter::OnRep_BagColor()
{
    if (!BagMaterial) return; // Initial replication can arrive before BeginPlay.
    // Remove the baked blue hue before tinting, retaining the texture's shading.
    BagMaterial->SetScalarParameterValue(TEXT("Saturate"),1.f);
    BagMaterial->SetVectorParameterValue(TEXT("Albedo Color"),BagColor);
}
void AMCToothCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    if (const auto* Identity=GetPlayerState<AMCPlayerState>()) ApplyPlayerColor(Identity->PlayerColor);
}
void AMCToothCharacter::ApplyPlayerColor(FLinearColor Color)
{
    if (!HasAuthority() || !FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) return;
    BagColor=FLinearColor(FMath::Clamp(Color.R,0.f,1.f),FMath::Clamp(Color.G,0.f,1.f),FMath::Clamp(Color.B,0.f,1.f),1.f);
    if (auto* Identity=GetPlayerState<AMCPlayerState>()) { Identity->PlayerColor=BagColor; Identity->ForceNetUpdate(); }
    OnRep_BagColor(); ForceNetUpdate();
}
float AMCToothCharacter::GetStamina() const { return CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->GetStamina(); }
float AMCToothCharacter::GetMaxStamina() const { return CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->GetMaxStamina(); }
float AMCToothCharacter::GetStaminaNormalized() const { return GetStamina()/GetMaxStamina(); }
bool AMCToothCharacter::IsStaminaExhausted() const { return CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->GetStaminaPrediction().Exhausted; }
void AMCToothCharacter::BuildInput()
{
    if (InputMap) return;
    InputMap = NewObject<UInputMappingContext>(this);
    auto Action = [this](EInputActionValueType Type) { UInputAction* Result = NewObject<UInputAction>(this); Result->ValueType = Type; return Result; };
    ForwardAction = Action(EInputActionValueType::Axis1D); RightAction = Action(EInputActionValueType::Axis1D);
    JumpAction = Action(EInputActionValueType::Boolean); BrushAction = Action(EInputActionValueType::Boolean);
    HandleAction = Action(EInputActionValueType::Boolean); PanelAction = Action(EInputActionValueType::Boolean);
    ConnectionAction = Action(EInputActionValueType::Boolean); RestartAction = Action(EInputActionValueType::Boolean);
    SwingAction=Action(EInputActionValueType::Boolean);
    BraceAction=Action(EInputActionValueType::Boolean);
    OrbitXAction=Action(EInputActionValueType::Axis1D); OrbitYAction=Action(EInputActionValueType::Axis1D);
    ZoomAction=Action(EInputActionValueType::Axis1D);
    SelfCareAction=Action(EInputActionValueType::Boolean);
    ThrowAction=Action(EInputActionValueType::Boolean);
    SprintAction=Action(EInputActionValueType::Boolean);
    InputMap->MapKey(SprintAction,EKeys::LeftShift); InputMap->MapKey(SprintAction,EKeys::Gamepad_LeftThumbstick);
    InputMap->MapKey(ThrowAction,EKeys::Q); InputMap->MapKey(ThrowAction,EKeys::Gamepad_FaceButton_Top);
    InputMap->MapKey(SelfCareAction,EKeys::C); InputMap->MapKey(SelfCareAction,EKeys::Gamepad_RightThumbstick);
    InputMap->MapKey(SwingAction,EKeys::F); InputMap->MapKey(SwingAction,EKeys::Gamepad_LeftTrigger);
    InputMap->MapKey(BraceAction,EKeys::RightMouseButton); InputMap->MapKey(BraceAction,EKeys::Gamepad_LeftShoulder);
    InputMap->MapKey(OrbitXAction,EKeys::MouseX); InputMap->MapKey(OrbitYAction,EKeys::MouseY);
    InputMap->MapKey(ZoomAction,EKeys::MouseWheelAxis);
    InputMap->MapKey(ForwardAction, EKeys::W);
    InputMap->MapKey(ForwardAction, EKeys::S).Modifiers.Add(NewObject<UInputModifierNegate>(this));
    InputMap->MapKey(ForwardAction, EKeys::Gamepad_LeftY);
    InputMap->MapKey(RightAction, EKeys::D);
    InputMap->MapKey(RightAction, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(this));
    InputMap->MapKey(RightAction, EKeys::Gamepad_LeftX);
    InputMap->MapKey(JumpAction, EKeys::SpaceBar); InputMap->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
    InputMap->MapKey(BrushAction, EKeys::LeftMouseButton); InputMap->MapKey(BrushAction, EKeys::Gamepad_RightShoulder);
    InputMap->MapKey(HandleAction, EKeys::E); InputMap->MapKey(HandleAction, EKeys::Gamepad_FaceButton_Left);
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four};
    const FKey Pad[]={EKeys::Gamepad_DPad_Left,EKeys::Gamepad_DPad_Up,EKeys::Gamepad_DPad_Right,EKeys::Gamepad_DPad_Down};
    for(int32 I=0;I<4;++I) { auto* ToolAction=Action(EInputActionValueType::Boolean); ToolActions.Add(ToolAction); InputMap->MapKey(ToolAction,Keys[I]); InputMap->MapKey(ToolAction,Pad[I]); }
    InputMap->MapKey(PanelAction, EKeys::F1); InputMap->MapKey(ConnectionAction, EKeys::F2); InputMap->MapKey(RestartAction, EKeys::R);
}
void AMCToothCharacter::PawnClientRestart()
{
    Super::PawnClientRestart(); BuildInput();
    if (APlayerController* PC = Cast<APlayerController>(Controller))
        if (ULocalPlayer* Local = PC->GetLocalPlayer())
            if (auto* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                // Possession changes before the corpse expires. Remove its bindings immediately.
                for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
                    if (*It!=this && It->AppliedInputSubsystem==Subsystem && It->InputMap) Subsystem->RemoveMappingContext(It->InputMap);
                Subsystem->RemoveMappingContext(InputMap); Subsystem->AddMappingContext(InputMap, 0); AppliedInputSubsystem=Subsystem;
            }
}
void AMCToothCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent); BuildInput();
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    Input->BindAction(ForwardAction, ETriggerEvent::Triggered, this, &AMCToothCharacter::MoveForward);
    Input->BindAction(RightAction, ETriggerEvent::Triggered, this, &AMCToothCharacter::MoveRight);
    Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AMCToothCharacter::StartJump);
    Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMCToothCharacter::StopJump);
    Input->BindAction(SprintAction,ETriggerEvent::Started,this,&AMCToothCharacter::StartSprint);
    Input->BindAction(SprintAction,ETriggerEvent::Completed,this,&AMCToothCharacter::StopSprint);
    Input->BindAction(SprintAction,ETriggerEvent::Canceled,this,&AMCToothCharacter::CancelSprintInput);
    Input->BindAction(BrushAction, ETriggerEvent::Started, this, &AMCToothCharacter::StartPrimary);
    Input->BindAction(BrushAction, ETriggerEvent::Completed, this, &AMCToothCharacter::StopPrimary);
    Input->BindAction(BrushAction, ETriggerEvent::Canceled, this, &AMCToothCharacter::StopPrimary);
    Input->BindAction(HandleAction, ETriggerEvent::Started, this, &AMCToothCharacter::StartHandle);
    Input->BindAction(HandleAction, ETriggerEvent::Completed, this, &AMCToothCharacter::StopHandle);
    Input->BindAction(HandleAction, ETriggerEvent::Canceled, this, &AMCToothCharacter::StopHandle);
    Input->BindAction(PanelAction, ETriggerEvent::Started, this, &AMCToothCharacter::TogglePanel);
    Input->BindAction(ConnectionAction, ETriggerEvent::Started, this, &AMCToothCharacter::ToggleConnection);
    Input->BindAction(RestartAction, ETriggerEvent::Started, this, &AMCToothCharacter::RestartRun);
    Input->BindAction(SwingAction,ETriggerEvent::Started,this,&AMCToothCharacter::SwingBrush);
    Input->BindAction(BraceAction,ETriggerEvent::Started,this,&AMCToothCharacter::StartBrace);
    Input->BindAction(BraceAction,ETriggerEvent::Triggered,this,&AMCToothCharacter::ContinueFogGuard);
    Input->BindAction(BraceAction,ETriggerEvent::Completed,this,&AMCToothCharacter::StopBrace);
    Input->BindAction(BraceAction,ETriggerEvent::Canceled,this,&AMCToothCharacter::StopBrace);
    Input->BindAction(OrbitXAction,ETriggerEvent::Triggered,this,&AMCToothCharacter::OrbitMouseX);
    Input->BindAction(OrbitYAction,ETriggerEvent::Triggered,this,&AMCToothCharacter::OrbitMouseY);
    Input->BindAction(ZoomAction,ETriggerEvent::Triggered,this,&AMCToothCharacter::CameraMouseWheel);
    Input->BindAction(SelfCareAction,ETriggerEvent::Started,this,&AMCToothCharacter::ToggleSelfCare);
    Input->BindAction(ThrowAction,ETriggerEvent::Started,this,&AMCToothCharacter::ThrowItem);
    Input->BindAction(ToolActions[0],ETriggerEvent::Started,this,&AMCToothCharacter::SelectBrush);
    Input->BindAction(ToolActions[1],ETriggerEvent::Started,this,&AMCToothCharacter::SelectPickaxe);
    Input->BindAction(ToolActions[2],ETriggerEvent::Started,this,&AMCToothCharacter::SelectKnife);
    Input->BindAction(ToolActions[3],ETriggerEvent::Started,this,&AMCToothCharacter::SelectSpray);
    Input->BindAction(ForwardAction,ETriggerEvent::Completed,this,&AMCToothCharacter::MoveForward);
    Input->BindAction(RightAction,ETriggerEvent::Completed,this,&AMCToothCharacter::MoveRight);
}
void AMCToothCharacter::MoveForward(const FInputActionValue& Value) { LocalPaddle.X=IsMimicCaptured() || HasFrozenLegs()?0:Value.Get<float>(); const auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement()); if (!IsMimicCaptured() && !HasFrozenLegs() && !ClingTooth && !OrderJumpTarget) AddMovementInput(Move->IsClimbing()?FVector::UpVector:CameraMoveDirection(false), LocalPaddle.X); }
void AMCToothCharacter::MoveRight(const FInputActionValue& Value) { LocalPaddle.Y=IsMimicCaptured() || HasFrozenLegs()?0:Value.Get<float>(); const auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement()); if (!IsMimicCaptured() && !HasFrozenLegs() && !ClingTooth && !OrderJumpTarget) AddMovementInput(Move->IsClimbing()?FVector::CrossProduct(FVector(Move->ClimbNormal),FVector::UpVector).GetSafeNormal():CameraMoveDirection(true), LocalPaddle.Y); }
void AMCToothCharacter::OrbitMouseX(const FInputActionValue& Value) { ApplyCameraOrbitInput(FVector2D(Value.Get<float>(),0)); }
void AMCToothCharacter::OrbitMouseY(const FInputActionValue& Value) { ApplyCameraOrbitInput(FVector2D(0,Value.Get<float>())); }
void AMCToothCharacter::CameraMouseWheel(const FInputActionValue& Value) { ZoomCamera(Value.Get<float>()); }
FVector2D AMCToothCharacter::WorldPaddleInput() const
{
    const FVector Direction=CameraMoveDirection(false)*LocalPaddle.X+CameraMoveDirection(true)*LocalPaddle.Y;
    return FVector2D(Direction.X,Direction.Y);
}
void AMCToothCharacter::StartJump()
{
    if(!CanWork() || HasFrozenLegs() || OrderJumpTarget) return;
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    if(Move->IsClimbing()) { Jump(); return; }
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) if(It->CanOrderJump(this)) { ServerOrderJump(*It); return; }
    Jump();
}
void AMCToothCharacter::ServerOrderJump_Implementation(AMCThroat* Throat)
{
    if(!HasFrozenLegs() && IsValid(Throat)) Throat->LaunchToUvula(this);
}
void AMCToothCharacter::ClientOrderLaunch_Implementation(FVector Velocity)
{
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if(!It->IsDisposed()) {
        GetCapsuleComponent()->IgnoreActorWhenMoving(*It,true); OrderJumpIgnoredActors.Add(*It);
    }
    if(GetCharacterMovement()->AirControl>0) OrderJumpAirControl=GetCharacterMovement()->AirControl;
    GetCharacterMovement()->AirControl=0; LaunchCharacter(Velocity,true,true);
}
void AMCToothCharacter::ClearOrderJump()
{
    const bool HadFlight=bOrderJumpLaunched || !OrderJumpIgnoredActors.IsEmpty();
    OrderJumpTarget=nullptr; bOrderJumpLaunched=false;
    if(HadFlight) GetCharacterMovement()->AirControl=OrderJumpAirControl;
    for(const auto& Entry:OrderJumpIgnoredActors) if(auto* A=Entry.Get()) GetCapsuleComponent()->IgnoreActorWhenMoving(A,false);
    OrderJumpIgnoredActors.Reset();
    if(HasAuthority()) ForceNetUpdate();
}
void AMCToothCharacter::ClientUvulaHop_Implementation(FVector Velocity) { LaunchCharacter(Velocity,true,true); }
void AMCToothCharacter::SetThroatCapture(AMCThroat* Throat)
{
    if(!HasAuthority() || SwallowedBy==Throat) return;
    if(Throat) ClearFrozenLegs();
    CancelGameplayInput(); ClearOrderJump(); if(Throat) ThroatCaptureStart=GetActorLocation(); SwallowedBy=Throat; OnRep_ThroatCapture(); ForceNetUpdate();
}
void AMCToothCharacter::ClientThroatExit_Implementation(FVector Location,FVector Velocity)
{
    SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);
    SwallowedBy=nullptr; OnRep_ThroatCapture(); bMouthCameraHeld=false; LaunchCharacter(Velocity,true,true);
}
void AMCToothCharacter::OnRep_ThroatCapture()
{
    // Exit RPC and property replication can both report the release. Do not
    // stop the ejection impulse a second time when the property catches up.
    if(!SwallowedBy && !bThroatCaptured) return;
    bThroatCaptured=SwallowedBy!=nullptr;
    if(ThroatTickPrerequisite.IsValid()) RemoveTickPrerequisiteActor(ThroatTickPrerequisite.Get());
    ThroatTickPrerequisite=SwallowedBy;
    if(SwallowedBy) AddTickPrerequisiteActor(SwallowedBy);
    ToothPhysics->SetThroatCaptured(bThroatCaptured);
    if(IsLocallyControlled()) {
        if(SwallowedBy) {
            bMouthCameraHeld=true;
        } else if(HasAuthority()) bMouthCameraHeld=false;
        // Remote owners keep holding until the reliable exit RPC supplies the
        // authoritative mouth position; a null pointer alone may arrive first.
    }
    if(SwallowedBy) { bInCoffee=false; ClingTooth=nullptr; }
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(SwallowedBy?MOVE_None:MOVE_Falling);
    GetCapsuleComponent()->SetCollisionEnabled(SwallowedBy?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    if(!SwallowedBy) GetCharacterMovement()->AirControl=OrderJumpAirControl;
}
bool AMCToothCharacter::IsMimicCaptured() const { return IsValid(MimicCaptor) || bMimicCaptured; }
void AMCToothCharacter::BeginMimicCapture(AMCRewardChest* Chest)
{
    if(!HasAuthority() || !IsValid(Chest) || Chest->GetWorld()!=GetWorld() || !Status->IsAlive() || SwallowedBy || MimicCaptor) return;
    ClearFrozenLegs();
    CancelGameplayInput(); ClearOrderJump(); DropFood(); FoodCollection->Stop(); ResetContact();
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(*It!=this && It->Grip) {
        if(It->Grip->BraceTarget()==this) It->Grip->ReleaseBrace();
        if(It->Grip->GrabbedPlayer==this) It->Grip->ReleasePlayer();
    }
    YawnEndsAt=0; YawnTongue=nullptr; OnRep_Yawn();
    RewardInteraction=nullptr; bBrushing=false; bHandling=false; bPrimaryHeld=false; bSelfCare=false;
    bWantsCling=false; ClingTooth=nullptr; bInCoffee=false; PaddleInput=FVector2D::ZeroVector; SwimIntent=FVector::ZeroVector;
    MimicCaptureStart=GetActorLocation(); MimicReleaseLocation=MimicCaptureStart;
    MimicCaptor=Chest; OnRep_MimicCapture(); ForceNetUpdate();
}
void AMCToothCharacter::EndMimicCapture(FVector ReleaseLocation)
{
    if(!HasAuthority() || (!MimicCaptor && !bMimicCaptured)) return;
    MimicReleaseLocation=ReleaseLocation.ContainsNaN()?FVector(MimicCaptureStart):ReleaseLocation;
    MimicCaptor=nullptr; OnRep_MimicCapture(); ForceNetUpdate();
}
void AMCToothCharacter::OnRep_MimicCapture()
{
    if(IsValid(MimicCaptor)) {
        if(!bMimicCaptured) {
            MimicRestoreMeshTransform=StandingMeshTransform();
            MimicRestoreCapsuleCollision=ToothPhysics->GetBodyState()==EMCBodyState::Standing
                ?GetCapsuleComponent()->GetCollisionEnabled():ECollisionEnabled::QueryAndPhysics;
            bMimicRestoreMeshVisible=GetMesh()->IsVisible(); bMimicRestoreNameVisible=PlayerNameLabel->IsVisible();
            bMimicRestoreNameHidden=PlayerNameLabel->bHiddenInGame;
            MimicCameraRotation=Camera->GetComponentRotation();
            MimicCameraDistance=FMath::Clamp(float(FVector::Dist(Camera->GetComponentLocation(),GetActorLocation()+FVector(0,0,30))),400.f,1600.f);
            bMimicCaptured=true;
            if(HasAuthority() || IsLocallyControlled()) CancelGameplayInput();
            ConsumeMovementInputVector();
            ToothPhysics->SetThroatCaptured(true);
        }
        if(MimicTickPrerequisite.Get()!=MimicCaptor) {
            if(MimicTickPrerequisite.IsValid()) RemoveTickPrerequisiteActor(MimicTickPrerequisite.Get());
            MimicTickPrerequisite=MimicCaptor; AddTickPrerequisiteActor(MimicCaptor);
        }
        bMouthCameraHeld=false;
        GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->SetMovementMode(MOVE_None);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Brush->SetVisibility(false); PlayerNameLabel->SetHiddenInGame(true);
    } else if(bMimicCaptured) {
        if(MimicTickPrerequisite.IsValid()) RemoveTickPrerequisiteActor(MimicTickPrerequisite.Get());
        MimicTickPrerequisite.Reset(); bMimicCaptured=false;
        SetActorLocation(MimicReleaseLocation,false,nullptr,ETeleportType::TeleportPhysics);
        ToothPhysics->SetThroatCaptured(false);
        GetMesh()->SetRelativeTransform(MimicRestoreMeshTransform);
        GetMesh()->SetVisibility(bMimicRestoreMeshVisible);
        PlayerNameLabel->SetVisibility(bMimicRestoreNameVisible);
        PlayerNameLabel->SetHiddenInGame(bMimicRestoreNameHidden);
        GetCapsuleComponent()->SetCollisionEnabled(MimicRestoreCapsuleCollision);
        GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        bMouthCameraHeld=false; bMouthCameraInitialized=false;
        if(auto* Arm=Cast<UMCOrbitSpringArmComponent>(CameraBoom)) Arm->SetIgnoredViewActor(nullptr);
    }
}
void AMCToothCharacter::UpdateMimicCapture(float)
{
    if(!IsValid(MimicCaptor)) {
        if(bMimicCaptured) {
            if(HasAuthority()) EndMimicCapture(MimicReleaseLocation);
            else { MimicCaptor=nullptr; OnRep_MimicCapture(); }
        }
        return;
    }
    if(!bMimicCaptured) OnRep_MimicCapture();
    const float Alpha=MimicCaptor->GetMimicSwallowAlpha();
    const float Pull=FMath::SmoothStep(0.f,1.f,Alpha);
    FVector P=FMath::Lerp(FVector(MimicCaptureStart),MimicCaptor->GetMimicCaptureLocation(),Pull);
    P.Z+=FMath::Sin(Pull*PI)*45;
    SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
    const float Shrink=FMath::Lerp(1.f,.06f,FMath::SmoothStep(.25f,.95f,Alpha));
    FTransform Pose=MimicRestoreMeshTransform; Pose.SetScale3D(Pose.GetScale3D()*Shrink); Pose.SetLocation(Pose.GetLocation()*Shrink);
    GetMesh()->SetRelativeTransform(Pose);
    GetMesh()->SetVisibility(bMimicRestoreMeshVisible && Alpha<.94f);
    GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->SetMovementMode(MOVE_None);
}
void AMCToothCharacter::StopJump() { StopJumping(); }
void AMCToothCharacter::SetSprintInputHeld(bool Held) { if(Held) StartSprint(); else StopSprint(); }
void AMCToothCharacter::StartSprint()
{
    if(bSprintInputHeld || !CanWork() || HasFrozenLegs()) return;
    bSprintInputHeld=true;
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    Move->SetSprinting(true);
    // Start the predicted lunge on the press. Waiting for release makes the
    // response depend on tap length and loses taps during slow frames.
    Move->RequestDash();
}
void AMCToothCharacter::StopSprint()
{
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    bSprintInputHeld=false;Move->SetSprinting(false);
}
void AMCToothCharacter::CancelSprintInput()
{
    bSprintInputHeld=false;
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    Move->SetSprinting(false);Move->SetWantsDash(false);
}
bool AMCToothCharacter::IsDashing() const { return CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->IsDashing(); }
float AMCToothCharacter::GetDashProgress() const { return CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->GetDashProgress(); }
FVector AMCToothCharacter::GetDashDirection() const { return FVector(CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->DashDirection); }
float AMCToothCharacter::GetDashDuration() const { return CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->GetDashDuration(); }
void AMCToothCharacter::StartBrush() { ServerSetWorking(true,true); }
void AMCToothCharacter::StopBrush() { ServerSetWorking(true,false); }
void AMCToothCharacter::StartHandle()
{
    if(!CanWork()) return;
    if(const auto* PC=Cast<APlayerController>(Controller);PC && PC->IsMoveInputIgnored()) return;
    AMCRewardChest* Rescue=nullptr; double RescueDistance=TNumericLimits<double>::Max();
    for(TActorIterator<AMCRewardChest> It(GetWorld());It;++It) if(It->CanRescue(this)) {
        const double Distance=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
        if(Distance<RescueDistance) {Rescue=*It;RescueDistance=Distance;}
    }
    if(Rescue) {
        StopBrace(); StopPrimary(); StopBrush(); FoodCollection->Stop();
        auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
        Move->SetWantsClimb(false); Move->CancelDash();
        bMimicRescueInputHeld=true; ServerSetMimicRescueHeld(Rescue,true); return;
    }
    if(AMCToothpick::FindPullTarget(this)) {
        FoodCollection->Stop(); StopPrimary();
        CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(false);
        ServerSetWorking(false,true); return;
    }
    AMCRewardChest* Nearby=nullptr;
    double Nearest=TNumericLimits<double>::Max();
    for(TActorIterator<AMCRewardChest> It(GetWorld());It;++It) {
        const double Distance=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
        if(It->Stage!=EMCRewardChestStage::Landed || Distance>=Nearest
            || Distance>FMath::Square(FMath::Clamp(It->OpenRadius,100.f,800.f))) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(RewardInteractionCandidate),true,this); Query.AddIgnoredActor(*It);
        FHitResult Block;
        if(GetWorld()->LineTraceSingleByChannel(Block,GetActorLocation()+FVector(0,0,35),It->Solid->Bounds.Origin,ECC_Visibility,Query)) continue;
        Nearby=*It; Nearest=Distance;
    }
    if(Nearby) { ServerBeginRewardOpening(Nearby); return; }
    if (CanWork() && !bInCoffee && !bSelfCare && !CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->IsClimbing()
        && (FoodCollection->bCollecting || FoodCollection->HasCandidate()))
    { ServerToggleFoodCollection(); return; }
    CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(true); ServerSetWorking(false,true);
}
void AMCToothCharacter::ServerBeginRewardOpening_Implementation(AMCRewardChest* Chest)
{
    if(IsValid(Chest) && Chest->GetWorld()==GetWorld()) Chest->BeginLockpicking(this);
}
void AMCToothCharacter::ServerSetMimicRescueHeld_Implementation(AMCRewardChest* Chest,bool Held)
{
    if(!Held) {
        if(IsValid(MimicRescueTarget)) MimicRescueTarget->EndRescue(this);
        MimicRescueTarget=nullptr; ForceNetUpdate(); return;
    }
    if(!IsValid(Chest) || Chest->GetWorld()!=GetWorld() || !CanWork() || !Chest->CanRescue(this)) return;
    if(MimicRescueTarget && MimicRescueTarget!=Chest) MimicRescueTarget->EndRescue(this);
    Grip->ReleaseBrace(); DropFood(); FoodCollection->Stop(); bBrushing=false; bHandling=false; bPrimaryHeld=false; bSelfCare=false; ResetContact();
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement()); Move->SetWantsClimb(false); Move->CancelDash();
    if(Chest->BeginRescue(this)) {MimicRescueTarget=Chest;ForceNetUpdate();}
}
void AMCToothCharacter::StopMimicRescue()
{
    if(!bMimicRescueInputHeld && !MimicRescueTarget) return;
    bMimicRescueInputHeld=false;
    if(HasAuthority() || IsLocallyControlled()) ServerSetMimicRescueHeld(nullptr,false);
}
void AMCToothCharacter::StopHandle() { StopMimicRescue(); CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(false); ServerSetWorking(false,false); }
void AMCToothCharacter::StartPrimary()
{
    if(IsMimicCaptured()) return;
    if (!Status->IsAlive()) { if (auto* PC=Cast<AMCPlayerController>(Controller)) PC->NextSpectator(); return; }
    Inventory->UpdateSprayAim(true);
    ServerSetPrimary(true);
}
void AMCToothCharacter::ServerToggleFoodCollection_Implementation()
{
    if (!CanWork() || bInCoffee || bSelfCare || CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->IsClimbing()
        || (!FoodCollection->bCollecting && !FoodCollection->HasCandidate())) return;
    CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(false);
    bPrimaryHeld=false; bBrushing=false; bHandling=false; DropFood(); ResetContact();
    Grip->ReleaseBrace();
    FoodCollection->Toggle(); ForceNetUpdate();
}
void AMCToothCharacter::SelectBrush() { Inventory->ServerSelect(EMCToolSlot::Brush); }
void AMCToothCharacter::SelectPickaxe() { Inventory->ServerSelect(EMCToolSlot::Pickaxe); }
void AMCToothCharacter::SelectKnife() { Inventory->ServerSelect(EMCToolSlot::Knife); }
void AMCToothCharacter::SelectSpray() { Inventory->ServerSelect(EMCToolSlot::Spray); }
bool AMCToothCharacter::CanSwitchTool() const { return GetWorld()->GetTimeSeconds()>=NextSwingTime; }
void AMCToothCharacter::NotifyTaskFeedback(bool Success,FVector Point)
{
    if(!HasAuthority()) return;
    const auto* GS=GetWorld()->GetGameState(); const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    (Success?TaskSuccessAt:TaskFailureAt)=Now; ForceNetUpdate();
    if(Success) AMCReactionVFX::Spawn(GetWorld(),Point.IsNearlyZero()?GetActorLocation()+FVector(0,0,90):Point+FVector(0,0,110),EMCReactionEffect::Stars,2.5f,155);
}
void AMCToothCharacter::StopPrimary() { Inventory->UpdateSprayAim(true); ServerSetPrimary(false); }
void AMCToothCharacter::SetPrimaryInputHeld(bool Held)
{
    if ((!HasAuthority() && !IsLocallyControlled()) || (Held && !CanWork())) return;
    // Swimming/cling uses the ordinary RPC even while work is unavailable.
    // A release must also clear that actual action state after leaving water.
    if (bSharedPrimaryInputHeld==Held && (Held || !bPrimaryHeld)) return;
    bSharedPrimaryInputHeld=Held;
    if(Held) StartPrimary(); else StopPrimary();
}
void AMCToothCharacter::SetHandleInputHeld(bool Held)
{
    if ((!HasAuthority() && !IsLocallyControlled()) || (Held && !CanWork()) || bSharedHandleInputHeld==Held) return;
    bSharedHandleInputHeld=Held;
    if(Held) StartHandle(); else StopHandle();
}
void AMCToothCharacter::SetFoodCollectionInput(bool Enabled)
{
    if((!HasAuthority() && !IsLocallyControlled()) || !FoodCollection || FoodCollection->bCollecting==Enabled) return;
    ServerToggleFoodCollection();
}
void AMCToothCharacter::SetJumpInputHeld(bool Held)
{
    if ((!HasAuthority() && !IsLocallyControlled()) || (Held && !CanWork()) || bSharedJumpInputHeld==Held) return;
    bSharedJumpInputHeld=Held;
    if(Held) StartJump(); else StopJump();
}
void AMCToothCharacter::SetSelfCareInput(bool Enabled)
{
    if ((!HasAuthority() && !IsLocallyControlled()) || bSelfCare==Enabled || !CanWork()) return;
    ToggleSelfCare();
}
void AMCToothCharacter::SetBraceInputHeld(bool Held) { if(Held) StartBrace(); else StopBrace(); }
void AMCToothCharacter::StartBrace()
{
    if(!Status->IsAlive()) { if(auto* PC=Cast<AMCPlayerController>(Controller)) PC->PreviousSpectator(); return; }
    if(!CanWork()) return;
    if(const auto* PC=Cast<APlayerController>(Controller); PC && PC->IsMoveInputIgnored()) return;
    StopPrimary(); StopHandle(); FoodCollection->Stop();
    CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->CancelDash();
    Grip->SetBraceHeld(true);
}
void AMCToothCharacter::StopBrace() { Grip->SetBraceHeld(false); }
void AMCToothCharacter::ContinueFogGuard()
{
    if(!Grip || Grip->IsBraceInputHeld() || !CanWork()) return;
    for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && It->IsActive() && It->Stage==EMCFogBrawlStage::Warning)
        {
            StartBrace();
            return;
        }
}
void AMCToothCharacter::ServerSetPrimary_Implementation(bool bActive)
{
    if(bActive && IsMimicCaptured()) return;
    if(bActive) Grip->ReleaseBrace();
    // E explicitly enters the retained collection mode. Default brush input must
    // keep cleaning beside food in the destruction loop instead of starting a stack.
    if(bActive && CanWork() && !bInCoffee && !bSelfCare && Inventory->IsCleaningTool() && FoodCollection->bCollecting) {
        bPrimaryHeld=false;bBrushing=false;bHandling=false;DropFood();ResetContact();FoodCollection->Toggle();ForceNetUpdate();return;
    }
    bPrimaryHeld=bActive && Status->IsAlive();
    bWantsCling=bPrimaryHeld;
    if (!bPrimaryHeld)
    {
        ClingTooth=nullptr; bBrushing=false; bHandling=false; DropFood(); ResetContact();
    }
    else ResolvePrimaryAction();
    Inventory->UpdateWatergunInput();
    ForceNetUpdate();
}
void AMCToothCharacter::ResolvePrimaryAction()
{
    if(FoodCollection->bCollecting) return;
    if (!bPrimaryHeld || !CanWork() || GetWorld()->GetTimeSeconds()<NextSwingTime-.3f) return;
    if(!bInCoffee && !Inventory->IsCleaningTool()) {
        if(Inventory->IsChainsawRunning()) {bBrushing=false;bHandling=false;return;}
        if(Inventory->Selected==EMCToolSlot::Spray) Inventory->ServerSpray();
        else ServerSwingBrush();
        return;
    }
    if (Grip->GrabbedPlayer) { bHandling=true; bBrushing=false; return; }
    if (HeldFood)
    {
        bHandling=true; bBrushing=false;
        if (Grip->HasFreeHand())
        {
            AMCFoodActor* Best=nullptr; float Score=MAX_flt;
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            {
                if (It->bBrushTool || It->IsDisposed() || It->Phase==EMCFoodPhase::Carried || !Grip->CanAcquire(*It) || !CanContact(*It)) continue;
                const float D=FVector::DistSquared(It->Visual->Bounds.GetBox().GetClosestPointTo(GetActorLocation()),GetActorLocation());
                if (D<Score && D<FMath::Square(It->Settings.GrabReach)) { Score=D; Best=*It; }
            }
            if (Best) Best->TryGrab(this);
        }
        return;
    }
    auto Distance=[&](AActor* Target)
    {
        auto* Primitive=Cast<UPrimitiveComponent>(Target->GetRootComponent());
        const FVector Point=Primitive?Primitive->Bounds.GetBox().GetClosestPointTo(GetActorLocation()):Target->GetActorLocation();
        return FVector::DistSquared(Point,GetActorLocation());
    };
    auto* Clean=HasBrush()?FindCareTarget(true):nullptr;
    // Keep scrubbing the acquired tooth while LMB is held, even beside loose food.
    if(bBrushing && Clean && Clean->GetOwner()==CareTarget
        && (Cast<AMCArenaTooth>(CareTarget) || Cast<AMCMouthSurface>(CareTarget) || Cast<AMCToothCharacter>(CareTarget))) { bHandling=false; return; }
    auto* Repair=FindCareTarget(false);
    bool BrushMode=Clean && (!Repair || Distance(Clean->GetOwner())<=Distance(Repair->GetOwner()));
    float BestDistance=BrushMode?Distance(Clean->GetOwner()):Repair?Distance(Repair->GetOwner()):MAX_flt;
    AMCFoodActor* BestFood=nullptr;
    if (!bSelfCare && !bInCoffee)
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        {
            if (It->bBrushTool || !It->UsesLegacyGrip() || It->IsDisposed() || It->Phase==EMCFoodPhase::Equipped || It->Phase==EMCFoodPhase::Carried || !CanContact(*It)) continue;
            const float D=Distance(*It);
            if (D<BestDistance && D<=FMath::Square(It->Settings.GrabReach)) { BestFood=*It; BestDistance=D; }
        }
    AMCToothCharacter* BestPlayer=nullptr;
    if (!bSelfCare && !bInCoffee && !Clean && !Repair)
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            if (*It==this || !It->Status->IsAlive()) continue;
            const FVector D=It->ToothPhysics->PhysicalLocation()-GetActorLocation();
            if (D.SizeSquared()<FMath::Min(BestDistance,FMath::Square(130.f)) && FVector::DotProduct(D.GetSafeNormal2D(),GetActorForwardVector())>.1f)
            { BestDistance=D.SizeSquared(); BestPlayer=*It; BestFood=nullptr; }
        }
    if (BestFood || BestPlayer) BrushMode=false;
    const bool Handling=BestFood || BestPlayer || Repair && !BrushMode;
    if (bBrushing!=BrushMode || bHandling!=Handling)
    {
        bBrushing=BrushMode; bHandling=Handling; ResetContact(); OnRep_Working(); ForceNetUpdate();
    }
    if (BestFood) BestFood->TryGrab(this);
    if (BestPlayer) Grip->BeginPlayerGrip(BestPlayer);
}
void AMCToothCharacter::CancelGameplayInput()
{
    if(Inventory) Inventory->CancelUpgradeUse();
    bSharedPrimaryInputHeld=false; bSharedHandleInputHeld=false; bSharedJumpInputHeld=false;
    StopBrace();
    FoodCollection->Stop();
    CancelSprintInput();CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->CancelDash();
    StopPrimary(); StopBrush(); StopHandle(); StopJump(); LocalPaddle=FVector2D::ZeroVector; ServerPaddle(LocalPaddle);
    GetCharacterMovement()->StopMovementImmediately();
}
void AMCToothCharacter::TogglePanel() { StopBrace(); StopPrimary(); StopBrush(); StopHandle(); if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->ToggleTuning(); }
void AMCToothCharacter::ToggleConnection() { StopBrace(); StopPrimary(); StopBrush(); StopHandle(); if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->ToggleConnection(); }
void AMCToothCharacter::RestartRun() { if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->RequestRestart(); }
void AMCToothCharacter::ServerSetWorking_Implementation(bool bBrush, bool bActive)
{
    if(bActive && IsMimicCaptured()) return;
    if(bActive) Grip->ReleaseBrace();
    if(!bBrush) CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(bActive && !AMCToothpick::FindPullTarget(this));
    if (!bBrush) { bWantsCling=bPrimaryHeld && Status->IsAlive(); if (!bActive) ClingTooth=nullptr; }
    if (bActive && (!CanWork() || GetWorld()->GetTimeSeconds()<NextSwingTime-0.3f)) return;
    if ((bBrush?bBrushing:bHandling)==bActive) return;
    if (bBrush) { bBrushing=bActive; if (bActive) { bHandling=false; DropFood(); } }
    else { bHandling=bActive; if (bActive) bBrushing=false; else DropFood(); }
    ResetContact();
    OnRep_Working(); ForceNetUpdate();
}
void AMCToothCharacter::OnRep_Working() { WorkStartedAt = GetWorld()->GetTimeSeconds(); }
void AMCToothCharacter::Landed(const FHitResult& Hit)
{
    NotifyCameraImpact(float(FMath::Max(0.,-FVector::DotProduct(GetVelocity(),Hit.ImpactNormal))),Hit.ImpactNormal);
    auto* Key=Cast<AMCArenaTooth>(Hit.GetActor());
    if (Key) Key->NotifyPianoLanding(this,Hit,-GetVelocity().Z);
    const bool PianoLanding=Key && Key->Settings.bPianoEnabled && Key->IsAvailable()
        && Hit.GetComponent()==Key->Body && Hit.ImpactNormal.Z>=0.5f && -GetVelocity().Z>=40;
    if (auto* Throat=Cast<AMCThroat>(Hit.GetActor())) Throat->NotifyUvulaLanding(this,Hit,-GetVelocity().Z);
    Super::Landed(Hit); LandingImpulse = 1.f;
    if(OrderJumpTarget || !OrderJumpIgnoredActors.IsEmpty()) ClearOrderJump();
    if (SoundPalette && !PianoLanding) SoundPalette->Play(this,TEXT("Jump"),GetActorLocation());
}
void AMCToothCharacter::FindWork(float DeltaSeconds)
{
    if(HasAuthority() && bHandling && !bPrimaryHeld && !bSelfCare)
        if(auto* Pick=AMCToothpick::FindPullTarget(this)) {
            CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(false);
            if(Pick->TryPull(this,DeltaSeconds)) {CareTarget=Pick;ContactProgress=Pick->PullProgress;ForceNetUpdate();return;}
        }
    if(CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->IsClimbing()) { bBrushing=false; DropFood(); ResetContact(); return; }
    if(OrderJumpTarget) { bBrushing=false; bHandling=false; ResetContact(); return; }
    if (bPrimaryHeld) ResolvePrimaryAction();
    if (!CanWork() || (!bBrushing && !bHandling)) { ResetContact(); DropFood(); return; }
    if (bHandling && !bSelfCare)
    {
        if (!HeldFood && !bPrimaryHeld)
        {
            AMCFoodActor* Best=nullptr; float Distance=MAX_flt;
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            {
                const float D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());
                if (!It->bBrushTool && It->UsesLegacyGrip() && !It->IsDisposed() && It->Phase!=EMCFoodPhase::Equipped && D<Distance && D<=FMath::Square(It->Settings.GrabReach)) { Best=*It; Distance=D; }
            }
            if (Best) Best->TryGrab(this);
        }
        if (HeldFood || Grip->GrabbedPlayer) { ResetContact(); return; }
    }
    AdvanceCare(DeltaSeconds);
}
void AMCToothCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(IsFreezingToDeath())
    {
        ConsumeMovementInputVector();GetCharacterMovement()->StopMovementImmediately();
        UpdateMouthCamera(DeltaSeconds);
        if(HasAuthority() && Status->IsAlive() && FreezeDeathAge()>=FreezeDeathHoldSeconds)
            Status->Damage(Status->State.MaxHealth,FVector::UpVector);
        return;
    }
    if(HasAuthority() && HasFrozenLegs() && (!Status->IsAlive() || IsMimicCaptured() || SwallowedBy)) ClearFrozenLegs();
    if(HasFrozenLegs()) UpdateIceLegVisuals();
    UpdateMimicCapture(DeltaSeconds);
    if(HasAuthority() && MimicRescueTarget && (!IsValid(MimicRescueTarget) || MimicRescueTarget->RescuePlayer!=this || !MimicRescueTarget->CanRescue(this))) {
        if(IsValid(MimicRescueTarget)) MimicRescueTarget->EndRescue(this);
        MimicRescueTarget=nullptr; ForceNetUpdate();
    }
    if(IsLocallyControlled() && bSprintInputHeld) {
        if(!CanWork() || HasFrozenLegs()) CancelSprintInput();
        else CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetSprinting(true);
    }
    UpdateYawn(DeltaSeconds);
    const auto* OrderState=GetWorld()->GetGameState();
    const double OrderNow=OrderState?OrderState->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(HasAuthority() && OrderJumpTarget && (!CanWork() || OrderNow-OrderJumpStartedAt>3)) ClearOrderJump();
    const float Prepare=OrderJumpTarget && !bOrderJumpLaunched?FMath::SmoothStep(0.f,OrderPrepareSeconds*.8f,float(OrderNow-OrderJumpStartedAt)):0.f;
    AnimationOrderPrepare=FMath::Lerp(AnimationOrderPrepare,Prepare,1-FMath::Exp(-24.f*DeltaSeconds));
    AnimationOrderFlight=FMath::Lerp(AnimationOrderFlight,OrderJumpTarget && bOrderJumpLaunched?1.f:0.f,1-FMath::Exp(-14.f*DeltaSeconds));
    const auto* OrderBase=Cast<UPrimitiveComponent>(GetMovementBaseObject());
    const auto* BaseThroat=OrderBase?Cast<AMCThroat>(OrderBase->GetOwner()):nullptr;
    const float Press=BaseThroat && BaseThroat->UvulaLanding==OrderBase?1.f:0.f;
    AnimationOrderPress=FMath::Lerp(AnimationOrderPress,Press,1-FMath::Exp(-16.f*DeltaSeconds));
    if(!HasAuthority() && SwallowedBy) {
        const auto* GS=GetWorld()->GetGameState(); const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        const float T=(SwallowedBy->ThroatPhase==EMCThroatPhase::Spasm || SwallowedBy->ThroatPhase==EMCThroatPhase::Vomiting)?1.f:FMath::Clamp(float(Now-SwallowedBy->PhaseStartedAt)/SwallowedBy->SwallowSeconds,0.f,1.f);
        const FVector End=SwallowedBy->GetActorTransform().TransformPosition(SwallowedBy->GateCenter+FVector(1050,0,20));
        const float Alpha=FMath::SmoothStep(0.f,.72f,T);
        FVector P=FMath::Lerp(ThroatCaptureStart,End,Alpha); P.Z+=FMath::Sin(Alpha*PI)*100;
        SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
    }
    UpdateMouthCamera(DeltaSeconds);
    if (IsLocallyControlled() && !bLoadedLocalTuning)
    {
        bLoadedLocalTuning = true;
        const FString File = FPaths::ProjectSavedDir()/TEXT("AnimationTuning.ini");
        float* Values[] = {&AnimationSettings.Squash,&AnimationSettings.Stretch,&AnimationSettings.Bob,&AnimationSettings.Lean,&AnimationSettings.FollowThrough,&AnimationSettings.Tempo,&AnimationSettings.Anticipation,&AnimationSettings.Exaggeration};
        const TCHAR* Names[] = {TEXT("Squash"),TEXT("Stretch"),TEXT("Bob"),TEXT("Lean"),TEXT("FollowThrough"),TEXT("Tempo"),TEXT("Anticipation"),TEXT("Exaggeration")};
        const float Min[] = {0,0,0,0,0,0.5f,0.05f,0.5f}; const float Max[] = {0.6f,0.6f,20,30,1,2,0.4f,2};
        for (int32 I=0; I<8; ++I) { GConfig->GetFloat(TEXT("ToothAnimation"),Names[I],*Values[I],File); *Values[I] = FMath::Clamp(*Values[I],Min[I],Max[I]); }
    }
    if (HasAuthority())
    {
        if (GetWorld()->GetTimeSeconds()-LastPaddleAt>.3) PaddleInput=FVector2D::ZeroVector;
        FindWork(FMath::Min(DeltaSeconds,0.1f));
        if (!SwallowedBy && !IsMimicCaptured() && Status->IsAlive() && GetActorLocation().Z < -300) Status->Damage(Status->State.MaxHealth);
    }
    if (IsLocallyControlled() && IsPlayerControlled() && bInCoffee)
    { PaddleSendElapsed+=DeltaSeconds; if (PaddleSendElapsed>=.05f) { ServerPaddle(WorldPaddleInput()); PaddleSendElapsed=0; } }
    const bool Swimming=GetCharacterMovement()->IsSwimming();
    const FVector StrokeIntent=GetCharacterMovement()->GetCurrentAcceleration().GetClampedToMaxSize(GetCharacterMovement()->GetMaxAcceleration())/FMath::Max(1.f,GetCharacterMovement()->GetMaxAcceleration());
    if (HasAuthority()) SwimIntent=Swimming?StrokeIntent:FVector::ZeroVector;
    AnimationSwim=FMath::FInterpTo(AnimationSwim,Swimming?1.f:0.f,DeltaSeconds,7.f);
    AnimationSwimEffort=FMath::FInterpTo(AnimationSwimEffort,ClingTooth?0.f:float((IsLocallyControlled()?StrokeIntent:FVector(SwimIntent)).Size2D()),DeltaSeconds,6.f);
    AnimationStroke=FMath::Fmod(AnimationStroke+DeltaSeconds*(3.5f+AnimationSwimEffort*5.f)*AnimationSwim,2*PI);
    const float Turn=FMath::Clamp(FMath::FindDeltaAngleDegrees(PreviousAnimationYaw,GetActorRotation().Yaw)/FMath::Max(DeltaSeconds,.001f)/180.f,-1.f,1.f);
    AnimationTurn=FMath::FInterpTo(AnimationTurn,Turn,DeltaSeconds,8.f); PreviousAnimationYaw=GetActorRotation().Yaw;
    const float GroundSpeed=GetVelocity().Size2D();
    AnimationBrake=FMath::FInterpTo(AnimationBrake,FMath::Clamp((PreviousAnimationSpeed-GroundSpeed)/FMath::Max(DeltaSeconds,.001f)/1600.f,0.f,1.f),DeltaSeconds,9.f);
    PreviousAnimationSpeed=GroundSpeed;
    Brush->SetVisibility(!IsMimicCaptured() && HasBrush() && Inventory->ShouldPresentTool() && !HeldFood && AnimationClimb<.05f && AnimationSwim<.05f && !OrderJumpTarget && AnimationOrderPress<.05f && AnimationOrderFlight<.05f
        && (!Grip || Grip->Blend()<.05f) && (!Expression || Expression->BodyAlpha()<.01f));
    if (StatusMaterial)
    {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        StatusMaterial->SetScalarParameterValue(TEXT("Coffee"),Status->CoffeeAmount());
        StatusMaterial->SetScalarParameterValue(TEXT("Damage"),1-Status->State.Health/FMath::Max(1.f,Status->State.MaxHealth));
        StatusMaterial->SetScalarParameterValue(TEXT("HitFlash"),Status->State.bCareReaction?0:FMath::Max(0.,1-(Now-Status->State.ReactionAt)/.16));
    }
    const auto& A = AnimationSettings;
    const float Time = GetWorld()->GetTimeSeconds();
    const float PreviewTime = FMath::Fmod(Time,8.f);
    const float PreviousAudioGait=Gait;
    UpdateLocomotion(DeltaSeconds);
    const float Speed=AnimationSpeed;
    LandingImpulse = FMath::FInterpTo(LandingImpulse,0.f,DeltaSeconds,11.f);
    const bool bAir = GetCharacterMovement()->IsFalling() || (bPreviewAnimation && PreviewTime>6.f);
    const bool bVisualBrush = bBrushing || (bPreviewAnimation && PreviewTime>=3.f && PreviewTime<6.f);
    const bool bWork = bVisualBrush || bHandling;
    const float WorkTime = bPreviewAnimation ? FMath::Max(0.f,PreviewTime-3.f) : Time - WorkStartedAt;
    const float Anticipation = bWork ? FMath::Clamp(1.f - WorkTime/FMath::Max(0.05f,A.Anticipation),0.f,1.f) : 0.f;
    const float Squash = (FMath::Sin(Gait*2.f)*0.22f*Speed + LandingImpulse + Anticipation*0.45f) * A.Squash * A.Exaggeration
        +AnimationOrderPrepare*.13f+AnimationOrderPress*.06f;
    const float Stretch = bAir ? A.Stretch * (bPreviewAnimation ? 0.8f : FMath::Clamp(FMath::Abs(GetVelocity().Z)/500.f,0.f,1.f)) : 0.f;
    const float GripBlend=Grip?Grip->Blend():0;
    const float BodyStretch=ToothPhysics->CanAct()?FMath::Clamp(Stretch-Squash,-.35f,.4f)*(1-GripBlend)*(1-AnimationSwim):0.f;
    if (StatusMaterial) StatusMaterial->SetScalarParameterValue(TEXT("BodyStretch"),BodyStretch);
    for (const auto& FaceMaterial:FaceMaterials) FaceMaterial->SetScalarParameterValue(TEXT("BodyStretch"),BodyStretch);
    GetMesh()->SetMorphTarget(TEXT("Squash"),ToothPhysics->CanAct()?FMath::Clamp(Squash/0.28f,0.f,1.f)*(1-GripBlend):0.f);
    GetMesh()->SetMorphTarget(TEXT("Stretch"),ToothPhysics->CanAct()?FMath::Clamp(Stretch/0.28f,0.f,1.f)*(1-GripBlend):0.f);
    const float Bob = bAir ? 0.f : (FMath::Abs(FMath::Sin(Gait))-.5f)*A.Bob*Speed*(1-.5f*AnimationSticky) + FMath::Sin(Time*2.f)*0.7f;
    const float Pitch = Speed*A.Lean + Anticipation*15.f + (bHandling && !HeldFood && !Grip->GrabbedPlayer && AnimationClimb<.05f ? FMath::Sin(Time*13.f)*7.f : 0.f);
    const float AttackTime=GetToolSwingElapsed();
    AnimationToolOffset=GetActorRotation().RotateVector(UMCInventoryComponent::SwingOffset(Inventory->Selected,AttackTime));
    const float AttackAngle=UMCInventoryComponent::SwingAngle(Inventory->Selected,AttackTime);
    const float Swing = AttackTime<Inventory->SwingDuration()?AttackAngle:bVisualBrush ? -35.f + FMath::Sin(WorkTime*18.f*A.Tempo)*65.f*(1.f-Anticipation) : bHandling ? 35.f : -12.f;
    const bool ChoppingTool=Inventory->Selected==EMCToolSlot::Pickaxe || Inventory->Selected==EMCToolSlot::Knife;
    BrushAngle = FMath::FInterpTo(BrushAngle,Swing,DeltaSeconds,ChoppingTool?25.f*UMCInventoryComponent::SwingPlayRate(Inventory->Selected):18.f-12.f*A.FollowThrough);
    AnimationGait=Gait; AnimationBrushAngle=BrushAngle*A.Exaggeration;
    if(Inventory->IsChainsawRunning()) {
        AnimationToolOffset=GetActorRotation().RotateVector(FVector(42,5,35)+FVector(FMath::Sin(Time*47)*3,0,FMath::Cos(Time*39)*2));
        AnimationBrushAngle=-16+FMath::Sin(Time*47)*4;
    }
    const float PoseEase=1-FMath::Exp(-16.f*DeltaSeconds);
    AnimationBob=FMath::Lerp(AnimationBob,Bob*(1-.85f*GripBlend),PoseEase);
    AnimationPitch=FMath::Lerp(AnimationPitch,(Pitch+(Status->IsLoose()?FMath::Sin(Time*7)*7:0))*(1-GripBlend),PoseEase);
    if(!UMCExperimentalAudioComponent::IsEnabled())
    {
        SoundAccumulator += DeltaSeconds;
        if (ToothPhysics->CanAct() && !Swimming && !bPreviewAnimation && SoundAccumulator > (bWork ? 0.28f : 0.34f) && SoundPalette && (bWork || (Speed > 0.2f && !bAir)))
        { SoundAccumulator = 0; SoundPalette->Play(this,bBrushing ? TEXT("Brush") : bHandling ? TEXT("Pull") : TEXT("Step"),GetActorLocation()); }
        BrushSoundAccumulator=0;
        FootstepSoundCooldown=0;
    }
    else
    {
        if(!bHandling) SoundAccumulator=0;
        FootstepSoundCooldown=FMath::Max(0.f,FootstepSoundCooldown-DeltaSeconds);
        const bool CanHearActions=GetNetMode()!=NM_DedicatedServer && ToothPhysics->CanAct() && !Swimming && !bPreviewAnimation;
        const bool FootPlanted=FMath::FloorToInt(PreviousAudioGait/PI)!=FMath::FloorToInt(Gait/PI);
        if(CanHearActions && GetCharacterMovement()->IsMovingOnGround() && GroundSpeed>15.f && Speed>.05f && FootPlanted && FootstepSoundCooldown<=0)
        {
            const float Pace=FMath::Clamp(GroundSpeed/FMath::Max(1.f,CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->WalkSpeed*1.5f),0.f,1.f);
            if(!ExperimentalAudio->Play(TEXT("Step"),GetActorLocation(),FMath::Lerp(.35f,.8f,Pace),Pace) && SoundPalette)
                SoundPalette->Play(this,TEXT("Step"),GetActorLocation());
            FootstepSoundCooldown=.13f;
        }
        // Match the contact that actually cleans; rendered wrist placement is for foam.
        const bool BrushTouching=CanHearActions && bBrushing && HasBrush() && BrushContact && BrushContact->IsWorkReady();
        if(BrushTouching)
        {
            BrushSoundAccumulator+=DeltaSeconds;
            const float StrokeSeconds=.28f/FMath::Clamp(AnimationSettings.Tempo,.5f,2.f);
            if(BrushSoundAccumulator>=StrokeSeconds)
            {
                BrushSoundAccumulator=FMath::Fmod(BrushSoundAccumulator,StrokeSeconds);
                const float BrushPace=FMath::Clamp((AnimationSettings.Tempo-.5f)/1.5f,0.f,1.f);
                if(!ExperimentalAudio->Play(TEXT("Brush"),BrushContact->ContactPoint(),.65f,BrushPace) && SoundPalette)
                    SoundPalette->Play(this,TEXT("Brush"),BrushContact->ContactPoint());
            }
        }
        else BrushSoundAccumulator=0;
        // Pull remains in the existing palette during this focused experiment.
        if(CanHearActions && bHandling && SoundPalette)
        {
            SoundAccumulator+=DeltaSeconds;
            if(SoundAccumulator>.28f) { SoundAccumulator=0; SoundPalette->Play(this,TEXT("Pull"),GetActorLocation()); }
        }
    }
}
void AMCToothCharacter::SwingBrush()
{
    if (!Status->IsAlive()) { if (auto* PC=Cast<AMCPlayerController>(Controller)) PC->PreviousSpectator(); return; }
    if (!bPreviewAnimation && ToothPhysics->CanAct()) ServerSwingBrush();
}
void AMCToothCharacter::ServerSwingBrush_Implementation()
{
    const float Now=GetWorld()->GetTimeSeconds();
    if (!CanWork() || Now<NextSwingTime) return;
    if(Inventory->Selected==EMCToolSlot::Knife && Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw)) return;
    Grip->ReleaseBrace();
    if(Inventory->Selected==EMCToolSlot::Spray) { Inventory->ServerSpray(); return; }
    FVector Point=FVector::ZeroVector,Normal=FVector::UpVector;
    CalculusTarget=Inventory->Selected==EMCToolSlot::Pickaxe && !Inventory->HasUpgrade(EMCToolUpgrade::Buffer)?FindCalculusTarget(Point,Normal):nullptr;
    CalculusContactLocal=CalculusTarget?CalculusTarget->Visual->GetComponentTransform().InverseTransformPosition(Point):FVector::ZeroVector;
    const FTransform Surface=CalculusTarget?CalculusTarget->Visual->GetComponentTransform():FTransform::Identity;
    CalculusNormalLocal=CalculusTarget?(Surface.InverseTransformVectorNoScale(Normal)*Surface.GetScale3D()).GetSafeNormal():FVector::UpVector;
    const auto* GS=GetWorld()->GetGameState();
    SwingStartedAt=GS?GS->GetServerWorldTimeSeconds():Now;
    NextSwingTime=Now+Inventory->SwingDuration(); ++ValidatedSwingCount;
    bBrushing=false; bHandling=false; DropFood(); ForceNetUpdate();
    MulticastSwing(SwingStartedAt,CalculusTarget,CalculusContactLocal,CalculusNormalLocal,uint8(Inventory->Selected));
    ResetContact();
    SwingContactEndsAt=Now+Inventory->SwingContactTime()+.12f;
    GetWorldTimerManager().SetTimer(SwingTimer,this,&AMCToothCharacter::ResolveSwing,Inventory->SwingContactTime(),false);
}
void AMCToothCharacter::MulticastSwing_Implementation(double StartedAt,AMCArenaTooth* AimTooth,FVector LocalPoint,FVector LocalNormal,uint8 ToolSlot)
{
    SwingStartedAt=StartedAt; CalculusTarget=AimTooth; CalculusContactLocal=LocalPoint; CalculusNormalLocal=LocalNormal;
    const FName Event=ToolSlot==uint8(EMCToolSlot::Knife)?FName(TEXT("KnifeSwing")):ToolSlot==uint8(EMCToolSlot::Pickaxe)?FName(TEXT("PickaxeSwing")):NAME_None;
    if((Event.IsNone() || !ExperimentalAudio->Play(Event,GetActorLocation(),.65f,.5f)) && SoundPalette)
        SoundPalette->Play(this,TEXT("Whoosh"),GetActorLocation());
}
void AMCToothCharacter::MulticastHitSound_Implementation(FVector Location,uint8 ToolSlot,float Intensity)
{
    const FName Event=ToolSlot==uint8(EMCToolSlot::Knife)?FName(TEXT("KnifeHit")):ToolSlot==uint8(EMCToolSlot::Pickaxe)?FName(TEXT("PickaxeHit")):NAME_None;
    if((Event.IsNone() || !ExperimentalAudio->Play(Event,Location,Intensity,.5f)) && SoundPalette)
        SoundPalette->Play(this,TEXT("Hit"),Location);
}
float AMCToothCharacter::GetToolSwingElapsed() const
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
    return float(Now-SwingStartedAt);
}
bool AMCToothCharacter::GetCalculusSwingContact(FVector& Point,FVector& Normal) const
{
    const float Elapsed=GetToolSwingElapsed();
    if(!Inventory || Inventory->Selected!=EMCToolSlot::Pickaxe || !CanWork() || bInCoffee
        || !IsValid(CalculusTarget) || !CalculusTarget->IsAvailable() || !CalculusTarget->Visual
        || Elapsed<0 || Elapsed>=Inventory->SwingDuration()) return false;
    const FTransform Surface=CalculusTarget->Visual->GetComponentTransform();
    Point=Surface.TransformPosition(CalculusContactLocal);
    const FVector Scale=Surface.GetScale3D().GetAbs().ComponentMax(FVector(.001f));
    Normal=Surface.TransformVectorNoScale(CalculusNormalLocal/Scale).GetSafeNormal();
    return !Point.ContainsNaN() && !Normal.IsNearlyZero();
}
bool AMCToothCharacter::WantsCalculusFacing(FVector& Direction) const
{
    FVector Point,Normal;
    if(!BrushContact || !GetCalculusSwingContact(Point,Normal)
        || !BrushContact->CanAcquireSurface(CalculusTarget) || !BrushContact->CanBrushToward(Point)) return false;
    Direction=(Point-GetActorLocation()).GetSafeNormal2D();
    return !Direction.IsNearlyZero();
}
AMCArenaTooth* AMCToothCharacter::FindCalculusTarget(FVector& Point,FVector& Normal) const
{
    if(!Inventory || Inventory->Selected!=EMCToolSlot::Pickaxe || !CanWork() || bInCoffee
        || bSelfCare || HeldFood || !BrushContact) return nullptr;
    auto Eligible=[&](AMCArenaTooth* Tooth,FVector& P,FVector& N) {
        return IsValid(Tooth) && Tooth->IsAvailable() && Tooth->Calculus
            && Tooth->Calculus->HasCalculus() && BrushContact->CanAcquireSurface(Tooth)
            && Tooth->Calculus->FindContact(const_cast<AMCToothCharacter*>(this),P,N)
            && BrushContact->CanBrushToward(P);
    };
    // Match cleaning: retain the acquired crown while held and reachable.
    if(Eligible(CalculusTarget,Point,Normal)) return CalculusTarget;
    AMCArenaTooth* Best=nullptr; float Distance=MAX_flt;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) {
        FVector P,N;
        if(!Eligible(*It,P,N)) continue;
        const float D=FVector::DistSquared(P,GetActorLocation());
        if(D<Distance) {Best=*It;Distance=D;Point=P;Normal=N;}
    }
    return Best;
}
void AMCToothCharacter::ResolveSwing()
{
    if (!HasAuthority() || !CanWork() || Inventory->Selected==EMCToolSlot::Spray) return;
    if(Inventory->Selected==EMCToolSlot::Pickaxe && Inventory->HasUpgrade(EMCToolUpgrade::Buffer)) {Inventory->ResolveBufferContact();return;}
    const uint8 AudioTool=uint8(Inventory->Selected);
    const float HitIntensity=FMath::Clamp(Inventory->Damage()/60.f,.25f,1.f);
    if(Inventory->Selected==EMCToolSlot::Pickaxe) {
        if(HasFrozenLegs() && HitFrozenLegsWithPickaxe(this,Inventory->Damage()))
        {
            ++ConfirmedHitCount; MulticastHitSound(GetActorLocation()-FVector(0,0,35),AudioTool,HitIntensity); return;
        }
        AMCToothCharacter* FrozenAlly=nullptr; float IceDistance=FMath::Square(180.f);
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            if(*It==this || !It->HasFrozenLegs() || !It->Status->IsAlive()) continue;
            const FVector Offset=It->GetActorLocation()-GetActorLocation();
            if(Offset.SizeSquared()>=IceDistance || FVector::DotProduct(Offset.GetSafeNormal2D(),GetActorForwardVector())<.25f) continue;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceLegSwing),false,this); Query.AddIgnoredActor(*It);
            FHitResult Block;
            if(GetWorld()->LineTraceSingleByChannel(Block,GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Query)) continue;
            FrozenAlly=*It; IceDistance=Offset.SizeSquared();
        }
        if(FrozenAlly && FrozenAlly->HitFrozenLegsWithPickaxe(this,Inventory->Damage()))
        {
            ++ConfirmedHitCount; MulticastHitSound(FrozenAlly->GetActorLocation()-FVector(0,0,35),AudioTool,HitIntensity); return;
        }
        for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
        {
            FVector Point;
            if(It->IsActive() && It->HitObstructionWithPickaxe(this,Inventory->Damage(),Point))
            { ++ConfirmedHitCount; MulticastHitSound(Point,AudioTool,HitIntensity); return; }
        }
        // An aimed swing never falls through to the tooth or a player if
        // another worker already broke its patch during the wind-up.
        if(CalculusTarget) {
            FVector Point,Normal,EligiblePoint,EligibleNormal;
            if(GetCalculusSwingContact(Point,Normal) && CalculusTarget->Calculus
                && CalculusTarget->Calculus->FindContact(this,EligiblePoint,EligibleNormal)
                && CalculusTarget->Calculus->ApplyPickaxeHit(this,Point,Normal,Inventory->Damage())) {
                ++ConfirmedHitCount; MulticastHitSound(Point,AudioTool,HitIntensity);
                if(!CalculusTarget->Calculus->HasCalculus())
                {
                    NotifyTaskFeedback(true,Point);
                    if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->NotifyAction(this,EMCTutorialAction::CalculusCleared,CalculusTarget);
                }
            }
            return;
        }
        AMCToothpick* BestPick=nullptr; float PickDistance=FMath::Square(180.f);
        for(TActorIterator<AMCToothpick> It(GetWorld());It;++It) {
            if(!It->CanReceivePickaxeHit(this)) continue;
            const FVector Offset=It->Body->Bounds.GetBox().GetClosestPointTo(GetActorLocation())-GetActorLocation();
            if(Offset.SizeSquared()<PickDistance) {BestPick=*It;PickDistance=Offset.SizeSquared();}
        }
        if(BestPick && BestPick->HitWithPickaxe(this,Inventory->Damage())) {++ConfirmedHitCount;MulticastHitSound(BestPick->GetActorLocation(),AudioTool,HitIntensity);return;}
        AMCIceEvent* BestMint=nullptr; float MintDistance=FMath::Square(180.f);
        for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) {
            if(It->Stage!=EMCIceEventStage::Active) continue;
            const FVector Offset=It->CandyContactPoint(GetActorLocation())-GetActorLocation();
            if(Offset.SizeSquared()<MintDistance && FVector::DotProduct(Offset.GetSafeNormal2D(),GetActorForwardVector())>.25f && CanContact(*It))
            { BestMint=*It; MintDistance=Offset.SizeSquared(); }
        }
        if(BestMint && BestMint->HitWithPickaxe(this,Inventory->Damage())) { ++ConfirmedHitCount; MulticastHitSound(BestMint->GetActorLocation(),AudioTool,HitIntensity); return; }
        AMCIceBlock* BestIce=nullptr; float Distance=FMath::Square(180.f);
        for(TActorIterator<AMCIceBlock> It(GetWorld());It;++It) {
            const FVector D=It->Body->Bounds.GetBox().GetClosestPointTo(GetActorLocation())-GetActorLocation();
            if(!It->bBroken && D.SizeSquared()<Distance && FVector::DotProduct(D.GetSafeNormal2D(),GetActorForwardVector())>.25f && CanContact(*It)) { BestIce=*It; Distance=D.SizeSquared(); }
        }
        if(BestIce && BestIce->HitWithPickaxe(this,Inventory->Damage())) { ++ConfirmedHitCount; MulticastHitSound(BestIce->GetActorLocation(),AudioTool,HitIntensity); if(BestIce->bBroken) NotifyTaskFeedback(true,BestIce->GetActorLocation()); return; }
    }
    if(!Inventory->IsCleaningTool()) {
        AMCNutEnemy* BestNut=nullptr; float Distance=FMath::Square(180.f);
        for(TActorIterator<AMCNutEnemy> It(GetWorld());It;++It) {
            if(!It->CanReceiveToolHit()) continue;
            const FVector Offset=It->GetToolTargetPoint(GetActorLocation())-GetActorLocation();
            if(Offset.SizeSquared()<Distance && FMath::Abs(Offset.Z)<120 && FVector::DotProduct(Offset.GetSafeNormal2D(),GetActorForwardVector())>.15f && CanContact(*It)) {BestNut=*It;Distance=Offset.SizeSquared();}
        }
        if(BestNut && BestNut->ReceiveToolDamage(Inventory->Damage(),this)>0) {++ConfirmedHitCount;MulticastHitSound(BestNut->GetActorLocation(),AudioTool,HitIntensity);return;}
    }
    AMCFoodActor* FoodTarget=nullptr; float FoodDistance=FMath::Square(180.f);
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
    {
        if (!Inventory->CanBreak(*It) || It->Phase==EMCFoodPhase::Swallowing) continue;
        FHitResult Contact;
        if (!It->FindToolContact(this,180.f,Contact)) continue;
        const float Distance=FVector::DistSquared(GetActorLocation(),Contact.ImpactPoint);
        if (Distance>=FoodDistance) continue;
        FoodTarget=*It; FoodDistance=Distance;
    }
    if (FoodTarget)
    {
        if (FoodTarget->HitFood(Inventory->Damage(),GetActorForwardVector(),this))
        {
            ++ConfirmedHitCount; MulticastHitSound(FoodTarget->GetActorLocation(),AudioTool,HitIntensity);
            if (FoodTarget->IsDisposed()) if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->NotifyAction(this,EMCTutorialAction::FoodCut,FoodTarget);
        }
        return;
    }
    AMCToothCharacter* Target=nullptr; float Best=FMath::Square(180.f);
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        if (*It==this || !It->Status->IsAlive() || It->ToothPhysics->GetBodyState()==EMCBodyState::Recovering) continue;
        const FVector Point=It->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?It->ToothPhysics->PhysicalLocation():It->GetActorLocation();
        const FVector Offset=Point-GetActorLocation();
        if (Offset.SizeSquared2D()>Best || FMath::Abs(Offset.Z)>120 || FVector::DotProduct(GetActorForwardVector(),Offset.GetSafeNormal2D())<0.25f) continue;
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCBrushHit),false,this); Params.AddIgnoredActor(*It);
        if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Point,ECC_Visibility,Params)) continue;
        Target=*It; Best=Offset.SizeSquared2D();
    }
    AMCArenaTooth* ArenaTarget=nullptr;
    AMCBossCharacter* BossTarget=nullptr;
    if (!Inventory->IsCleaningTool()) for (TActorIterator<AMCBossCharacter> It(GetWorld());It;++It)
    {
        const FVector Point=It->GetMeleeTargetPoint(GetActorLocation());
        const FVector Offset=Point-GetActorLocation();
        const FVector AimOffset=It->GetActorLocation()-GetActorLocation();
        if (!It->CanReceiveWeaponHit() || Offset.SizeSquared2D()>=Best || FMath::Abs(Offset.Z)>120
            || FVector::DotProduct(GetActorForwardVector(),AimOffset.GetSafeNormal2D())<.25f) continue;
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCBossWeaponHit),false,this); Query.AddIgnoredActor(*It);
        if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Point,ECC_Visibility,Query)) continue;
        BossTarget=*It; Best=Offset.SizeSquared2D();
    }
    if (BossTarget)
    {
        if (BossTarget->ReceiveBossDamage(Inventory->Damage(),this)>0.f)
        { ++ConfirmedHitCount; MulticastHitSound(BossTarget->GetMeleeTargetPoint(GetActorLocation()),AudioTool,HitIntensity); }
        return;
    }
    if(Inventory->IsCleaningTool()) for (TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
    {
        const FVector Offset=It->GetActorLocation()-GetActorLocation();
        if (!It->IsAvailable() || Offset.SizeSquared2D()>=Best || FMath::Abs(Offset.Z)>120 || FVector::DotProduct(GetActorForwardVector(),Offset.GetSafeNormal2D())<0.25f) continue;
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCArenaBrushHit),false,this); Params.AddIgnoredActor(*It);
        if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Params)) continue;
        ArenaTarget=*It; Best=Offset.SizeSquared2D();
    }
    if (ArenaTarget)
    {
        if (ArenaTarget->ReceiveArenaHit(ArenaTarget->Settings.BrushHitDamage,ArenaTarget->GetActorLocation()-GetActorLocation()))
        { ++ConfirmedHitCount; MulticastHitSound(ArenaTarget->GetActorLocation(),AudioTool,HitIntensity); }
        return;
    }
    if (!Target) {
        // Retry only a missed knife swing. A hit on food or a player consumes
        // the contact window, so follow-through cannot deal damage twice.
        const float Left=SwingContactEndsAt-GetWorld()->GetTimeSeconds();
        if(Inventory->Selected==EMCToolSlot::Knife && Left>KINDA_SMALL_NUMBER)
            GetWorldTimerManager().SetTimer(SwingTimer,this,&AMCToothCharacter::ResolveSwing,FMath::Min(1.f/30.f,Left),false);
        return;
    }
    ++ConfirmedHitCount;
    FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D(); if (Direction.IsNearlyZero()) Direction=GetActorForwardVector();
    Target->ToothPhysics->ApplyHit(Direction*ToothPhysics->Settings.Knockback+FVector(0,0,ToothPhysics->Settings.Lift),Target->GetActorLocation()+FVector(0,0,15));
    Target->Status->Damage(Inventory->IsCleaningTool()?25.f:Inventory->Damage(),Direction);
    MulticastHitSound(Target->GetActorLocation(),AudioTool,HitIntensity);
}
void AMCToothCharacter::NotifyCameraImpact(float ImpactSpeed,const FVector& Direction)
{
    if(!HasAuthority() || !Cast<APlayerController>(Controller) || !Status->IsAlive() || IsMimicCaptured()
        || !FMath::IsFinite(ImpactSpeed) || ImpactSpeed<=120.f || Direction.ContainsNaN()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    // Mesh/capsule contacts and an explicit knockback can describe the same impact.
    if(Now-LastCameraImpactAt<.1 && ImpactSpeed<=LastCameraImpactSpeed*1.25f) return;
    LastCameraImpactAt=Now; LastCameraImpactSpeed=ImpactSpeed;
    ClientCameraImpact(FMath::Min(ImpactSpeed,1000.f),Direction.GetSafeNormal());
}

void AMCToothCharacter::ClientCameraImpact_Implementation(float ImpactSpeed,FVector_NetQuantizeNormal Direction)
{
    if(IsLocallyControlled()) if(auto* PlayerCamera=Cast<UMCPlayerCameraComponent>(Camera))
        PlayerCamera->AddImpact(ImpactSpeed,Direction);
}

void AMCToothCharacter::NotifyGroundImpact(float Strength,const FVector& Source)
{
    if(HasAuthority() && Cast<APlayerController>(Controller) && Status->IsAlive()) ClientGroundImpact(Strength,Source);
}

void AMCToothCharacter::ClientGroundImpact_Implementation(float Strength,FVector_NetQuantize Source)
{
    if(IsLocallyControlled()) if(auto* PlayerCamera=Cast<UMCPlayerCameraComponent>(Camera))
        PlayerCamera->AddGroundImpact(Strength,Source);
}

void AMCToothCharacter::OnBodyHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit)
{
    if(HasAuthority() && HitComponent && OtherComponent && OtherActor!=this) {
        const bool bOwnSimulating=HitComponent->IsSimulatingPhysics(Hit.MyBoneName);
        const FVector OwnVelocity=bOwnSimulating
            ?HitComponent->GetPhysicsLinearVelocity(Hit.MyBoneName):GetVelocity();
        const FVector OtherVelocity=OtherComponent->IsSimulatingPhysics(Hit.BoneName)
            ?OtherComponent->GetPhysicsLinearVelocity(Hit.BoneName):OtherComponent->GetComponentVelocity();
        const float ClosingSpeed=float(FMath::Max(0.,-FVector::DotProduct(OwnVelocity-OtherVelocity,Hit.ImpactNormal)));
        // Walking capsules use CharacterMovement mass; GetMass is only valid
        // for a simulated body and otherwise emits a warning on every contact.
        const float OwnMass=bOwnSimulating?HitComponent->GetMass():GetCharacterMovement()->Mass;
        const float ImpulseSpeed=float(NormalImpulse.Size())/FMath::Max(1.f,OwnMass);
        NotifyCameraImpact(FMath::Max(ClosingSpeed,ImpulseSpeed),Hit.ImpactNormal);
    }
    if(Inventory) Inventory->HandleChainsawCollision(OtherActor,Hit);
    if(HasAuthority()) FoodCollection->HandleCarrierCollision(OtherActor,OtherComponent,NormalImpulse,Hit);
    if(auto* OtherPlayer=Cast<AMCToothCharacter>(OtherActor)) {
        if(HasAuthority() && OtherPlayer!=this) {
            FHitResult Reverse=Hit;Reverse.Normal=-Hit.Normal;Reverse.ImpactNormal=-Hit.ImpactNormal;
            OtherPlayer->FoodCollection->HandleCarrierCollision(this,HitComponent,-NormalImpulse,Reverse);
        }
        // Ordinary player contact only knocks a food carrier. Explicit attacks
        // still go through ResolveSwing/ApplyHit independently of this callback.
        return;
    }
    if (Cast<AMCFoodActor>(OtherActor)) return;
    if (!HasAuthority() || !ToothPhysics->CanAct() || !OtherComponent || OtherActor==this || GetWorld()->GetTimeSeconds()-LastEnvironmentHit<0.6f) return;
    FVector Impact=FVector::ZeroVector;
    if (OtherComponent->IsSimulatingPhysics()) Impact=(OtherComponent->GetPhysicsLinearVelocity()-GetVelocity())*0.7f;
    // Scenery still blocks the capsule, but a running bump is not a knockdown.
    if (Impact.Size()<ToothPhysics->Settings.FallThreshold) return;
    LastEnvironmentHit=GetWorld()->GetTimeSeconds(); Impact.Z=FMath::Max(150.f,Impact.Z);
    ToothPhysics->ApplyHit(Impact,Hit.ImpactPoint);
}
void AMCToothCharacter::SpawnPracticeTooth()
{
    if (!HasAuthority() || !IsLocallyControlled()) return;
    if (IsValid(PracticeTooth)) PracticeTooth->Destroy();
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    PracticeTooth=GetWorld()->SpawnActor<AMCToothCharacter>(GetClass(),GetActorLocation()+GetActorForwardVector()*130,GetActorRotation()+FRotator(0,180,0),Params);
    if (PracticeTooth) PracticeTooth->ToothPhysics->SetTuning(ToothPhysics->Settings);
}
void AMCToothCharacter::SaveTuning()
{
    const FString File = FPaths::ProjectSavedDir()/TEXT("AnimationTuning.ini");
    const float Values[] = {AnimationSettings.Squash,AnimationSettings.Stretch,AnimationSettings.Bob,AnimationSettings.Lean,AnimationSettings.FollowThrough,AnimationSettings.Tempo,AnimationSettings.Anticipation,AnimationSettings.Exaggeration};
    const TCHAR* Names[] = {TEXT("Squash"),TEXT("Stretch"),TEXT("Bob"),TEXT("Lean"),TEXT("FollowThrough"),TEXT("Tempo"),TEXT("Anticipation"),TEXT("Exaggeration")};
    for (int32 I=0; I<8; ++I) GConfig->SetFloat(TEXT("ToothAnimation"),Names[I],Values[I],File);
    GConfig->Flush(false,File);
}
void AMCToothCharacter::ResetTuning() { AnimationSettings = AnimationProfile ? AnimationProfile->Settings : FMCAnimationSettings(); SaveTuning(); }
void AMCToothCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCToothCharacter,bBrushing); DOREPLIFETIME(AMCToothCharacter,bHandling);
    DOREPLIFETIME(AMCToothCharacter,bPrimaryHeld);
    DOREPLIFETIME(AMCToothCharacter,IceLegHealth);
    DOREPLIFETIME(AMCToothCharacter,FreezeDeathStartedAt);
    DOREPLIFETIME(AMCToothCharacter,CalculusTarget); DOREPLIFETIME(AMCToothCharacter,CalculusContactLocal);
    DOREPLIFETIME(AMCToothCharacter,CalculusNormalLocal); DOREPLIFETIME(AMCToothCharacter,SwingStartedAt);
    DOREPLIFETIME(AMCToothCharacter,BagColor);
    DOREPLIFETIME(AMCToothCharacter,bSelfCare); DOREPLIFETIME(AMCToothCharacter,CareTarget); DOREPLIFETIME(AMCToothCharacter,ContactProgress);
    DOREPLIFETIME(AMCToothCharacter,HeldFood); DOREPLIFETIME(AMCToothCharacter,RespawnAt); DOREPLIFETIME(AMCToothCharacter,RespawnSourceId);
    DOREPLIFETIME(AMCToothCharacter,EquippedBrush); DOREPLIFETIME(AMCToothCharacter,bInCoffee); DOREPLIFETIME(AMCToothCharacter,ClingTooth);
    DOREPLIFETIME(AMCToothCharacter,SwimIntent);
    DOREPLIFETIME(AMCToothCharacter,SwallowedBy); DOREPLIFETIME(AMCToothCharacter,OrderJumpTarget);
    DOREPLIFETIME(AMCToothCharacter,OrderJumpStartedAt); DOREPLIFETIME(AMCToothCharacter,bOrderJumpLaunched);
    DOREPLIFETIME(AMCToothCharacter,ThroatCaptureStart);
    DOREPLIFETIME(AMCToothCharacter,TaskSuccessAt); DOREPLIFETIME(AMCToothCharacter,TaskFailureAt);
    DOREPLIFETIME(AMCToothCharacter,RewardInteraction);
    DOREPLIFETIME(AMCToothCharacter,MimicCaptor); DOREPLIFETIME(AMCToothCharacter,MimicRescueTarget);
    DOREPLIFETIME(AMCToothCharacter,MimicCaptureStart); DOREPLIFETIME(AMCToothCharacter,MimicReleaseLocation);
    DOREPLIFETIME(AMCToothCharacter,YawnEndsAt);DOREPLIFETIME(AMCToothCharacter,YawnTongue);DOREPLIFETIME(AMCToothCharacter,YawnAnchor);DOREPLIFETIME(AMCToothCharacter,YawnStartedAt);DOREPLIFETIME(AMCToothCharacter,YawnPullDirection);
}

bool AMCToothCharacter::CanWork() const
{
    if(IsFreezingToDeath()) return false;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const auto* Player=Cast<AMCPlayerController>(GetController());
    return (!Player || (!Player->IsBossIntroPlaying() && !Player->IsRewardInteractionActive())) && !IsValid(RewardInteraction) && !IsMimicCaptured() && !SwallowedBy && Status->IsAlive() && ToothPhysics->CanAct() && (!GS || (!GS->bLobbyWaiting && GS->Phase!=EMCShiftPhase::Won && GS->Phase!=EMCShiftPhase::Lost));
}
void AMCToothCharacter::ToggleSelfCare() { ServerToggleSelfCare(); }
void AMCToothCharacter::ServerToggleSelfCare_Implementation()
{
    if (!CanWork() || !Status->Settings.bAllowSelfCare) return;
    bSelfCare=!bSelfCare; DropFood(); ResetContact(); ForceNetUpdate();
}
void AMCToothCharacter::DropFood()
{
    if (!HasAuthority()) return;
    if (IsValid(Grip->Secondary.Food)) Grip->Secondary.Food->Release(this);
    if (IsValid(HeldFood)) HeldFood->Release(this);
    Grip->ReleasePlayer();
}
void AMCToothCharacter::ResetContact() { CareTarget=nullptr; ContactElapsed=0; ContactProgress=0; BrushContact->Release(); }
bool AMCToothCharacter::CanContact(AActor* Target) const
{
    if (!IsValid(Target) || !CanWork()) return false;
    if (Target==this) return bSelfCare && Status->Settings.bAllowSelfCare;
    if (const auto* Arena=Cast<AMCArenaTooth>(Target); Arena && !Arena->IsAvailable()) return false;
    FVector Point=Target->GetActorLocation();
    if (auto* Primitive=Cast<UPrimitiveComponent>(Target->GetRootComponent()))
        Point=Primitive->Bounds.GetBox().GetClosestPointTo(GetActorLocation());
    if(auto* Pick=Cast<AMCToothpick>(Target)) Point=Pick->Body->Bounds.GetBox().GetClosestPointTo(GetActorLocation());
    const FVector Offset=Point-GetActorLocation();
    if (Offset.Size()>Status->Settings.Reach || FVector::DotProduct(GetActorForwardVector(),Offset.GetSafeNormal2D())<-.25f) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCareContact),false,this); Params.AddIgnoredActor(Target);
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Point,ECC_Visibility,Params);
}
UMCToothStatusComponent* AMCToothCharacter::FindCareTarget(bool bBrush) const
{
    if (bSelfCare) return CanContact(const_cast<AMCToothCharacter*>(this)) && Status->NeedsCare(bBrush)?Status.Get():nullptr;
    UMCToothStatusComponent* Best=nullptr; float Distance=MAX_flt;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if (*It==this) continue;
        auto* Candidate=It->FindComponentByClass<UMCToothStatusComponent>();
        if (const auto* Patch=Cast<AMCMouthSurface>(*It); Patch && (Patch->bUlcer || !bBrush)) continue;
        if (!Candidate || !Candidate->NeedsCare(bBrush)) continue;
        FVector Contact=It->GetActorLocation();
        if(auto* Arena=Cast<AMCArenaTooth>(*It); bBrush && Arena) {
            FVector N; if(!Arena->FindDirtyContact(const_cast<AMCToothCharacter*>(this),Contact,N)) continue;
            if(CareTarget==Arena) return Candidate;
        } else if(auto* Patch=Cast<AMCMouthSurface>(*It); bBrush && Patch) {
            FVector N; if(!Patch->FindDirtyContact(const_cast<AMCToothCharacter*>(this),Contact,N)) continue;
            if(CareTarget==Patch) return Candidate;
        } else if(auto* Player=Cast<AMCToothCharacter>(*It); bBrush && Player) {
            FVector N; if(!Player->FindPlayerBrushContact(this,Contact,N)) continue;
            if(CareTarget==Player) return Candidate;
        } else if(!CanContact(*It)) continue;
        const float D=FVector::DistSquared(GetActorLocation(),Contact);
        if (D<Distance) { Best=Candidate; Distance=D; }
    }
    return Best;
}
void AMCToothCharacter::AdvanceCare(float Dt)
{
    if (!HasAuthority()) return;
    if (!CanWork() || (!bBrushing && !bHandling) || HeldFood || (bBrushing && !HasBrush()) || bInCoffee) { ResetContact(); return; }
    auto* Target=FindCareTarget(bBrushing);
    if (!Target) { ResetContact(); return; }
    if (CareTarget!=Target->GetOwner() || bLastContactBrush!=bBrushing) { ResetContact(); CareTarget=Target->GetOwner(); bLastContactBrush=bBrushing; }
    if (!FMath::IsFinite(Dt) || Dt<=0) return;
    if (bBrushing) if (auto* Surface=Cast<AMCMouthSurface>(Target->GetOwner())) {
        if(!Surface->BrushLiquid(this,FMath::Min(Dt,.1f))) ResetContact();
        else { ContactProgress=1-Surface->RemainingLiquid(); if(Surface->IsClean()) NotifyTaskFeedback(true,Surface->GetActorLocation()); }
        return;
    }
    if (bBrushing) if (auto* Arena=Cast<AMCArenaTooth>(Target->GetOwner()))
    {
        if (!Arena->BrushGrime(this,FMath::Min(Dt,.1f))) ResetContact();
        else { ContactProgress=1-Arena->RemainingGrime(); if(!Target->NeedsCare(true)) NotifyTaskFeedback(true,Arena->GetActorLocation()); }
        return;
    }
    if (bBrushing) if (const auto* Player=Cast<AMCToothCharacter>(Target->GetOwner()); Player && Player!=this)
    {
        FVector Point,Normal;
        if (!Player->FindPlayerBrushContact(this,Point,Normal)) { ResetContact(); return; }
        BrushContact->Contact(Target->GetOwner(),Point,Normal);
        if (!BrushContact->IsWorkReady()) { ContactElapsed=0; ContactProgress=0; return; }
    }
    ContactElapsed+=FMath::Min(Dt,.1f)*(bBrushing?Inventory->CleaningSpeedMultiplier():1.f);
    const float Seconds=Target->Settings.ContactSeconds;
    ContactProgress=FMath::Clamp(ContactElapsed/Seconds,0.f,1.f);
    if (ContactElapsed+KINDA_SMALL_NUMBER>=Seconds)
    {
        if (Target->CareContact(bBrushing,this) && bBrushing) ++SuccessfulBrushContacts;
        ContactElapsed=0; ContactProgress=0;
        if (!Target->NeedsCare(bBrushing)) { NotifyTaskFeedback(true,Target->GetOwner()->GetActorLocation()); ResetContact(); }
        ForceNetUpdate();
    }
}
bool AMCToothCharacter::FindPlayerBrushContact(const AMCToothCharacter* Worker,FVector& Point,FVector& Normal) const
{
    if (!IsValid(Worker) || Worker==this || !Status->NeedsCare(true) || !Worker->BrushContact->CanAcquireSurface(this)) return false;
    const FVector From=Worker->GetActorLocation()+FVector(0,0,55);
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const FVector Facing=(From-GetActorLocation()).GetSafeNormal2D();
    const FVector Sample=GetActorLocation()+Facing*150+FVector(0,0,55+FMath::Sin(Now*12)*2.5)+GetActorRightVector()*FMath::Sin(Now*24)*3;
    FName Bone; float Distance=0;
    if (!GetMesh()->K2_GetClosestPointOnPhysicsAsset(Sample,Point,Normal,Bone,Distance))
    {
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlayerBrushSurface),false,Worker);
        if (!GetCapsuleComponent()->LineTraceComponent(Hit,Sample,GetActorLocation()+FVector(0,0,55),Query)) return false;
        Point=Hit.ImpactPoint; Normal=Hit.ImpactNormal;
    }
    Normal=Normal.GetSafeNormal();
    if (Point.ContainsNaN() || Normal.IsNearlyZero() || FVector::DotProduct(Normal,(From-Point).GetSafeNormal())<.05
        || !Worker->BrushContact->CanReachAfterFacing(Point,Normal,this)) return false;
    FHitResult Block; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlayerBrushOcclusion),false,Worker); Query.AddIgnoredActor(this);
    return !GetWorld()->LineTraceSingleByChannel(Block,From,Point-Normal*2,ECC_Visibility,Query);
}
float AMCToothCharacter::FreezeDeathAge() const
{
    if(!IsFreezingToDeath()) return 0;
    const auto* GS=GetWorld()->GetGameState();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    return FMath::Max(0.f,float(Now-FreezeDeathStartedAt));
}
bool AMCToothCharacter::BeginFreezeDeath()
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || !Status->IsAlive() || IsFreezingToDeath() || IsMimicCaptured() || SwallowedBy
        || (GS && (GS->bTutorialActive || GS->bLobbyWaiting))) return false;
    FreezeDeathStartedAt=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    CancelGameplayInput();ClearOrderJump();ClearFrozenLegs();Grip->ReleaseBrace();DropFood();ResetContact();
    ClingTooth=nullptr;bWantsCling=false;NotifyTaskFeedback(false,GetActorLocation());
    OnRep_FreezeDeath();ForceNetUpdate();return true;
}
void AMCToothCharacter::OnRep_FreezeDeath()
{
    if(!IsFreezingToDeath()) return;
    if(!IsValid(FreezeDeathVisual)) FreezeDeathVisual=AMCFreezeDeathVFX::SpawnLocal(this,FreezeDeathStartedAt);
    GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();
    ConsumeMovementInputVector();
    ToothPhysics->SetThroatCaptured(true);ToothPhysics->SetComponentTickEnabled(false);
    Muscles->SetComponentTickEnabled(false);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetAllBodiesSimulatePhysics(false);GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetComponentTickEnabled(false);
    // Replace only the rendered body. The pawn remains the camera/view target until the usual respawn.
    if(IsValid(FreezeDeathVisual) || GetNetMode()==NM_DedicatedServer) GetMesh()->SetHiddenInGame(true,true);
    if(Brush) Brush->SetHiddenInGame(true);
}
void AMCToothCharacter::StatusChanged()
{
    if (!HasAuthority() || Status->IsAlive() || bDeathReported) return;
    ClearFrozenLegs();
    StopMimicRescue();
    if(IsMimicCaptured()) EndMimicCapture(MimicCaptureStart);
    Grip->ReleaseBrace();
    bDeathReported=true; bBrushing=false; bHandling=false; DropFood(); ResetContact();
    ClingTooth=nullptr; bWantsCling=false;
    if(!IsFreezingToDeath()) ToothPhysics->EnterDeath();
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->PlayerDied(this);
}
void AMCToothCharacter::FellOutOfWorld(const UDamageType&)
{
    if (!HasAuthority()) return;
    if (AMCTutorialDirector::IsSafeTutorial(GetWorld()))
    {
        ClearFrozenLegs();
        CancelGameplayInput(); DropFood();
        SetActorLocation(FVector(-700,0,180),false,nullptr,ETeleportType::TeleportPhysics);
        GetCharacterMovement()->Velocity=FVector::ZeroVector;
        return;
    }
    Status->Damage(Status->State.MaxHealth);
}
void AMCToothCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if(IsValid(FreezeDeathVisual)) FreezeDeathVisual->Destroy();
    if(HasAuthority()) ClearFrozenLegs();
    for(UStaticMeshComponent* Part:IceLegParts) if(Part) Part->SetVisibility(false);
    StopMimicRescue();
    if(HasAuthority() && IsMimicCaptured()) EndMimicCapture(MimicCaptureStart);
    if(MimicTickPrerequisite.IsValid()) RemoveTickPrerequisiteActor(MimicTickPrerequisite.Get());
    ClearCameraWallReveal();
    Grip->ReleaseBrace();
    DropFood();
    if (AppliedInputSubsystem.IsValid() && InputMap) AppliedInputSubsystem->RemoveMappingContext(InputMap);
    Super::EndPlay(Reason);
}
bool AMCToothCharacter::HasBrush() const
{
    return !Inventory || Inventory->IsCleaningTool();
}
void AMCToothCharacter::ThrowItem() { ServerThrowItem(); }
void AMCToothCharacter::ServerThrowItem_Implementation()
{
    if (!CanWork()) return;
    Grip->ReleaseBrace();
    if(FoodCollection->bCollecting || !FoodCollection->Pieces.IsEmpty()) {FoodCollection->Stop(true);return;}
    if (Grip->GrabbedPlayer) Grip->ThrowPlayer();
    else if (HeldFood)
    {
        auto* Other=Grip->Secondary.Food.Get(); HeldFood->Throw(this);
        if (IsValid(Other)) Other->Throw(this);
    }
    bPrimaryHeld=false; bWantsCling=false; ClingTooth=nullptr; bHandling=false; bBrushing=false; ResetContact();
}
void AMCToothCharacter::ServerPaddle_Implementation(FVector2D Direction)
{
    if (!bInCoffee || IsMimicCaptured() || HasFrozenLegs() || !Status->IsAlive() || Direction.ContainsNaN()) return;
    PaddleInput=Direction.GetClampedToMaxSize(1); LastPaddleAt=GetWorld()->GetTimeSeconds();
}
