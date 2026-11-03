#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCVFXNativePresentationSettings.generated.h"
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UNiagaraSystem;
class UCurveFloat;
enum class EMCPerkVFXSlotMode : uint8;
enum class EMCPassiveFeedbackKind : uint8;

UENUM(BlueprintType)
enum class EMCVFXNativePartKind : uint8
{
    ShieldFlash, ShellDebris, ShellShards, ShieldCracks,
    CastFocus, CastRelease, FlightFlames, FlightHalo, FlightWake,
    ImpactFlames, ImpactDebris, ContactParticles, FloorDust, DangerRing, ProjectileBody,
    PassiveSoft, PassiveGlow, PassiveSolid, PassiveLightning
};

/** Cosmetic sizing/clock only. Gameplay duration, contact frames and link endpoints are never changed. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeFeedbackShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") float Scale=1;
    /** Shared feedback clock: Solid for feedback cues, LightningStroke for a link. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing") float DurationScale=1;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeContactShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Corners=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float OuterRadiusFraction=.20f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MinRadius=16;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MaxRadius=42;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float InnerRadiusRatio=.32f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float DepthOffset=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float ExpandSeconds=.035f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float FlashBeginSeconds=.025f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float FlashEndSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 SplinterBranches=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SplinterWidthA=1.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SplinterWidthB=.8f;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeDebrisShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Count=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MinSpeed=85;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MaxSpeed=160;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MinSize=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MaxSize=4.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float Gravity=380;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float UpSpeed=65;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float RadialSpread=.6f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float Damping=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float FadeStartFraction=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float RadiusFractionMin=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float RadiusFractionMax=.23f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SizeClampMin=11;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SizeClampMax=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float ForwardFraction=.65f;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeFlameShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Layers=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Ribbons=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Rows=18;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float InnerRadiusFraction=.96f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float OuterRadiusFraction=1.20f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float InnerWidthFraction=.34f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float OuterWidthFraction=.29f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float InnerOpacity=.52f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float OuterOpacity=.29f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MinLengthRadii=2.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MaxLengthRadii=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SelfGlow=.95f;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeHaloShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Rings=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Segments=24;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float InnerRadiusFraction=.90f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float OuterRadiusFraction=1.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float FirstOpacity=.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SecondOpacity=.13f;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeBurstFlameShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Shape") int32 Ribbons=4;
    UPROPERTY(EditAnywhere,Category="Shape") int32 Rows=8;
    UPROPERTY(EditAnywhere,Category="Shape") float BaseWidth=.80f;
    UPROPERTY(EditAnywhere,Category="Shape") float TipWidthLoss=.42f;
    UPROPERTY(EditAnywhere,Category="Shape") float HeightRadii=3.8f;
    UPROPERTY(EditAnywhere,Category="Shape") float SwayRadii=.20f;
    UPROPERTY(EditAnywhere,Category="Shape") float SwayFrequency=7;
    UPROPERTY(EditAnywhere,Category="Shape") float SwaySpeed=8;
    UPROPERTY(EditAnywhere,Category="Shape") float MinChargeRadius=8;
    UPROPERTY(EditAnywhere,Category="Shape") float MaxChargeRadius=24;
    UPROPERTY(EditAnywhere,Category="Shape") float ImpactRadiusFraction=.45f;
    UPROPERTY(EditAnywhere,Category="Shape") float MaxImpactRadius=65;
    UPROPERTY(EditAnywhere,Category="Material") float SelfGlow=.95f;
};
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeImpactDebrisShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Shape") int32 Count=6;
    UPROPERTY(EditAnywhere,Category="Shape") float SizeFraction=.08f;
    UPROPERTY(EditAnywhere,Category="Shape") float SpreadFraction=.8f;
    UPROPERTY(EditAnywhere,Category="Shape") float HopHeight=65;
    UPROPERTY(EditAnywhere,Category="Shape") float PitchRate=160;
    UPROPERTY(EditAnywhere,Category="Shape") float YawRate=90;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeWakeShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Segments=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float HistorySeconds=.42f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float WidthFraction=.26f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float Opacity=.34f;
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeCastShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Arms=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") int32 Segments=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MinFocusRadius=14;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float MaxFocusRadius=40;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float SpinSpeed=3.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float ReleaseMinLength=35;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float ReleaseMaxLength=110;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shape") float ReleaseWidth=12;
};

/** Exact five-column authored strip data; NOT a replacement spline. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeCrackSample
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Authored geometry") TArray<FVector> Points;
    UPROPERTY(EditAnywhere,Category="Authored geometry") TArray<FVector> Normals;
};
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeCrackPath
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Authored geometry") int32 First=0;
    UPROPERTY(EditAnywhere,Category="Authored geometry") int32 Count=0;
    UPROPERTY(EditAnywhere,Category="Authored geometry") float RevealAt=0;
    UPROPERTY(EditAnywhere,Category="Authored geometry") float FullAt=1;
    UPROPERTY(EditAnywhere,Category="Authored geometry") bool bRim=false;
};
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeCracksShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Shape") float WidthScale=1;
    UPROPERTY(EditAnywhere,Category="Shape") float LipLift=.18f;
    UPROPERTY(EditAnywhere,Category="Shape") float BlendSeconds=.14f;
    UPROPERTY(EditAnywhere,Category="Shape") FLinearColor CoreColor=FLinearColor(.009f,.004f,.002f);
    UPROPERTY(EditAnywhere,Category="Shape") FLinearColor LipColor=FLinearColor(.31f,.16f,.066f);
    UPROPERTY(EditAnywhere,Category="Shape") FLinearColor RimColor=FLinearColor(.48f,.20f,.035f);
    UPROPERTY(EditAnywhere,Category="Authored geometry") TArray<FMCVFXNativeCrackSample> Samples;
    UPROPERTY(EditAnywhere,Category="Authored geometry") TArray<FMCVFXNativeCrackPath> Paths;
};

/** Only controls listed by the renderer descriptor are editable for this part. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativePartSettings
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,Category="Identity") FName PartID;
    UPROPERTY(VisibleAnywhere,Category="Identity") EMCVFXNativePartKind Kind=EMCVFXNativePartKind::ShieldFlash;
    UPROPERTY(EditAnywhere,Category="Part") bool bEnabled=true;
    UPROPERTY(EditAnywhere,Category="Resources") TSoftObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere,Category="Resources") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere,Category="Resources") TSoftObjectPtr<UNiagaraSystem> System;
    /** Real Tint only on Arcane/Telegraph. Core/flame have no Tint binding. */
    UPROPERTY(EditAnywhere,Category="Appearance") FLinearColor Tint=FLinearColor::White;
    /** Only vertex alpha users; masked fracture is explicitly cutout, never soft fade. */
    UPROPERTY(EditAnywhere,Category="Appearance") float OpacityScale=1;
    /** Actual native PlayBurst scale; not particle lifetime or a gameplay radius. */
    UPROPERTY(EditAnywhere,Category="Appearance") float SystemScale=1;
    UPROPERTY(EditAnywhere,Category="Appearance") TSoftObjectPtr<UCurveFloat> OpacityCurve;
    UPROPERTY(EditAnywhere,Category="Appearance") TSoftObjectPtr<UCurveFloat> ExpansionCurve;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeContactShape Contact;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeDebrisShape Debris;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeFlameShape Flame;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeBurstFlameShape BurstFlame;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeImpactDebrisShape ImpactDebris;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeHaloShape Halo;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeWakeShape Wake;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeCastShape Cast;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeCracksShape Cracks;
    UPROPERTY(EditAnywhere,Category="Shape") FMCVFXNativeFeedbackShape Feedback;
};
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVFXNativeEventSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Native parts") TArray<FMCVFXNativePartSettings> Parts;
    /** Explicit additive path; default false preserves existing Composition suppression. */
    UPROPERTY(EditAnywhere,Category="Native parts") bool bKeepNativeAccentsWithComposition=false;
};
struct MESSCONTROL_API FMCVFXNativePartDescriptor
{
    FName PartID;
    EMCVFXNativePartKind Kind;
    FText Label;
    TArray<FName> EditableProperties;
    TArray<FName> SourceOnlyProperties;
    TMap<FName,FVector2D> NumericBounds;
    bool bProtectedGeometry=false;
    bool bMaskedCutout=false;
};
/** Native runtime authoring data, not an editor-only Recipe or opaque fallback. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCVFXNativePresentationSettings : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere,Category="Identity") int32 SchemaVersion=1;
    UPROPERTY(VisibleAnywhere,Category="Identity") FName SourceID;
    UPROPERTY(EditAnywhere,Category="Native events") TMap<FName,FMCVFXNativeEventSettings> Events;
    /** Shield cracks: real replicated ShieldHealth observer, not fake Hit/Break burst. */
    UPROPERTY(EditAnywhere,Category="Persistent native state") TArray<FMCVFXNativePartSettings> PersistentParts;
    bool ValidateForSource(FName ExpectedSource,const TArray<FName>& AllowedEvents,FString& Error) const;
    bool InitFromNativeDefaults(FName Source,FString& Error,bool bKeepWithComposition=false);
    /** Shared with provider metadata; UI never switches on a private list of skill IDs. */
    static TArray<FMCVFXNativePartDescriptor> DescribeParts(FName Source,FName Event);
    /** Exact native cue identity, shared by runtime, original-source rows and providers. */
    static bool GetPassiveFeedbackKind(FName Source,FName Event,EMCPassiveFeedbackKind& OutKind);
    const FMCVFXNativePartSettings* FindPart(FName Event,FName PartID) const;
    uint64 ComputeSettingsHash() const;
};

/** Already-discovered saved-source token; native cue actor carries it once. */
USTRUCT()
struct MESSCONTROL_API FMCVFXNativePresentationRef
{
    GENERATED_BODY()
    UPROPERTY() TSoftObjectPtr<UMCVFXNativePresentationSettings> Source;
    UPROPERTY() uint64 SettingsHash=0;
};
/** Local immutable snapshot. Private MIDs per consumer copy uniforms from these prepared templates. */
USTRUCT()
struct MESSCONTROL_API FMCVFXPreparedNativePresentation
{
    GENERATED_BODY()
    UPROPERTY() FMCVFXNativePresentationRef Reference;
    UPROPERTY() TObjectPtr<UMCVFXNativePresentationSettings> Snapshot;
    UPROPERTY() TArray<TObjectPtr<UObject>> Resources;
    UPROPERTY() TMap<FName,TObjectPtr<UMaterialInstanceDynamic>> MaterialTemplates;
    /** Explicit preparation only; resolving an accepted cue calls this with bAllowLoad=false. */
    static bool Prepare(FName ExpectedSource,const TArray<FName>& AllowedEvents,
        const UMCVFXNativePresentationSettings* Source,UObject* Outer,bool bAllowLoad,
        FMCVFXPreparedNativePresentation& Out,FString& Error);
    static bool ResolveResidentReference(const FMCVFXNativePresentationRef& Reference,FName ExpectedSource,
        const TArray<FName>& AllowedEvents,UObject* Outer,FMCVFXPreparedNativePresentation& Out,FString& Error);
    bool IsReady() const;
    bool ShouldRenderNativeAccents(FName Event,EMCPerkVFXSlotMode SlotMode) const;
    /** Immutable retained variant for renderers whose animation is entirely vertex-driven. Never change its parameters. */
    UMaterialInstanceDynamic* GetPreparedMaterial(FName Event,FName PartID) const;
    UMaterialInstanceDynamic* CreatePrivateMaterial(FName Event,FName PartID,UObject* Outer) const;
    const FMCVFXNativePartSettings* FindPart(FName Event,FName PartID) const;
    void GetNiagaraSystems(TArray<UNiagaraSystem*>& Out) const;
};
