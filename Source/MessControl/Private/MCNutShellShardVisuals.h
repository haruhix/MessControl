#pragma once

#include "MCNutCombatVisuals.h"

/** Closed, faceted shell chips: presentation geometry, never collision bodies. */
namespace MCNutShellShardVisuals
{
inline void Triangle(MCNutCombatVisuals::FMesh& Mesh,FVector A,FVector B,FVector C,FLinearColor Color,float Phase)
{
    const FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
    if(Normal.IsNearlyZero()) return;
    const int32 First=Mesh.Points.Num();
    const FVector2D Texcoords[]={FVector2D(.18f,.12f+Phase),FVector2D(.50f,.66f+Phase),FVector2D(.82f,.93f+Phase)};
    int32 Vertex=0;
    for(const FVector Point:{A,B,C}) {
        Mesh.Points.Add(Point);Mesh.Normals.Add(Normal);
        // Interior UVs retain the solid silhouette; varying coordinates make
        // grain and the final masked erosion visible rather than a whole-chip pop.
        Mesh.UV.Add(Texcoords[Vertex++]);Mesh.Colors.Add(Color);
        Mesh.Tangents.Add(FProcMeshTangent((B-A).GetSafeNormal(),false));
    }
    Mesh.Indices.Append({First,First+1,First+2});
}

inline void Shard(MCNutCombatVisuals::FMesh& Mesh,const FTransform& Pose,float Size,int32 Seed,float Alpha)
{
    const FVector2D Outline[]={FVector2D(-.75f,-.29f),FVector2D(.08f,-.68f),FVector2D(.86f,-.32f),
        FVector2D(.67f,.47f),FVector2D(-.08f,.82f),FVector2D(-.62f,.37f)};
    FRandomStream Random(Seed);
    FVector Front[6],Back[6];
    const float Thickness=Size*Random.FRandRange(.13f,.20f);
    for(int32 I=0;I<6;++I) {
        const FVector2D P=Outline[I]*Random.FRandRange(.83f,1.12f);
        Front[I]=Pose.TransformPosition(FVector(P.X*Size,P.Y*Size,Size*.09f*(1-P.SizeSquared())));
        Back[I]=Pose.TransformPosition(FVector(P.X*Size,P.Y*Size,Size*.09f*(1-P.SizeSquared())-Thickness));
    }
    const FVector Outer=Pose.TransformPosition(FVector(0,0,Size*.22f));
    const FVector Inner=Pose.TransformPosition(FVector(0,0,Size*.22f-Thickness));
    const float Tone=Random.FRandRange(.84f,1.10f);
    const FLinearColor Shell(.22f*Tone,.085f*Tone,.025f*Tone,Alpha);
    const FLinearColor Pith(.42f*Tone,.24f*Tone,.10f*Tone,Alpha);
    const FLinearColor Edge(.31f*Tone,.15f*Tone,.052f*Tone,Alpha);
    for(int32 I=0;I<6;++I) {
        const int32 Next=(I+1)%6;
        const float Phase=Seed*.017f+I*.31f;
        Triangle(Mesh,Outer,Front[I],Front[Next],Shell,Phase);
        Triangle(Mesh,Inner,Back[Next],Back[I],Pith,Phase+.13f);
        Triangle(Mesh,Front[I],Back[I],Back[Next],Edge,Phase+.26f);
        Triangle(Mesh,Front[I],Back[Next],Front[Next],Edge,Phase+.39f);
    }
}
}
