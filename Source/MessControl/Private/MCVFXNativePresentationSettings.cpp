#include "MCVFXNativePresentationSettings.h"
#include "MCPerkVFXProfile.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCNutShieldCrackLayout.h"
#include "MCPassiveFeedbackCue.h"
#include "MCPassiveCombatCue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "Curves/CurveFloat.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace MCNativeArtPilot
{
const FName Shield(TEXT("Boss.NutKnight.Shield")),Fireball(TEXT("Boss.NutMage.Fireball"));
struct FFeedbackBinding {const TCHAR* Source;const TCHAR* Event;EMCPassiveFeedbackKind Kind;};
const FFeedbackBinding FeedbackBindings[]={
    {TEXT("B2_CleanBreath"),TEXT("Proc"),EMCPassiveFeedbackKind::Heal},
    {TEXT("B2_RefreshingWork"),TEXT("Proc"),EMCPassiveFeedbackKind::Heal},
    {TEXT("B2_ThickEnamel"),TEXT("Proc"),EMCPassiveFeedbackKind::Enamel},
    {TEXT("B2_Shield"),TEXT("Proc"),EMCPassiveFeedbackKind::Shield},
    {TEXT("B2_SecondWind"),TEXT("Proc"),EMCPassiveFeedbackKind::Wind},
    {TEXT("B2_CleanAcceleration"),TEXT("Proc"),EMCPassiveFeedbackKind::Speed},
    {TEXT("B2_ReturnTrip"),TEXT("Proc"),EMCPassiveFeedbackKind::Speed},
    {TEXT("B2_HoudiniDash"),TEXT("Vanish"),EMCPassiveFeedbackKind::Vanish},
    {TEXT("B2_HoudiniDash"),TEXT("Return"),EMCPassiveFeedbackKind::Return},
    {TEXT("B2_NeatFreak"),TEXT("Link"),EMCPassiveFeedbackKind::Neighbor},
    {TEXT("B2_StartFromDash"),TEXT("Proc"),EMCPassiveFeedbackKind::PowerHit},
    {TEXT("B2_Billiards"),TEXT("Impact"),EMCPassiveFeedbackKind::Billiards}
};
TArray<FName> FeedbackEvents(FName Source)
{if(Source==TEXT("B2_ThunderGod"))return {TEXT("Link")};TArray<FName> Result;for(const auto& B:FeedbackBindings)if(Source==B.Source)Result.Add(B.Event);return Result;}
FName Key(FName Event,FName Part){return FName(*(Event.ToString()+TEXT("/")+Part.ToString()));}
template<class T> T* Resolve(const TSoftObjectPtr<T>& P,bool Load)
{return P.IsNull()?nullptr:Load?P.LoadSynchronous():P.Get();}
bool NativeArtFiniteColor(FLinearColor C)
{return FMath::IsFinite(C.R)&&FMath::IsFinite(C.G)&&FMath::IsFinite(C.B)&&FMath::IsFinite(C.A);}
bool Between(float V,float Lo,float Hi){return FMath::IsFinite(V)&&V>=Lo&&V<=Hi;}
bool ShapeValid(const FMCVFXNativePartSettings& P,FString& Error)
{
    using K=EMCVFXNativePartKind;bool Good=true;
    const auto& C=P.Contact;const auto& D=P.Debris;const auto& F=P.Flame;
    const auto& B=P.BurstFlame;const auto& H=P.Halo;const auto& W=P.Wake;const auto& A=P.Cast;
    switch(P.Kind) {
    case K::ShieldFlash:
        Good=C.Corners>=6&&C.Corners<=24&&C.Corners%2==0&&Between(C.OuterRadiusFraction,.01f,2)
            &&Between(C.MinRadius,1,300)&&Between(C.MaxRadius,C.MinRadius,300)&&Between(C.InnerRadiusRatio,0,1)
            &&Between(C.DepthOffset,-50,50)&&Between(C.ExpandSeconds,.001f,5)&&Between(C.FlashBeginSeconds,0,5)
            &&Between(C.FlashEndSeconds,C.FlashBeginSeconds+.001f,5)&&C.SplinterBranches>=0&&C.SplinterBranches<=12
            &&Between(C.SplinterWidthA,.01f,10)&&Between(C.SplinterWidthB,.01f,10);break;
    case K::ShellDebris:case K::ShellShards:
        Good=D.Count>=0&&D.Count<=32&&Between(D.MinSpeed,0,1500)&&Between(D.MaxSpeed,D.MinSpeed,1500)
            &&Between(D.MinSize,.1f,100)&&Between(D.MaxSize,D.MinSize,100)&&Between(D.Gravity,0,3000)
            &&Between(D.UpSpeed,-500,1500)&&Between(D.RadialSpread,0,3)&&Between(D.Damping,0,20)&&Between(D.FadeStartFraction,0,1)
            &&Between(D.RadiusFractionMin,.001f,2)&&Between(D.RadiusFractionMax,D.RadiusFractionMin,2)
            &&Between(D.SizeClampMin,.1f,100)&&Between(D.SizeClampMax,D.SizeClampMin,100)&&Between(D.ForwardFraction,0,3);break;
    case K::FlightFlames:
        Good=F.Layers>=1&&F.Layers<=4&&F.Ribbons>=1&&F.Ribbons<=12&&F.Rows>=2&&F.Rows<=32
            &&Between(F.InnerRadiusFraction,.01f,3)&&Between(F.OuterRadiusFraction,.01f,3)
            &&Between(F.InnerWidthFraction,.01f,3)&&Between(F.OuterWidthFraction,.01f,3)
            &&Between(F.InnerOpacity,0,1)&&Between(F.OuterOpacity,0,1)&&Between(F.MinLengthRadii,.1f,16)
            &&Between(F.MaxLengthRadii,F.MinLengthRadii,32)&&Between(F.SelfGlow,0,4);break;
    case K::ImpactFlames:
        Good=B.Ribbons>=1&&B.Ribbons<=12&&B.Rows>=2&&B.Rows<=32&&Between(B.BaseWidth,.01f,2)
            &&Between(B.TipWidthLoss,0,B.BaseWidth)&&Between(B.HeightRadii,.1f,12)&&Between(B.SwayRadii,0,2)
            &&Between(B.SwayFrequency,0,40)&&Between(B.SwaySpeed,0,40)&&Between(B.MinChargeRadius,1,300)
            &&Between(B.MaxChargeRadius,B.MinChargeRadius,300)&&Between(B.ImpactRadiusFraction,.01f,2)
            &&Between(B.MaxImpactRadius,1,300)&&Between(B.SelfGlow,0,4);break;
    case K::FlightHalo:
        Good=H.Rings>=1&&H.Rings<=4&&H.Segments>=8&&H.Segments<=48&&Between(H.InnerRadiusFraction,.01f,3)
            &&Between(H.OuterRadiusFraction,H.InnerRadiusFraction,4)&&Between(H.FirstOpacity,0,1)&&Between(H.SecondOpacity,0,1);break;
    case K::FlightWake:
        Good=W.Segments>=1&&W.Segments<=24&&Between(W.HistorySeconds,0,1)&&Between(W.WidthFraction,.01f,2)&&Between(W.Opacity,0,1);break;
    case K::CastFocus:case K::CastRelease:
        Good=A.Arms>=0&&A.Arms<=8&&A.Segments>=2&&A.Segments<=24&&Between(A.MinFocusRadius,1,300)
            &&Between(A.MaxFocusRadius,A.MinFocusRadius,300)&&Between(A.SpinSpeed,-20,20)
            &&Between(A.ReleaseMinLength,1,300)&&Between(A.ReleaseMaxLength,A.ReleaseMinLength,300)&&Between(A.ReleaseWidth,.1f,100);break;
    case K::ImpactDebris:
        Good=P.ImpactDebris.Count>=0&&P.ImpactDebris.Count<=32&&Between(P.ImpactDebris.SizeFraction,0,1)
            &&Between(P.ImpactDebris.SpreadFraction,0,2)&&Between(P.ImpactDebris.HopHeight,0,300)
            &&Between(P.ImpactDebris.PitchRate,-1080,1080)&&Between(P.ImpactDebris.YawRate,-1080,1080);break;
    case K::ShieldCracks:
        Good=Between(P.Cracks.WidthScale,.1f,3)&&Between(P.Cracks.LipLift,0,2)&&Between(P.Cracks.BlendSeconds,.01f,2)
            &&NativeArtFiniteColor(P.Cracks.CoreColor)&&NativeArtFiniteColor(P.Cracks.LipColor)&&NativeArtFiniteColor(P.Cracks.RimColor);break;
    case K::PassiveSoft:case K::PassiveGlow:case K::PassiveSolid:case K::PassiveLightning:
        Good=Between(P.Feedback.Scale,.5f,1.8f)&&Between(P.Feedback.DurationScale,.5f,2);break;
    default:break;
    }
    if(!Good)Error=TEXT("Native geometry value exceeds its declared renderer budget: ")+P.PartID.ToString();
    return Good;
}
FMCVFXNativePartDescriptor D(FName ID,EMCVFXNativePartKind Kind,const TCHAR* Label,
    std::initializer_list<const TCHAR*> Editable,std::initializer_list<const TCHAR*> SourceOnly={})
{
    FMCVFXNativePartDescriptor R;R.PartID=ID;R.Kind=Kind;R.Label=FText::FromString(Label);
    for(const TCHAR* P:Editable)R.EditableProperties.Add(FName(P));
    for(const TCHAR* P:SourceOnly)R.SourceOnlyProperties.Add(FName(P));
    return R;
}
FMCVFXNativePartSettings Defaults(const FMCVFXNativePartDescriptor& Descriptor,FName Event)
{
    FMCVFXNativePartSettings P;P.PartID=Descriptor.PartID;P.Kind=Descriptor.Kind;
    if(P.Kind==EMCVFXNativePartKind::PassiveLightning){P.Material=GetDefault<AMCPassiveCombatCue>()->LinkMaterial.Get();return P;}
    // A character passive must not resolve knight/mage CDOs and their resources
    // merely because it shares this typed authoring contract.
    if(P.Kind==EMCVFXNativePartKind::PassiveSoft || P.Kind==EMCVFXNativePartKind::PassiveGlow || P.Kind==EMCVFXNativePartKind::PassiveSolid) {
        const auto* Passive=GetDefault<AMCPassiveFeedbackCue>();
        P.Material=P.Kind==EMCVFXNativePartKind::PassiveSoft?Passive->SoftMaterial.Get():
            P.Kind==EMCVFXNativePartKind::PassiveGlow?Passive->GlowMaterial.Get():Passive->SolidMaterial.Get();
        return P;
    }
    const auto* Cue=GetDefault<AMCNutCombatEffect>();
    const auto* Shot=GetDefault<AMCNutSpellProjectile>();
    const bool Break=Event==TEXT("Break");
    using K=EMCVFXNativePartKind;
    switch(P.Kind) {
    case K::ShieldFlash:
        P.Material=Cue->MagicMaterial;P.Tint=FLinearColor(.65f,.31f,.085f);
        if(Break){P.Contact.OuterRadiusFraction=.27f;P.Contact.FlashEndSeconds=.15f;P.Contact.SplinterBranches=5;}break;
    case K::ShellDebris:
        P.Mesh=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell"));
        if(Break){P.Debris.Count=12;P.Debris.MinSpeed=135;P.Debris.MaxSpeed=310;P.Debris.MinSize=1.4f;P.Debris.MaxSize=3.2f;P.Debris.UpSpeed=110;P.Debris.RadialSpread=1;}break;
    case K::ShellShards:
        P.Material=Cue->ShieldFractureMaterial;P.Debris.Count=5;P.Debris.MinSpeed=155;P.Debris.MaxSpeed=265;
        P.Debris.Gravity=270;P.Debris.UpSpeed=115;P.Debris.Damping=2.8f;P.Debris.FadeStartFraction=.76f;break;
    case K::ShieldCracks:
        P.Material=Cue->ShieldFractureMaterial;
        for(const auto& V:MCNutShieldCrackLayout::Samples) {
            FMCVFXNativeCrackSample S;S.Points.Append(V.Points,5);S.Normals.Append(V.Normals,5);P.Cracks.Samples.Add(MoveTemp(S));
        }
        for(const auto& V:MCNutShieldCrackLayout::Paths) {
            FMCVFXNativeCrackPath R;R.First=V.First;R.Count=V.Count;R.RevealAt=V.RevealAt;R.FullAt=V.FullAt;R.bRim=V.Rim;P.Cracks.Paths.Add(R);
        }break;
    case K::CastFocus:P.Material=Cue->MagicMaterial;P.Tint=FLinearColor(.56f,.19f,1.f);break;
    case K::CastRelease:P.Material=Cue->MagicMaterial;P.Tint=FLinearColor(.56f,.19f,1.f);break;
    case K::FlightFlames:P.Material=Shot->FlameMaterial;break;
    case K::FlightHalo:P.Material=Shot->CoreMaterial;break;
    case K::FlightWake:P.Material=Shot->WakeMaterial;P.Tint=FLinearColor(1,.40f,.055f);break;
    case K::ImpactFlames:P.Material=Cue->FlameMaterial;break;
    case K::ImpactDebris:P.Mesh=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Kernel.SM_Walnut_Kernel"));break;
    case K::ContactParticles:
        P.System=P.PartID==TEXT("CastSparks")?Cue->CastChargeSystem:
            P.PartID==TEXT("ReleaseSparks")?Cue->ArcaneBurstSystem:
            P.PartID==TEXT("EmberTrail")?Shot->TrailSystem:
            P.PartID==TEXT("ImpactSparks")?Cue->FireImpactSystem:Cue->ShieldHitSystem;
        P.SystemScale=P.PartID==TEXT("ShieldSparks")?(Break?.6f:.3f):P.PartID==TEXT("ReleaseSparks")?.65f:1.f;break;
    case K::FloorDust:P.System=Cue->DustImpactSystem;P.SystemScale=Break?1.2f:.35f;break;
    case K::DangerRing:P.Material=Cue->TelegraphMaterial;P.Tint=FLinearColor(1,.56f,.10f);break;
    case K::ProjectileBody:P.Mesh=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole"));break;
    }
    return P;
}
}

bool UMCVFXNativePresentationSettings::GetPassiveFeedbackKind(FName Source,FName Event,EMCPassiveFeedbackKind& Out)
{for(const auto& Binding:MCNativeArtPilot::FeedbackBindings)if(Source==Binding.Source && Event==Binding.Event){Out=Binding.Kind;return true;}return false;}

TArray<FMCVFXNativePartDescriptor> UMCVFXNativePresentationSettings::DescribeParts(FName Source,FName Event)
{
    using namespace MCNativeArtPilot;using K=EMCVFXNativePartKind;TArray<FMCVFXNativePartDescriptor> R;
    if(Source==Shield) {
        if(Event.IsNone()) {
            auto P=D(TEXT("ShieldCracks"),K::ShieldCracks,TEXT("Трещины настоящего щита"),
                {TEXT("Material"),TEXT("Cracks.WidthScale"),TEXT("Cracks.LipLift"),TEXT("Cracks.BlendSeconds"),
                 TEXT("Cracks.CoreColor"),TEXT("Cracks.LipColor"),TEXT("Cracks.RimColor"),TEXT("Cracks.Samples"),TEXT("Cracks.Paths")});
            P.bMaskedCutout=true;R.Add(P);
        } else if(Event==TEXT("Hit") || Event==TEXT("Break")) {
            R.Add(D(TEXT("ShieldContactMagic"),K::ShieldFlash,TEXT("Блик и раскол в месте контакта"),
                {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("ExpansionCurve"),TEXT("Contact")}));
            R.Add(D(TEXT("ShellDebris"),K::ShellDebris,TEXT("Мелкие скорлупки"),{TEXT("Mesh"),TEXT("Debris.Count"),
                TEXT("Debris.MinSpeed"),TEXT("Debris.MaxSpeed"),TEXT("Debris.MinSize"),TEXT("Debris.MaxSize"),
                TEXT("Debris.Gravity"),TEXT("Debris.UpSpeed"),TEXT("Debris.RadialSpread"),TEXT("Debris.FadeStartFraction")}));
            R.Add(D(TEXT("ShieldSparks"),K::ContactParticles,TEXT("Искры контакта"),{TEXT("System"),TEXT("SystemScale")}));
            R.Add(D(TEXT("FloorDust"),K::FloorDust,TEXT("Пыль на настоящем полу"),{TEXT("System"),TEXT("SystemScale")}));
            if(Event==TEXT("Break")) {
                auto P=D(TEXT("BrokenShell"),K::ShellShards,TEXT("Крупные осколки скорлупы"),{TEXT("Material"),TEXT("Debris.Count"),
                    TEXT("Debris.MinSpeed"),TEXT("Debris.MaxSpeed"),TEXT("Debris.Gravity"),TEXT("Debris.UpSpeed"),TEXT("Debris.Damping"),
                    TEXT("Debris.FadeStartFraction"),TEXT("Debris.RadiusFractionMin"),TEXT("Debris.RadiusFractionMax"),
                    TEXT("Debris.SizeClampMin"),TEXT("Debris.SizeClampMax"),TEXT("Debris.ForwardFraction")});
                P.bMaskedCutout=true;R.Add(P);
            }
        }
    } else if(Source==Fireball) {
        if(Event==TEXT("Charge")) {
            R.Add(D(TEXT("ChargeFocusMagic"),K::CastFocus,TEXT("Фокус и спирали в ладони"),
                {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("ExpansionCurve"),TEXT("Cast.Arms"),
                 TEXT("Cast.Segments"),TEXT("Cast.MinFocusRadius"),TEXT("Cast.MaxFocusRadius"),TEXT("Cast.SpinSpeed")}));
            R.Add(D(TEXT("ChargeFlames"),K::ImpactFlames,TEXT("Огонь зарядки"),
                {TEXT("Material"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("BurstFlame")}));
            R.Add(D(TEXT("CastSparks"),K::ContactParticles,TEXT("Искры зарядки"),{TEXT("System")}));
        } else if(Event==TEXT("Release")) {
            R.Add(D(TEXT("ReleaseMagic"),K::CastRelease,TEXT("Выпуск из настоящей ладони"),
                {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("Cast.ReleaseMinLength"),TEXT("Cast.ReleaseMaxLength"),TEXT("Cast.ReleaseWidth")}));
            R.Add(D(TEXT("ReleaseSparks"),K::ContactParticles,TEXT("Искры выпуска"),{TEXT("System"),TEXT("SystemScale")}));
        } else if(Event==TEXT("Flight")) {
            auto Body=D(TEXT("WalnutBody"),K::ProjectileBody,TEXT("Настоящий орех — игровая форма"),{},
                {TEXT("Mesh"),TEXT("NativeRadius"),TEXT("NativeTrajectory")});
            Body.bProtectedGeometry=true;R.Add(Body);
            R.Add(D(TEXT("FlightFlames"),K::FlightFlames,TEXT("Ленты огня вокруг ореха"),
                {TEXT("Material"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("ExpansionCurve"),TEXT("Flame")}));
            R.Add(D(TEXT("FlightCore"),K::FlightHalo,TEXT("Горячий ореол"),
                {TEXT("Material"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("Halo")}));
            R.Add(D(TEXT("FlightWake"),K::FlightWake,TEXT("Направленный след"),
                {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("Wake")}));
            R.Add(D(TEXT("EmberTrail"),K::ContactParticles,TEXT("Настоящий Niagara-след"),{TEXT("System")}));
        } else if(Event==TEXT("Impact")) {
            auto Ring=D(TEXT("ImpactRing"),K::DangerRing,TEXT("Нативное кольцо контакта"),{},
                {TEXT("Material"),TEXT("Tint"),TEXT("NativePoint"),TEXT("NativeNormal"),TEXT("NativeRadius"),TEXT("NativeDuration")});
            Ring.bProtectedGeometry=true;R.Add(Ring);
            R.Add(D(TEXT("ImpactFlames"),K::ImpactFlames,TEXT("Огонь попадания"),
                {TEXT("Material"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("BurstFlame")}));
            R.Add(D(TEXT("ImpactSparks"),K::ContactParticles,TEXT("Искры попадания"),{TEXT("System")}));
            R.Add(D(TEXT("ImpactDebris"),K::ImpactDebris,TEXT("Осколки ядра ореха"),{TEXT("Mesh"),TEXT("ImpactDebris")}));
        }
    } else if(Source==TEXT("B2_ThunderGod") && Event==TEXT("Link")) {
        R.Add(D(TEXT("LightningStroke"),K::PassiveLightning,TEXT("Родная молния · общий материал двух лент"),
            {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve"),TEXT("Feedback.Scale"),TEXT("Feedback.DurationScale")},
            {TEXT("AcceptedSourcePosition"),TEXT("AcceptedTargetPosition"),TEXT("NoiseSeed")}));
    } else {
        EMCPassiveFeedbackKind Kind;
        if(GetPassiveFeedbackKind(Source,Event,Kind)) {
            auto Soft=D(TEXT("FeedbackSoft"),K::PassiveSoft,TEXT("Мягкие формы · настоящий материал"),
                {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve")});
            auto Glow=D(TEXT("FeedbackGlow"),K::PassiveGlow,TEXT("Акценты свечения · настоящий материал"),
                {TEXT("Material"),TEXT("Tint"),TEXT("OpacityScale"),TEXT("OpacityCurve")});
            auto Solid=D(TEXT("FeedbackSolid"),K::PassiveSolid,TEXT("Объёмные формы и общий темп"),
                {TEXT("Material"),TEXT("Tint"),TEXT("Feedback.DurationScale")});
            // Link positions come from accepted source/target endpoints. Scaling
            // its generated path would lie about that contact, so it is not an art control.
            if(Kind!=EMCPassiveFeedbackKind::Neighbor)for(auto* P:{&Soft,&Glow,&Solid}) {
                P->EditableProperties.Add(TEXT("Feedback.Scale"));P->EditableProperties.Add(TEXT("ExpansionCurve"));
            }
            if(Kind==EMCPassiveFeedbackKind::Heal || Kind==EMCPassiveFeedbackKind::Neighbor)R.Add(MoveTemp(Glow));
            else if(Kind!=EMCPassiveFeedbackKind::Speed && Kind!=EMCPassiveFeedbackKind::Billiards)R.Add(MoveTemp(Soft));
            R.Add(MoveTemp(Solid));
        }
    }
    for(auto& P:R) {
        if(!P.bProtectedGeometry)P.EditableProperties.Add(TEXT("bEnabled"));
        P.NumericBounds.Add(TEXT("OpacityScale"),FVector2D(0,2));
        P.NumericBounds.Add(TEXT("SystemScale"),FVector2D(.3,2));
        P.NumericBounds.Add(TEXT("Feedback.Scale"),FVector2D(.5,1.8));
        P.NumericBounds.Add(TEXT("Feedback.DurationScale"),FVector2D(.5,2));
    }
    return R;
}

bool UMCVFXNativePresentationSettings::InitFromNativeDefaults(FName Source,FString& Error,bool Keep)
{
    using namespace MCNativeArtPilot;Error.Reset();
    const auto Feedback=FeedbackEvents(Source);
    if(Source!=Shield && Source!=Fireball && Feedback.IsEmpty()){Error=TEXT("This native-art source has no renderer contract.");return false;}
    TMap<FName,FMCVFXNativeEventSettings> NewEvents;TArray<FMCVFXNativePartSettings> NewPersistent;
    const TArray<FName> IDs=Source==Shield?TArray<FName>{TEXT("Hit"),TEXT("Break")}:Source==Fireball?
        TArray<FName>{TEXT("Charge"),TEXT("Release"),TEXT("Flight"),TEXT("Impact")}:Feedback;
    for(FName E:IDs) {
        FMCVFXNativeEventSettings V;V.bKeepNativeAccentsWithComposition=Keep;
        for(const auto& D:DescribeParts(Source,E))V.Parts.Add(Defaults(D,E));
        NewEvents.Add(E,MoveTemp(V));
    }
    for(const auto& D:DescribeParts(Source,NAME_None))NewPersistent.Add(Defaults(D,NAME_None));
    SourceID=Source;SchemaVersion=1;Events=MoveTemp(NewEvents);PersistentParts=MoveTemp(NewPersistent);return true;
}
const FMCVFXNativePartSettings* UMCVFXNativePresentationSettings::FindPart(FName Event,FName Part) const
{
    const auto* E=Events.Find(Event);const auto& A=Event.IsNone()?PersistentParts:E?E->Parts:PersistentParts;
    if(!Event.IsNone() && !E)return nullptr;
    return A.FindByPredicate([&](const auto& V){return V.PartID==Part;});
}

bool UMCVFXNativePresentationSettings::ValidateForSource(FName Expected,const TArray<FName>& Allowed,FString& Error) const
{
    using namespace MCNativeArtPilot;Error.Reset();
    if(SchemaVersion!=1 || SourceID!=Expected || (Expected!=Shield && Expected!=Fireball && FeedbackEvents(Expected).IsEmpty()))
    {Error=TEXT("Native-art source/version does not match the real renderer.");return false;}
    if(Events.Num()>4 || PersistentParts.Num()>4){Error=TEXT("Native-art pilot exceeds its explicit event/part bound.");return false;}
    auto ValidateParts=[&](FName Event,const TArray<FMCVFXNativePartSettings>& Parts) {
        const auto Descriptors=DescribeParts(Expected,Event);TSet<FName> Seen;
        if(Parts.Num()>Descriptors.Num()){Error=TEXT("Native part count exceeds renderer contract.");return false;}
        for(const auto& P:Parts) {
            const auto* D=Descriptors.FindByPredicate([&](const auto& V){return V.PartID==P.PartID && V.Kind==P.Kind;});
            if(!D || Seen.Contains(P.PartID)){Error=TEXT("Unknown/duplicate native part or wrong renderer kind: ")+P.PartID.ToString();return false;}
            Seen.Add(P.PartID);
            const auto Required=Defaults(*D,Event);
            if(P.bEnabled && ((!Required.Material.IsNull()&&P.Material.IsNull())||(!Required.Mesh.IsNull()&&P.Mesh.IsNull())
                ||(!Required.System.IsNull()&&P.System.IsNull())))
            {Error=TEXT("Enabled native part is missing its required resource; disable the part explicitly instead.");return false;}
            if(!NativeArtFiniteColor(P.Tint) || !FMath::IsFinite(P.OpacityScale) || P.OpacityScale<0 || P.OpacityScale>2
                || !FMath::IsFinite(P.SystemScale) || P.SystemScale<.3f || P.SystemScale>2)
            {Error=TEXT("Invalid native appearance value: ")+P.PartID.ToString();return false;}
            if(P.Kind==EMCVFXNativePartKind::PassiveSolid && P.Tint.A!=1.f)
            {Error=TEXT("Opaque passive solids expose an RGB color multiplier, not alpha transparency.");return false;}
            if(D->bProtectedGeometry) {
                const auto Default=Defaults(*D,Event);
                if(!P.bEnabled || P.Material!=Default.Material || P.Mesh!=Default.Mesh || P.System!=Default.System
                    || P.OpacityScale!=1 || !P.OpacityCurve.IsNull() || !P.ExpansionCurve.IsNull() || P.Tint!=Default.Tint)
                {Error=TEXT("Protected native body/footprint cannot be replaced, hidden or moved by art settings.");return false;}
            }
            if(!ShapeValid(P,Error))return false;
            const auto Native=Defaults(*D,Event);
            auto Editable=[&](FName Property){return D->EditableProperties.Contains(Property);};
            if((!Editable(TEXT("Material"))&&P.Material!=Native.Material)||(!Editable(TEXT("Mesh"))&&P.Mesh!=Native.Mesh)
                ||(!Editable(TEXT("System"))&&P.System!=Native.System)||(!Editable(TEXT("Tint"))&&P.Tint!=Native.Tint)
                ||(!Editable(TEXT("OpacityScale"))&&P.OpacityScale!=Native.OpacityScale)
                ||(!Editable(TEXT("OpacityCurve"))&&!P.OpacityCurve.IsNull())||(!Editable(TEXT("ExpansionCurve"))&&!P.ExpansionCurve.IsNull())
                ||(!Editable(TEXT("SystemScale"))&&P.SystemScale!=Native.SystemScale))
            {Error=TEXT("A native part received an unsupported art override: ")+P.PartID.ToString();return false;}
            for(FName Group:{FName(TEXT("Contact")),FName(TEXT("Debris")),FName(TEXT("Flame")),FName(TEXT("BurstFlame")),
                FName(TEXT("ImpactDebris")),FName(TEXT("Halo")),FName(TEXT("Wake")),FName(TEXT("Cast")),FName(TEXT("Cracks")),FName(TEXT("Feedback"))}) {
                const auto* S=FindFProperty<FStructProperty>(FMCVFXNativePartSettings::StaticStruct(),Group);
                const void* Current=S->ContainerPtrToValuePtr<void>(&P);
                const void* BuiltIn=S->ContainerPtrToValuePtr<void>(&Native);
                for(TFieldIterator<FProperty> Field(S->Struct);Field;++Field) {
                    const FName Path(*(Group.ToString()+TEXT(".")+Field->GetName()));
                    if(!Editable(Group)&&!Editable(Path)&&!Field->Identical_InContainer(Current,BuiltIn)) {
                        Error=TEXT("Native renderer does not consume this field: ")+Path.ToString();return false;
                    }
                }
            }
            if(P.Kind==EMCVFXNativePartKind::ShieldCracks) {
                if(P.Cracks.Samples.Num()>2048 || P.Cracks.Paths.Num()>128){Error=TEXT("Crack geometry exceeds bounds.");return false;}
                for(const auto& S:P.Cracks.Samples) {
                    if(S.Points.Num()!=5 || S.Normals.Num()!=5){Error=TEXT("Crack strip sample must have exactly five columns.");return false;}
                    for(int32 I=0;I<5;++I)if(S.Points[I].ContainsNaN() || S.Normals[I].ContainsNaN() || !S.Normals[I].IsNormalized())
                    {Error=TEXT("Invalid local crack geometry.");return false;}
                }
                for(const auto& R:P.Cracks.Paths)if(R.First<0 || R.Count<2 || R.First+R.Count>P.Cracks.Samples.Num()
                    || !FMath::IsFinite(R.RevealAt) || !FMath::IsFinite(R.FullAt) || R.FullAt<R.RevealAt)
                {Error=TEXT("Invalid crack path range/reveal interval.");return false;}
            }
        }
        return true;
    };
    for(const auto& V:Events)if(!Allowed.Contains(V.Key) || DescribeParts(Expected,V.Key).IsEmpty() || !ValidateParts(V.Key,V.Value.Parts))
    {if(Error.IsEmpty())Error=TEXT("Native settings declared an event outside the provider.");return false;}
    return ValidateParts(NAME_None,PersistentParts);
}

uint64 UMCVFXNativePresentationSettings::ComputeSettingsHash() const
{
    FString Text=FString::Printf(TEXT("%d/%s"),SchemaVersion,*SourceID.ToString());TArray<FName> Keys;Events.GetKeys(Keys);
    Keys.Sort([](FName A,FName B){return A.LexicalLess(B);});
    for(FName E:Keys) {
        Text+=E.ToString();FMCVFXNativeEventSettings::StaticStruct()->ExportText(Text,&Events[E],nullptr,nullptr,PPF_None,nullptr);
    }
    for(const auto& P:PersistentParts)FMCVFXNativePartSettings::StaticStruct()->ExportText(Text,&P,nullptr,nullptr,PPF_None,nullptr);
    // Curves are immutable prepared data as well. A saved curve edited in
    // place must not silently match an older location snapshot by path alone.
    auto HashCurves=[&](const TArray<FMCVFXNativePartSettings>& Parts) {
        for(const auto& P:Parts)for(const auto* Ref:{&P.OpacityCurve,&P.ExpansionCurve})
            if(const auto* Curve=Ref->Get())
                FRichCurve::StaticStruct()->ExportText(Text,&Curve->FloatCurve,nullptr,nullptr,PPF_None,nullptr);
    };
    for(FName E:Keys)HashCurves(Events[E].Parts);HashCurves(PersistentParts);
    FTCHARToUTF8 Bytes(*Text);uint8 Digest[20];FSHA1::HashBuffer(Bytes.Get(),Bytes.Length(),Digest);
    uint64 Hash=0;FMemory::Memcpy(&Hash,Digest,sizeof(Hash));return Hash?Hash:1;
}

bool FMCVFXPreparedNativePresentation::Prepare(FName Expected,const TArray<FName>& Allowed,
    const UMCVFXNativePresentationSettings* Source,UObject* Outer,bool Load,
    FMCVFXPreparedNativePresentation& Out,FString& Error)
{
    using namespace MCNativeArtPilot;Error.Reset();FMCVFXPreparedNativePresentation Candidate;
    if(!Source){Out=MoveTemp(Candidate);return true;}
    if(!Outer || !Source->ValidateForSource(Expected,Allowed,Error))return false;
    if(Source->GetOutermost()==GetTransientPackage() && Outer->GetWorld() && Outer->GetWorld()->GetNetMode()!=NM_Standalone)
    {Error=TEXT("Transient native-art working sources are local preview data, not network assets.");return false;}
    Candidate.Reference.Source=const_cast<UMCVFXNativePresentationSettings*>(Source);
    Candidate.Snapshot=DuplicateObject<UMCVFXNativePresentationSettings>(Source,Outer);
    Candidate.Snapshot->ClearFlags(RF_Public|RF_Standalone);Candidate.Snapshot->SetFlags(RF_Transient);
    auto PrepareParts=[&](FName Event,TArray<FMCVFXNativePartSettings>& Parts) {
        const auto Descriptors=UMCVFXNativePresentationSettings::DescribeParts(Source->SourceID,Event);
        for(auto& P:Parts) {
            auto* Material=Resolve(P.Material,Load);auto* Mesh=Resolve(P.Mesh,Load);auto* System=Resolve(P.System,Load);
            if((!P.Material.IsNull()&&!Material)||(!P.Mesh.IsNull()&&!Mesh)||(!P.System.IsNull()&&!System))
            {Error=TEXT("Native part resources are not resident: ")+P.PartID.ToString();return false;}
            for(UObject* O:{static_cast<UObject*>(Material),static_cast<UObject*>(Mesh),static_cast<UObject*>(System)})if(O)Candidate.Resources.AddUnique(O);
            for(TSoftObjectPtr<UCurveFloat>* Ref:{&P.OpacityCurve,&P.ExpansionCurve})if(!Ref->IsNull()) {
                auto* Curve=Resolve(*Ref,Load);if(!Curve){Error=TEXT("Native curve is not resident.");return false;}
                const auto Keys=Curve->FloatCurve.GetCopyOfKeys();
                if(Keys.Num()>128){Error=TEXT("Native curve exceeds the 128-key preparation bound.");return false;}
                for(const auto& K:Keys)if(!Between(K.Time,0,1)||!Between(K.Value,0,2)
                    ||!FMath::IsFinite(K.ArriveTangent)||!FMath::IsFinite(K.LeaveTangent))
                {Error=TEXT("Native art curves use finite normalized phase/value data.");return false;}
                Candidate.Resources.AddUnique(Curve);
                auto* Copy=DuplicateObject<UCurveFloat>(Curve,Candidate.Snapshot);
                Copy->ClearFlags(RF_Public|RF_Standalone);Copy->SetFlags(RF_Transient);*Ref=Copy;Candidate.Resources.Add(Copy);
            }
            const auto* D=Descriptors.FindByPredicate([&](const auto& V){return V.PartID==P.PartID;});
            if(Material) {
                if(!Material->GetMaterial() || Material->GetMaterial()->MaterialDomain!=MD_Surface)
                {Error=TEXT("Native geometry requires a compatible surface material.");return false;}
                const bool Feedback=P.Kind==EMCVFXNativePartKind::PassiveSoft||P.Kind==EMCVFXNativePartKind::PassiveGlow||P.Kind==EMCVFXNativePartKind::PassiveSolid||P.Kind==EMCVFXNativePartKind::PassiveLightning;
                if(Feedback) {
                    const auto Blend=Material->GetBlendMode();
                    const bool Opaque=Blend==BLEND_Opaque||Blend==BLEND_Masked;
                    if((P.Kind==EMCVFXNativePartKind::PassiveSolid && Blend!=BLEND_Opaque)
                        ||(P.Kind!=EMCVFXNativePartKind::PassiveSolid && Opaque))
                    {Error=TEXT("Passive solid needs an opaque material; soft/glow sections need a translucent/additive vertex-color material.");return false;}
                }
                TArray<FMaterialParameterInfo> Scalars,Vectors;TArray<FGuid> ScalarIDs,VectorIDs;
                Material->GetAllScalarParameterInfo(Scalars,ScalarIDs);Material->GetAllVectorParameterInfo(Vectors,VectorIDs);
                auto Scalar=[&](FName N){return Scalars.ContainsByPredicate([&](const auto& V){return V.Name==N;});};
                auto Vector=[&](FName N){return Vectors.ContainsByPredicate([&](const auto& V){return V.Name==N;});};
                const bool Flame=P.Kind==EMCVFXNativePartKind::ImpactFlames||P.Kind==EMCVFXNativePartKind::FlightFlames;
                const bool Arcane=P.Kind==EMCVFXNativePartKind::ShieldFlash||P.Kind==EMCVFXNativePartKind::CastFocus
                    ||P.Kind==EMCVFXNativePartKind::CastRelease||P.Kind==EMCVFXNativePartKind::FlightWake;
                if((Flame&&(!Scalar(TEXT("FireAge"))||!Scalar(TEXT("SelfGlow"))))
                    ||(Arcane&&(!Vector(TEXT("Tint"))||!Scalar(TEXT("Age"))||!Scalar(TEXT("Progress"))||!Scalar(TEXT("Beam"))))
                    ||(P.Kind==EMCVFXNativePartKind::FlightHalo&&!Scalar(TEXT("Age"))))
                {Error=TEXT("Native material is missing the actual renderer parameter interface: ")+P.PartID.ToString();return false;}
                // Dedicated servers retain the source contract for transport,
                // but do not allocate renderer-only dynamic variants.
                if(Outer->GetWorld() && Outer->GetWorld()->GetNetMode()==NM_DedicatedServer)continue;
                auto* MID=UMaterialInstanceDynamic::Create(Material,Candidate.Snapshot);
                if(!MID){Error=TEXT("Unable to prepare native material variant.");return false;}
                if(!Feedback && D && (D->EditableProperties.Contains(TEXT("Tint"))||D->SourceOnlyProperties.Contains(TEXT("Tint"))))
                    MID->SetVectorParameterValue(TEXT("Tint"),P.Tint);
                if(P.Kind==EMCVFXNativePartKind::ImpactFlames)MID->SetScalarParameterValue(TEXT("SelfGlow"),P.BurstFlame.SelfGlow);
                if(P.Kind==EMCVFXNativePartKind::FlightFlames)MID->SetScalarParameterValue(TEXT("SelfGlow"),P.Flame.SelfGlow);
                Candidate.MaterialTemplates.Add(Key(Event,P.PartID),MID);Candidate.Resources.Add(MID);
            }
        }return true;
    };
    for(auto& E:Candidate.Snapshot->Events)if(!PrepareParts(E.Key,E.Value.Parts))return false;
    if(!PrepareParts(NAME_None,Candidate.Snapshot->PersistentParts))return false;
    Candidate.Reference.SettingsHash=Source->ComputeSettingsHash();
    Out=MoveTemp(Candidate);return true;
}
bool FMCVFXPreparedNativePresentation::ResolveResidentReference(const FMCVFXNativePresentationRef& Ref,FName Expected,
    const TArray<FName>& Allowed,UObject* Outer,FMCVFXPreparedNativePresentation& Out,FString& Error)
{
    if(Ref.Source.IsNull()){Out=FMCVFXPreparedNativePresentation();return true;}
    const auto* Source=Ref.Source.Get(); // NEVER load an accepted cue synchronously.
    if(!Source || Ref.SettingsHash==0 || Source->ComputeSettingsHash()!=Ref.SettingsHash)
    {Error=TEXT("Native art source/version is not resident at the accepted event.");return false;}
    if(!Source->ValidateForSource(Expected,Allowed,Error))return false;
    FMCVFXPreparedNativePresentation Candidate;
    const auto* Loading=Outer && Outer->GetWorld()?Outer->GetWorld()->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
    if(!Loading || !Loading->GetPreparedNativeStyle(Source,Candidate) || Candidate.Reference.SettingsHash!=Ref.SettingsHash)
    {Error=TEXT("Native art snapshot/variants were not prepared before this accepted cue.");return false;}
    if(!Candidate.IsReady()){Error=TEXT("Native art resources were not ready before the accepted event.");return false;}
    Out=MoveTemp(Candidate);return true;
}
bool FMCVFXPreparedNativePresentation::IsReady() const
{
    for(UObject* O:Resources) {
        if(!IsValid(O))return false;
        if(const auto* NS=Cast<UNiagaraSystem>(O);NS && !NS->IsReadyToRun())return false;
#if WITH_EDITOR
        if(const auto* M=Cast<UMaterialInterface>(O);M && M->IsCompiling())return false;
#endif
    }return true;
}
bool FMCVFXPreparedNativePresentation::ShouldRenderNativeAccents(FName Event,EMCPerkVFXSlotMode Mode) const
{
    if(Mode==EMCPerkVFXSlotMode::UseDefault)return true;
    if(Mode==EMCPerkVFXSlotMode::Disabled || !Snapshot)return false;
    const auto* E=Snapshot->Events.Find(Event);return E && E->bKeepNativeAccentsWithComposition;
}
UMaterialInstanceDynamic* FMCVFXPreparedNativePresentation::CreatePrivateMaterial(FName Event,FName Part,UObject* Outer) const
{
    auto* Template=GetPreparedMaterial(Event,Part);
    if(!Template || !Outer)return nullptr;
    auto* MID=UMaterialInstanceDynamic::Create(Template->Parent,Outer);
    if(MID)MID->CopyInterpParameters(Template);return MID;
}
UMaterialInstanceDynamic* FMCVFXPreparedNativePresentation::GetPreparedMaterial(FName Event,FName Part)const
{const auto* Template=MaterialTemplates.Find(MCNativeArtPilot::Key(Event,Part));return Template?Template->Get():nullptr;}
const FMCVFXNativePartSettings* FMCVFXPreparedNativePresentation::FindPart(FName Event,FName Part) const
{return Snapshot?Snapshot->FindPart(Event,Part):nullptr;}
void FMCVFXPreparedNativePresentation::GetNiagaraSystems(TArray<UNiagaraSystem*>& Out) const
{for(UObject* O:Resources)if(auto* NS=Cast<UNiagaraSystem>(O))Out.AddUnique(NS);}
