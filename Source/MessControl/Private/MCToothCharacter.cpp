#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCColdCola.h"
#include "MCMouthSurface.h"
#include "MCThroat.h"
#include "MCBrushContactComponent.h"
#include "MCArenaTooth.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCGameMode.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MCPlayerController.h"
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
#include "PhysicsControlComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
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
    BrushPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BrushPivot"));
    BrushPivot->SetupAttachment(GetMesh(),TEXT("hand_r"));
    Brush = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MiniBrush"));
    Brush->SetupAttachment(BrushPivot); Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("ArenaCamera"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->SetUsingAbsoluteRotation(true); CameraBoom->SetRelativeRotation(FRotator(-16,0,0));
    CameraBoom->TargetArmLength = 1350; CameraBoom->TargetOffset = FVector(120,0,130);
    CameraBoom->bUsePawnControlRotation = false; CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 7; CameraBoom->CameraLagMaxDistance = 80;
    CameraBoom->bDoCollisionTest = true; CameraBoom->ProbeSize=24; CameraBoom->ProbeChannel=ECC_Camera;
    CameraBoom->AddTickPrerequisiteActor(this);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(CameraBoom);
    Camera->FieldOfView = FollowFOV;
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
                if (auto* FaceMaterial=GetMesh()->CreateDynamicMaterialInstance(I)) FaceMaterials.Add(FaceMaterial);
            }
        }
    }
}
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
    OrbitXAction=Action(EInputActionValueType::Axis1D); OrbitYAction=Action(EInputActionValueType::Axis1D);
    ZoomAction=Action(EInputActionValueType::Axis1D);
    SelfCareAction=Action(EInputActionValueType::Boolean);
    ThrowAction=Action(EInputActionValueType::Boolean);
    SprintAction=Action(EInputActionValueType::Boolean);
    InputMap->MapKey(SprintAction,EKeys::LeftShift); InputMap->MapKey(SprintAction,EKeys::Gamepad_LeftThumbstick);
    InputMap->MapKey(ThrowAction,EKeys::Q); InputMap->MapKey(ThrowAction,EKeys::Gamepad_FaceButton_Top);
    InputMap->MapKey(SelfCareAction,EKeys::C); InputMap->MapKey(SelfCareAction,EKeys::Gamepad_RightThumbstick);
    InputMap->MapKey(SwingAction,EKeys::RightMouseButton); InputMap->MapKey(SwingAction,EKeys::Gamepad_LeftShoulder);
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
    Input->BindAction(SprintAction,ETriggerEvent::Canceled,this,&AMCToothCharacter::StopSprint);
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
void AMCToothCharacter::MoveForward(const FInputActionValue& Value) { LocalPaddle.X=Value.Get<float>(); const auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement()); if (!ClingTooth && !OrderJumpTarget) AddMovementInput(Move->IsClimbing()?FVector::UpVector:CameraMoveDirection(false), LocalPaddle.X); }
void AMCToothCharacter::MoveRight(const FInputActionValue& Value) { LocalPaddle.Y=Value.Get<float>(); const auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement()); if (!ClingTooth && !OrderJumpTarget) AddMovementInput(Move->IsClimbing()?FVector::CrossProduct(FVector(Move->ClimbNormal),FVector::UpVector).GetSafeNormal():CameraMoveDirection(true), LocalPaddle.Y); }
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
    if(!CanWork() || OrderJumpTarget) return;
    auto* Move=CastChecked<UMCToothMovementComponent>(GetCharacterMovement());
    if(Move->IsClimbing()) { Jump(); return; }
    for(TActorIterator<AMCThroat> It(GetWorld());It;++It) if(It->CanOrderJump(this)) { ServerOrderJump(*It); return; }
    Jump();
}
void AMCToothCharacter::ServerOrderJump_Implementation(AMCThroat* Throat)
{
    if(IsValid(Throat)) Throat->LaunchToUvula(this);
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
void AMCToothCharacter::StopJump() { StopJumping(); }
void AMCToothCharacter::StartSprint() { CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetSprinting(true); }
void AMCToothCharacter::StopSprint() { CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetSprinting(false); }
void AMCToothCharacter::StartBrush() { ServerSetWorking(true,true); }
void AMCToothCharacter::StopBrush() { ServerSetWorking(true,false); }
void AMCToothCharacter::StartHandle() { CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(true); ServerSetWorking(false,true); }
void AMCToothCharacter::StopHandle() { CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(false); ServerSetWorking(false,false); }
void AMCToothCharacter::StartPrimary() { ServerSetPrimary(true); }
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
void AMCToothCharacter::StopPrimary() { ServerSetPrimary(false); }
void AMCToothCharacter::ServerSetPrimary_Implementation(bool bActive)
{
    if(bActive && CanWork() && !bInCoffee && !bSelfCare && Inventory->IsCleaningTool() && (FoodCollection->bCollecting || FoodCollection->HasCandidate())) {
        bPrimaryHeld=false;bBrushing=false;bHandling=false;DropFood();ResetContact();FoodCollection->Toggle();ForceNetUpdate();return;
    }
    bPrimaryHeld=bActive && Status->IsAlive();
    bWantsCling=bPrimaryHeld;
    if (!bPrimaryHeld)
    {
        ClingTooth=nullptr; bBrushing=false; bHandling=false; DropFood(); ResetContact();
    }
    else ResolvePrimaryAction();
    ForceNetUpdate();
}
void AMCToothCharacter::ResolvePrimaryAction()
{
    if(FoodCollection->bCollecting) return;
    if (!bPrimaryHeld || !CanWork() || GetWorld()->GetTimeSeconds()<NextSwingTime-.3f) return;
    if(!bInCoffee && !Inventory->IsCleaningTool()) {
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
        && (Cast<AMCArenaTooth>(CareTarget) || Cast<AMCMouthSurface>(CareTarget))) { bHandling=false; return; }
    auto* Repair=FindCareTarget(false);
    bool BrushMode=Clean && (!Repair || Distance(Clean->GetOwner())<=Distance(Repair->GetOwner()));
    float BestDistance=BrushMode?Distance(Clean->GetOwner()):Repair?Distance(Repair->GetOwner()):MAX_flt;
    AMCFoodActor* BestFood=nullptr;
    if (!bSelfCare && !bInCoffee)
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        {
            if (!It->UsesLegacyGrip() || It->IsDisposed() || It->Phase==EMCFoodPhase::Equipped || It->Phase==EMCFoodPhase::Carried || (It->bBrushTool && EquippedBrush) || !CanContact(*It)) continue;
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
    FoodCollection->Stop();
    StopSprint();
    StopPrimary(); StopBrush(); StopHandle(); StopJump(); LocalPaddle=FVector2D::ZeroVector; ServerPaddle(LocalPaddle);
    GetCharacterMovement()->StopMovementImmediately();
}
void AMCToothCharacter::TogglePanel() { StopPrimary(); StopBrush(); StopHandle(); if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->ToggleTuning(); }
void AMCToothCharacter::ToggleConnection() { StopPrimary(); StopBrush(); StopHandle(); if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->ToggleConnection(); }
void AMCToothCharacter::RestartRun() { if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->RequestRestart(); }
void AMCToothCharacter::ServerSetWorking_Implementation(bool bBrush, bool bActive)
{
    if(!bBrush) CastChecked<UMCToothMovementComponent>(GetCharacterMovement())->SetWantsClimb(bActive);
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
    if (auto* Throat=Cast<AMCThroat>(Hit.GetActor())) Throat->NotifyUvulaLanding(this,Hit,-GetVelocity().Z);
    Super::Landed(Hit); LandingImpulse = 1.f;
    if(OrderJumpTarget || !OrderJumpIgnoredActors.IsEmpty()) ClearOrderJump();
    if (SoundPalette) SoundPalette->Play(this,TEXT("Jump"),GetActorLocation());
}
void AMCToothCharacter::FindWork(float DeltaSeconds)
{
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
                if (It->UsesLegacyGrip() && !It->IsDisposed() && It->Phase!=EMCFoodPhase::Equipped && (!It->bBrushTool || !EquippedBrush) && D<Distance && D<=FMath::Square(It->Settings.GrabReach) && (!It->bBrushTool || CanContact(*It))) { Best=*It; Distance=D; }
            }
            if (Best && Best->TryGrab(this) && Best->bBrushTool) { ResetContact(); return; }
        }
        if (HeldFood || Grip->GrabbedPlayer) { ResetContact(); return; }
    }
    AdvanceCare(DeltaSeconds);
}
void AMCToothCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
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
        if (!SwallowedBy && Status->IsAlive() && GetActorLocation().Z < -300) Status->Damage(Status->State.MaxHealth);
    }
    if (IsLocallyControlled() && bInCoffee)
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
    Brush->SetVisibility(HasBrush() && Inventory->ShouldPresentTool() && !HeldFood && AnimationClimb<.05f && AnimationSwim<.05f && !OrderJumpTarget && AnimationOrderPress<.05f && AnimationOrderFlight<.05f
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
    const float AttackTime=Time-SwingStartedAt;
    AnimationToolOffset=GetActorRotation().RotateVector(UMCInventoryComponent::SwingOffset(Inventory->Selected,AttackTime));
    const float AttackAngle=UMCInventoryComponent::SwingAngle(Inventory->Selected,AttackTime);
    const float Swing = AttackTime<Inventory->SwingDuration()?AttackAngle:bVisualBrush ? -35.f + FMath::Sin(WorkTime*18.f*A.Tempo)*65.f*(1.f-Anticipation) : bHandling ? 35.f : -12.f;
    BrushAngle = FMath::FInterpTo(BrushAngle,Swing,DeltaSeconds,Inventory->Selected==EMCToolSlot::Pickaxe?25.f:18.f-12.f*A.FollowThrough);
    AnimationGait=Gait; AnimationBrushAngle=BrushAngle*A.Exaggeration;
    const float PoseEase=1-FMath::Exp(-16.f*DeltaSeconds);
    AnimationBob=FMath::Lerp(AnimationBob,Bob*(1-.85f*GripBlend),PoseEase);
    AnimationPitch=FMath::Lerp(AnimationPitch,(Pitch+(Status->IsLoose()?FMath::Sin(Time*7)*7:0))*(1-GripBlend),PoseEase);
    SoundAccumulator += DeltaSeconds;
    if (ToothPhysics->CanAct() && !Swimming && !bPreviewAnimation && SoundAccumulator > (bWork ? 0.28f : 0.34f) && SoundPalette && (bWork || (Speed > 0.2f && !bAir)))
    { SoundAccumulator = 0; SoundPalette->Play(this,bBrushing ? TEXT("Brush") : bHandling ? TEXT("Pull") : TEXT("Step"),GetActorLocation()); }
}
void AMCToothCharacter::SwingBrush() { if (!bPreviewAnimation && ToothPhysics->CanAct()) ServerSwingBrush(); }
void AMCToothCharacter::ServerSwingBrush_Implementation()
{
    const float Now=GetWorld()->GetTimeSeconds();
    if (!CanWork() || Now<NextSwingTime) return;
    if(Inventory->Selected==EMCToolSlot::Spray) { Inventory->ServerSpray(); return; }
    NextSwingTime=Now+Inventory->SwingDuration(); ++ValidatedSwingCount;
    bPrimaryHeld=false; bBrushing=false; bHandling=false; DropFood(); ForceNetUpdate(); MulticastSwing();
    ResetContact();
    GetWorldTimerManager().SetTimer(SwingTimer,this,&AMCToothCharacter::ResolveSwing,Inventory->SwingContactTime(),false);
}
void AMCToothCharacter::MulticastSwing_Implementation()
{
    SwingStartedAt=GetWorld()->GetTimeSeconds();
    if (SoundPalette) SoundPalette->Play(this,TEXT("Whoosh"),GetActorLocation());
}
void AMCToothCharacter::MulticastHitSound_Implementation(FVector Location) { if (SoundPalette) SoundPalette->Play(this,TEXT("Hit"),Location); }
void AMCToothCharacter::ResolveSwing()
{
    if (!HasAuthority() || !CanWork()) return;
    if(Inventory->Selected==EMCToolSlot::Pickaxe) {
        AMCIceBlock* BestIce=nullptr; float Distance=FMath::Square(180.f);
        for(TActorIterator<AMCIceBlock> It(GetWorld());It;++It) {
            const FVector D=It->Body->Bounds.GetBox().GetClosestPointTo(GetActorLocation())-GetActorLocation();
            if(!It->bBroken && D.SizeSquared()<Distance && FVector::DotProduct(D.GetSafeNormal2D(),GetActorForwardVector())>.25f && CanContact(*It)) { BestIce=*It; Distance=D.SizeSquared(); }
        }
        if(BestIce && BestIce->HitWithPickaxe(this,Inventory->Damage())) { ++ConfirmedHitCount; MulticastHitSound(BestIce->GetActorLocation()); if(BestIce->bBroken) NotifyTaskFeedback(true,BestIce->GetActorLocation()); return; }
    }
    AMCFoodActor* FoodTarget=nullptr; float FoodDistance=FMath::Square(180.f);
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
    {
        const FVector Offset=It->Visual->Bounds.GetBox().GetClosestPointTo(GetActorLocation())-GetActorLocation();
        if (Inventory->CanBreak(*It) && CanContact(*It) && Offset.SizeSquared()<FoodDistance && FVector::DotProduct(GetActorForwardVector(),Offset.GetSafeNormal2D())>.25f)
        { FoodTarget=*It; FoodDistance=Offset.SizeSquared(); }
    }
    if (FoodTarget)
    {
        if (FoodTarget->HitFood(Inventory->Damage(),GetActorForwardVector())) { ++ConfirmedHitCount; MulticastHitSound(FoodTarget->GetActorLocation());  }
        return;
    }
    if(!Inventory->IsCleaningTool()) return;
    AMCToothCharacter* Target=nullptr; float Best=FMath::Square(180.f);
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        if (*It==this || It->ToothPhysics->GetBodyState()==EMCBodyState::Recovering) continue;
        const FVector Point=It->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll?It->ToothPhysics->PhysicalLocation():It->GetActorLocation();
        const FVector Offset=Point-GetActorLocation();
        if (Offset.SizeSquared2D()>Best || FMath::Abs(Offset.Z)>120 || FVector::DotProduct(GetActorForwardVector(),Offset.GetSafeNormal2D())<0.25f) continue;
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCBrushHit),false,this); Params.AddIgnoredActor(*It);
        if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Point,ECC_Visibility,Params)) continue;
        Target=*It; Best=Offset.SizeSquared2D();
    }
    AMCArenaTooth* ArenaTarget=nullptr;
    for (TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
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
        { ++ConfirmedHitCount; MulticastHitSound(ArenaTarget->GetActorLocation()); }
        return;
    }
    if (!Target) return;
    ++ConfirmedHitCount;
    FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D(); if (Direction.IsNearlyZero()) Direction=GetActorForwardVector();
    Target->ToothPhysics->ApplyHit(Direction*ToothPhysics->Settings.Knockback+FVector(0,0,ToothPhysics->Settings.Lift),Target->GetActorLocation()+FVector(0,0,15));
    Target->Status->Damage(25,Direction);
    MulticastHitSound(Target->GetActorLocation());
}
void AMCToothCharacter::OnBodyHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit)
{
    if (Cast<AMCFoodActor>(OtherActor)) return;
    if (!HasAuthority() || !ToothPhysics->CanAct() || !OtherComponent || OtherActor==this || GetWorld()->GetTimeSeconds()-LastEnvironmentHit<0.6f) return;
    FVector Impact=FVector::ZeroVector;
    if (OtherComponent->IsSimulatingPhysics()) Impact=(OtherComponent->GetPhysicsLinearVelocity()-GetVelocity())*0.7f;
    else if (Hit.ImpactNormal.Z<0.4f && GetVelocity().Size()>500.f) Impact=Hit.ImpactNormal*GetVelocity().Size()*0.6f;
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
    DOREPLIFETIME(AMCToothCharacter,bSelfCare); DOREPLIFETIME(AMCToothCharacter,CareTarget); DOREPLIFETIME(AMCToothCharacter,ContactProgress);
    DOREPLIFETIME(AMCToothCharacter,HeldFood); DOREPLIFETIME(AMCToothCharacter,RespawnAt); DOREPLIFETIME(AMCToothCharacter,RespawnSourceId);
    DOREPLIFETIME(AMCToothCharacter,EquippedBrush); DOREPLIFETIME(AMCToothCharacter,bInCoffee); DOREPLIFETIME(AMCToothCharacter,ClingTooth);
    DOREPLIFETIME(AMCToothCharacter,SwimIntent);
    DOREPLIFETIME(AMCToothCharacter,SwallowedBy); DOREPLIFETIME(AMCToothCharacter,OrderJumpTarget);
    DOREPLIFETIME(AMCToothCharacter,OrderJumpStartedAt); DOREPLIFETIME(AMCToothCharacter,bOrderJumpLaunched);
    DOREPLIFETIME(AMCToothCharacter,ThroatCaptureStart);
    DOREPLIFETIME(AMCToothCharacter,TaskSuccessAt); DOREPLIFETIME(AMCToothCharacter,TaskFailureAt);
    DOREPLIFETIME(AMCToothCharacter,YawnEndsAt);DOREPLIFETIME(AMCToothCharacter,YawnTongue);DOREPLIFETIME(AMCToothCharacter,YawnAnchor);DOREPLIFETIME(AMCToothCharacter,YawnStartedAt);DOREPLIFETIME(AMCToothCharacter,YawnPullDirection);
}

bool AMCToothCharacter::CanWork() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return !IsYawning() && !SwallowedBy && Status->IsAlive() && ToothPhysics->CanAct() && (!GS || (GS->Phase!=EMCShiftPhase::Won && GS->Phase!=EMCShiftPhase::Lost));
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
    ContactElapsed+=FMath::Min(Dt,.1f);
    const float Seconds=Target->Settings.ContactSeconds;
    ContactProgress=FMath::Clamp(ContactElapsed/Seconds,0.f,1.f);
    if (ContactElapsed+KINDA_SMALL_NUMBER>=Seconds)
    {
        if (Target->CareContact(bBrushing) && bBrushing) ++SuccessfulBrushContacts;
        ContactElapsed=0; ContactProgress=0;
        if (!Target->NeedsCare(bBrushing)) { NotifyTaskFeedback(true,Target->GetOwner()->GetActorLocation()); ResetContact(); }
        ForceNetUpdate();
    }
}
void AMCToothCharacter::StatusChanged()
{
    if (!HasAuthority() || Status->IsAlive() || bDeathReported) return;
    bDeathReported=true; bBrushing=false; bHandling=false; DropFood(); ResetContact();
    if (EquippedBrush) EquippedBrush->Throw(this);
    ClingTooth=nullptr; bWantsCling=false;
    ToothPhysics->EnterDeath();
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->PlayerDied(this);
}
void AMCToothCharacter::FellOutOfWorld(const UDamageType&) { if (HasAuthority()) Status->Damage(Status->State.MaxHealth); }
void AMCToothCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearCameraWallReveal();
    if (HasAuthority() && EquippedBrush) EquippedBrush->Throw(this);
    DropFood();
    if (AppliedInputSubsystem.IsValid() && InputMap) AppliedInputSubsystem->RemoveMappingContext(InputMap);
    Super::EndPlay(Reason);
}
bool AMCToothCharacter::HasBrush() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return (!Inventory || Inventory->IsCleaningTool()) && (!GS || !GS->bPhysicalBrushes || (IsValid(EquippedBrush) && !EquippedBrush->IsDisposed()));
}
void AMCToothCharacter::ThrowItem() { ServerThrowItem(); }
void AMCToothCharacter::ServerThrowItem_Implementation()
{
    if (!CanWork()) return;
    if(FoodCollection->bCollecting || !FoodCollection->Pieces.IsEmpty()) {FoodCollection->Stop(true);return;}
    if (Grip->GrabbedPlayer) Grip->ThrowPlayer();
    else if (HeldFood)
    {
        auto* Other=Grip->Secondary.Food.Get(); HeldFood->Throw(this);
        if (IsValid(Other)) Other->Throw(this);
    }
    else if (EquippedBrush) EquippedBrush->Throw(this);
    bPrimaryHeld=false; bWantsCling=false; ClingTooth=nullptr; bHandling=false; bBrushing=false; ResetContact();
}
void AMCToothCharacter::ServerPaddle_Implementation(FVector2D Direction)
{
    if (!bInCoffee || !Status->IsAlive() || Direction.ContainsNaN()) return;
    PaddleInput=Direction.GetClampedToMaxSize(1); LastPaddleAt=GetWorld()->GetTimeSeconds();
}
