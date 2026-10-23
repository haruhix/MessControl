#include "MCWardrobePreviewActor.h"

#include "MCToothCharacter.h"
#include "MCCharacterCosmeticsComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "Camera/CameraTypes.h"
#include "Blueprint/UserWidget.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"

AMCWardrobePreviewActor::AMCWardrobePreviewActor()
{
    bReplicates=false;
    SetReplicateMovement(false);
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
    auto* Stage=CreateDefaultSubobject<USceneComponent>(TEXT("WardrobeDisplayRoot"));
    SetRootComponent(Stage);
    Body=CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("WardrobeDisplayBody"));
    Body->SetupAttachment(Stage);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore);
    Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);
    Body->SetOnlyOwnerSee(true);
    Body->bCastHiddenShadow=false;
    Body->PrimaryComponentTick.bStartWithTickEnabled=false;
}

bool AMCWardrobePreviewActor::IsPreparedFor(const AMCToothCharacter* Source) const
{
    return IsValid(Source) && SourceCharacter.Get()==Source && Body && Body->GetSkinnedAsset()
        && Body->GetSkinnedAsset()==Source->GetMesh()->GetSkinnedAsset();
}

bool AMCWardrobePreviewActor::Prepare(APlayerController* Controller,AMCToothCharacter* Source)
{
    if(!IsValid(Controller) || !Controller->IsLocalController() || !IsValid(Source)
        || Source->GetWorld()!=GetWorld() || Controller->GetWorld()!=GetWorld()
        || !Source->GetMesh() || !Source->GetMesh()->GetSkeletalMeshAsset()) return false;
    EndPresentation();
    LocalController=Controller;SourceCharacter=Source;SetOwner(Controller);
    SetActorTransform(Source->GetActorTransform());
    Body->SetSkinnedAssetAndUpdate(Source->GetMesh()->GetSkeletalMeshAsset());
    Body->SetRelativeTransform(Source->StandingMeshTransform());
    Body->EmptyOverrideMaterials();BodyMaterials.Reset();BagMaterial=nullptr;
    for(int32 Index=0;Index<Source->GetMesh()->GetNumMaterials();++Index) {
        auto* Original=Source->GetMesh()->GetMaterial(Index);
        if(!Original) continue;
        // UE accepts only a material or constant instance as a MID parent. Preserve the nearest
        // authored constant instance (including static switches), then copy the hero's live overrides.
        UMaterialInterface* StaticParent=Original;
        while(const auto* Dynamic=Cast<UMaterialInstanceDynamic>(StaticParent)) StaticParent=Dynamic->Parent;
        if(!StaticParent) return false;
        // Every override belongs to the display. No fitting-room operation touches the real hero's MID.
        auto* DisplayMaterial=UMaterialInstanceDynamic::Create(StaticParent,this);
        DisplayMaterial->CopyMaterialUniformParameters(Original);
        Body->SetMaterial(Index,DisplayMaterial);BodyMaterials.Add(DisplayMaterial);
        for(const UMaterialInterface* Material=Original;Material;) {
            if(Material->GetName().Contains(TEXT("MI_Bag"))) {BagMaterial=DisplayMaterial;break;}
            const auto* Instance=Cast<UMaterialInstance>(Material);
            Material=Instance?Instance->Parent.Get():nullptr;
        }
    }
    CopyDisplayPose();
    for(auto& Entry:AccessoryParts) if(IsValid(Entry.Value)) Entry.Value->DestroyComponent();
    AccessoryParts.Reset();
    auto* Catalog=Source->Cosmetics?Source->Cosmetics->GetCatalog():UMCCosmeticCatalog::GetDefaultCatalog();
    if(Catalog) for(const FName ItemId:Catalog->GetRowNames()) {
        const auto* Row=UMCCosmeticCatalog::Find(Catalog,ItemId);
        if(!Row || !Row->bEnabled || !Row->Mesh || !Body->DoesSocketExist(Row->AttachBone)) continue;
        auto* Part=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient);
        Part->SetStaticMesh(Row->Mesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);Part->SetSimulatePhysics(false);Part->SetIsReplicated(false);
        Part->SetOnlyOwnerSee(true);Part->bCastHiddenShadow=false;
        Part->SetupAttachment(Body,Row->AttachBone);
        FTransform Fit=Row->LocalTransform;
        if(Row->bMeshSpaceFit) {
            const auto& Ref=Source->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
            FTransform ReferenceBone=FTransform::Identity;
            for(int32 Bone=Ref.FindBoneIndex(Row->AttachBone);Bone>=0;Bone=Ref.GetParentIndex(Bone))
                ReferenceBone=ReferenceBone*Ref.GetRefBonePose()[Bone];
            Fit=Fit.GetRelativeTransform(ReferenceBone);
        }
        Part->SetRelativeTransform(Fit);
        for(int32 Index=0;Index<Row->Materials.Num();++Index) if(Row->Materials[Index]) Part->SetMaterial(Index,Row->Materials[Index]);
        Part->SetVisibility(false);AddInstanceComponent(Part);Part->RegisterComponent();
        AccessoryParts.Add(ItemId,Part);
    }
    SetDraftColor(Source->GetPlayerColor());
    SetDraftLoadout(Source->Cosmetics?Source->Cosmetics->EquippedLoadout:FMCCharacterCosmeticLoadout());
    SetActorTickEnabled(false);
    return true;
}

void AMCWardrobePreviewActor::CopyDisplayPose()
{
    auto* Source=SourceCharacter.Get();
    if(!Source || !Source->GetMesh() || Source->GetMesh()->GetBoneSpaceTransforms().IsEmpty()) return;
    // Copy the existing artist rig's evaluated standing pose once, not a T-pose or a second ragdoll.
    Body->CopyPoseFromSkeletalComponent(Source->GetMesh());
    Body->ActiveMorphTargets=Source->GetMesh()->ActiveMorphTargets;
    Body->MorphTargetWeights=Source->GetMesh()->MorphTargetWeights;
    Body->RefreshBoneTransforms();
    Body->MarkRenderDynamicDataDirty();
}

bool AMCWardrobePreviewActor::BeginPresentation(const AActor* Booth)
{
    auto* Controller=LocalController.Get();auto* Source=SourceCharacter.Get();
    if(!Controller || !Controller->IsLocalController() || !IsPreparedFor(Source) || !IsValid(Booth)) return false;
    if(bPresenting) return true;
    PreviousViewTarget=Controller->GetViewTarget();bPreviousAutoManageCamera=Controller->bAutoManageActiveCameraTarget;
    // The authored booth's +Y faces its open approach (the prompt lies on the opposite side).
    // Use that stable frame, not whichever side of the interaction OBB the real player entered.
    CameraFront=Booth->GetActorRightVector().GetSafeNormal2D();
    if(CameraFront.IsNearlyZero()) return false;
    SetActorRotation(CameraFront.Rotation());
    SetActorScale3D(Source->GetActorScale3D());
    Body->SetRelativeTransform(Source->StandingMeshTransform());
    for(int32 Index=0;Index<Source->GetMesh()->GetNumMaterials();++Index)
        if(auto* DisplayMaterial=Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Index)))
            if(auto* Current=Source->GetMesh()->GetMaterial(Index)) DisplayMaterial->CopyMaterialUniformParameters(Current);
    CopyDisplayPose();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCWardrobeStage),false,this);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Query.AddIgnoredActor(*It);
    const auto* Capsule=Source->GetCapsuleComponent();
    const float Radius=Capsule->GetScaledCapsuleRadius();
    bool bPlaced=false;
    for(int32 Step=0;Step<6;++Step) {
        const FVector Approach=Booth->GetActorLocation()+CameraFront*(Radius*(3.f+Step*2.f));
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Approach+FVector(0,0,500),Approach-FVector(0,0,2000),ECC_Visibility,Query)
            || Floor.ImpactNormal.Z<.7f) continue;
        const FBoxSphereBounds Bounds=Body->CalcBounds(Body->GetComponentTransform());
        const FVector VisualOffset=Bounds.Origin-GetActorLocation();
        const double FootOffset=GetActorLocation().Z-(Bounds.Origin.Z-Bounds.BoxExtent.Z);
        FVector Stage=Approach-FVector(VisualOffset.X,VisualOffset.Y,0);
        Stage.Z=Floor.ImpactPoint.Z+FootOffset+FMath::Max(.1,Bounds.BoxExtent.Z*.015);
        // Imported visual pivots need not match the gameplay capsule. Validate the actual display volume.
        if(GetWorld()->OverlapBlockingTestByChannel(Stage+VisualOffset,FQuat::Identity,ECC_Camera,
            FCollisionShape::MakeBox(Bounds.BoxExtent*.9),Query)) continue;
        SetActorLocation(Stage);bPlaced=true;break;
    }
    if(!bPlaced) return false;
    bPresenting=true;bSettledPoseCopied=false;PoseSettleAge=0;VisibilityRefreshAge=0;
    HideRealPlayersForThisView();
    SetActorHiddenInGame(false);SetActorTickEnabled(true);
    Controller->bAutoManageActiveCameraTarget=false;
    Controller->SetViewTarget(this);
    const FBoxSphereBounds DisplayBounds=Body->CalcBounds(Body->GetComponentTransform());
    UE_LOG(LogTemp,Display,TEXT("MC_WARDROBE_PREVIEW_STAGE actor=%s front=%s boundsCenter=%s boundsExtent=%s"),
        *GetActorLocation().ToString(),*CameraFront.ToString(),*DisplayBounds.Origin.ToString(),*DisplayBounds.BoxExtent.ToString());
    UE_LOG(LogTemp,Display,TEXT("MC_WARDROBE_PREVIEW_OPEN pc=%s source=%s preview=%s replicated=%d hiddenActors=%d"),
        *GetNameSafe(Controller),*GetNameSafe(Source),*GetName(),GetIsReplicated()?1:0,AddedHiddenActors.Num());
    return true;
}

void AMCWardrobePreviewActor::SetDraftColor(FLinearColor Color)
{
    if(!BagMaterial || !FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) return;
    Color=Color.GetClamped(0,1);Color.A=1;
    BagMaterial->SetScalarParameterValue(TEXT("Saturate"),1.f);
    BagMaterial->SetVectorParameterValue(TEXT("Albedo Color"),Color);
}

void AMCWardrobePreviewActor::SetDraftLoadout(const FMCCharacterCosmeticLoadout& Loadout)
{
    auto* Source=SourceCharacter.Get();
    auto* Catalog=Source && Source->Cosmetics?Source->Cosmetics->GetCatalog():nullptr;
    FText Error;if(!Catalog || !UMCCosmeticCatalog::ValidateLoadout(Catalog,Loadout,Error)) return;
    for(auto& Entry:AccessoryParts) if(Entry.Value) {
        const auto* Row=UMCCosmeticCatalog::Find(Catalog,Entry.Key);
        Entry.Value->SetVisibility(Row && Loadout.ItemForSlot(Row->Slot)==Entry.Key);
    }
}

void AMCWardrobePreviewActor::HideActorForThisView(AActor* Actor)
{
    auto* Controller=LocalController.Get();if(!Controller || !IsValid(Actor) || Actor==this) return;
    if(!Controller->HiddenActors.Contains(Actor)) {Controller->HiddenActors.Add(Actor);AddedHiddenActors.Add(Actor);}
    // Screen-space WidgetComponents render through a Slate player layer, outside the primitive HiddenActors pass.
    TInlineComponentArray<UWidgetComponent*> Widgets(Actor);
    for(auto* Component:Widgets) if(Component && Component->GetWidgetSpace()==EWidgetSpace::Screen) {
        auto* Widget=Component->GetUserWidgetObject();if(!Widget) continue;
        if(!HiddenWidgetOpacity.Contains(Widget)) HiddenWidgetOpacity.Add(Widget,Widget->GetRenderOpacity());
        Widget->SetRenderOpacity(0.f);
    }
}

void AMCWardrobePreviewActor::HideRealPlayersForThisView()
{
    if(!bPresenting || !LocalController.IsValid()) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        HideActorForThisView(*It);
        TArray<AActor*> Attached;It->GetAttachedActors(Attached,true,true);
        for(auto* Actor:Attached) HideActorForThisView(Actor);
        // Held tools need not have reached their attachment replication yet.
        HideActorForThisView(It->EquippedBrush);HideActorForThisView(It->HeldFood);
        if(It->FoodCollection) for(const auto& Piece:It->FoodCollection->Pieces) HideActorForThisView(Piece.Get());
    }
}

void AMCWardrobePreviewActor::RestoreViewVisibility()
{
    if(auto* Controller=LocalController.Get()) for(const auto& Actor:AddedHiddenActors)
        if(Actor.IsValid()) Controller->HiddenActors.Remove(Actor.Get());
    AddedHiddenActors.Reset();
    for(const auto& Entry:HiddenWidgetOpacity) if(auto* Widget=Entry.Key.Get()) Widget->SetRenderOpacity(Entry.Value);
    HiddenWidgetOpacity.Reset();
}

void AMCWardrobePreviewActor::EndPresentation()
{
    if(bPresenting) if(auto* Controller=LocalController.Get()) {
        // Never steal a camera already changed by travel, death or another presentation.
        if(Controller->GetViewTarget()==this) {
            auto* Target=PreviousViewTarget.Get();
            if(!IsValid(Target) || Target->GetWorld()!=GetWorld()
                || (Target==SourceCharacter.Get() && Controller->GetPawn()!=SourceCharacter.Get())) Target=Controller->GetPawn();
            if(Target) Controller->SetViewTarget(Target);
        }
        // This flag was overridden by our presentation even when travel or death already changed the target.
        Controller->bAutoManageActiveCameraTarget=bPreviousAutoManageCamera;
        UE_LOG(LogTemp,Display,TEXT("MC_WARDROBE_PREVIEW_CLOSE pc=%s preview=%s restoreActors=%d restoreWidgets=%d"),
            *GetNameSafe(Controller),*GetName(),AddedHiddenActors.Num(),HiddenWidgetOpacity.Num());
    }
    bPresenting=false;PreviousViewTarget.Reset();RestoreViewVisibility();
    SetActorHiddenInGame(true);SetActorTickEnabled(false);
}

void AMCWardrobePreviewActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!bPresenting) return;
    if(!SourceCharacter.IsValid() || !LocalController.IsValid() || LocalController->GetPawn()!=SourceCharacter.Get()) {EndPresentation();return;}
    PoseSettleAge+=DeltaSeconds;
    if(!bSettledPoseCopied && PoseSettleAge>=.28f) {CopyDisplayPose();bSettledPoseCopied=true;}
    // Four players are cheap to scan; do it before every draw so a late join or newly attached tool cannot flash in the fitting view.
    HideRealPlayersForThisView();
}

void AMCWardrobePreviewActor::CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult)
{
    int32 Width=1280,Height=720;
    if(const auto* Controller=LocalController.Get()) Controller->GetViewportSize(Width,Height);
    const float Aspect=Height>0?FMath::Clamp(float(Width)/Height,.7f,3.f):16.f/9.f;
    const float HalfTan=FMath::Tan(FMath::DegreesToRadians(HorizontalFieldOfView*.5f));
    const FBoxSphereBounds Bounds=Body->CalcBounds(Body->GetComponentTransform());
    // Hub avatars are scaled down. World-centimetre floors such as 80/250 would put the camera inside scenery.
    const float ModelHeight=FMath::Max(.5f,float(Bounds.BoxExtent.Z*2));
    const float HorizontalRadius=float(FMath::Abs(CameraFront.Y)*Bounds.BoxExtent.X+FMath::Abs(CameraFront.X)*Bounds.BoxExtent.Y);
    const float SafeHalfWidth=2.f*FMath::Max(.1f,FMath::Min(PreviewScreenX-.03f,.62f-PreviewScreenX));
    const float Distance=FMath::Max3(ModelHeight*.5f,ModelHeight*Aspect/(2*HalfTan*PreviewHeightFraction),HorizontalRadius/(HalfTan*SafeHalfWidth));
    const FVector Center=Bounds.Origin;
    const FVector BaseEye=Center+CameraFront*Distance+FVector(0,0,ModelHeight*.08f);
    const FRotator ViewRotation=(Center-BaseEye).Rotation();
    const FVector Right=ViewRotation.Quaternion().GetRightVector();
    const float Shift=(.5f-PreviewScreenX)*2*Distance*HalfTan;
    OutResult.Location=BaseEye+Right*Shift;OutResult.Rotation=ViewRotation;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCWardrobeCamera),false,this);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Query.AddIgnoredActor(*It);
    FHitResult CameraBlock;
    if(GetWorld()->SweepSingleByChannel(CameraBlock,Center,OutResult.Location,FQuat::Identity,ECC_Camera,
        FCollisionShape::MakeSphere(FMath::Max(.25f,ModelHeight*.025f)),Query))
        OutResult.Location=FMath::Lerp(Center,OutResult.Location,FMath::Clamp(CameraBlock.Time-.02f,.1f,1.f));
    OutResult.FOV=HorizontalFieldOfView;OutResult.AspectRatio=Aspect;OutResult.bConstrainAspectRatio=false;
    OutResult.ProjectionMode=ECameraProjectionMode::Perspective;
}

void AMCWardrobePreviewActor::EndPlay(const EEndPlayReason::Type Reason)
{
    EndPresentation();Super::EndPlay(Reason);
}
