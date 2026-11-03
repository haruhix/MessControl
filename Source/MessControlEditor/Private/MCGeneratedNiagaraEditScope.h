#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "UObject/UObjectIterator.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

/** Read-only identity/size proof, never executes or cancels a history record. */
struct FMCNiagaraUndoHistorySnapshot
{
    UTransactor* Transactor = nullptr;
    int32 QueueLength = 0;
    int32 UndoCount = 0;
    uint64 Bytes = 0;
    TArray<FGuid> TransactionIDs;

    static FMCNiagaraUndoHistorySnapshot Capture()
    {
        check(IsInGameThread());
        FMCNiagaraUndoHistorySnapshot Result;
        Result.Transactor = GEditor ? GEditor->Trans.Get() : nullptr;
        if (Result.Transactor)
        {
            Result.QueueLength = Result.Transactor->GetQueueLength();
            Result.UndoCount = Result.Transactor->GetUndoCount();
            Result.Bytes = Result.Transactor->GetUndoSize();
            for (int32 I = 0; I < Result.QueueLength; ++I)
            {
                const FTransaction* Transaction = Result.Transactor->GetTransaction(I);
                Result.TransactionIDs.Add(Transaction ? Transaction->GetId() : FGuid());
            }
        }
        return Result;
    }

    bool IsIdenticalTo(const FMCNiagaraUndoHistorySnapshot& Other) const
    {
        return Transactor == Other.Transactor && QueueLength == Other.QueueLength
            && UndoCount == Other.UndoCount && Bytes == Other.Bytes && TransactionIDs == Other.TransactionIDs;
    }
};

/**
 * Generated, explicitly-owned fresh NS work ONLY, inside ONE synchronous GT call.
 * Declare this BEFORE creation/the external context; destroy the context BEFORE
 * this guard. Never cross RPC, Tick, dialog/message pump, compile wait, save or GC.
 * No Begin/End/Cancel/Reset is called; the old transactor stays strongly alive.
 */
class FMCScopedGeneratedNiagaraNoUndo final
{
public:
    static bool CanEnterEnvironment(FString& OutError)
    {
        OutError.Reset();
        if (!IsInGameThread() || !GIsEditor || !GEditor || GEditor->PlayWorld || IsRunningCookCommandlet())
        {
            OutError = TEXT("Generated authoring requires the Editor game thread outside PIE/cook.");
            return false;
        }
        if (IsActive() || GIsTransacting || GUndo || !GEditor->Trans || GEditor->IsTransactionActive())
        {
            OutError = TEXT("Preserve active Undo/Redo/editor transactions; generated authoring cannot nest or join them.");
            return false;
        }
        return true;
    }

    // Also callable immediately after creation while this guard is active.
    // Fresh/no-users prevents ResetSystem/UpdateContext from reinitializing a
    // live component or taking its GPU flush/task-pump path inside the guard.
    static bool HasNoLiveUsers(UNiagaraSystem* System, FString& OutError)
    {
        if (!IsInGameThread() || !GEditor || !IsValid(System))
        {
            OutError = TEXT("Cannot validate generated target live users.");
            return false;
        }
        for (TObjectIterator<UNiagaraComponent> It; It; ++It)
        {
            if (IsValid(*It) && It->GetAsset() == System)
            {
                OutError = TEXT("Generated authoring requires no Niagara component referencing this draft; no preview is stopped automatically.");
                return false;
            }
        }
        return true;
    }

    static bool CanEnter(UNiagaraSystem* System, FString& OutError)
    {
        if (!CanEnterEnvironment(OutError)) return false;
        if (!IsValid(System) || System->IsTemplate() || !System->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
        {
            OutError = TEXT("An initialized project-owned generated Niagara draft is required.");
            return false;
        }
        if (!HasNoLiveUsers(System, OutError)) return false;
        auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!Editors)
        {
            OutError = TEXT("Cannot establish that the generated draft has no artist editor.");
            return false;
        }
        if (!Editors->FindEditorsForAssetAndSubObjects(System).IsEmpty())
        {
            OutError = TEXT("Close the artist editor for this draft before generated authoring; no window is closed automatically.");
            return false;
        }
        TArray<UObject*> Owned;
        GetObjectsWithOuter(System, Owned, EGetObjectsFlags::IncludeNestedObjects);
        Owned.Add(System);
        for (UObject* Object : Owned)
        {
            if (GEditor->Trans->IsObjectInTransactionBuffer(Object))
            {
                OutError = TEXT("The target or a subobject already belongs to existing Undo history; preserve it and use a fresh generated draft.");
                return false;
            }
        }
        return true;
    }

    FMCScopedGeneratedNiagaraNoUndo()
        : Editor(CheckedEditor())
        , SavedTransactor(Editor->Trans.Get())
        , TransactorGuard(Editor->Trans, TObjectPtr<UTransactor>(nullptr))
        , UndoGuard(GUndo, static_cast<ITransaction*>(nullptr))
        , ActiveGuard(IsActive(), true)
    {
    }

    ~FMCScopedGeneratedNiagaraNoUndo()
    {
        ensureAlwaysMsgf(IsInGameThread() && GEditor == Editor && Editor->Trans == nullptr && GUndo == nullptr,
            TEXT("Generated Niagara authoring crossed an unsupported reentrant transaction boundary."));
        // Member guards restore GUndo and GEditor->Trans before the strong hold
        // is released. Caller may only compile/wait/save AFTER this destructor.
    }

    FMCScopedGeneratedNiagaraNoUndo(const FMCScopedGeneratedNiagaraNoUndo&) = delete;
    FMCScopedGeneratedNiagaraNoUndo& operator=(const FMCScopedGeneratedNiagaraNoUndo&) = delete;

private:
    static bool& IsActive() { static bool bActive = false; return bActive; }
    static UEditorEngine* CheckedEditor()
    {
        FString Error;
        checkf(CanEnterEnvironment(Error), TEXT("%s"), *Error);
        return GEditor;
    }

    UEditorEngine* Editor;
    TStrongObjectPtr<UTransactor> SavedTransactor;
    TGuardValue<TObjectPtr<UTransactor>> TransactorGuard;
    TGuardValue<ITransaction*> UndoGuard;
    TGuardValue<bool> ActiveGuard;
};
