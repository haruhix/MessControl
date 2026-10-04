#include "MCBossIntro.h"
#include "MCBossCharacter.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "DefaultLevelSequenceInstanceData.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "TimerManager.h"

AMCBossIntro::AMCBossIntro()
{
    PrimaryActorTick.bCanEverTick=false;
    bReplicates=false;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
    IntroFill=CreateDefaultSubobject<UPointLightComponent>(TEXT("IntroFill"));
    IntroFill->SetupAttachment(RootComponent);
    IntroFill->SetRelativeLocation(FVector(280,-200,210));
    IntroFill->SetIntensityUnits(ELightUnits::Lumens);
    IntroFill->SetIntensity(4500.f);
    IntroFill->SetAttenuationRadius(700.f);
    IntroFill->SetLightColor(FLinearColor(.87f,.94f,1.f));
    IntroFill->SetCastShadows(false);
    IntroFill->SetVolumetricScatteringIntensity(0.f);
    IntroFill->SetVisibility(false);
}

bool AMCBossIntro::PlayIntro(AMCPlayerController* Player,AMCBossCharacter* Boss)
{
    if (!Player || !Player->IsLocalController() || !IsValid(Boss) || !Boss->IsBossAlive()) return false;
    auto* Sequence=LoadObject<ULevelSequence>(nullptr,TEXT("/Game/Gameplay/Boss/Sequences/LS_ZombieIntro.LS_ZombieIntro"));
    if (!Sequence || !Sequence->GetMovieScene()) return false;
    FGuid CameraGuid;
    for (int32 Index=0;Index<Sequence->GetMovieScene()->GetPossessableCount();++Index)
    {
        const FMovieScenePossessable& Binding=Sequence->GetMovieScene()->GetPossessable(Index);
        if (Binding.GetName()==TEXT("MC_IntroCamera")) { CameraGuid=Binding.GetGuid(); break; }
    }
    if (!CameraGuid.IsValid()) return false;
    Controller=Player; Subject=Boss; OriginalPawn=Player->GetPawn();
    PreviousViewTarget=Player->GetViewTarget(); bPreviousAutoCamera=Player->bAutoManageActiveCameraTarget;
    const FVector Feet=Boss->GetActorLocation()-FVector(0,0,Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    SetActorLocationAndRotation(Feet,FRotator(0,Boss->GetActorRotation().Yaw,0));
    FActorSpawnParameters Spawn; Spawn.Owner=Player;
    Camera=GetWorld()->SpawnActor<ACineCameraActor>(GetActorLocation(),GetActorRotation(),Spawn);
    if (!Camera) return false;
    auto* Lens=Camera->GetCineCameraComponent();
    FCameraFilmbackSettings Filmback;
    Filmback.SensorWidth=36.f; Filmback.SensorHeight=20.25f;
    Lens->SetFilmback(Filmback);
    Lens->SetCurrentFocalLength(28.f); Lens->SetCurrentAperture(5.6f);
    Lens->PostProcessSettings.bOverride_AutoExposureBias=true;
    Lens->PostProcessSettings.AutoExposureBias=1.f;
    FCameraFocusSettings Focus; Focus.FocusMethod=ECameraFocusMethod::Tracking;
    Focus.TrackingFocusSettings.ActorToTrack=Boss;
    Focus.TrackingFocusSettings.RelativeOffset=FVector(0,0,65);
    Lens->FocusSettings=Focus;
    FMovieSceneSequencePlaybackSettings Settings;
    Settings.bAutoPlay=false; Settings.bHideHud=true;
    Settings.FinishCompletionStateOverride=EMovieSceneCompletionModeOverride::ForceRestoreState;
    ALevelSequenceActor* CreatedSequenceActor=nullptr;
    SequencePlayer=ULevelSequencePlayer::CreateLevelSequencePlayer(this,Sequence,Settings,CreatedSequenceActor);
    SequenceActor=CreatedSequenceActor;
    if (!SequencePlayer || !SequenceActor) return false;
    SequenceActor->SetBinding(UE::MovieScene::FRelativeObjectBindingID(CameraGuid),{Camera},false);
    auto* Origin=NewObject<UDefaultLevelSequenceInstanceData>(SequenceActor);
    Origin->TransformOriginActor=this;
    SequenceActor->DefaultInstanceData=Origin; SequenceActor->bOverrideInstanceData=true;
    SequencePlayer->OnFinished.AddDynamic(this,&AMCBossIntro::FinishIntro);
    SequencePlayer->OnStop.AddDynamic(this,&AMCBossIntro::FinishIntro);
    Player->bAutoManageActiveCameraTarget=false;
    Player->BeginBossIntroPresentation(); bPresentationStarted=true;
    IntroFill->SetVisibility(true);
    StartedAt=GetWorld()->GetTimeSeconds();
    GetWorldTimerManager().SetTimer(Watchdog,this,&AMCBossIntro::CheckIntro,.1f,true);
    SequencePlayer->Play();
    UE_LOG(LogTemp,Display,TEXT("MC_BOSS_INTRO_BEGIN boss=%s sequence=%s"),*Boss->GetName(),*Sequence->GetName());
    return true;
}

void AMCBossIntro::CheckIntro()
{
    auto* Player=Controller.Get(); auto* Boss=Subject.Get();
    const auto* Hero=Player?Cast<AMCToothCharacter>(Player->GetPawn()):nullptr;
    if (!Player || !Boss || !Boss->IsBossAlive() || Player->GetPawn()!=OriginalPawn.Get()
        || !Hero || !Hero->Status || !Hero->Status->IsAlive()
        || (GetWorld()->GetTimeSeconds()-StartedAt>1. && Boss->Runtime.AnimationPreview!=EMCBossAnimationPreview::Roar)
        || GetWorld()->GetTimeSeconds()-StartedAt>5.6)
        FinishIntro();
}

void AMCBossIntro::CancelIntro() { FinishIntro(); }
void AMCBossIntro::FinishIntro()
{
    if (bFinishing) return;
    bFinishing=true;
    IntroFill->SetVisibility(false);
    GetWorldTimerManager().ClearTimer(Watchdog);
    if (SequencePlayer)
    {
        SequencePlayer->OnFinished.RemoveDynamic(this,&AMCBossIntro::FinishIntro);
        SequencePlayer->OnStop.RemoveDynamic(this,&AMCBossIntro::FinishIntro);
        SequencePlayer->Stop();
    }
    if (auto* Player=Controller.Get(); Player && bPresentationStarted)
    {
        Player->bAutoManageActiveCameraTarget=bPreviousAutoCamera;
        AActor* Target=Player->GetPawn()==OriginalPawn.Get()?PreviousViewTarget.Get():Player->GetPawn();
        if (!IsValid(Target)) Target=Player->GetPawn();
        if (IsValid(Target)) Player->SetViewTargetWithBlend(Target,.2f);
        Player->FinishBossIntroPresentation();
        bPresentationStarted=false;
    }
    if (IsValid(Camera)) Camera->Destroy();
    if (IsValid(SequenceActor)) SequenceActor->Destroy();
    Camera=nullptr; SequenceActor=nullptr; SequencePlayer=nullptr;
    UE_LOG(LogTemp,Display,TEXT("MC_BOSS_INTRO_END restored_input=1"));
    if (!IsActorBeingDestroyed()) Destroy();
}

void AMCBossIntro::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FinishIntro();
    Super::EndPlay(EndPlayReason);
}
