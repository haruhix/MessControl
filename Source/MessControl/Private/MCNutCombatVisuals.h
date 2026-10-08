#pragma once

#include "ProceduralMeshComponent.h"

namespace MCNutCombatVisuals
{
struct FMesh
{
    TArray<FVector> Points,Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    void Quad(FVector Center,FVector Right,FVector Up,float HalfWidth,float HalfHeight,FLinearColor Color=FLinearColor::White)
    {
        const int32 First=Points.Num();const FVector Normal=FVector::CrossProduct(Right,Up).GetSafeNormal();
        const FVector2D Corners[]={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};
        for(FVector2D P:Corners) {
            Points.Add(Center+Right*((P.X*2-1)*HalfWidth)+Up*((P.Y*2-1)*HalfHeight));
            Normals.Add(Normal);UV.Add(P);Colors.Add(Color);Tangents.Add(FProcMeshTangent(Right,false));
        }
        Indices.Append({First,First+1,First+2,First,First+2,First+3});
    }
    void Upload(UProceduralMeshComponent* Component,int32 Section=0) const
    {
        const auto* Existing=Component->GetProcMeshSection(Section);
        if(Existing && Existing->ProcVertexBuffer.Num()==Points.Num())
            Component->UpdateMeshSection_LinearColor(Section,Points,Normals,UV,Colors,Tangents,false);
        else Component->CreateMeshSection_LinearColor(Section,Points,Indices,Normals,UV,Colors,Tangents,false,false);
    }
};

inline void RenderFlames(UProceduralMeshComponent* Mesh,float Radius,float Age,float Strength,FVector Tail=FVector::ZeroVector)
{
    FMesh Data;
    constexpr int32 Rows=8;
    for(int32 Ribbon=0;Ribbon<4;++Ribbon) {
        const float Phase=Ribbon*2.39996f;
        const FVector Right(FMath::Cos(Phase),FMath::Sin(Phase),0);
        const FVector Normal=FVector::CrossProduct(Right,FVector::UpVector);
        const int32 First=Data.Points.Num();
        for(int32 Row=0;Row<=Rows;++Row) {
            const float T=Row/float(Rows),Width=Radius*(.80f-.42f*T);
            const FVector P=FVector(0,0,-Radius*.4f+Radius*3.8f*T)+Tail*T*T
                +Right*(FMath::Sin(T*7-Age*8+Phase)*Radius*.20f*T);
            for(int32 Side=0;Side<2;++Side) {
                Data.Points.Add(P+Right*((Side*2-1)*Width));Data.Normals.Add(Normal);
                Data.UV.Add(FVector2D(float(Side),T));Data.Colors.Add(FLinearColor(FMath::Frac(Phase/(2*PI)),.9f,0,Strength));
                Data.Tangents.Add(FProcMeshTangent(Right,false));
            }
            if(Row>0) {const int32 V=First+(Row-1)*2;Data.Indices.Append({V,V+1,V+3,V,V+3,V+2});}
        }
    }
    Data.Upload(Mesh);
}

inline void Configure(UPrimitiveComponent* Component)
{
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCanEverAffectNavigation(false);
    Component->SetCastShadow(false);
}
}
