#include "MCVFXNativeSources.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCPassiveFeedbackCue.h"
#include "MCPassiveCombatCue.h"
#include "MCVFXNativePresentationSettings.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"

namespace
{
void Add(TArray<FMCVFXNativeSourcePart>& Out,FName ID,const TCHAR* Label,EMCVFXNativeSourceKind Kind,const FSoftObjectPath& Asset)
{if(!Asset.IsNull()){FMCVFXNativeSourcePart P;P.ID=ID;P.Label=FText::FromString(Label);P.Kind=Kind;P.Asset=Asset;Out.Add(MoveTemp(P));}}
void Geometry(TArray<FMCVFXNativeSourcePart>& Out,FName ID,const TCHAR* Label,const TCHAR* File)
{FMCVFXNativeSourcePart P;P.ID=ID;P.Label=FText::FromString(Label);P.Kind=EMCVFXNativeSourceKind::Geometry;P.SourceFile=File;Out.Add(MoveTemp(P));}
}
FText MCVFXNativeSources::KindLabel(EMCVFXNativeSourceKind Kind)
{
    if(Kind==EMCVFXNativeSourceKind::Niagara)return FText::FromString(TEXT("Niagara"));
    if(Kind==EMCVFXNativeSourceKind::Material)return FText::FromString(TEXT("Материал"));
    return FText::FromString(TEXT("Геометрия"));
}
TArray<FMCVFXNativeSourcePart> MCVFXNativeSources::DescribeKnightShield(FName Event)
{
    TArray<FMCVFXNativeSourcePart> Out;if(Event!=TEXT("Hit") && Event!=TEXT("Break"))return Out;
    const auto* Cue=GetDefault<AMCNutCombatEffect>();
    Add(Out,TEXT("Contact.Dust"),TEXT("Пыль контакта"),EMCVFXNativeSourceKind::Niagara,Cue->DustImpactSystem.ToSoftObjectPath());
    Add(Out,TEXT("Contact.Sparks"),TEXT("Искры щита"),EMCVFXNativeSourceKind::Niagara,Cue->ShieldHitSystem.ToSoftObjectPath());
    Add(Out,TEXT("Contact.FlashMaterial"),TEXT("Вспышка контакта"),EMCVFXNativeSourceKind::Material,Cue->MagicMaterial.ToSoftObjectPath());
    if(Event==TEXT("Break"))Add(Out,TEXT("Break.FractureMaterial"),TEXT("Осколки скорлупы"),EMCVFXNativeSourceKind::Material,Cue->ShieldFractureMaterial.ToSoftObjectPath());
    Geometry(Out,TEXT("Contact.NativeGeometry"),Event==TEXT("Break")?TEXT("Вспышка, щепки и осколки"):TEXT("Вспышка и щепки"),TEXT("Source/MessControl/Private/MCNutCombatEffect.cpp"));
    return Out;
}
TArray<FMCVFXNativeSourcePart> MCVFXNativeSources::DescribeMageFireball(FName Event)
{
    TArray<FMCVFXNativeSourcePart> Out;const auto* Cue=GetDefault<AMCNutCombatEffect>();const auto* Shot=GetDefault<AMCNutSpellProjectile>();
    if(Event==TEXT("Charge")) {
        Add(Out,TEXT("Charge.Sparks"),TEXT("Искры накопления"),EMCVFXNativeSourceKind::Niagara,Cue->CastChargeSystem.ToSoftObjectPath());
        Add(Out,TEXT("Charge.FocusMaterial"),TEXT("Свечение в ладонях"),EMCVFXNativeSourceKind::Material,Cue->MagicMaterial.ToSoftObjectPath());
        Add(Out,TEXT("Charge.FlameMaterial"),TEXT("Огонь в ладонях"),EMCVFXNativeSourceKind::Material,Cue->FlameMaterial.ToSoftObjectPath());
        Geometry(Out,TEXT("Charge.NativeGeometry"),TEXT("Плоскости свечения и спирали"),TEXT("Source/MessControl/Private/MCNutCombatEffect.cpp"));
    } else if(Event==TEXT("Release")) {
        Add(Out,TEXT("Release.Burst"),TEXT("Вспышка выпуска"),EMCVFXNativeSourceKind::Niagara,Cue->ArcaneBurstSystem.ToSoftObjectPath());
        Add(Out,TEXT("Release.Material"),TEXT("Свечение выпуска"),EMCVFXNativeSourceKind::Material,Cue->MagicMaterial.ToSoftObjectPath());
        Geometry(Out,TEXT("Release.NativeGeometry"),TEXT("Плоскости выпуска"),TEXT("Source/MessControl/Private/MCNutCombatEffect.cpp"));
    } else if(Event==TEXT("Flight")) {
        Add(Out,TEXT("Flight.Embers"),TEXT("Угли в шлейфе"),EMCVFXNativeSourceKind::Niagara,Shot->TrailSystem.ToSoftObjectPath());
        Add(Out,TEXT("Flight.Flames"),TEXT("Огненные ленты"),EMCVFXNativeSourceKind::Material,Shot->FlameMaterial.ToSoftObjectPath());
        Add(Out,TEXT("Flight.Core"),TEXT("Горячая сердцевина"),EMCVFXNativeSourceKind::Material,Shot->CoreMaterial.ToSoftObjectPath());
        Add(Out,TEXT("Flight.Wake"),TEXT("Огненный след"),EMCVFXNativeSourceKind::Material,Shot->WakeMaterial.ToSoftObjectPath());
        Geometry(Out,TEXT("Flight.NativeGeometry"),TEXT("Орех, кольца и ленты"),TEXT("Source/MessControl/Private/MCNutSpellProjectile.cpp"));
    } else if(Event==TEXT("Impact")) {
        Add(Out,TEXT("Impact.Burst"),TEXT("Огненное попадание"),EMCVFXNativeSourceKind::Niagara,Cue->FireImpactSystem.ToSoftObjectPath());
        Add(Out,TEXT("Impact.FlameMaterial"),TEXT("Пламя попадания"),EMCVFXNativeSourceKind::Material,Cue->FlameMaterial.ToSoftObjectPath());
        // FireImpact has no Magic geometry branch. Its real protected-area
        // renderer uses the telegraph material; footprint dimensions stay native.
        Add(Out,TEXT("Impact.FootprintMaterial"),TEXT("Материал зоны · форма от игры"),EMCVFXNativeSourceKind::Material,Cue->TelegraphMaterial.ToSoftObjectPath());
        Geometry(Out,TEXT("Impact.NativeGeometry"),TEXT("Вспышка и осколки"),TEXT("Source/MessControl/Private/MCNutCombatEffect.cpp"));
    }
    return Out;
}
TArray<FMCVFXNativeSourcePart> MCVFXNativeSources::DescribePassiveFeedback(FName SourceID,FName Event)
{
    TArray<FMCVFXNativeSourcePart> Out;EMCPassiveFeedbackKind Kind;
    if(SourceID==TEXT("B2_ThunderGod") && Event==TEXT("Link")) {
        Add(Out,TEXT("LightningStroke"),TEXT("Родная молния · общий материал лент"),EMCVFXNativeSourceKind::Material,FSoftObjectPath(GetDefault<AMCPassiveCombatCue>()->LinkMaterial.Get()));
        Geometry(Out,TEXT("Lightning.NativeGeometry"),TEXT("Настоящая молния · seed и концы из игры"),TEXT("Source/MessControl/Private/MCPassiveCombatCue.cpp"));
        return Out;
    }
    if(!UMCVFXNativePresentationSettings::GetPassiveFeedbackKind(SourceID,Event,Kind))return Out;
    const auto* Cue=GetDefault<AMCPassiveFeedbackCue>();
    for(const auto& Part:UMCVFXNativePresentationSettings::DescribeParts(SourceID,Event)) {
        auto* Material=Part.Kind==EMCVFXNativePartKind::PassiveSoft?Cue->SoftMaterial.Get():
            Part.Kind==EMCVFXNativePartKind::PassiveGlow?Cue->GlowMaterial.Get():Cue->SolidMaterial.Get();
        Add(Out,Part.PartID,*Part.Label.ToString(),EMCVFXNativeSourceKind::Material,FSoftObjectPath(Material));
    }
    Geometry(Out,TEXT("Feedback.NativeGeometry"),TEXT("Настоящие формы пассивки · исходный генератор"),TEXT("Source/MessControl/Private/MCPassiveFeedbackCue.cpp"));
    return Out;
}
