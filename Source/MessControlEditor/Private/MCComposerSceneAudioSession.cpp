#include "MCComposerSceneAudioSession.h"

#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

namespace MCComposerSceneAudioSessionPrivate
{
FString Digest(const FString& Text)
{
    const FTCHARToUTF8 Bytes(*Text);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
}

template <typename ValueType>
FString MapValues(const TMap<FName, ValueType>& Values)
{
    TArray<FName> Names;
    Values.GetKeys(Names);
    Names.Sort(FNameLexicalLess());
    FString Text;
    for (const FName Name : Names)
    {
        FString Value;
        ValueType::StaticStruct()->ExportText(Value, &Values.FindChecked(Name), nullptr, nullptr, PPF_None, nullptr);
        const FString Key = Name.ToString();
        Text += FString::Printf(TEXT("%d:%s%d:%s;"), Key.Len(), *Key, Value.Len(), *Value);
    }
    return Digest(Text);
}

FString PaletteValues(const UMCSoundPalette* Palette)
{
    return IsValid(Palette)
        ? MapValues(Palette->Events) + TEXT("/") + MapValues(Palette->Scenes)
        : TEXT("Invalid palette");
}

/** Include file content as well as metadata: a same-size external rewrite is a conflict. */
FString DiskValues(const UMCSoundPalette* Palette, bool bHashContent)
{
    if (!IsValid(Palette)) return TEXT("Invalid palette");
    const FString PackageName = Palette->GetOutermost()->GetName();
    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetAssetPackageExtension()))
        return TEXT("Unsaved:") + PackageName;

    FString Text = Filename;
    const FString Base = FPaths::ChangeExtension(Filename, TEXT(""));
    for (const FString& File : {Filename, Base + TEXT(".uexp"), Base + TEXT(".ubulk"), Base + TEXT(".uptnl")})
    {
        const int64 Size = IFileManager::Get().FileSize(*File);
        Text += TEXT("|") + File + TEXT(":") + LexToString(Size);
        if (Size >= 0)
        {
            Text += TEXT(":") + LexToString(IFileManager::Get().GetTimeStamp(*File).GetTicks());
            if (bHashContent)
            {
                const FMD5Hash Hash = FMD5Hash::HashFile(*File);
                if (!Hash.IsValid()) return TEXT("Unreadable:") + File;
                Text += TEXT(":") + LexToString(Hash);
            }
        }
    }
    return Digest(Text);
}

bool ValidateLayer(const FMCSoundSceneLayer& Layer, const FString& Label, bool bValidateSavedSound, FString& Error)
{
    if (!FMath::IsFinite(Layer.Volume) || Layer.Volume < 0.f || Layer.Volume > 2.f
        || !FMath::IsFinite(Layer.FadeInSeconds) || Layer.FadeInSeconds < 0.f || Layer.FadeInSeconds > 10.f
        || !FMath::IsFinite(Layer.FadeOutSeconds) || Layer.FadeOutSeconds < 0.f || Layer.FadeOutSeconds > 10.f)
    {
        Error = Label + TEXT(": громкость должна быть 0–2, время перехода — 0–10 с; значения должны быть конечными.");
        return false;
    }
    if (Layer.Sound.IsNull()) return true; // Intentional silence is a valid authoring decision.
    if (!Layer.Sound.ToSoftObjectPath().IsValid())
    {
        Error = Label + TEXT(": некорректная ссылка на звук.");
        return false;
    }
    if (!bValidateSavedSound) return true;
    const USoundBase* Sound = Layer.Sound.LoadSynchronous();
    if (!IsValid(Sound) || Sound->HasAnyFlags(RF_Transient) || Sound->IsEditorOnly()
        || Sound->GetOutermost() == GetTransientPackage() || Sound->GetOutermost()->IsDirty()
        || !FPackageName::DoesPackageExist(Sound->GetOutermost()->GetName()))
    {
        Error = Label + TEXT(": звук должен быть существующим сохранённым runtime-ассетом без несохранённых изменений.");
        return false;
    }
    return true;
}

bool ValidateScene(FName Name, const FMCSoundScene& Scene, bool bValidateSavedSounds, FString& Error)
{
    if (Name.IsNone())
    {
        Error = TEXT("У аудиосцены должно быть имя.");
        return false;
    }
    return ValidateLayer(Scene.Music, Name.ToString() + TEXT(" · музыка"), bValidateSavedSounds, Error)
        && ValidateLayer(Scene.Ambience, Name.ToString() + TEXT(" · атмосфера"), bValidateSavedSounds, Error);
}
}

TSharedRef<FMCComposerSceneAudioSession> FMCComposerSceneAudioSession::Shared()
{
    static TSharedRef<FMCComposerSceneAudioSession> Session = MakeShared<FMCComposerSceneAudioSession>();
    return Session;
}

FMCComposerSceneAudioSession::FMCComposerSceneAudioSession()
    : Status(TEXT("Выберите палитру звуков для музыки и атмосферы."))
{
    RegisterUndo();
}

FMCComposerSceneAudioSession::~FMCComposerSceneAudioSession()
{
    if (bRegisteredForUndo && GEditor) GEditor->UnregisterForUndo(this);
}

void FMCComposerSceneAudioSession::RegisterUndo()
{
    if (!bRegisteredForUndo && GEditor)
    {
        GEditor->RegisterForUndo(this);
        bRegisteredForUndo = true;
    }
}

bool FMCComposerSceneAudioSession::Fail(FString& Error, const FString& Message)
{
    Error = Message;
    Status = Message;
    return false;
}

UMCSoundPalette* FMCComposerSceneAudioSession::MakeDraft(UMCSoundPalette* Palette) const
{
    auto* Copy = DuplicateObject<UMCSoundPalette>(Palette, GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), Palette->GetClass(), TEXT("ComposerSceneAudioDraft")));
    if (Copy)
    {
        Copy->ClearFlags(RF_Public | RF_Standalone);
        Copy->SetFlags(RF_Transient | RF_Transactional);
    }
    return Copy;
}

void FMCComposerSceneAudioSession::CaptureBaseline(bool bCaptureDisk)
{
    using namespace MCComposerSceneAudioSessionPrivate;
    SourceValuesAtOpen = PaletteValues(Source.Get());
    SourceEventsAtOpen = IsValid(Source.Get()) ? MapValues(Source->Events) : FString();
    DraftValuesAtBaseline = PaletteValues(Draft.Get());
    if (bCaptureDisk)
    {
        DiskAtOpen = ReadDiskValues(true);
        bDiskConflictLatched = false;
    }
}

FString FMCComposerSceneAudioSession::ReadDiskValues(bool bForceContentHash) const
{
    using namespace MCComposerSceneAudioSessionPrivate;
    const FString Stat = DiskValues(Source.Get(), false);
    if (bForceContentHash || CachedDiskValues.IsEmpty() || Stat != CachedDiskStat)
    {
        CachedDiskValues = DiskValues(Source.Get(), true);
        CachedDiskStat = Stat;
    }
    return CachedDiskValues;
}

bool FMCComposerSceneAudioSession::Open(UMCSoundPalette* Palette, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || bApplying) return Fail(Error, TEXT("Палитру Composer сейчас нельзя открыть: требуется свободный поток редактора вне применения."));
    if (!IsValid(Palette) || Palette->HasAnyFlags(RF_ClassDefaultObject))
        return Fail(Error, TEXT("Не найдена палитра звуков."));
    RegisterUndo();
    if (Source.Get() == Palette && IsValid(Draft.Get())) return true; // Two panels retain one draft.
    if (HasChanges())
        return Fail(Error, TEXT("Сначала примените или отмените изменения текущей палитры; общий черновик сохранён."));
    auto* Copy = MakeDraft(Palette);
    if (!Copy) return Fail(Error, TEXT("Не удалось создать черновик аудиосцен."));
    Source.Reset(Palette);
    Draft.Reset(Copy);
    CaptureBaseline(true);
    ++Revision;
    Status = TEXT("Музыка и атмосфера открыты в общем черновике Composer.");
    return true;
}

const FMCSoundScene* FMCComposerSceneAudioSession::GetScene(FName Scene) const
{
    return IsValid(Draft.Get()) ? Draft->Scenes.Find(Scene) : nullptr;
}

TArray<FName> FMCComposerSceneAudioSession::GetSceneNames() const
{
    TArray<FName> Names;
    if (IsValid(Draft.Get())) Draft->Scenes.GetKeys(Names);
    Names.Sort(FNameLexicalLess());
    return Names;
}

bool FMCComposerSceneAudioSession::SetScene(FName Scene, const FMCSoundScene& Settings, FString& Error)
{
    using namespace MCComposerSceneAudioSessionPrivate;
    Error.Reset();
    if (!IsInGameThread() || bApplying || !IsValid(Draft.Get()))
        return Fail(Error, TEXT("Черновик аудиосцен сейчас недоступен."));
    if (!ValidateScene(Scene, Settings, false, Error)) { Status = Error; return false; }
    const auto* Existing = GetScene(Scene);
    if (Existing && Existing->Music == Settings.Music && Existing->Ambience == Settings.Ambience) return true;
    const FScopedTransaction Transaction(NSLOCTEXT("MCComposerSceneAudio", "EditScene", "Изменить аудиосцену Composer"));
    Draft->Modify();
    Draft->Scenes.Add(Scene, Settings);
    Draft->PostEditChange();
    ++Revision;
    Status = TEXT("Аудиосцена изменена в черновике. Назначения в игре ещё не изменены.");
    return true;
}

bool FMCComposerSceneAudioSession::RemoveScene(FName Scene, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || bApplying || !IsValid(Draft.Get()))
        return Fail(Error, TEXT("Черновик аудиосцен сейчас недоступен."));
    if (!Draft->Scenes.Contains(Scene)) return true;
    const FScopedTransaction Transaction(NSLOCTEXT("MCComposerSceneAudio", "RemoveScene", "Убрать аудиосцену Composer"));
    Draft->Modify();
    Draft->Scenes.Remove(Scene);
    Draft->PostEditChange();
    ++Revision;
    Status = TEXT("Аудиосцена убрана из черновика. В игре она сохранена до применения.");
    return true;
}

bool FMCComposerSceneAudioSession::HasChanges() const
{
    return IsValid(Draft.Get())
        && MCComposerSceneAudioSessionPrivate::PaletteValues(Draft.Get()) != DraftValuesAtBaseline;
}

bool FMCComposerSceneAudioSession::HasExternalChanges() const
{
    using namespace MCComposerSceneAudioSessionPrivate;
    return Source.Get() && (!IsValid(Source.Get()) || bDiskConflictLatched
        || PaletteValues(Source.Get()) != SourceValuesAtOpen || ReadDiskValues() != DiskAtOpen);
}

bool FMCComposerSceneAudioSession::Apply(FString& Error)
{
    using namespace MCComposerSceneAudioSessionPrivate;
    Error.Reset();
    if (!IsInGameThread() || bApplying || !GEditor || GEditor->PlayWorld || GEditor->IsTransactionActive())
        return Fail(Error, TEXT("Применение аудиосцен доступно вне PIE и активной транзакции редактора."));
    const TGuardValue<bool> Applying(bApplying, true);
    if (!IsValid(Source.Get()) || !IsValid(Draft.Get()))
        return Fail(Error, TEXT("Откройте существующую палитру звуков."));
    if (HasExternalChanges())
        return Fail(Error, TEXT("Исходная палитра или её файл изменены. Черновик сохранён; обновите источник перед применением."));

    UPackage* Package = Source->GetOutermost();
    FString Filename;
    if (Source->HasAnyFlags(RF_Transient) || Package == GetTransientPackage()
        || Package->HasAnyPackageFlags(PKG_EditorOnly | PKG_PlayInEditor | PKG_Cooked)
        || !FPackageName::IsValidLongPackageName(Package->GetName(), false)
        || !FPackageName::DoesPackageExist(Package->GetName(), &Filename))
        return Fail(Error, TEXT("Применение требует существующую сохранённую runtime-палитру; временный ассет не назначается в игру."));
    if (Package->IsDirty())
        return Fail(Error, TEXT("В исходной палитре есть несохранённые изменения. Сначала сохраните их отдельно; Composer не сохранит их вместе со своим черновиком."));
    if (IFileManager::Get().IsReadOnly(*Filename))
        return Fail(Error, TEXT("Файл исходной палитры доступен только для чтения. Выполните checkout отдельно и повторите применение."));
    if (MapValues(Draft->Events) != SourceEventsAtOpen)
        return Fail(Error, TEXT("Этот черновик редактирует только музыку и атмосферу. Изменения Events не будут потеряны или сохранены неявно; отмените их отдельно."));
    for (const auto& Pair : Draft->Scenes)
        if (!ValidateScene(Pair.Key, Pair.Value, true, Error)) { Status = Error; return false; }
    if (!HasChanges())
    {
        Status = TEXT("Нет изменений аудиосцен для сохранения.");
        return true;
    }
    // Sound validation may run asset loads/editor callbacks. Recheck immediately before mutation.
    const FString VerifiedDisk = ReadDiskValues(true);
    if (VerifiedDisk.StartsWith(TEXT("Unreadable:")))
        return Fail(Error, TEXT("Не удалось прочитать текущий файл палитры для проверки. Применение отменено; черновик сохранён."));
    if (HasExternalChanges() || VerifiedDisk != DiskAtOpen || Package->IsDirty())
        return Fail(Error, TEXT("Исходная палитра изменилась во время проверки ресурсов. Применение отменено; черновик сохранён."));

    const auto PreviousScenes = Source->Scenes;
    FScopedTransaction Transaction(NSLOCTEXT("MCComposerSceneAudio", "ApplyScenes", "Применить аудиосцены Composer"));
    Source->Modify();
    Source->Scenes = Draft->Scenes; // Events and other palette properties remain source-owned.
    Source->PostEditChange();
    Package->MarkPackageDirty();
    if (MapValues(Source->Events) != SourceEventsAtOpen)
    {
        Source->Scenes = PreviousScenes;
        Package->MarkPackageDirty();
        Transaction.Cancel();
        ++Revision;
        return Fail(Error, TEXT("Обработчик редактора изменил Events исходной палитры. Сохранение отменено; чужие правки оставлены в памяти, Scenes восстановлены, черновик сохранён."));
    }
    const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({Package}, true);
    if (!bSaved || Package->IsDirty() || !FPackageName::DoesPackageExist(Package->GetName()))
    {
        Source->Scenes = PreviousScenes;
        Source->PostEditChange();
        Package->MarkPackageDirty();
        Transaction.Cancel();
        // A failed save may have touched disk. Do not pretend that memory rollback restored its bytes.
        bDiskConflictLatched = true;
        ++Revision;
        return Fail(Error, TEXT("Не удалось подтвердить сохранение палитры. Прежние Scenes восстановлены в памяти; пакет оставлен несохранённым. Проверьте файл перед повторным открытием. Черновик сохранён."));
    }
    CaptureBaseline(true);
    ++Revision;
    Status = TEXT("Музыка и атмосфера сохранены в исходной палитре. Events сохранены без изменений.");
    return true;
}

bool FMCComposerSceneAudioSession::Revert(FString& Error)
{
    using namespace MCComposerSceneAudioSessionPrivate;
    Error.Reset();
    if (!IsInGameThread() || bApplying || !IsValid(Source.Get()))
        return Fail(Error, TEXT("Исходная палитра недоступна; откройте её заново."));
    auto* Copy = MakeDraft(Source.Get());
    if (!Copy) return Fail(Error, TEXT("Не удалось восстановить черновик аудиосцен."));
    const bool bDiskChanged = bDiskConflictLatched || ReadDiskValues(true) != DiskAtOpen;
    Draft.Reset(Copy); // A new object keeps old Undo records from mutating the refreshed draft.
    CaptureBaseline(!bDiskChanged);
    bDiskConflictLatched = bDiskChanged;
    ++Revision;
    Status = bDiskChanged
        ? TEXT("Черновик отменён. Файл палитры менялся: перезагрузите ассет и откройте свежий источник, чтобы избежать перезаписи старой памятью.")
        : TEXT("Черновик отменён; загружены текущие значения исходной палитры.");
    return true;
}

FString FMCComposerSceneAudioSession::GetStatus() const
{
    if (bDiskConflictLatched)
        return TEXT("Файл палитры менялся. Нужна перезагрузка ассета и открытие свежего источника; применение заблокировано.");
    if (HasExternalChanges())
        return TEXT("Исходная палитра изменилась. Черновик сохранён; применение заблокировано до обновления источника.");
    if (IsValid(Source.Get()) && Source->GetOutermost()->IsDirty())
        return TEXT("Исходная палитра имеет несохранённые изменения. Composer не сохранит их неявно.");
    return Status;
}

void FMCComposerSceneAudioSession::PostUndo(bool bSuccess)
{
    if (bSuccess && IsValid(Draft.Get()))
    {
        ++Revision;
        Status = TEXT("Черновик аудиосцен обновлён общей командой Undo/Redo.");
    }
}

void FMCComposerSceneAudioSession::PostRedo(bool bSuccess)
{
    PostUndo(bSuccess);
}
