#include "MCReactionVFX.h"
#include "MCFirePatch.h"
#include "MCThroat.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PointLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

namespace {
// Air and task feedback use soft translucent shapes. Fire is a separate particle
// ribbon material; its turbulent opacity and emissive core define the silhouette.
struct FReactionMesh {
    TArray<FVector> P,N;
    TArray<int32> Tri;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    TArray<FProcMeshTangent> Tangents;
    FVector View,Right;

    int32 Add(FVector Point,FLinearColor Color) {
        const int32 I=P.Add(Point);
        N.Add(View);UV.Add(FVector2D::ZeroVector);C.Add(Color);
        Tangents.Add(FProcMeshTangent(Right,false));return I;
    }
    void Disk(FVector Center,FVector R,FVector U,float Size,FLinearColor Color,int32 Segments=24,bool Soft=true) {
        const int32 I=Add(Center,Color);
        FLinearColor Edge=Color;if(Soft) Edge.A=0;
        for(int32 J=0;J<=Segments;++J) {
            const float A=J*2*PI/Segments;
            Add(Center+(R*FMath::Cos(A)+U*FMath::Sin(A))*Size,Edge);
            if(J<Segments) Tri.Append({I,I+J+1,I+J+2});
        }
    }
    void Stroke(const TArray<FVector>& Points,float Width,FLinearColor Color,bool Taper=true,bool Feather=false) {
        if(Points.Num()<2) return;
        const int32 First=P.Num(),Stride=Feather?3:2;
        for(int32 I=0;I<Points.Num();++I) {
            const FVector Along=(Points[FMath::Min(I+1,Points.Num()-1)]-Points[FMath::Max(I-1,0)]).GetSafeNormal();
            const FVector Side=FVector::CrossProduct(View,Along).GetSafeNormal();
            const float T=I/float(Points.Num()-1);
            const float Shape=Taper?FMath::Pow(FMath::Max(0.f,FMath::Sin(T*PI)),.65f):1.f;
            FLinearColor Edge=Color;if(Feather) Edge.A=0;
            Add(Points[I]-Side*Width*Shape,Edge);
            if(Feather) Add(Points[I],Color);
            Add(Points[I]+Side*Width*Shape,Edge);
            if(I>0) for(int32 J=0;J<Stride-1;++J) {
                const int32 V=First+(I-1)*Stride+J;
                Tri.Append({V,V+1,V+Stride+1,V,V+Stride+1,V+Stride});
            }
        }
        if(!Taper) {
            Disk(Points[0],Right,FVector::CrossProduct(View,Right).GetSafeNormal(),Width,Color,12,false);
            Disk(Points.Last(),Right,FVector::CrossProduct(View,Right).GetSafeNormal(),Width,Color,12,false);
        }
    }
    void Line(FVector A,FVector B,float Width,FLinearColor Color) {Stroke({A,B},Width,Color,false);}
    void RoundedPolygon(FVector Center,FVector R,FVector U,const TArray<FVector2D>& Points,FLinearColor Color,float Round=.16f) {
        if(Points.Num()<3) return;
        TArray<FVector2D> Outline;
        for(int32 I=0;I<Points.Num();++I) {
            const FVector2D A=FMath::Lerp(Points[I],Points[(I+Points.Num()-1)%Points.Num()],Round);
            const FVector2D B=FMath::Lerp(Points[I],Points[(I+1)%Points.Num()],Round);
            for(int32 J=0;J<=3;++J) {
                const float T=J/3.f;
                Outline.Add(A*(1-T)*(1-T)+Points[I]*(2*(1-T)*T)+B*T*T);
            }
        }
        const int32 First=Add(Center,Color);
        for(int32 I=0;I<=Outline.Num();++I) {
            const FVector2D Q=Outline[I%Outline.Num()];
            Add(Center+R*Q.X+U*Q.Y,Color);
            if(I<Outline.Num()) Tri.Append({First,First+I+1,First+I+2});
        }
    }
    void Star(FVector Center,FVector R,FVector U,float Size,float Angle,FLinearColor Color) {
        TArray<FVector2D> Points;
        for(int32 J=0;J<10;++J) {
            const float A=J*PI/5+Angle,K=Size*(J%2?.47f:1.f);
            Points.Add(FVector2D(FMath::Sin(A)*K,FMath::Cos(A)*K));
        }
        RoundedPolygon(Center,R,U,Points,Color,.18f);
    }
    void FireRibbon(FVector Base,float Width,float Height,float Phase,float Age,float Opacity,float Hotness) {
        constexpr int32 Rows=14,Columns=4,Stride=Columns+1;
        const int32 First=P.Num();
        const FLinearColor Data(FMath::Frac(Phase/(2*PI)),Hotness,0,Opacity);
        for(int32 Row=0;Row<=Rows;++Row) {
            const float T=Row/float(Rows);
            const float Bend=(FMath::Sin(T*6.5f-Age*4.3f+Phase)*.32f+FMath::Sin(Age*2.4f+Phase)*.27f)*T*T;
            const FVector Center=Base+FVector(0,0,T*Height)+Right*(Bend*Width);
            for(int32 Column=0;Column<=Columns;++Column) {
                const float U=Column/float(Columns);
                const int32 I=Add(Center+Right*((U*2-1)*Width),Data);
                UV[I]=FVector2D(U,T);
                N[I]=FVector::CrossProduct(Right,FVector::UpVector).GetSafeNormal();
                if(Row>0 && Column>0) {
                    const int32 A=First+(Row-1)*Stride+Column-1;
                    Tri.Append({A,A+1,A+Stride+1,A,A+Stride+1,A+Stride});
                }
            }
        }
    }
    void SmokePuff(FVector Center,FVector U,float Size,float Seed,float Life,float Opacity) {
        const int32 First=P.Num();
        const FLinearColor Data(Seed,Life,0,Opacity);
        for(int32 I=0;I<4;++I) {
            const float X=(I==1 || I==2)?1.f:-1.f,Y=I>=2?1.f:-1.f;
            const int32 Index=Add(Center+(Right*X+U*Y)*Size,Data);
            UV[Index]=FVector2D((X+1)*.5f,(Y+1)*.5f);
        }
        Tri.Append({First,First+1,First+2,First,First+2,First+3});
    }
    void Upload(UProceduralMeshComponent* Mesh,int32 Section) {
        Mesh->CreateMeshSection_LinearColor(Section,P,Tri,N,UV,C,Tangents,false);
    }
};
}
AMCReactionVFX::AMCReactionVFX()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(true);PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Particles"));SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);Mesh->SetAbsolute(false,true,false);
    FireLight=CreateDefaultSubobject<UPointLightComponent>(TEXT("FireWarmth"));FireLight->SetupAttachment(Mesh);
    FireLight->SetCastShadows(false);FireLight->SetVisibility(false);FireLight->SetLightColor(FLinearColor(1,.30f,.12f));
    FireLight->IntensityUnits=ELightUnits::Lumens;
}
double AMCReactionVFX::Now() const {const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}
AMCReactionVFX* AMCReactionVFX::Spawn(UWorld* World,FVector P,EMCReactionEffect E,float Duration,float Size,FVector Aim,float InFlowLength)
{
    if(!World || World->GetNetMode()==NM_Client) return nullptr;
    const FTransform T(P);auto* V=World->SpawnActorDeferred<AMCReactionVFX>(StaticClass(),T);
    if(V) {V->Effect=E;V->bLoop=Duration<=0;V->Seconds=Duration>0?FMath::Clamp(Duration,.2f,60.f):1;V->Radius=FMath::Clamp(Size,10.f,600.f);V->Direction=Aim.GetSafeNormal();V->FlowLength=FMath::Clamp(InFlowLength,0.f,8000.f);V->FinishSpawning(T);}return V;
}
void AMCReactionVFX::BeginPlay()
{
    Super::BeginPlay();if(HasAuthority()) {StartedAt=Now();if(!bLoop) SetLifeSpan(Seconds+.15f);ForceNetUpdate();}
    if(GetNetMode()!=NM_DedicatedServer) {
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_Reaction.M_Reaction"))) {Material=UMaterialInstanceDynamic::Create(Base,this);Mesh->SetMaterial(0,Material);}
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_ReactionSoft.M_ReactionSoft"))) {SoftMaterial=UMaterialInstanceDynamic::Create(Base,this);Mesh->SetMaterial(1,SoftMaterial);}
        // Load on every rendering peer, including clients initialized before the
        // replicated Effect arrives. Mesh material slots retain both MIDs.
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"))) {FireMaterial=UMaterialInstanceDynamic::Create(Base,this);Mesh->SetMaterial(2,FireMaterial);}
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_ReactionSmoke.M_ReactionSmoke"))) Mesh->SetMaterial(3,UMaterialInstanceDynamic::Create(Base,this));
    }
}
void AMCReactionVFX::Tick(float Dt)
{
    Super::Tick(Dt);if(GetNetMode()==NM_DedicatedServer) return;
    const float Age=FMath::Max(0.,Now()-StartedAt),T=bLoop?0:Age/Seconds;
    if(T>1) {Mesh->SetVisibility(false);FireLight->SetVisibility(false);return;}
    const float Fade=1-FMath::SmoothStep(.75f,1.f,T);
    for(auto* M:{Material.Get(),SoftMaterial.Get()}) if(M) {M->SetVectorParameterValue(TEXT("Color"),FLinearColor::White);M->SetScalarParameterValue(TEXT("Opacity"),Fade);}
    FVector Eye=GetActorLocation()+FVector(-1,0,1);if(const auto* PC=GetWorld()->GetFirstPlayerController();PC && PC->PlayerCameraManager) Eye=PC->PlayerCameraManager->GetCameraLocation();
    const FVector View=(Eye-GetActorLocation()).GetSafeNormal();
    const FVector Right=FVector::CrossProduct(FVector::UpVector,View).GetSafeNormal(),Up=FVector::CrossProduct(View,Right).GetSafeNormal();
    FReactionMesh Glow,Soft,Flame,Smoke;Glow.View=Soft.View=Flame.View=Smoke.View=View;Glow.Right=Soft.Right=Flame.Right=Smoke.Right=Right;
    FireLight->SetVisibility(Effect==EMCReactionEffect::Fire);
    const FVector Axis=Direction.IsNearlyZero()?FVector::ForwardVector:Direction;
    const FVector R=FVector::CrossProduct(Axis,FVector::UpVector).GetSafeNormal(),V=FVector::CrossProduct(R,Axis).GetSafeNormal();
    if(Effect==EMCReactionEffect::Stars) {
        const float Pop=FMath::SmoothStep(0.f,.20f,Age)*(1+.13f*FMath::Sin(Age*17)*FMath::Exp(-Age*4));
        const FVector Badge=FVector(0,0,FMath::Min(Age,.8f)*12);
        const float Size=31*Pop;
        Soft.Disk(Badge-View*.3f,Right,Up,Size+3*Pop,FLinearColor(.02f,.31f,.19f,.96f),40,false);
        Soft.Disk(Badge,Right,Up,Size,FLinearColor(.009f,.105f,.065f,1),40,false);
        // All three layers share a material section; the dark backing keeps the
        // check readable over tissue, the tooth character, and bright UI markers.
        const FVector Check=Badge+View*1.5f;
        Soft.Line(Check+(Right*15-Up*1)*Pop,Check+(Right*4-Up*12)*Pop,3.8f*Pop,FLinearColor(.67f,.82f,.72f,1));
        Soft.Line(Check+(Right*4-Up*12)*Pop,Check+(-Right*17+Up*14)*Pop,3.8f*Pop,FLinearColor(.67f,.82f,.72f,1));
        for(int32 I=0;I<8;++I) {
            const float A=I*2.39996f,Flight=FMath::SmoothStep(.09f,.85f,Age);
            const FVector P=(Right*FMath::Cos(A)+Up*FMath::Sin(A))*Radius*(.27f+Flight*.36f)
                +FVector(0,0,Age*16-Age*Age*7);
            const float StarSize=(8+I%3*3)*Pop*(1-T*.35f);
            Soft.Star(P-View*.1f,Right,Up,StarSize+1.2f,A+Age*(I%2?.65f:-.65f),FLinearColor(.34f,.10f,.012f,.92f));
            Soft.Star(P,Right,Up,StarSize,A+Age*(I%2?.65f:-.65f),FLinearColor(.84f,.46f,.025f,.97f));
        }
        const FLinearColor Palette[]={FLinearColor(.52f,.075f,.13f,.9f),FLinearColor(.025f,.30f,.41f,.9f),FLinearColor(.07f,.42f,.25f,.9f),FLinearColor(.72f,.40f,.025f,.9f)};
        for(int32 I=0;I<16;++I) {
            const float A=I*2.39996f,Speed=23+(I%4)*10;
            const FVector P=Right*FMath::Cos(A)*(16+Age*Speed)
                +Up*(FMath::Sin(A)*25+Age*(52+I%3*13)-Age*Age*38);
            const FVector Tilt=Right*FMath::Cos(A+Age*4)+Up*FMath::Sin(A+Age*4);
            Soft.Line(P-Tilt*3.5f,P+Tilt*3.5f,1.8f,Palette[I%4]);
        }
    } else if(Effect==EMCReactionEffect::Fire) {
        const auto* Fire=Cast<AMCFirePatch>(GetAttachParentActor());
        const float Heat=FMath::Clamp(Fire?Fire->Heat:1.f,0.f,1.f)*FMath::SmoothStep(0.f,.30f,Age)*Fade;
        const float Shrink=FMath::Sqrt(Heat);
        FireLight->SetRelativeLocation(FVector(0,0,24*Shrink));FireLight->SetAttenuationRadius(Radius*2.7f);
        FireLight->SetSourceRadius(18);FireLight->SetSoftSourceRadius(28);
        FireLight->SetIntensity(1.5f*Heat);
        const FVector SeedPoint=Fire && Fire->Tongue?Fire->SurfaceAnchor:GetActorLocation();
        const float Seed=FMath::Frac(FMath::Sin(SeedPoint.X*.031f+SeedPoint.Y*.017f)*145.73f)*2*PI;
        if(FireMaterial) FireMaterial->SetScalarParameterValue(TEXT("FireAge"),Age);
        // The transparent noise material creates and erodes the moving contour.
        // There are no closed flame meshes, solid bases or outline layers.
        if(Heat>.001f) {
            Glow.Disk(FVector(0,0,1),FVector::ForwardVector,FVector::RightVector,Radius*.98f,FLinearColor(.32f,.064f,.011f,.09f*Heat));
            Flame.FireRibbon(FVector(0,0,2),Radius*1.10f*Shrink,25*Shrink,Seed,Age,.30f*Heat,.62f);
            for(int32 I=0;I<3;++I) {
                const float Phase=Seed+I*2.39996f;
                const FVector Base=FVector(FMath::Cos(Phase),FMath::Sin(Phase),0)*Radius*.27f*Shrink+FVector(0,0,3);
                const float Variation=.5f+.5f*FMath::Sin(Phase*2.13f);
                const float Breath=.91f+.09f*FMath::Sin(Age*(4.1f+Variation)+Phase);
                const float Height=(I==0?84+Variation*28:58+Variation*24)*Breath*Shrink;
                const float Width=(23+Variation*8)*Shrink;
                Flame.FireRibbon(Base,Width,Height,Phase,Age,.83f*Heat,.82f);
            }
        }
        for(int32 I=0;I<2;++I) {
            const float U=FMath::Frac(Age*.43f+I*.5f+Seed/(2*PI)),A=I*2.39996f+Seed;
            const FVector P=FVector(FMath::Cos(A+U),FMath::Sin(A+U),0)*Radius*(.17f+U*.42f)+FVector(0,0,(38+U*124)*Shrink);
            const FLinearColor Spark(1.9f,.82f,.20f,FMath::Sin(U*PI)*.28f*Heat);
            Glow.Disk(P,Right,Up,1.35f*Shrink,Spark,10,true);
            Glow.Line(P-FVector(0,0,3)*Shrink,P, .45f*Shrink,Spark);
        }
        for(int32 I=0;I<3;++I) {
            const float U=FMath::Frac(Age*.27f+I*.333f+Seed/(2*PI));
            const FVector P=Right*(FMath::Sin(Seed+I*2.4f+U*4)*Radius*.43f+U*18)+FVector(0,0,(95+U*110)*Shrink);
            const float Size=(35+U*38)*Shrink;
            const float Opacity=FMath::SmoothStep(0.f,.14f,U)*(1-FMath::SmoothStep(.62f,1.f,U))*.68f*Heat;
            const float Roll=Seed+I*.7f+U*.45f;
            const FVector SmokeRight=Right*FMath::Cos(Roll)+Up*FMath::Sin(Roll),SmokeUp=Up*FMath::Cos(Roll)-Right*FMath::Sin(Roll);
            Smoke.Right=SmokeRight;
            Smoke.SmokePuff(P,SmokeUp,Size,Seed/(2*PI),U,Opacity);
        }
    } else if(Effect==EMCReactionEffect::Steam) {
        const float Grow=FMath::SmoothStep(0.f,.18f,Age),Dissolve=1-FMath::SmoothStep(.22f,1.f,T);
        for(int32 I=0;I<4;++I) {
            const float A=I*2.4f;
            const FVector P=Right*FMath::Cos(A)*Radius*.25f+FVector(0,0,6+Age*(24+I*5));
            const float Size=(8+Age*11)*Grow;
            const FLinearColor Tint(.22f,.33f,.34f,.30f*Dissolve);
            Soft.Disk(P,Right,Up,Size,Tint);
            Soft.Disk(P+Right*Size*.55f-Up*Size*.16f,Right,Up,Size*.72f,Tint);
            Soft.Disk(P-Right*Size*.5f-Up*Size*.25f,Right,Up,Size*.65f,Tint);
        }
    } else if(Effect==EMCReactionEffect::Suction || Effect==EMCReactionEffect::Yawn) {
        const bool Yawn=Effect==EMCReactionEffect::Yawn;
        const float Span=FlowLength>0?FlowLength:Radius*(Yawn?4.f:1.65f);
        const float Strength=FMath::SmoothStep(0.f,.45f,Age)*Fade;
        const float Wide=Radius*(Yawn?1.30f:.76f),Narrow=Radius*.15f;
        // This actor lives at the mouth inlet. Every air shape starts behind it
        // across the tongue and accelerates into that fixed world-space source.
        auto FlowPoint=[&](float Travel,float Angle) {
            const float Width=FMath::Lerp(Wide,Narrow,Travel);
            return -Axis*Span*(1-Travel)
                +R*FMath::Cos(Angle)*Width
                +V*FMath::Sin(Angle)*Width*.30f
                +FVector(0,0,Radius*.20f*(1-Travel));
        };
        const int32 Streams=Yawn?6:3;
        for(int32 I=0;I<Streams;++I) {
            const float Head=FMath::Frac(Age*(Yawn?.58f:.84f)+I/float(Streams));
            const float Tail=FMath::Max(0.f,Head-.24f),Seed=I*2.39996f;
            TArray<FVector> Path;
            for(int32 J=0;J<=24;++J) {
                const float U=FMath::Lerp(Tail,Head,J/24.f),Travel=U*U;
                Path.Add(FlowPoint(Travel,Seed+Travel*5.4f));
            }
            const float Alpha=FMath::Sin(Head*PI)*.64f*Strength;
            Soft.Stroke(Path,Yawn?2.0f:1.5f,FLinearColor(.19f,.46f,.40f,Alpha),true,true);
        }
        // Little rounded, curled wisps give the vacuum an airy cartoon volume.
        // They shrink and gain speed toward the inlet instead of bursting from a face.
        const int32 Puffs=Yawn?8:4;
        for(int32 I=0;I<Puffs;++I) {
            const float U=FMath::Frac(Age*(Yawn?.42f:.65f)+I/float(Puffs)),Travel=U*U;
            const FVector Center=FlowPoint(Travel,I*2.39996f+Travel*4.2f);
            const float Size=(Yawn?18.f:12.f)*(1-Travel*.65f)*(1+(I%3)*.16f);
            const float Alpha=FMath::SmoothStep(0.f,.18f,U)*(1-FMath::SmoothStep(.86f,1.f,U))*Strength;
            const FLinearColor Mist(.23f,.38f,.36f,.12f*Alpha);
            Soft.Disk(Center,Right,Up,Size,Mist);
            Soft.Disk(Center+Right*Size*.44f-Up*Size*.14f,Right,Up,Size*.7f,Mist);
            Soft.Disk(Center-Right*Size*.35f+Up*Size*.1f,Right,Up,Size*.63f,Mist);
            TArray<FVector> Curl;
            for(int32 J=0;J<=18;++J) {
                const float A=-PI*.3f+J/18.f*PI*1.55f+I*.7f+Travel*.9f;
                Curl.Add(Center+(Right*FMath::Cos(A)+Up*FMath::Sin(A))*(Size*(.80f-.30f*J/18.f)));
            }
            Soft.Stroke(Curl,Yawn?1.7f:1.2f,FLinearColor(.28f,.53f,.47f,.62f*Alpha),true,true);
        }
        // A short swirling funnel at the aperture identifies the mouth as the
        // suction source, even when players are farther across the tongue.
        float MouthOpen=1;
        if(Yawn) {
            MouthOpen=0;float Nearest=FLT_MAX;
            for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
                const float Distance=FVector::DistSquared(It->GetActorLocation(),GetActorLocation());
                if(Distance<Nearest) {Nearest=Distance;MouthOpen=It->OpenAmount();}
            }
        }
        const float InletVisible=FMath::SmoothStep(.70f,.84f,MouthOpen);
        const float InletScale=Yawn?FMath::SmoothStep(.30f,.90f,MouthOpen):1.f;
        const float InletSpan=FMath::Min(Span*.34f,Radius*1.65f)*InletScale;
        for(int32 I=0;I<(Yawn?3:2);++I) {
            TArray<FVector> Spiral;
            for(int32 J=0;J<=36;++J) {
                const float U=J/36.f,A=I*2*PI/(Yawn?3.f:2.f)+Age*3.5f+U*PI*2.1f;
                const float Width=FMath::Lerp(Radius*.78f,Narrow,U)*InletScale;
                Spiral.Add(-Axis*InletSpan*(1-U)+(R*FMath::Cos(A)+V*FMath::Sin(A)*.48f)*Width);
            }
            Soft.Stroke(Spiral,Yawn?2.6f:1.6f,FLinearColor(.22f,.48f,.43f,(Yawn?.56f:.36f)*Strength*InletVisible),true,true);
        }
    } else {
        // Ulcer creation has a short tissue-coloured pressure puff. Food hit
        // feedback is handled by the food mesh itself, without particle bursts.
        const float Spread=FMath::SmoothStep(0.f,1.f,T);
        for(int32 I=0;I<5;++I) {
            const float A=I*2.39996f;
            const FVector P=(Right*FMath::Cos(A)+Up*FMath::Sin(A))*Radius*.26f*Spread;
            Soft.Disk(P,Right,Up,(8+Spread*10)*(1-T*.35f),FLinearColor(.26f,.045f,.05f,.24f*(1-T)));
        }
    }
    Glow.Upload(Mesh,0);Soft.Upload(Mesh,1);
    if(Flame.P.Num()) Flame.Upload(Mesh,2);else Mesh->ClearMeshSection(2);
    if(Smoke.P.Num()) Smoke.Upload(Mesh,3);else Mesh->ClearMeshSection(3);
}
void AMCReactionVFX::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCReactionVFX,Effect);DOREPLIFETIME(AMCReactionVFX,StartedAt);DOREPLIFETIME(AMCReactionVFX,Seconds);DOREPLIFETIME(AMCReactionVFX,Radius);DOREPLIFETIME(AMCReactionVFX,Direction);DOREPLIFETIME(AMCReactionVFX,FlowLength);DOREPLIFETIME(AMCReactionVFX,bLoop);}
