#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "MCTaskActor.h"
#include "MCGameState.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
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

AMCToothCharacter::AMCToothCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 58.f);
    GetCharacterMovement()->MaxWalkSpeed = 440.f;
    GetCharacterMovement()->MaxAcceleration = 1800.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 1600.f;
    GetCharacterMovement()->JumpZVelocity = 500.f;
    GetCharacterMovement()->GravityScale = 1.6f;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0, 650, 0);
    bUseControllerRotationYaw = false;
    BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
    BodyPivot->SetupAttachment(RootComponent); BodyPivot->SetRelativeLocation(FVector(0,0,-58));
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToothBody"));
    Body->SetupAttachment(BodyPivot); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BrushPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BrushPivot"));
    BrushPivot->SetupAttachment(BodyPivot); BrushPivot->SetRelativeLocation(FVector(25,42,38));
    Brush = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MiniBrush"));
    Brush->SetupAttachment(BrushPivot); Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("ArenaCamera"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->SetUsingAbsoluteRotation(true); CameraBoom->SetRelativeRotation(FRotator(-16,0,0));
    CameraBoom->TargetArmLength = 1350; CameraBoom->TargetOffset = FVector(120,0,130);
    CameraBoom->bUsePawnControlRotation = false; CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 5; CameraBoom->CameraLagMaxDistance = 170;
    CameraBoom->bDoCollisionTest = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(CameraBoom);
    Camera->FieldOfView = 55;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ToothAsset(TEXT("/Game/Art/Meshes/SM_ToothHero"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BrushAsset(TEXT("/Game/Art/Meshes/SM_Brush"));
    static ConstructorHelpers::FObjectFinder<UMCAnimationProfile> AnimAsset(TEXT("/Game/Data/DA_ToothAnimation"));
    static ConstructorHelpers::FObjectFinder<UMCSoundPalette> SoundAsset(TEXT("/Game/Data/DA_MouthSounds"));
    if (ToothAsset.Succeeded()) Body->SetStaticMesh(ToothAsset.Object);
    if (BrushAsset.Succeeded()) Brush->SetStaticMesh(BrushAsset.Object);
    if (AnimAsset.Succeeded()) AnimationProfile = AnimAsset.Object;
    if (SoundAsset.Succeeded()) SoundPalette = SoundAsset.Object;
}
void AMCToothCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (AnimationProfile) AnimationSettings = AnimationProfile->Settings;
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
    InputMap->MapKey(ForwardAction, EKeys::W);
    InputMap->MapKey(ForwardAction, EKeys::S).Modifiers.Add(NewObject<UInputModifierNegate>(this));
    InputMap->MapKey(ForwardAction, EKeys::Gamepad_LeftY);
    InputMap->MapKey(RightAction, EKeys::D);
    InputMap->MapKey(RightAction, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(this));
    InputMap->MapKey(RightAction, EKeys::Gamepad_LeftX);
    InputMap->MapKey(JumpAction, EKeys::SpaceBar); InputMap->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
    InputMap->MapKey(BrushAction, EKeys::LeftMouseButton); InputMap->MapKey(BrushAction, EKeys::Gamepad_RightShoulder);
    InputMap->MapKey(HandleAction, EKeys::E); InputMap->MapKey(HandleAction, EKeys::Gamepad_FaceButton_Left);
    InputMap->MapKey(PanelAction, EKeys::F1); InputMap->MapKey(ConnectionAction, EKeys::F2); InputMap->MapKey(RestartAction, EKeys::R);
}
void AMCToothCharacter::PawnClientRestart()
{
    Super::PawnClientRestart(); BuildInput();
    if (APlayerController* PC = Cast<APlayerController>(Controller))
        if (ULocalPlayer* Local = PC->GetLocalPlayer())
            if (auto* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            { Subsystem->RemoveMappingContext(InputMap); Subsystem->AddMappingContext(InputMap, 0); }
}
void AMCToothCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent); BuildInput();
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    Input->BindAction(ForwardAction, ETriggerEvent::Triggered, this, &AMCToothCharacter::MoveForward);
    Input->BindAction(RightAction, ETriggerEvent::Triggered, this, &AMCToothCharacter::MoveRight);
    Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AMCToothCharacter::StartJump);
    Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AMCToothCharacter::StopJump);
    Input->BindAction(BrushAction, ETriggerEvent::Started, this, &AMCToothCharacter::StartBrush);
    Input->BindAction(BrushAction, ETriggerEvent::Completed, this, &AMCToothCharacter::StopBrush);
    Input->BindAction(BrushAction, ETriggerEvent::Canceled, this, &AMCToothCharacter::StopBrush);
    Input->BindAction(HandleAction, ETriggerEvent::Started, this, &AMCToothCharacter::StartHandle);
    Input->BindAction(HandleAction, ETriggerEvent::Completed, this, &AMCToothCharacter::StopHandle);
    Input->BindAction(HandleAction, ETriggerEvent::Canceled, this, &AMCToothCharacter::StopHandle);
    Input->BindAction(PanelAction, ETriggerEvent::Started, this, &AMCToothCharacter::TogglePanel);
    Input->BindAction(ConnectionAction, ETriggerEvent::Started, this, &AMCToothCharacter::ToggleConnection);
    Input->BindAction(RestartAction, ETriggerEvent::Started, this, &AMCToothCharacter::RestartRun);
}
void AMCToothCharacter::MoveForward(const FInputActionValue& Value) { AddMovementInput(FVector::ForwardVector, Value.Get<float>()); }
void AMCToothCharacter::MoveRight(const FInputActionValue& Value) { AddMovementInput(FVector::RightVector, Value.Get<float>()); }
void AMCToothCharacter::StartJump() { Jump(); }
void AMCToothCharacter::StopJump() { StopJumping(); }
void AMCToothCharacter::StartBrush() { ServerSetWorking(true,true); }
void AMCToothCharacter::StopBrush() { ServerSetWorking(true,false); }
void AMCToothCharacter::StartHandle() { ServerSetWorking(false,true); }
void AMCToothCharacter::StopHandle() { ServerSetWorking(false,false); }
void AMCToothCharacter::TogglePanel() { StopBrush(); StopHandle(); if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->ToggleTuning(); }
void AMCToothCharacter::ToggleConnection() { StopBrush(); StopHandle(); if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->ToggleConnection(); }
void AMCToothCharacter::RestartRun() { if (auto* PC = Cast<AMCPlayerController>(Controller)) PC->RequestRestart(); }
void AMCToothCharacter::ServerSetWorking_Implementation(bool bBrush, bool bActive)
{
    if (bBrush) bBrushing = bActive; else bHandling = bActive;
    OnRep_Working(); ForceNetUpdate();
}
void AMCToothCharacter::OnRep_Working() { WorkStartedAt = GetWorld()->GetTimeSeconds(); }
void AMCToothCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit); LandingImpulse = 1.f;
    if (SoundPalette) SoundPalette->Play(this,TEXT("Jump"),GetActorLocation());
}
void AMCToothCharacter::FindWork(float DeltaSeconds)
{
    if (!bBrushing && !bHandling) return;
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>();
    if (!State || State->Phase != EMCShiftPhase::Working) return;
    AMCTaskActor* Best = nullptr; float BestDistance = 180.f * 180.f;
    for (TActorIterator<AMCTaskActor> It(GetWorld()); It; ++It)
    {
        const bool bNeedsBrush = It->Kind == EMCTaskKind::Coffee;
        if (It->Progress >= 1.f || (bNeedsBrush ? !bBrushing : !bHandling)) continue;
        FVector Offset = It->GetActorLocation() - GetActorLocation();
        const float Distance = Offset.SizeSquared2D();
        if (Distance > BestDistance || FMath::Abs(Offset.Z) > 140.f) continue;
        if (FVector::DotProduct(GetActorForwardVector(), Offset.GetSafeNormal2D()) < -0.25f) continue;
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCWork), false, this); Params.AddIgnoredActor(*It);
        if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), It->GetActorLocation()+FVector(0,0,45), ECC_Visibility, Params)) continue;
        Best = *It; BestDistance = Distance;
    }
    if (Best) Best->ApplyWork(this, Best->Kind == EMCTaskKind::Coffee, DeltaSeconds);
}
void AMCToothCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
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
        WorkAccumulator += DeltaSeconds;
        if (WorkAccumulator >= 0.1f) { FindWork(FMath::Min(WorkAccumulator,0.2f)); WorkAccumulator = 0; }
        if (GetActorLocation().Z < -300) { SetActorLocation(FVector(-700,0,160),false,nullptr,ETeleportType::TeleportPhysics); GetCharacterMovement()->StopMovementImmediately(); }
    }
    const auto& A = AnimationSettings;
    const float Time = GetWorld()->GetTimeSeconds();
    const float PreviewTime = FMath::Fmod(Time,8.f);
    const float Speed = bPreviewAnimation ? (PreviewTime<3.f ? 0.8f : 0.f) : FMath::Clamp(GetVelocity().Size2D()/440.f, 0.f, 1.f);
    Gait += DeltaSeconds * (4.f + 10.f*Speed) * A.Tempo;
    LandingImpulse = FMath::FInterpTo(LandingImpulse,0.f,DeltaSeconds,11.f);
    const bool bAir = GetCharacterMovement()->IsFalling() || (bPreviewAnimation && PreviewTime>6.f);
    const bool bVisualBrush = bBrushing || (bPreviewAnimation && PreviewTime>=3.f && PreviewTime<6.f);
    const bool bWork = bVisualBrush || bHandling;
    const float WorkTime = bPreviewAnimation ? FMath::Max(0.f,PreviewTime-3.f) : Time - WorkStartedAt;
    const float Anticipation = bWork ? FMath::Clamp(1.f - WorkTime/FMath::Max(0.05f,A.Anticipation),0.f,1.f) : 0.f;
    const float Squash = (FMath::Sin(Gait*2.f)*0.22f*Speed + LandingImpulse + Anticipation*0.45f) * A.Squash * A.Exaggeration;
    const float Stretch = bAir ? A.Stretch * (bPreviewAnimation ? 0.8f : FMath::Clamp(FMath::Abs(GetVelocity().Z)/500.f,0.f,1.f)) : 0.f;
    const float ZScale = FMath::Clamp(1.f-Squash+Stretch,0.55f,1.65f);
    const float XYScale = 1.f/FMath::Sqrt(ZScale);
    BodyPivot->SetRelativeScale3D(FVector(XYScale,XYScale,ZScale));
    const float Bob = bAir ? 0.f : FMath::Abs(FMath::Sin(Gait))*A.Bob*Speed + FMath::Sin(Time*2.f)*0.7f;
    BodyPivot->SetRelativeLocation(FVector(0,0,-58+Bob));
    const float Pitch = Speed*A.Lean + Anticipation*15.f + (bHandling ? FMath::Sin(Time*13.f)*7.f : 0.f);
    BodyPivot->SetRelativeRotation(FRotator(Pitch,0,FMath::Sin(Gait)*Speed*A.Lean*0.35f));
    const float Swing = bVisualBrush ? -35.f + FMath::Sin(WorkTime*18.f*A.Tempo)*65.f*(1.f-Anticipation) : bHandling ? 35.f : -12.f;
    BrushAngle = FMath::FInterpTo(BrushAngle,Swing,DeltaSeconds,18.f-12.f*A.FollowThrough);
    BrushPivot->SetRelativeRotation(FRotator(BrushAngle*A.Exaggeration,15.f*FMath::Sin(Gait)*Speed,8.f));
    SoundAccumulator += DeltaSeconds;
    if (!bPreviewAnimation && SoundAccumulator > (bWork ? 0.28f : 0.34f) && SoundPalette && (bWork || (Speed > 0.2f && !bAir)))
    { SoundAccumulator = 0; SoundPalette->Play(this,bBrushing ? TEXT("Brush") : bHandling ? TEXT("Pull") : TEXT("Step"),GetActorLocation()); }
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
}
