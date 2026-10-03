#include "MCFoodCollisionEditorLibrary.h"

#include "MCFoodCollisionBaker.h"
#include "MCFoodCollisionData.h"
#include "MCDayPlan.h"
#include "Containers/Ticker.h"
#include "DataTableEditorUtils.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Features/IModularFeatures.h"
#include "IPIEAuthorizer.h"
#include "Misc/CommandLine.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCFoodCollisionEditor, Log, All);

namespace
{
    bool bAuthoringCollision=false;

    bool IsEditorAuthoringSession()
    {
        return GIsEditor && !FParse::Param(FCommandLine::Get(),TEXT("game"));
    }

    bool IsAutomaticBakingEnabled()
    {
        return !FParse::Param(FCommandLine::Get(),TEXT("MCFoodCollisionNoAutoBake"));
    }

    bool IsFoodMenu(const UDataTable* Menu)
    {
        return IsValid(Menu) && Menu->GetRowStruct()==FMCFoodRow::StaticStruct()
            && !Menu->HasAnyFlags(RF_Transient)
            && !Menu->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor)
            && Menu->GetOutermost()->GetName().StartsWith(TEXT("/Game/"));
    }

    void ReportErrors(const TArray<FString>& Errors)
    {
        for(const FString& Error:Errors) UE_LOG(LogMCFoodCollisionEditor,Error,TEXT("%s"),*Error);
    }

    bool SaveMenu(UDataTable* Menu,TArray<FString>& Errors)
    {
        UPackage* Package=Menu->GetOutermost();
        if(!Package->IsDirty()) return true;
        const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args;
        Args.TopLevelFlags=RF_Public | RF_Standalone;
        Args.SaveFlags=SAVE_NoError;
        Args.Error=GWarn;
        if(UPackage::SavePackage(Package,Menu,*Filename,Args)) return true;
        Errors.Add(FString::Printf(TEXT("Could not save food collision references in %s"),*Menu->GetPathName()));
        return false;
    }

    bool BakeMenu(UDataTable* Menu,bool bSaveProfiles,bool bSaveMenu,TArray<FString>& Errors)
    {
        if(!IsEditorAuthoringSession())
        {
            Errors.Add(TEXT("Food collision bake is available only in an editor authoring session or an editor commandlet."));
            return false;
        }
        if(!IsFoodMenu(Menu))
        {
            Errors.Add(TEXT("Food collision bake requires a persistent /Game DataTable with FMCFoodRow rows."));
            return false;
        }
        if(bAuthoringCollision || (GEditor && GEditor->PlayWorld))
        {
            Errors.Add(TEXT("Food collision cannot be baked during another bake or an active PIE session."));
            return false;
        }
        TGuardValue<bool> Guard(bAuthoringCollision,true);
        TArray<UMCFoodCollisionData*> Changed;
        if(!MCFoodCollisionBaker::BakeFoodMenuCollisions(Menu,bSaveProfiles,Errors,Changed))
        {
            if(Errors.IsEmpty()) Errors.Add(FString::Printf(TEXT("Food collision bake failed for %s"),*Menu->GetPathName()));
            return false;
        }
        return !bSaveMenu || SaveMenu(Menu,Errors);
    }
}

class FMessControlEditorModule final : public IModuleInterface, public IPIEAuthorizer
{
    class FMenuListener final : public FDataTableEditorUtils::INotifyOnDataTableChanged
    {
    public:
        explicit FMenuListener(FMessControlEditorModule& InOwner):Owner(InOwner) {}
        virtual void PreChange(const UDataTable*,FDataTableEditorUtils::EDataTableChangeInfo) override {}
        virtual void PostChange(const UDataTable* Table,FDataTableEditorUtils::EDataTableChangeInfo) override
        {
            Owner.QueueMenu(const_cast<UDataTable*>(Table));
        }
    private:
        FMessControlEditorModule& Owner;
    };

public:
    virtual void StartupModule() override
    {
        if(!IsEditorAuthoringSession()) return;
        SaveHandle=FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this,&FMessControlEditorModule::OnObjectPreSave);
        // Export/audit commandlets never author assets implicitly. Explicit library calls still work.
        if(IsRunningCommandlet() || !IsAutomaticBakingEnabled()) return;
        MenuListener=MakeUnique<FMenuListener>(*this);
        AssetLoadedHandle=FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this,&FMessControlEditorModule::OnAssetLoaded);
        PropertyChangedHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,&FMessControlEditorModule::OnPropertyChanged);
        PrePIEHandle=FEditorDelegates::PreBeginPIE.AddRaw(this,&FMessControlEditorModule::PreparePIE);
        UndoHandle=FEditorDelegates::PostUndoRedo.AddRaw(this,&FMessControlEditorModule::OnUndoRedo);
        IModularFeatures::Get().RegisterModularFeature(IPIEAuthorizer::GetModularFeatureName(),this);
        bPIEAuthorizerRegistered=true;
        if(GEditor)
        {
            ImportSubsystem=GEditor->GetEditorSubsystem<UImportSubsystem>();
            if(ImportSubsystem.IsValid()) ReimportHandle=ImportSubsystem->OnAssetReimport.AddRaw(this,&FMessControlEditorModule::OnAssetReimport);
        }
        DebounceTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FMessControlEditorModule::TickPending),.1f);
        InitialLoadTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FMessControlEditorModule::InitializeMenu));
    }

    virtual void ShutdownModule() override
    {
        FTSTicker::GetCoreTicker().RemoveTicker(DebounceTicker);
        FTSTicker::GetCoreTicker().RemoveTicker(InitialLoadTicker);
        FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);
        FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
        FEditorDelegates::PreBeginPIE.Remove(PrePIEHandle);
        FEditorDelegates::PostUndoRedo.Remove(UndoHandle);
        if(ImportSubsystem.IsValid()) ImportSubsystem->OnAssetReimport.Remove(ReimportHandle);
        if(bPIEAuthorizerRegistered) IModularFeatures::Get().UnregisterModularFeature(IPIEAuthorizer::GetModularFeatureName(),this);
        MenuListener.Reset();
        Pending.Reset();
    }

private:
    void QueueMenu(UDataTable* Menu)
    {
        if(!bAuthoringCollision && IsFoodMenu(Menu)) Pending.Add(Menu,FPlatformTime::Seconds()+.25);
    }

    bool InitializeMenu(float)
    {
        QueueMenu(LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu")));
        return false;
    }

    bool TickPending(float)
    {
        if(bAuthoringCollision || (GEditor && GEditor->PlayWorld)) return true;
        const double Now=FPlatformTime::Seconds();
        TArray<TWeakObjectPtr<UDataTable>> Ready;
        for(auto It=Pending.CreateIterator();It;++It)
        {
            if(!It.Key().IsValid()) It.RemoveCurrent();
            else if(It.Value()<=Now) { Ready.Add(It.Key()); It.RemoveCurrent(); }
        }
        for(const auto& WeakMenu:Ready)
        {
            TArray<FString> Errors;
            BakeMenu(WeakMenu.Get(),false,false,Errors);
            ReportErrors(Errors);
        }
        return true;
    }

    void OnAssetLoaded(UObject* Asset)
    {
        if(IsInGameThread()) QueueMenu(Cast<UDataTable>(Asset));
    }

    void QueueMenusUsingMesh(UStaticMesh* Mesh)
    {
        if(!Mesh || bAuthoringCollision) return;
        for(TObjectIterator<UDataTable> It;It;++It)
        {
            if(!IsFoodMenu(*It)) continue;
            bool bUsesMesh=false;
            It->ForeachRow<FMCFoodRow>(TEXT("FoodCollisionMeshChanged"),[&](FName,const FMCFoodRow& Row)
            {
                for(const auto& Choice:Row.WholeMeshes) bUsesMesh|=Choice.ToSoftObjectPath()==FSoftObjectPath(Mesh);
                for(const auto& Choice:Row.FragmentMeshes) bUsesMesh|=Choice.ToSoftObjectPath()==FSoftObjectPath(Mesh);
            });
            if(bUsesMesh) QueueMenu(*It);
        }
    }

    void OnPropertyChanged(UObject* Object,FPropertyChangedEvent&)
    {
        if(!Object || !IsInGameThread()) return;
        UStaticMesh* Mesh=Cast<UStaticMesh>(Object);
        QueueMenusUsingMesh(Mesh?Mesh:Object->GetTypedOuter<UStaticMesh>());
    }

    void OnUndoRedo()
    {
        for(TObjectIterator<UDataTable> It;It;++It) QueueMenu(*It);
    }

    void OnAssetReimport(UObject* Asset)
    {
        if(auto* Menu=Cast<UDataTable>(Asset)) QueueMenu(Menu);
        else QueueMenusUsingMesh(Cast<UStaticMesh>(Asset));
    }

    void OnObjectPreSave(UObject* Object,FObjectPreSaveContext Context)
    {
        UDataTable* Menu=Cast<UDataTable>(Object);
        if(!IsFoodMenu(Menu) || bAuthoringCollision) return;
        TArray<FString> Errors;
        if(Context.IsCooking())
        {
            // The packaging preflight performs authoring. Cook workers only check the saved result.
            if(!MCFoodCollisionBaker::IsFoodMenuCollisionCurrent(Menu,Errors))
            {
                Errors.Add(FString::Printf(TEXT("Stale food collision in %s; run the food collision editor bake before cooking."),*Menu->GetPathName()));
                ReportErrors(Errors);
            }
            return;
        }
        if(IsRunningCommandlet() || !IsAutomaticBakingEnabled() || Context.IsProceduralSave()) return;
        Pending.Remove(Menu);
        BakeMenu(Menu,true,false,Errors);
        ReportErrors(Errors);
    }

    void PreparePIE(bool)
    {
        LastPIEError=FText::GetEmpty();
        // PIE can be requested before the deferred startup ticker has loaded the default menu.
        UDataTable* DefaultMenu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        Pending.Reset();
        TArray<TWeakObjectPtr<UDataTable>> Menus;
        for(TObjectIterator<UDataTable> It;It;++It) if(IsFoodMenu(*It)) Menus.Add(*It);
        TArray<FString> Errors;
        if(!IsFoodMenu(DefaultMenu)) Errors.Add(TEXT("The default breakfast menu could not be loaded for its food collision bake."));
        for(const auto& Menu:Menus)
        {
            if(!BakeMenu(Menu.Get(),true,true,Errors)) break;
        }
        if(!Errors.IsEmpty())
        {
            ReportErrors(Errors);
            LastPIEError=FText::FromString(FString::Join(Errors,TEXT("\n")));
        }
    }

    virtual TValueOrError<bool,FText> IsPIEAuthorizedInternal(bool) const override
    {
        if(!LastPIEError.IsEmpty()) return MakeError(LastPIEError);
        return MakeValue(true);
    }

    TUniquePtr<FMenuListener> MenuListener;
    TMap<TWeakObjectPtr<UDataTable>,double> Pending;
    TWeakObjectPtr<UImportSubsystem> ImportSubsystem;
    FDelegateHandle SaveHandle,AssetLoadedHandle,PropertyChangedHandle,PrePIEHandle,UndoHandle,ReimportHandle;
    FTSTicker::FDelegateHandle DebounceTicker,InitialLoadTicker;
    FText LastPIEError;
    bool bPIEAuthorizerRegistered=false;
};

bool UMCFoodCollisionEditorLibrary::BakeMenuCollision(UDataTable* Menu,bool bSave)
{
    TArray<FString> Errors;
    const bool bSuccess=BakeMenu(Menu,bSave,bSave,Errors);
    ReportErrors(Errors);
    return bSuccess;
}

bool UMCFoodCollisionEditorLibrary::IsMenuCurrent(UDataTable* Menu)
{
    TArray<FString> Errors;
    if(!IsEditorAuthoringSession() || !IsFoodMenu(Menu)) return false;
    return MCFoodCollisionBaker::IsFoodMenuCollisionCurrent(Menu,Errors);
}

IMPLEMENT_MODULE(FMessControlEditorModule,MessControlEditor)
