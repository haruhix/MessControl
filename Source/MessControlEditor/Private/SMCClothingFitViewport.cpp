#include "SMCClothingFitViewport.h"

#include "MCCosmeticCatalog.h"
#include "MCCosmeticFit.h"
#include "MCDataAssets.h"
#include "MCToothCharacter.h"
#include "AdvancedPreviewScene.h"
#include "AssetViewerSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataTableEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SceneView.h"
#include "Slate/SceneViewport.h"
#include "StaticMeshResources.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "MCClothingFitEditor"
namespace MCClothingFitViewportPrivate
{
    TArray<TWeakPtr<SMCClothingFitViewport>> OpenViews;

    // The advanced preview's stock Tick writes shared viewer lighting settings.
    // This private studio never calls it, changes its profile, or forwards light shortcuts.
    class FPrivateScene final : public FAdvancedPreviewScene
    {
    public:
        FPrivateScene():FAdvancedPreviewScene(FPreviewScene::ConstructionValues()
            .AllowAudioPlayback(false).SetTransactional(false).ShouldSimulatePhysics(false))
        {
            DefaultSettings->OnAssetViewerSettingsChanged().Remove(RefreshDelegate);RefreshDelegate.Reset();
            SkyComponent->SetVisibility(false);PostProcessComponent->bEnabled=false;
            SetFloorVisibility(false,true);SetEnvironmentVisibility(false,true);
            SetLightDirection(FRotator(-35,145,0));SetLightBrightness(3.f);SetLightColor(FColor::White);
            if(DirectionalLight) DirectionalLight->SetForwardShadingPriority(1);
            if(auto* Cube=LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap"))) SetSkyCubemap(Cube);
            SetSkyBrightness(.65f);SkyLight->SetVisibility(true);SkyLight->SetLightColor(FLinearColor::White);
            auto* Fill=NewObject<UDirectionalLightComponent>(GetTransientPackage(),NAME_None,RF_Transient);
            Fill->SetIntensity(.8f);Fill->SetCastShadows(false);Fill->SetForwardShadingPriority(0);
            Fill->SetAtmosphereSunLight(false);AddComponent(Fill,FTransform(FRotator(-25,-130,0)));
        }
        virtual void Tick(float) override {UpdateCaptureContents();}
    };

    FString ExportRow(const FMCCosmeticCatalogRow& Row,UDataTable* Table)
    {
        FString Result;FMCCosmeticCatalogRow::StaticStruct()->ExportText(Result,&Row,nullptr,Table,PPF_None,nullptr);
        return Result;
    }
    FString ObjectPath(const UObject* Object) {return Object?Object->GetPathName():FString();}
    TArray<TSharedPtr<FJsonValue>> MaterialPaths(const UMeshComponent* Component)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        if(Component) for(int32 Index=0;Index<Component->GetNumMaterials();++Index)
            Result.Add(MakeShared<FJsonValueString>(ObjectPath(Component->GetMaterial(Index))));
        return Result;
    }
}

class FMCClothingFitViewportClient final : public FEditorViewportClient
{
public:
    FMCClothingFitViewportClient(const TSharedRef<FAdvancedPreviewScene>& InScene,const TSharedRef<SMCClothingFitViewport>& InWidget)
        :FEditorViewportClient(nullptr,&InScene.Get(),StaticCastSharedRef<SEditorViewport>(InWidget))
        ,SceneLifetime(InScene),FitWidget(InWidget)
    {
        SetViewMode(VMI_Lit);SetRealtime(true);ShowWidget(false);OverrideNearClipPlane(1.f);
        bUsingOrbitCamera=true;ViewFOV=65.f;
        DrawHelper.bDrawPivot=DrawHelper.bDrawWorldBox=DrawHelper.bDrawKillZ=DrawHelper.bDrawGrid=false;
        EngineShowFlags.SetSelectionOutline(false);ExposureSettings.bFixed=true;ExposureSettings.FixedEV100=1.f;
    }
    bool FrameClothingBounds(const FBox& Bounds,const FRotator& PresetRotation)
    {
        if(!Viewport || Viewport->GetSizeXY().GetMin()<=0 || !Bounds.IsValid) return false;
        // Get the actual editor projection, including its axis-constraint policy.
        // Native FocusViewportOnBox expands a sphere by aspect, which leaves this
        // short, wide Row Editor far too empty. Fit the eight geometry-box corners.
        FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Viewport,GetScene(),EngineShowFlags).SetRealtimeUpdate(true));
        const FSceneView* FrameView=CalcSceneView(&Family);if(!FrameView) return false;
        const auto& Projection=FrameView->ViewMatrices.GetViewToClip();
        const double HorizontalScale=FMath::Abs(double(Projection.M[0][0]));
        const double VerticalScale=FMath::Abs(double(Projection.M[1][1]));
        if(!FMath::IsFinite(HorizontalScale) || !FMath::IsFinite(VerticalScale) || HorizontalScale<=0 || VerticalScale<=0) return false;
        constexpr double ScreenFill=.80;
        const FVector Center=Bounds.GetCenter();const FQuat Orientation=PresetRotation.Quaternion();
        const FVector Forward=Orientation.GetAxisX(),Right=Orientation.GetAxisY(),Up=Orientation.GetAxisZ();
        double Distance=10.;
        for(int32 Corner=0;Corner<8;++Corner) {
            const FVector Point((Corner&1)?Bounds.Max.X:Bounds.Min.X,(Corner&2)?Bounds.Max.Y:Bounds.Min.Y,(Corner&4)?Bounds.Max.Z:Bounds.Min.Z);
            const FVector Offset=Point-Center;const double Depth=FVector::DotProduct(Offset,Forward);
            Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(Offset,Right))*HorizontalScale/ScreenFill-Depth);
            Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(Offset,Up))*VerticalScale/ScreenFill-Depth);
            Distance=FMath::Max(Distance,2.-Depth);
        }
        if(!FMath::IsFinite(Distance)) return false;
        ToggleOrbitCamera(false);SetViewRotation(PresetRotation);SetLookAtLocation(Center);
        SetViewLocation(Center-Forward*Distance);ToggleOrbitCamera(true);return true;
    }
    virtual bool ShouldOrbitCamera() const override {return true;}
    virtual FLinearColor GetBackgroundColor() const override {return FLinearColor(.045f,.05f,.06f);}
    virtual void Tick(float DeltaSeconds) override
    {
        FEditorViewportClient::Tick(DeltaSeconds);
        if(const auto Pinned=FitWidget.Pin()) Pinned->AdvancePreview(DeltaSeconds);
    }
    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
    {
        if(EventArgs.Key==EKeys::F && EventArgs.Event==IE_Pressed) {
            if(const auto Pinned=FitWidget.Pin()) Pinned->Focus();return true;
        }
        // No FAdvancedPreviewScene input handler: orbit only, no global profile edits.
        return FEditorViewportClient::InputKey(EventArgs);
    }
    virtual void OverridePostProcessSettings(FSceneView& View) override
    {
        FPostProcessSettings Studio;Studio.bOverride_AutoExposureMethod=true;Studio.AutoExposureMethod=AEM_Manual;
        Studio.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Studio.AutoExposureApplyPhysicalCameraExposure=false;
        Studio.bOverride_AutoExposureBias=true;Studio.AutoExposureBias=0.f;
        View.OverridePostProcessSettings(Studio,1.f);
    }
private:
    // SEditorViewport's FSceneViewport can retain Client after derived widget members.
    TSharedRef<FAdvancedPreviewScene> SceneLifetime;
    TWeakPtr<SMCClothingFitViewport> FitWidget;
};

class SMCClothingFitViewport::FTableListener final : public FDataTableEditorUtils::INotifyOnDataTableChanged
{
public:
    explicit FTableListener(SMCClothingFitViewport& InOwner):Owner(InOwner) {}
    virtual void PreChange(const UDataTable*,FDataTableEditorUtils::EDataTableChangeInfo) override {}
    virtual void PostChange(const UDataTable* Table,FDataTableEditorUtils::EDataTableChangeInfo) override
    {
        if(Table==Owner.SourceTable.Get()) Owner.RequestRowRefresh();
    }
private:
    SMCClothingFitViewport& Owner;
};

SMCClothingFitViewport::SMCClothingFitViewport() = default;

void SMCClothingFitViewport::Construct(const FArguments& InArgs)
{
    RowScope=InArgs._RowScope;PreviewScene=MakeShared<MCClothingFitViewportPrivate::FPrivateScene>();
    auto* Body=NewObject<USkeletalMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);Body->SetSimulatePhysics(false);
    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);Body->SetEnableAnimation(false);
    Body->SetUpdateAnimationInEditor(false);Body->bForceRefpose=true;
    PreviewScene->AddComponent(Body,FTransform::Identity);DisplayBody=Body;
    auto* Part=NewObject<UStaticMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);Part->SetSimulatePhysics(false);Part->SetVisibility(false);
    PreviewScene->AddComponent(Part,FTransform::Identity);DisplayClothing=Part;
    // Saved runtime character defaults are read, never instantiated as a player actor.
    UClass* PlayerClass=LoadClass<AMCToothCharacter>(nullptr,TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    const auto* Character=PlayerClass?PlayerClass->GetDefaultObject<AMCToothCharacter>():GetDefault<AMCToothCharacter>();
    PreviewAppearance.Reset(Character?Character->Appearance.Get():nullptr);
    RebuildBody();
    SEditorViewport::Construct(SEditorViewport::FArguments());
    RefreshRow();
    TableListener=MakeUnique<FTableListener>(*this);
    PropertyChangedHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,&SMCClothingFitViewport::ObjectChanged);
    UndoHandle=FEditorDelegates::PostUndoRedo.AddRaw(this,&SMCClothingFitViewport::UndoRedo);
    if(GEditor) {
        ImportSubsystem=GEditor->GetEditorSubsystem<UImportSubsystem>();
        if(ImportSubsystem.IsValid()) ReimportHandle=ImportSubsystem->OnAssetReimport.AddRaw(this,&SMCClothingFitViewport::AssetReimported);
    }
    MCClothingFitViewportPrivate::OpenViews.RemoveAll([](const auto& Entry){return !Entry.IsValid();});
    MCClothingFitViewportPrivate::OpenViews.Add(SharedThis(this));
}
SMCClothingFitViewport::~SMCClothingFitViewport()
{
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
    FEditorDelegates::PostUndoRedo.Remove(UndoHandle);TableListener.Reset();
    if(ImportSubsystem.IsValid()) ImportSubsystem->OnAssetReimport.Remove(ReimportHandle);
    if(auto* Part=DisplayClothing.Get()) {PreviewScene->RemoveComponent(Part);Part->DestroyComponent();}
    if(auto* Body=DisplayBody.Get()) {PreviewScene->RemoveComponent(Body);Body->DestroyComponent();}
    if(FitViewportClient) FitViewportClient->Viewport=nullptr;
    PreviewAppearance.Reset();
    MCClothingFitViewportPrivate::OpenViews.RemoveAll([this](const auto& Entry){const auto Pinned=Entry.Pin();return !Pinned || Pinned.Get()==this;});
}
TSharedRef<FEditorViewportClient> SMCClothingFitViewport::MakeEditorViewportClient()
{
    FitViewportClient=MakeShared<FMCClothingFitViewportClient>(PreviewScene.ToSharedRef(),SharedThis(this));
    return FitViewportClient.ToSharedRef();
}

bool SMCClothingFitViewport::ResolveRow(UDataTable*& OutTable,FName& OutRowName,const FMCCosmeticCatalogRow*& OutRow) const
{
    OutTable=nullptr;OutRowName=NAME_None;OutRow=nullptr;
    const auto Scope=RowScope.Pin();
    if(!Scope || Scope->GetStruct()!=FMCCosmeticCatalogRow::StaticStruct() || !Scope->GetPackage()) return false;
    const uint8* Memory=Scope->GetStructMemory();if(!Memory) return false;
    ForEachObjectWithPackage(Scope->GetPackage(),[&](UObject* Object){
        auto* Table=Cast<UDataTable>(Object);
        if(!Table || Table->GetRowStruct()!=FMCCosmeticCatalogRow::StaticStruct() || Table->HasAnyFlags(RF_Transient)
            || Table->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor)) return true;
        for(const auto& Pair:Table->GetRowMap()) if(Pair.Value==Memory) {
            // Validate identity each read; no copied row, stale pointer or private FStructFromDataTable cast.
            OutTable=Table;OutRowName=Pair.Key;OutRow=reinterpret_cast<const FMCCosmeticCatalogRow*>(Memory);return false;
        }
        return true;
    },EGetObjectsFlags::None);
    return OutTable && OutRow;
}
void SMCClothingFitViewport::RequestRowRefresh() {bRefreshRequested=true;}
void SMCClothingFitViewport::UndoRedo() {RequestRowRefresh();}
void SMCClothingFitViewport::ObjectChanged(UObject* Object,FPropertyChangedEvent&)
{
    if(!Object || Object->HasAnyFlags(RF_Transient)) return;
    if(Object==PreviewAppearance.Get() || Object->IsA<USkeletalMesh>() || Object->IsA<UMaterialInterface>()) bBodyRefreshRequested=true;
    if(Object==SourceTable.Get() || Object==PreviewAppearance.Get() || Object->IsA<UMaterialInterface>()
        || Object->IsA<UStaticMesh>() || Object->IsA<USkeletalMesh>()) RequestRowRefresh();
}
bool SMCClothingFitViewport::IsDisplayedDependency(const UObject* Object) const
{
    if(!Object) return false;
    if(Object==SourceTable.Get() || Object==PreviewAppearance.Get()) return true;
    const auto* Body=DisplayBody.Get();const auto* Part=DisplayClothing.Get();
    if(Object==(Body?Body->GetSkeletalMeshAsset():nullptr) || Object==(Part?Part->GetStaticMesh():nullptr)) return true;
    auto UsesMaterial=[Object](const UMeshComponent* Component) {
        if(!Component) return false;
        for(int32 Index=0;Index<Component->GetNumMaterials();++Index)
            for(const UMaterialInterface* Material=Component->GetMaterial(Index);Material;) {
                if(Material==Object) return true;
                const auto* Instance=Cast<UMaterialInstance>(Material);Material=Instance?Instance->Parent.Get():nullptr;
            }
        return false;
    };
    return UsesMaterial(Body) || UsesMaterial(Part);
}
void SMCClothingFitViewport::AssetReimported(UObject* Object)
{
    if(!IsDisplayedDependency(Object)) return;
    bBodyRefreshRequested=true;RequestRowRefresh();
}
void SMCClothingFitViewport::SetAppearance(UMCPlayerAppearance* InAppearance)
{
    if(!InAppearance) {
        UClass* PlayerClass=LoadClass<AMCToothCharacter>(nullptr,TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
        const auto* Character=PlayerClass?PlayerClass->GetDefaultObject<AMCToothCharacter>():GetDefault<AMCToothCharacter>();
        InAppearance=Character?Character->Appearance.Get():nullptr;
    }
    PreviewAppearance.Reset(InAppearance);RebuildBody();RequestRowRefresh();bFrameRequested=true;
}
FString SMCClothingFitViewport::GetAppearancePath() const {return MCClothingFitViewportPrivate::ObjectPath(PreviewAppearance.Get());}
void SMCClothingFitViewport::RebuildBody()
{
    auto* Body=DisplayBody.Get();if(!Body) return;
    const auto* Appearance=PreviewAppearance.Get();
    UClass* PlayerClass=LoadClass<AMCToothCharacter>(nullptr,TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
    const auto* Defaults=PlayerClass?PlayerClass->GetDefaultObject<AMCToothCharacter>():GetDefault<AMCToothCharacter>();
    const auto* DefaultMesh=Defaults?Defaults->GetMesh():nullptr;
    auto* BodyMesh=Appearance && Appearance->SkeletalMesh?Appearance->SkeletalMesh.Get()
        :DefaultMesh && DefaultMesh->GetSkeletalMeshAsset()?DefaultMesh->GetSkeletalMeshAsset()
        :LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Art/Rig/SK_ToothHero.SK_ToothHero"));
    Body->SetSkeletalMeshAsset(BodyMesh);Body->EmptyOverrideMaterials();
    // Same profile transform as AMCToothCharacter::StandingMeshTransform. Reference-pose fit is explicit.
    Body->SetWorldTransform(Appearance && Appearance->SkeletalMesh==BodyMesh?Appearance->MeshTransform
        :DefaultMesh?DefaultMesh->GetRelativeTransform():FTransform(FVector(0,0,-58)));
    if(DefaultMesh && DefaultMesh->GetSkeletalMeshAsset()==BodyMesh)
        for(int32 Index=0;Index<DefaultMesh->GetNumMaterials();++Index)
            if(auto* Material=DefaultMesh->GetMaterial(Index)) Body->SetMaterial(Index,Material);
    if(Appearance && Appearance->SkeletalMesh==BodyMesh && Appearance->Material) Body->SetMaterial(0,Appearance->Material);
    for(int32 Index=0;Index<Body->GetNumMaterials();++Index) if(auto* Original=Body->GetMaterial(Index)) {
        UMaterialInterface* StaticParent=Original;
        while(const auto* Dynamic=Cast<UMaterialInstanceDynamic>(StaticParent)) StaticParent=Dynamic->Parent;
        if(!StaticParent) continue;
        auto* PrivateMaterial=UMaterialInstanceDynamic::Create(StaticParent,Body);PrivateMaterial->SetFlags(RF_Transient);
        PrivateMaterial->CopyMaterialUniformParameters(Original);Body->SetMaterial(Index,PrivateMaterial);
    }
    Body->RefreshBoneTransforms();Body->UpdateComponentToWorld();Body->UpdateBounds();
    bFrameRequested=true;
}
void SMCClothingFitViewport::RefreshRow()
{
    bRefreshRequested=false;RefreshAge=0;
    auto* Part=DisplayClothing.Get();auto* Body=DisplayBody.Get();
    UDataTable* Table=nullptr;FName RowName;const FMCCosmeticCatalogRow* Row=nullptr;
    if(!Part || !Body || !ResolveRow(Table,RowName,Row)) {
        bRowValid=false;bAttachValid=false;SourceTable.Reset();SourceRow=NAME_None;
        if(Part) Part->SetVisibility(false);
        Diagnostics=LOCTEXT("MissingRow","No live selected clothing row. Select a row in this DataTable's Row Editor.").ToString();return;
    }
    SourceTable=Table;SourceRow=RowName;RowExport=MCClothingFitViewportPrivate::ExportRow(*Row,Table);bRowValid=true;
    TArray<FText> Notes;
    if(!Row->Mesh) Notes.Add(LOCTEXT("MissingMesh","Missing clothing mesh."));
    if(!Body->GetSkeletalMeshAsset()) Notes.Add(LOCTEXT("MissingBody","Missing body skeletal mesh."));
    bAttachValid=Body->GetSkeletalMeshAsset() && !Row->AttachBone.IsNone() && Body->DoesSocketExist(Row->AttachBone);
    if(!bAttachValid) Notes.Add(FText::Format(LOCTEXT("MissingBone","Attachment '{0}' is absent from this body rig."),FText::FromName(Row->AttachBone)));
    if(!Row->LocalTransform.IsValid()) Notes.Add(LOCTEXT("InvalidTransform","Invalid fitting transform; clothing is hidden."));
    Part->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);Part->SetStaticMesh(Row->Mesh);Part->EmptyOverrideMaterials();
    if(bAttachValid && Row->LocalTransform.IsValid()) {
        Part->AttachToComponent(Body,FAttachmentTransformRules::KeepRelativeTransform,Row->AttachBone);
        ResolvedFit=MCCosmeticFit::ResolveAttachmentTransform(Body->GetSkeletalMeshAsset(),Row->AttachBone,Row->LocalTransform,Row->bMeshSpaceFit);
        if(ResolvedFit.IsValid()) Part->SetRelativeTransform(ResolvedFit);
    }
    for(int32 Index=0;Index<Row->Materials.Num();++Index) {
        if(Row->Materials[Index]) Part->SetMaterial(Index,Row->Materials[Index]);
        else Notes.Add(FText::Format(LOCTEXT("MissingOverride","Material override {0} is empty; using mesh material."),FText::AsNumber(Index)));
    }
    for(int32 Index=0;Index<Part->GetNumMaterials();++Index)
        if(!Part->GetMaterial(Index)) Notes.Add(FText::Format(LOCTEXT("MissingSurface","Clothing material slot {0} is missing."),FText::AsNumber(Index)));
    Part->SetVisibility(Row->Mesh && bAttachValid && Row->LocalTransform.IsValid() && ResolvedFit.IsValid());
    Part->UpdateComponentToWorld();Part->UpdateBounds();MCCosmeticFit::UpdateDeformation(Part,Body);
    if(Row->Icon.IsNull()) Notes.Add(LOCTEXT("MissingIcon","Product icon is missing; fitted previews do not replace the required icon."));
    if(!Row->bEnabled) Notes.Add(LOCTEXT("DisabledRow","This item is disabled in runtime data; its fit remains editable."));
    FText CommerceError;if(Row->bUseCanonicalCommerce && !UMCCosmeticCatalog::ValidateCommerce(*Row,CommerceError)) Notes.Add(CommerceError);
    const int32 Cost=UMCCosmeticCatalog::EffectivePrice(*Row);
    const FText Price=Row->bUseCanonicalCommerce?(Cost>=0?FText::Format(LOCTEXT("Price","Computed cost: {0}."),FText::AsNumber(Cost))
        :LOCTEXT("InvalidPrice","Computed cost unavailable; correct the commerce fields."))
        :LOCTEXT("LegacyPrice","Offer metadata remains in the linked legacy catalog until explicitly migrated.");
    Diagnostics=FText::Format(LOCTEXT("Status","{0} | {1} | {2}\n{3}\n{4}"),FText::FromName(RowName),FText::FromName(Row->Slot),
        FText::FromName(Row->AttachBone),Price,Notes.IsEmpty()?LOCTEXT("FitReady","Mesh and attachment resolved. Reference pose; orbit to inspect front and side.")
            :FText::Join(FText::AsCultureInvariant(TEXT(" ")),Notes)).ToString();
    Invalidate();
}
void SMCClothingFitViewport::AdvancePreview(float DeltaSeconds)
{
    RefreshAge+=FMath::Max(0.f,DeltaSeconds);
    if(RefreshAge>=.2f) {
        UDataTable* Table=nullptr;FName RowName;const FMCCosmeticCatalogRow* Row=nullptr;
        const bool Found=ResolveRow(Table,RowName,Row);
        if(!Found || Table!=SourceTable.Get() || RowName!=SourceRow
            || MCClothingFitViewportPrivate::ExportRow(*Row,Table)!=RowExport) bRefreshRequested=true;
        RefreshAge=0;
    }
    if(bBodyRefreshRequested) {bBodyRefreshRequested=false;RebuildBody();bRefreshRequested=true;}
    if(bRefreshRequested) RefreshRow();
    if(bFrameRequested && FitViewportClient && FitViewportClient->Viewport
        && FitViewportClient->Viewport->GetSizeXY().GetMin()>0) {bFrameRequested=false;SetViewPreset(ViewPreset);}
}
FText SMCClothingFitViewport::GetDiagnosticsText() const {return FText::FromString(Diagnostics);}
FBox SMCClothingFitViewport::GetDisplayBounds() const
{
    FBox Bounds(ForceInit);
    // Framing uses reference geometry before the runtime/WPO culling extensions.
    // The actual component/render bounds stay unchanged.
    if(const auto* Body=DisplayBody.Get();Body && Body->GetSkeletalMeshAsset())
        Bounds+=Body->GetSkeletalMeshAsset()->GetImportedBounds().TransformBy(Body->GetComponentTransform()).GetBox();
    if(const auto* Part=DisplayClothing.Get();Part && Part->IsVisible() && Part->GetStaticMesh()) {
        const auto* RenderData=Part->GetStaticMesh()->GetRenderData();
        Bounds+=(RenderData?RenderData->Bounds.TransformBy(Part->GetComponentTransform()).GetBox():Part->Bounds.GetBox());
    }
    return Bounds;
}
void SMCClothingFitViewport::Focus() {SetViewPreset(ViewPreset);}
void SMCClothingFitViewport::SetViewPreset(int32 Preset)
{
    ViewPreset=FMath::Clamp(Preset,0,2);const FBox Bounds=GetDisplayBounds();
    if(!FitViewportClient || !Bounds.IsValid) {bFrameRequested=true;return;}
    const FVector Direction=ViewPreset==1?FVector(0,1,.15):ViewPreset==2?FVector(-1,0,.15):FVector(1,0,.15);
    bFrameRequested=!FitViewportClient->FrameClothingBounds(Bounds,(-Direction).Rotation());Invalidate();
}
bool SMCClothingFitViewport::Matches(const FString& TablePath,FName RowName) const
{
    UDataTable* Table=nullptr;FName ActualRow;const FMCCosmeticCatalogRow* Row=nullptr;
    return ResolveRow(Table,ActualRow,Row) && Table->GetPathName()==TablePath && ActualRow==RowName;
}
TSharedRef<FJsonObject> SMCClothingFitViewport::Inspect() const
{
    auto Result=MakeShared<FJsonObject>();UDataTable* Table=nullptr;FName RowName;const FMCCosmeticCatalogRow* Row=nullptr;
    const bool Live=ResolveRow(Table,RowName,Row);
    const auto* Body=DisplayBody.Get();const auto* Part=DisplayClothing.Get();
    const bool Synced=Live && RowExport==MCClothingFitViewportPrivate::ExportRow(*Row,Table) && !bRefreshRequested && !bBodyRefreshRequested;
    Result->SetBoolField(TEXT("valid"),Live && Synced && bRowValid && bAttachValid && Body && Part && Part->IsVisible());
    Result->SetBoolField(TEXT("rowLive"),Live);Result->SetBoolField(TEXT("synchronized"),Synced);
    Result->SetStringField(TEXT("table"),MCClothingFitViewportPrivate::ObjectPath(Table));Result->SetStringField(TEXT("row"),RowName.ToString());
    Result->SetBoolField(TEXT("tableDirty"),Table && Table->GetOutermost()->IsDirty());
    Result->SetStringField(TEXT("appearance"),GetAppearancePath());Result->SetStringField(TEXT("pose"),TEXT("Actual body rig reference pose; no gameplay animator instantiated"));
    Result->SetStringField(TEXT("world"),MCClothingFitViewportPrivate::ObjectPath(PreviewScene?PreviewScene->GetWorld():nullptr));
    Result->SetBoolField(TEXT("isolatedEditorPreviewWorld"),PreviewScene && PreviewScene->GetWorld()->WorldType==EWorldType::EditorPreview);
    Result->SetStringField(TEXT("bodyMesh"),MCClothingFitViewportPrivate::ObjectPath(Body?Body->GetSkeletalMeshAsset():nullptr));
    Result->SetStringField(TEXT("bodyComponent"),MCClothingFitViewportPrivate::ObjectPath(Body));
    Result->SetStringField(TEXT("clothingComponent"),MCClothingFitViewportPrivate::ObjectPath(Part));
    Result->SetBoolField(TEXT("registered"),Body && Part && Body->IsRegistered() && Part->IsRegistered());
    Result->SetArrayField(TEXT("bodyMaterials"),MCClothingFitViewportPrivate::MaterialPaths(Body));
    Result->SetArrayField(TEXT("clothingMaterials"),MCClothingFitViewportPrivate::MaterialPaths(Part));
    Result->SetStringField(TEXT("bodyTransform"),Body?Body->GetComponentTransform().ToString():FString());
    Result->SetStringField(TEXT("resolvedFit"),ResolvedFit.ToString());
    Result->SetStringField(TEXT("clothingWorldTransform"),Part?Part->GetComponentTransform().ToString():FString());
    Result->SetBoolField(TEXT("attachedToActualBody"),Part && Part->GetAttachParent()==Body);
    Result->SetStringField(TEXT("bounds"),GetDisplayBounds().ToString());Result->SetStringField(TEXT("diagnostics"),Diagnostics);
    Result->SetStringField(TEXT("bodyRenderBounds"),Body?Body->Bounds.GetBox().ToString():FString());
    Result->SetStringField(TEXT("clothingRenderBounds"),Part?Part->Bounds.GetBox().ToString():FString());
    const FIntPoint Pixels=FitViewportClient && FitViewportClient->Viewport?FitViewportClient->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    Result->SetNumberField(TEXT("viewportWidth"),Pixels.X);Result->SetNumberField(TEXT("viewportHeight"),Pixels.Y);
    Result->SetStringField(TEXT("cameraLocation"),FitViewportClient?FitViewportClient->GetViewLocation().ToString():FString());
    Result->SetStringField(TEXT("framePolicy"),TEXT("80% projection fit of actual reference geometry; culling extensions excluded; explicit preset/first-valid request only"));
    if(Live) {
        Result->SetStringField(TEXT("rowExport"),MCClothingFitViewportPrivate::ExportRow(*Row,Table));
        Result->SetStringField(TEXT("mesh"),MCClothingFitViewportPrivate::ObjectPath(Row->Mesh));
        Result->SetStringField(TEXT("attachBone"),Row->AttachBone.ToString());Result->SetBoolField(TEXT("meshSpaceFit"),Row->bMeshSpaceFit);
        Result->SetStringField(TEXT("authoredTransform"),Row->LocalTransform.ToString());
        Result->SetBoolField(TEXT("iconMissing"),Row->Icon.IsNull());Result->SetStringField(TEXT("icon"),Row->Icon.ToSoftObjectPath().ToString());
        Result->SetNumberField(TEXT("computedCost"),UMCCosmeticCatalog::EffectivePrice(*Row));
    }
    return Result;
}
bool SMCClothingFitViewport::Capture(const FString& OutputFile)
{
    FString Filename=FPaths::ConvertRelativePathToFull(OutputFile);FPaths::NormalizeFilename(Filename);
    if(!IsInGameThread() || !FPaths::CollapseRelativeDirectories(Filename) || !Filename.StartsWith(TEXT("E:/"),ESearchCase::IgnoreCase)
        || !Filename.EndsWith(TEXT(".png"),ESearchCase::IgnoreCase) || IFileManager::Get().FileExists(*Filename)
        || !Inspect()->GetBoolField(TEXT("synchronized"))) return false;
    const auto Target=GetSceneViewport();if(!Target) return false;
    const FIntPoint Size=Target->GetSizeXY();if(Size.GetMin()<=0 || Size.GetMax()>8192) return false;
    TArray<FColor> Pixels;if(!Target->ReadPixels(Pixels) || Pixels.Num()!=int64(Size.X)*Size.Y) return false;
    TArray64<uint8> PNG;if(!FImageUtils::CompressImage(PNG,TEXT("PNG"),FImageView(Pixels.GetData(),Size.X,Size.Y,EGammaSpace::sRGB))) return false;
    return IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true) && FFileHelper::SaveArrayToFile(PNG,*Filename);
}
TSharedPtr<SMCClothingFitViewport> SMCClothingFitViewport::FindOpen(const FString& TablePath,FName RowName,FString& OutError)
{
    TSharedPtr<SMCClothingFitViewport> Found;
    for(const auto& Entry:MCClothingFitViewportPrivate::OpenViews) if(const auto Pinned=Entry.Pin();Pinned && Pinned->Matches(TablePath,RowName)) {
        if(Found) {OutError=TEXT("Multiple open fitting viewports match this exact table/row; no arbitrary view is selected.");return nullptr;}
        Found=Pinned;
    }
    if(!Found) OutError=TEXT("No existing fitting viewport matches the exact table object path and selected row.");
    return Found;
}
#undef LOCTEXT_NAMESPACE
