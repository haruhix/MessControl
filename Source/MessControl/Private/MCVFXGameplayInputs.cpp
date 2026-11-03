#include "MCVFXGameplayInputs.h"
#include "Materials/MaterialInterface.h"
#include "MCVFXNativeInputs.h"
#include "MCFreezeCapturedGeometry.h"
#include "MCNiagaraMaterialContextComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
bool FindInputType(FName Name,FNiagaraTypeDefinition& Out)
{
    // Niagara names use FName's case-insensitive identity. Display-case aliases
    // must not bypass read-only protection while addressing the same slot.
    const FName Key=Name;
    if(Key==FName(TEXT("User.BodyRadius")) || Key==FName(TEXT("User.BodyHalfHeight")) || Key==FName(TEXT("User.SubjectVisible"))
        || Key==FName(TEXT("User.ContactDelay")) || Key==FName(TEXT("User.StateAge"))) {Out=FNiagaraTypeDefinition::GetFloatDef();return true;}
    if(Key==FName(TEXT("User.ScreenOrientationLocal")) || Key==FName(TEXT("User.ProjectedDirectionOrientationLocal"))) {Out=FNiagaraTypeDefinition::GetQuatDef();return true;}
    if(Key==FName(TEXT("User.ScreenRightLocal")) || Key==FName(TEXT("User.ScreenUpLocal")) || Key==FName(TEXT("User.ScreenViewLocal"))
        || Key==FName(TEXT("User.WorldUpLocal")) || Key==FName(TEXT("User.SubjectOffsetLocal")) || Key==FName(TEXT("User.WorldDirectionLocal"))
        || Key==FName(TEXT("User.ProjectedDirectionLocal")) || Key==FName(TEXT("User.ProjectedSideLocal")))
    {Out=FNiagaraTypeDefinition::GetVec3Def();return true;}
    if(Key==FName(TEXT("User.ToolPartCount"))) {Out=FNiagaraTypeDefinition::GetIntDef();return true;}
    if(Key==FName(TEXT("User.EchoOverhead"))||Key==FName(TEXT("User.SurfacePreparing"))) {Out=FNiagaraTypeDefinition::GetBoolDef();return true;}
    if(Key==FName(TEXT("User.SurfaceTriangleCapacity"))) {Out=FNiagaraTypeDefinition::GetIntDef();return true;}
    if(Key==FName(TEXT("User.RiverSourceMesh"))||Key==FName(TEXT("User.RiverMesh"))) {Out=FNiagaraTypeDefinition(UStaticMesh::StaticClass());return true;}
    if(Key==FName(TEXT("User.RiverMaterial"))||Key==FName(TEXT("User.RiverOriginalMaterial"))) {Out=FNiagaraTypeDefinition(UMaterialInterface::StaticClass());return true;}
    if(Key==FName(TEXT("User.VisualSubject"))) {Out=FNiagaraTypeDefinition(USceneComponent::StaticClass());return true;}
    if(Key==FName(TEXT("User.MaterialTarget"))) {Out=FNiagaraTypeDefinition(UMaterialInstanceDynamic::StaticClass());return true;}
    if(Key==FName(TEXT("User.MaterialContextResources"))) {Out=FNiagaraTypeDefinition(UMCNiagaraMaterialContextResources::StaticClass());return true;}
    for(int32 I=0;I<3;++I) {
        if(Key==FName(*FString::Printf(TEXT("User.ToolMesh%d"),I))) {Out=FNiagaraTypeDefinition(UStaticMesh::StaticClass());return true;}
        if(Key==FName(*FString::Printf(TEXT("User.ToolOrientation%d"),I))) {Out=FNiagaraTypeDefinition::GetQuatDef();return true;}
        if(Key==FName(*FString::Printf(TEXT("User.ToolPosition%d"),I)) || Key==FName(*FString::Printf(TEXT("User.ToolScale%d"),I)))
        {Out=FNiagaraTypeDefinition::GetVec3Def();return true;}
    }
    return false;
}
bool Has(const UNiagaraSystem* System,FName Name,const FNiagaraTypeDefinition& Type)
{return System && System->GetExposedParameters().IndexOf(FNiagaraVariable(Type,Name))!=INDEX_NONE;}
}

bool MCVFXGameplayInputs::IsReadOnly(FName Name)
{FNiagaraTypeDefinition Type;return MCVFXNativeInputs::IsNativeName(Name)||FindInputType(Name,Type);}

bool MCVFXGameplayInputs::ValidateSystem(const UNiagaraSystem* System,FString& Error)
{
    if(!System) {Error=TEXT("Niagara System is unavailable.");return false;}
    if(FMCPreparedFreezeGeometryLayer::UsesCapture(System)
        &&!FMCPreparedFreezeGeometryLayer::ValidateDeclaredInputs(System,Error))return false;
    TArray<FNiagaraVariable> Variables;System->GetExposedParameters().GetParameters(Variables);
    for(const auto& Variable:Variables) {
        FNiagaraTypeDefinition Expected;
        if(FindInputType(Variable.GetName(),Expected) && Variable.GetType()!=Expected) {
            Error=FString::Printf(TEXT("Read-only context input %s must use Niagara type %s."),
                *Variable.GetName().ToString(),*Expected.GetName());return false;
        }
    }
    return true;
}

void MCVFXGameplayInputs::ApplySubjectFrame(UNiagaraComponent* C,const UNiagaraSystem* NS,
    const USceneComponent* Subject,const FTransform& Frame,const FVector& Direction)
{
    if(!C || !NS || C->GetNetMode()==NM_DedicatedServer)return;
    const bool Valid=IsValid(Subject) && IsValid(Subject->GetOwner()) && Subject->GetWorld()==C->GetWorld();
    const FNiagaraVariable SubjectKey(FNiagaraTypeDefinition(USceneComponent::StaticClass()),TEXT("User.VisualSubject"));
    if(NS->GetExposedParameters().IndexOf(SubjectKey)!=INDEX_NONE) {
        // SetVariableObject deliberately uses generic UObject; this declared
        // USceneComponent slot needs its exact Niagara type instead.
        auto& Store=C->GetOverrideParameters();
        if(Store.IndexOf(SubjectKey)==INDEX_NONE)Store.AddParameter(SubjectKey);
        Store.SetUObject(Valid?const_cast<USceneComponent*>(Subject):nullptr,SubjectKey);
    }
    // The prepared local service fills this exact object slot once a candidate
    // is atomically reserved. Never clear an already reserved pointer per tick.
    const FNiagaraVariable ResourceKey(FNiagaraTypeDefinition(UMCNiagaraMaterialContextResources::StaticClass()),TEXT("User.MaterialContextResources"));
    if(NS->GetExposedParameters().IndexOf(ResourceKey)!=INDEX_NONE
        && C->GetOverrideParameters().IndexOf(ResourceKey)==INDEX_NONE)C->GetOverrideParameters().AddParameter(ResourceKey);
    if(!Valid) {
        if(Has(NS,TEXT("User.SubjectVisible"),FNiagaraTypeDefinition::GetFloatDef()))C->SetVariableFloat(TEXT("User.SubjectVisible"),0.f);
        return;
    }
    const auto* Character=Cast<ACharacter>(Subject->GetOwner());
    if(const auto* Capsule=Character?Character->GetCapsuleComponent():nullptr) {
        if(Has(NS,TEXT("User.BodyRadius"),FNiagaraTypeDefinition::GetFloatDef()))C->SetVariableFloat(TEXT("User.BodyRadius"),Capsule->GetScaledCapsuleRadius());
        if(Has(NS,TEXT("User.BodyHalfHeight"),FNiagaraTypeDefinition::GetFloatDef()))C->SetVariableFloat(TEXT("User.BodyHalfHeight"),Capsule->GetScaledCapsuleHalfHeight());
    }
    const bool bFrame=Has(NS,TEXT("User.ScreenOrientationLocal"),FNiagaraTypeDefinition::GetQuatDef())
        || Has(NS,TEXT("User.SubjectOffsetLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.ScreenRightLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.ScreenUpLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.ScreenViewLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.WorldUpLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.SubjectVisible"),FNiagaraTypeDefinition::GetFloatDef())
        || Has(NS,TEXT("User.WorldDirectionLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.ProjectedDirectionLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.ProjectedSideLocal"),FNiagaraTypeDefinition::GetVec3Def())
        || Has(NS,TEXT("User.ProjectedDirectionOrientationLocal"),FNiagaraTypeDefinition::GetQuatDef());
    if(!bFrame)return;
    const bool bFollowsSubject=Has(NS,TEXT("User.SubjectOffsetLocal"),FNiagaraTypeDefinition::GetVec3Def());
    const FVector Origin=bFollowsSubject?Subject->GetOwner()->GetActorLocation():Frame.GetLocation();
    FVector Eye=Origin+FVector(-300,-400,250);
    if(const auto* PC=Subject->GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager)
        Eye=PC->PlayerCameraManager->GetCameraLocation();
    const FVector View=(Eye-Origin).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
    const FVector Right=FVector::CrossProduct(FVector::UpVector,View).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
    const FVector Up=FVector::CrossProduct(View,Right).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    const FQuat Basis=Frame.GetRotation().Inverse()*FRotationMatrix::MakeFromXZ(Right,View).ToQuat();
    if(Has(NS,TEXT("User.ScreenOrientationLocal"),FNiagaraTypeDefinition::GetQuatDef()))C->SetVariableQuat(TEXT("User.ScreenOrientationLocal"),Basis.GetNormalized());
    if(Has(NS,TEXT("User.SubjectOffsetLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.SubjectOffsetLocal"),Frame.InverseTransformPosition(Origin));
    if(Has(NS,TEXT("User.ScreenRightLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.ScreenRightLocal"),Frame.InverseTransformVector(Right));
    if(Has(NS,TEXT("User.ScreenUpLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.ScreenUpLocal"),Frame.InverseTransformVector(Up));
    if(Has(NS,TEXT("User.ScreenViewLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.ScreenViewLocal"),Frame.InverseTransformVector(View));
    if(Has(NS,TEXT("User.WorldUpLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.WorldUpLocal"),Frame.InverseTransformVector(FVector::UpVector));
    if(Has(NS,TEXT("User.SubjectVisible"),FNiagaraTypeDefinition::GetFloatDef()))C->SetVariableFloat(TEXT("User.SubjectVisible"),FVector::DistSquared(Eye,Origin)<=FMath::Square(4000.f)?1.f:0.f);
    const FVector WorldDirection=Direction.GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
    const FVector Projected=(WorldDirection-View*FVector::DotProduct(WorldDirection,View)).GetSafeNormal(UE_SMALL_NUMBER,Right);
    const FVector Side=FVector::CrossProduct(View,Projected).GetSafeNormal(UE_SMALL_NUMBER,Up);
    if(Has(NS,TEXT("User.WorldDirectionLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.WorldDirectionLocal"),Frame.InverseTransformVector(WorldDirection));
    if(Has(NS,TEXT("User.ProjectedDirectionLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.ProjectedDirectionLocal"),Frame.InverseTransformVector(Projected));
    if(Has(NS,TEXT("User.ProjectedSideLocal"),FNiagaraTypeDefinition::GetVec3Def()))C->SetVariableVec3(TEXT("User.ProjectedSideLocal"),Frame.InverseTransformVector(Side));
    if(Has(NS,TEXT("User.ProjectedDirectionOrientationLocal"),FNiagaraTypeDefinition::GetQuatDef()))C->SetVariableQuat(TEXT("User.ProjectedDirectionOrientationLocal"),
        (Frame.GetRotation().Inverse()*FRotationMatrix::MakeFromXY(Projected,Side).ToQuat()).GetNormalized());
}
