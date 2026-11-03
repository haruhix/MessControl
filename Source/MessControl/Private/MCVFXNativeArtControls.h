#pragma once
// Renderer programs remain native; only declared art inputs vary.
#include "MCVFXNativePresentationSettings.h"
#include "MCNutCombatVisuals.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "NiagaraSystem.h"

namespace MCNativeArtControls
{
inline bool Enabled(const FMCVFXNativePartSettings* P){return !P||P->bEnabled;}
inline float Curve(const TSoftObjectPtr<UCurveFloat>& Ref,float Phase,float Default)
{
    const auto* C=Ref.Get();if(!C)return Default; // only already prepared/private curve data
    const float V=C->GetFloatValue(FMath::Clamp(Phase,0.f,1.f));return FMath::IsFinite(V)?FMath::Clamp(V,0.f,2.f):Default;
}
inline float Opacity(const FMCVFXNativePartSettings* P,float Age,float Seconds,float Original)
{
    if(!P)return Original;if(!P->bEnabled)return 0;
    return Original*P->OpacityScale*Curve(P->OpacityCurve,Age/FMath::Max(.001f,Seconds),1);
}
inline float Expansion(const FMCVFXNativePartSettings* P,float Age,float Seconds)
{return P?Curve(P->ExpansionCurve,Age/FMath::Max(.001f,Seconds),1):1;}
inline UNiagaraSystem* System(const FMCVFXNativePartSettings* P,const TSoftObjectPtr<UNiagaraSystem>& Legacy)
{return P?(P->bEnabled?P->System.Get():nullptr):Legacy.LoadSynchronous();}
inline UStaticMesh* Mesh(const FMCVFXNativePartSettings* P,const TCHAR* Legacy)
{return P?(P->bEnabled?P->Mesh.Get():nullptr):LoadObject<UStaticMesh>(nullptr,Legacy);}
inline float SystemScale(const FMCVFXNativePartSettings* P,float Legacy){return P?P->SystemScale:Legacy;}

/** Exact original four-ribbon program, parameterized instead of replaced with generic VFX.
 * R/G remain seed/hotness. Opacity applies ONLY to A.
 */
inline void RenderBurstFlames(UProceduralMeshComponent* Component,const FMCVFXNativePartSettings* Part,
    float Radius,float Age,float NativeSeconds,float Strength,FVector Tail=FVector::ZeroVector)
{
    using namespace MCNutCombatVisuals;
    if(!Part){RenderFlames(Component,Radius,Age,Strength,Tail);return;}
    if(!Part->bEnabled){Component->SetVisibility(false);return;}
    const auto& S=Part->BurstFlame;FMesh Data;
    for(int32 Ribbon=0;Ribbon<S.Ribbons;++Ribbon) {
        const float Phase=Ribbon*2.39996f;
        const FVector Right(FMath::Cos(Phase),FMath::Sin(Phase),0),Normal=FVector::CrossProduct(Right,FVector::UpVector);
        const int32 First=Data.Points.Num();
        for(int32 Row=0;Row<=S.Rows;++Row) {
            const float T=Row/float(S.Rows),Width=Radius*(S.BaseWidth-S.TipWidthLoss*T);
            const FVector P=FVector(0,0,-Radius*.4f+Radius*S.HeightRadii*T)+Tail*T*T
                +Right*(FMath::Sin(T*S.SwayFrequency-Age*S.SwaySpeed+Phase)*Radius*S.SwayRadii*T);
            for(int32 Side=0;Side<2;++Side) {
                Data.Points.Add(P+Right*((Side*2-1)*Width));Data.Normals.Add(Normal);
                Data.UV.Add(FVector2D(float(Side),T));
                Data.Colors.Add(FLinearColor(FMath::Frac(Phase/(2*PI)),.9f,0,Opacity(Part,Age,NativeSeconds,Strength)));
                Data.Tangents.Add(FProcMeshTangent(Right,false));
            }
            if(Row>0){const int32 V=First+(Row-1)*2;Data.Indices.Append({V,V+1,V+3,V,V+3,V+2});}
        }
    }
    Data.Upload(Component);
}
}
