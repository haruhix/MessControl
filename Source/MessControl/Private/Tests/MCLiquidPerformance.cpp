#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MCMouthSurface.h"
#include "MCGameState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DynamicRHI.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Opt-in standalone diagnostic: identical camera/gameplay, on/off/on visual passes.
// Wall-clock samples include the complete frame, including render/present stalls.
void MCTickLiquidPerformance(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        double Start=-1,Last=0;
        int32 Phase=-1;
        TArray<float> Frames[3],GPU[3];
        FString CSV=TEXT("phase,frame_ms,gpu_ms\n");
    };
    static FRun Run;
    if (Run.World.Get()!=World) { Run=FRun(); Run.World=World; }
    const double Now=FPlatformTime::Seconds();
    // Automatic tongue jolts would change the workload between A/B windows.
    if (auto* GS=World->GetGameState<AMCGameState>())
    {
        GS->bDevManualEvents=true;
        if (GS->Phase==EMCShiftPhase::Working) GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    }
    if (Run.Start<0)
    {
        if (World->GetTimeSeconds()<10) return;
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        int32 Count=0;
        for (TActorIterator<AMCMouthSurface> It(World);It;++It) if (!It->bUlcer && !It->IsClean()) ++Count;
        if (!Count) return;
        Run.Start=Now; Run.Last=Now;
        UE_LOG(LogTemp,Display,TEXT("MC_LIQUID_PERF_BEGIN patches=%d"),Count);
    }
    const double Elapsed=Now-Run.Start;
    const int32 Phase=FMath::Min(3,int32(Elapsed/8));
    auto* Toggle=IConsoleManager::Get().FindConsoleVariable(TEXT("mc.LiquidRender"));
    if (Phase!=Run.Phase)
    {
        Run.Phase=Phase;
        Toggle->Set(Phase==1?0:1,ECVF_SetByCode);
        UE_LOG(LogTemp,Display,TEXT("MC_LIQUID_PERF_PHASE %d visuals=%d"),Phase,Phase==1?0:1);
    }
    if (Phase<3 && Elapsed-Phase*8>=2)
    {
        const float FrameMs=float((Now-Run.Last)*1000);
        const float GPUMs=float(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
        Run.Frames[Phase].Add(FrameMs); Run.GPU[Phase].Add(GPUMs);
        Run.CSV+=FString::Printf(TEXT("%d,%.4f,%.4f\n"),Phase,FrameMs,GPUMs);
    }
    Run.Last=Now;
    if (Phase==3)
    {
        for (int32 I=0;I<3;++I)
        {
            auto& F=Run.Frames[I]; F.Sort();
            float Sum=0,GPUSum=0; for (float V:F) Sum+=V; for (float V:Run.GPU[I]) GPUSum+=V;
            if (F.Num()) UE_LOG(LogTemp,Display,TEXT("MC_LIQUID_PERF phase=%d enabled=%d frames=%d mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f gpu_ms=%.3f"),
                I,I==1?0:1,F.Num(),Sum/F.Num(),F[F.Num()/2],F[FMath::Min(F.Num()-1,FMath::FloorToInt(F.Num()*.95f))],GPUSum/F.Num());
        }
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("Profiling");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FFileHelper::SaveStringToFile(Run.CSV,*(Folder/TEXT("LiquidAB.csv")));
        FPlatformMisc::RequestExitWithStatus(false,0);
    }
}
#endif
