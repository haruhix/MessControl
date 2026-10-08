#include "MCPlaytestSession.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCProgressionComponent.h"
#include "MCSingleDayDirector.h"
#include "MCPlaytestBotController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "String/LexFromString.h"

#if !UE_BUILD_SHIPPING
void MCTickPlaytestBotsSmoke(UWorld* World);
#endif

namespace
{
    void PlaytestMessage(const FString& Message, bool bError=false)
    {
        UE_LOG(LogTemp, Display, TEXT("MC_BOTS %s"), *Message);
        if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, bError?FColor::Yellow:FColor::Cyan, Message);
    }

    FString CsvField(FString Value)
    {
        Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
        Value.ReplaceInline(TEXT("\r"), TEXT(" "));
        Value.ReplaceInline(TEXT("\n"), TEXT(" "));
        return TEXT("\"")+Value+TEXT("\"");
    }

#if !UE_BUILD_SHIPPING
    FAutoConsoleCommandWithWorldAndArgs StartBotsCommand(TEXT("MC.Bots"),
        TEXT("Start a fresh diagnostic shift: MC.Bots <1..4> <novice|regular|skilled> <coop|observe> [seed]. Team cap includes human players. Uncalibrated prototype, physics is not deterministic."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
        {
            int32 Count=0, Seed=0;
            if (Args.Num()<3 || Args.Num()>4 || !LexTryParseString(Count, *Args[0])
                || (Args.Num()==4 && !LexTryParseString(Seed, *Args[3])))
            { PlaytestMessage(TEXT("MC.Bots <count> <novice|regular|skilled> <coop|observe> [seed]; starts a new shift."), true); return; }
            EMCPlaytestBotSkill Skill=EMCPlaytestBotSkill::Regular;
            if (Args[1].Equals(TEXT("novice"), ESearchCase::IgnoreCase)) Skill=EMCPlaytestBotSkill::Novice;
            else if (Args[1].Equals(TEXT("skilled"), ESearchCase::IgnoreCase)) Skill=EMCPlaytestBotSkill::Skilled;
            else if (!Args[1].Equals(TEXT("regular"), ESearchCase::IgnoreCase))
            { PlaytestMessage(TEXT("Skill must be novice, regular or skilled."), true); return; }
            const bool bObserve=Args[2].Equals(TEXT("observe"), ESearchCase::IgnoreCase);
            if (!bObserve && !Args[2].Equals(TEXT("coop"), ESearchCase::IgnoreCase))
            { PlaytestMessage(TEXT("Mode must be coop or observe."), true); return; }
            if (!World || !World->IsGameWorld() || !World->GetAuthGameMode<AMCGameMode>())
            { PlaytestMessage(TEXT("Run this command in the host's active game / PIE, with MCGameMode."), true); return; }
            AMCPlaytestSession* Session=AMCPlaytestSession::Find(World);
            if (Session && Session->IsActive())
            { PlaytestMessage(TEXT("A playtest is already active. Use MC.Bots.Stop before starting another."), true); return; }
            if (Args.Num()==3)
                Seed=World->GetGameState<AMCGameState>()?World->GetGameState<AMCGameState>()->RunSeed:FMath::Rand();
            if (!Session) Session=World->SpawnActor<AMCPlaytestSession>();
            FString Error;
            if (!Session || !Session->StartSession(Count, Skill, bObserve, Seed, Error))
            { PlaytestMessage(Error.IsEmpty()?TEXT("Could not create playtest session."):Error, true); if (Session && !Session->IsActive()) Session->Destroy(); }
        }));

    FAutoConsoleCommandWithWorldAndArgs StopBotsCommand(TEXT("MC.Bots.Stop"),
        TEXT("Save the diagnostic report, remove bots, restore human players and start a clean normal shift."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            if (AMCPlaytestSession* Session=AMCPlaytestSession::Find(World)) Session->StopSession();
            else PlaytestMessage(TEXT("No active playtest session."));
        }));

    FAutoConsoleCommandWithWorldAndArgs ReportBotsCommand(TEXT("MC.Bots.Report"),
        TEXT("Save and display the current playtest metrics and CSV path."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
        {
            if (AMCPlaytestSession* Session=AMCPlaytestSession::Find(World)) Session->PrintReport();
            else PlaytestMessage(TEXT("No active playtest session."));
        }));
#endif
}

AMCPlaytestSession::AMCPlaytestSession()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.25f;
    SetActorTickEnabled(false);
}

AMCPlaytestSession* AMCPlaytestSession::Find(UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<AMCPlaytestSession> It(World); It; ++It)
        if (!It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}

bool AMCPlaytestSession::IsObserver(const AController* Controller) const
{
    return Observers.ContainsByPredicate([Controller](const FMCPlaytestObserver& Observer) { return Observer.Controller.Get()==Controller; });
}

bool AMCPlaytestSession::StartSession(int32 Count, EMCPlaytestBotSkill Skill, bool bObserve, int32 Seed, FString& Error)
{
    Error.Empty();
#if UE_BUILD_SHIPPING
    Error=TEXT("Diagnostic bots are disabled in shipping builds.");
    return false;
#else
    AMCGameMode* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AMCGameMode>():nullptr;
    AMCGameState* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if (!HasAuthority() || !Mode || !State || !GetWorld()->IsGameWorld())
    { Error=TEXT("Playtests require an authoritative MCGameMode game world."); return false; }
    if (bActive || (Find(GetWorld()) && Find(GetWorld())!=this && Find(GetWorld())->IsActive()))
    { Error=TEXT("A playtest is already active. Use MC.Bots.Stop first."); return false; }
    if ((State->bTutorialActive && !Mode->bUseSingleDayLoop) || State->bLobbyWaiting || State->bDevManualEvents)
    { Error=TEXT("Leave the lobby/manual event mode before starting a playtest; legacy training must be finished first."); return false; }
    if (static_cast<uint8>(Skill)>static_cast<uint8>(EMCPlaytestBotSkill::Skilled))
    { Error=TEXT("Invalid bot skill."); return false; }
    InitialHumans=0;
    for (auto It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (AMCGameMode::IsGameplayParticipant(It->Get())) ++InitialHumans;
    const int32 Cap=FMath::Clamp(State->RunSettings.MaxPlayers, 1, 4);
    if (Count<1 || Count>Cap)
    { Error=FString::Printf(TEXT("Bot count must be between 1 and %d."), Cap); return false; }
    if (Count+(bObserve?0:InitialHumans)>Cap)
    { Error=FString::Printf(TEXT("Requested team exceeds the %d-player limit (humans=%d, bots=%d)."), Cap, bObserve?0:InitialHumans, Count); return false; }
    bool bHasStart=false;
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) { bHasStart=true; break; }
    if (!bHasStart || !Mode->DefaultPawnClass || !Mode->DefaultPawnClass->IsChildOf(AMCToothCharacter::StaticClass())
        || !Mode->PlayerStateClass || !Mode->PlayerStateClass->IsChildOf(AMCPlayerState::StaticClass()))
    { Error=TEXT("Playtests need a PlayerStart and the game's tooth pawn / player state classes."); return false; }

    // Restart before adding participants: no old objectives, reward ownership or pending deaths survive.
    const bool bSkippedTraining=State->bTutorialActive && Mode->bUseSingleDayLoop;
    Mode->RestartShiftForPlaytest(Seed);
    DecisionSeed=Seed;
    BotSkill=Skill;
    bObservation=bObserve;
    InitialTeamSize=Count+(bObserve?0:InitialHumans);
    Bots.Reset(); BotDeaths.Reset(); Observers.Reset(); PendingRows.Reset();
    TerminalOutcome.Empty(); bLoggedWriteFailure=false;
    if (bObserve)
    {
        for (auto It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC=It->Get();
            if (!PC || !AMCGameMode::IsGameplayParticipant(PC)) continue;
            APlayerState* Identity=PC->GetPlayerState<APlayerState>();
            FMCPlaytestObserver Observer;
            Observer.Controller=PC; Observer.PreviousState=PC->GetStateName();
            Observer.bWasSpectator=Identity->IsSpectator(); Observer.bWasOnlySpectator=Identity->IsOnlyASpectator();
            Observers.Add(Observer);
            if (APawn* Body=PC->GetPawn())
            {
                PC->SetInitialLocationAndRotation(Body->GetActorLocation()+FVector(0,0,350), PC->GetControlRotation());
                if (AMCToothCharacter* Hero=Cast<AMCToothCharacter>(Body)) { Hero->CancelGameplayInput(); Hero->DropFood(); }
                PC->UnPossess(); Body->Destroy(); // No PlayerDied: observing does not consume reserve teeth.
            }
            PC->StartSpectatingOnly();
            PC->ClientGotoState(NAME_Spectating);
            PC->ResetIgnoreMoveInput(); PC->ResetIgnoreLookInput();
            if (ASpectatorPawn* Spectator=PC->GetSpectatorPawn()) Spectator->SetActorEnableCollision(false);
            PC->ForceNetUpdate(); Identity->ForceNetUpdate();
        }
    }
    for (int32 Index=0; Index<Count; ++Index)
        if (!SpawnBot(Mode, Index, Error))
        { CleanupParticipants(true); Mode->RestartShift(); return false; }

    const FString Name=FString::Printf(TEXT("Bots_%s_%s.csv"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    ReportPath=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Playtests"), Name));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
    PendingRows.Add(TEXT("schema_version,engine_version,build_version,map,mode,run_seed,decision_seed,skill,initial_humans,initial_team,participants,elapsed_seconds,day,step,phase,tasks_total,tasks_left,failed_events,mouth_health,reserve_teeth,bot_id,bot_name,bot_alive,bot_points,bot_deaths,idle_seconds,work_seconds,path_failures,activity,outcome,bot_x,bot_y,bot_z,bot_speed,brush_contacts,confirmed_hits,contact_progress,tutorial_active,single_day_stage,team_level,total_experience,pending_bot_choices"));
    StartedAt=State->GetServerWorldTimeSeconds();
    NextSnapshotAt=StartedAt+1.; NextFlushAt=StartedAt+5.;
    bActive=true;
    SetActorTickEnabled(true);
    Mode->BeginPlaytestSequence();
    UE_LOG(LogTemp,Display,TEXT("MC_BOTS_SETUP single_day=%d skipped_training=%d participants=%d tutorial=%d"),
        State->bSingleDayLoop?1:0,bSkippedTraining?1:0,Mode->GetGameplayParticipantCount(),State->bTutorialActive?1:0);
    WriteSnapshot(TEXT("started")); FlushReport();
    PlaytestMessage(FString::Printf(TEXT("Diagnostic prototype (uncalibrated): %d %s bots, %s, seed=%d. New shift. Physics is not deterministic. CSV: %s"), Count, MCPlaytestSkillName(Skill), bObserve?TEXT("observe"):TEXT("coop"), Seed, *ReportPath));
    return true;
#endif
}

bool AMCPlaytestSession::SpawnBot(AMCGameMode* Mode, int32 Index, FString& Error)
{
    FActorSpawnParameters ControllerSpawn;
    ControllerSpawn.ObjectFlags|=RF_Transient;
    AMCPlaytestBotController* Bot=GetWorld()->SpawnActor<AMCPlaytestBotController>(AMCPlaytestBotController::StaticClass(), FTransform::Identity, ControllerSpawn);
    if (!Bot) { Error=TEXT("Could not spawn bot controller."); return false; }
    AMCPlayerState* Identity=Bot->GetPlayerState<AMCPlayerState>();
    if (!Identity) { Bot->Destroy(); Error=TEXT("Bot has no game player state."); return false; }
    Bot->Configure(BotSkill, Index, DecisionSeed);
    Identity->SetIsABot(true);
    Identity->SetPlayerName(FString::Printf(TEXT("Bot %d (%s)"), Index+1, MCPlaytestSkillName(BotSkill)));
    Identity->SetPlayerId(10000+Index);
    static const FLinearColor Colors[]={FLinearColor(.24f,.65f,1),FLinearColor(1,.35f,.25f),FLinearColor(.4f,.9f,.3f),FLinearColor(.8f,.4f,1)};
    Identity->PlayerColor=Colors[(Index+(bObservation?0:InitialHumans))%UE_ARRAY_COUNT(Colors)];
    TArray<APlayerStart*> Starts;
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) Starts.Add(*It);
    Starts.Sort([](const APlayerStart& A, const APlayerStart& B) { return A.GetName()<B.GetName(); });
    if (APlayerStart* Preferred=Cast<APlayerStart>(Mode->FindPlayerStart(Bot)))
    { Starts.Remove(Preferred); Starts.Insert(Preferred,0); }
    FActorSpawnParameters PawnSpawn;
    PawnSpawn.Owner=Bot;
    PawnSpawn.ObjectFlags|=RF_Transient;
    PawnSpawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    AMCToothCharacter* Hero=nullptr;
    for (APlayerStart* Start:Starts)
    {
        for (int32 Attempt=0; Attempt<9 && !Hero; ++Attempt)
        {
            FVector Location=Start->GetActorLocation();
            if (Attempt>0)
            {
                const float Angle=(Attempt-1)*PI/4.f;
                Location+=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*180.f;
                // Offset candidates must sit on the actual arena floor.
                FHitResult Floor;
                FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPlaytestSpawn), false);
                for (TActorIterator<AMCToothCharacter> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
                if (!GetWorld()->LineTraceSingleByChannel(Floor,Location+FVector(0,0,100),Location-FVector(0,0,300),ECC_Visibility,Query) || Floor.ImpactNormal.Z<.4f) continue;
                const AMCToothCharacter* Defaults=Mode->DefaultPawnClass->GetDefaultObject<AMCToothCharacter>();
                Location=Floor.ImpactPoint+FVector(0,0,Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4);
            }
            Hero=GetWorld()->SpawnActor<AMCToothCharacter>(Mode->DefaultPawnClass, Location, FRotator(0,Start->GetActorRotation().Yaw,0), PawnSpawn);
        }
        if (Hero) break;
    }
    if (!Hero)
    { Bot->Destroy(); Error=TEXT("No collision-safe space at player starts. Move/add PlayerStarts and retry."); return false; }
    Bot->Possess(Hero);
    Hero->ApplyPlayerColor(Identity->PlayerColor);
    Identity->ForceNetUpdate();
    Bots.Add(Bot); BotDeaths.Add(0);
    return true;
}

void AMCPlaytestSession::RecordDeath(AMCToothCharacter* Hero)
{
    if (!bActive || !Hero) return;
    for (int32 Index=0; Index<Bots.Num(); ++Index)
        if (Bots[Index].Get()==Hero->GetController()) { ++BotDeaths[Index]; return; }
}

FString AMCPlaytestSession::CurrentOutcome() const
{
    const AMCGameState* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if (!State) return TEXT("world_unavailable");
    if (State->Phase==EMCShiftPhase::Lost) return TEXT("lost");
    if (State->Phase==EMCShiftPhase::Won) return TEXT("won");
    if (State->bDayOneComplete) return TEXT("day_one_complete");
    return TEXT("running");
}

void AMCPlaytestSession::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bActive || !HasAuthority()) return;
#if !UE_BUILD_SHIPPING
    MCTickPlaytestBotsSmoke(GetWorld());
#endif
    const AMCGameState* State=GetWorld()->GetGameState<AMCGameState>();
    if (!State) return;
    const double Now=State->GetServerWorldTimeSeconds();
    const FString Outcome=CurrentOutcome();
    if (TerminalOutcome.IsEmpty() && Outcome!=TEXT("running"))
    {
        TerminalOutcome=Outcome;
        WriteSnapshot(Outcome); FlushReport(); PrintReport();
        SetActorTickEnabled(false);
        return;
    }
    if (Now>=NextSnapshotAt) { NextSnapshotAt=Now+1.; WriteSnapshot(Outcome); }
    if (Now>=NextFlushAt) { NextFlushAt=Now+5.; FlushReport(); }
}

void AMCPlaytestSession::WriteSnapshot(const FString& Outcome)
{
    const AMCGameState* State=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    const AMCGameMode* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AMCGameMode>():nullptr;
    if (!State || !Mode || ReportPath.IsEmpty()) return;
    const FString Prefix=FString::Printf(TEXT("2,%s,%s,%s,%s,%d,%d,%s,%d,%d,%d,%.3f,%d,%d,%d,%d,%d,%d,%.3f,%d"),
        *CsvField(FEngineVersion::Current().ToString()), *CsvField(FApp::GetBuildVersion()), *CsvField(GetWorld()->GetMapName()),
        bObservation?TEXT("observe"):TEXT("coop"), State->RunSeed, DecisionSeed, MCPlaytestSkillName(BotSkill),
        InitialHumans, InitialTeamSize, Mode->GetGameplayParticipantCount(), FMath::Max(0.,State->GetServerWorldTimeSeconds()-StartedAt),
        State->Day, State->StepIndex, static_cast<int32>(State->Phase), State->TasksTotal, State->TasksLeft, State->FailedEvents,
        State->MouthHealth, State->AvailableArenaTeeth());
    for (int32 Index=0; Index<Bots.Num(); ++Index)
    {
        const AMCPlaytestBotController* Bot=Bots[Index].Get();
        const AMCToothCharacter* Hero=Bot?Cast<AMCToothCharacter>(Bot->GetPawn()):nullptr;
        const AMCPlayerState* Identity=Bot?Bot->GetPlayerState<AMCPlayerState>():nullptr;
        const FVector Position=Hero?Hero->GetActorLocation():FVector::ZeroVector;
        // These contact counters belong to the current pawn life; bot_deaths identifies respawn resets.
        PendingRows.Add(Prefix+FString::Printf(TEXT(",%d,%s,%d,%d,%d,%.3f,%.3f,%d,%s,%s,%.3f,%.3f,%.3f,%.3f,%d,%d,%.3f,%d,%d,%d,%lld,%d"), Index+1,
            *CsvField(Identity?Identity->GetPlayerName():TEXT("missing")), Hero && Hero->Status && Hero->Status->IsAlive()?1:0,
            Identity?Identity->Points:0, BotDeaths[Index], Bot?Bot->GetIdleSeconds():0.f, Bot?Bot->GetWorkSeconds():0.f,
            Bot?Bot->GetPathFailureCount():0, *CsvField(Bot?Bot->GetActivity():TEXT("controller_missing")), *CsvField(Outcome),
            Position.X, Position.Y, Position.Z, Hero?Hero->GetVelocity().Size():0.f,
            Hero?Hero->SuccessfulBrushContacts:0, Hero?Hero->ConfirmedHitCount:0, Hero?Hero->ContactProgress:0.f,
            State->bTutorialActive?1:0, State->SingleDayDirector?static_cast<int32>(State->SingleDayDirector->Stage):INDEX_NONE,
            State->Progression?State->Progression->TeamLevel:1, State->Progression?State->Progression->TotalExperience:int64(0),
            Identity?Identity->PendingLevelChoices:0));
    }
}

void AMCPlaytestSession::FlushReport()
{
    if (PendingRows.IsEmpty() || ReportPath.IsEmpty()) return;
    if (FFileHelper::SaveStringArrayToFile(PendingRows, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(), FILEWRITE_Append))
    { PendingRows.Reset(); bLoggedWriteFailure=false; }
    else if (!bLoggedWriteFailure)
    { bLoggedWriteFailure=true; PlaytestMessage(FString::Printf(TEXT("Could not save playtest CSV: %s"), *ReportPath), true); }
}

void AMCPlaytestSession::PrintReport()
{
    if (!bActive) return;
    const AMCGameState* State=GetWorld()->GetGameState<AMCGameState>();
    WriteSnapshot(TerminalOutcome.IsEmpty()?TEXT("snapshot"):TerminalOutcome); FlushReport();
    if (!State) return;
    PlaytestMessage(FString::Printf(TEXT("Uncalibrated diagnostic: %s, %.1fs, day=%d step=%d tutorial=%d single_day_stage=%d team_level=%d tasks=%d/%d health=%.1f reserves=%d. CSV: %s"),
        *CurrentOutcome(), State->GetServerWorldTimeSeconds()-StartedAt, State->Day, State->StepIndex, State->bTutorialActive?1:0,
        State->SingleDayDirector?static_cast<int32>(State->SingleDayDirector->Stage):INDEX_NONE,
        State->Progression?State->Progression->TeamLevel:1, State->TasksLeft,
        State->TasksTotal, State->MouthHealth, State->AvailableArenaTeeth(), *ReportPath));
    for (int32 Index=0; Index<Bots.Num(); ++Index)
        if (AMCPlaytestBotController* Bot=Bots[Index].Get())
            UE_LOG(LogTemp, Display, TEXT("MC_BOTS bot=%d deaths=%d idle=%.1fs work=%.1fs path_failures=%d activity=%s"),
                Index+1, BotDeaths[Index], Bot->GetIdleSeconds(), Bot->GetWorkSeconds(), Bot->GetPathFailureCount(), *Bot->GetActivity());
}

void AMCPlaytestSession::CleanupParticipants(bool bRestoreObservers)
{
    for (const TWeakObjectPtr<AMCPlaytestBotController>& WeakBot:Bots)
        if (AMCPlaytestBotController* Bot=WeakBot.Get())
        {
            if (APawn* Body=Bot->GetPawn())
            {
                if (AMCToothCharacter* Hero=Cast<AMCToothCharacter>(Body)) { Hero->CancelGameplayInput(); Hero->DropFood(); }
                Bot->UnPossess(); Body->Destroy();
            }
            Bot->Destroy(); // Controller cleanup removes its ordinary replicated PlayerState.
        }
    Bots.Reset(); BotDeaths.Reset();
    if (bRestoreObservers)
        for (const FMCPlaytestObserver& Observer:Observers)
            if (APlayerController* PC=Observer.Controller.Get())
            {
                if (APlayerState* Identity=PC->GetPlayerState<APlayerState>())
                { Identity->SetIsOnlyASpectator(Observer.bWasOnlySpectator); Identity->SetIsSpectator(Observer.bWasSpectator); Identity->ForceNetUpdate(); }
                PC->ChangeState(Observer.PreviousState);
                PC->ClientGotoState(Observer.PreviousState);
                PC->ForceNetUpdate();
            }
    Observers.Reset();
}

void AMCPlaytestSession::StopSession()
{
    if (!HasAuthority()) return;
    if (bActive)
    {
        WriteSnapshot(TerminalOutcome.IsEmpty()?TEXT("stopped"):TerminalOutcome); FlushReport();
        bActive=false; SetActorTickEnabled(false);
        CleanupParticipants(true);
        if (AMCGameMode* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->RestartShift();
        PlaytestMessage(FString::Printf(TEXT("Playtest stopped; humans restored and a clean normal shift started. CSV: %s"), *ReportPath));
    }
    Destroy();
}

void AMCPlaytestSession::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bActive)
    {
        WriteSnapshot(TerminalOutcome.IsEmpty()?TEXT("world_ended"):TerminalOutcome); FlushReport();
        bActive=false;
        // World teardown must never create fresh human bodies or start another shift.
        CleanupParticipants(EndPlayReason==EEndPlayReason::Destroyed);
    }
    Super::EndPlay(EndPlayReason);
}
