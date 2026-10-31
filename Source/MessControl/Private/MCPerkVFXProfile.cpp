#include "MCPerkVFXProfile.h"

namespace MCPerkVFXProfilePrivate
{
template<typename T> bool ValidateNames(const TArray<T>& Values,TSet<FName>& Names,FString& Error)
{
    for(const auto& Item:Values) {
        if(!Item.bOverride) continue;
        if(!Item.Name.ToString().StartsWith(TEXT("User.")) || Names.Contains(Item.Name)) {
            Error=FString::Printf(TEXT("%s must be a unique canonical User.* name."),*Item.Name.ToString());return false;
        }
        Names.Add(Item.Name);
    }
    return true;
}
}

TArray<FName> UMCPerkVFXProfile::GetSupportedStages()
{return {TEXT("Start"),TEXT("Loop"),TEXT("Impact"),TEXT("End"),TEXT("Warning")};}

bool UMCPerkVFXProfile::IsSupportedStage(FName Stage)
{return Stage==TEXT("Start") || Stage==TEXT("Loop") || Stage==TEXT("Impact") || Stage==TEXT("End") || Stage==TEXT("Warning");}

FMCVFXCompositionLayer UMCPerkVFXProfile::AsLayer(const FMCPerkVFXSlot& Slot)
{
    FMCVFXCompositionLayer Layer;
    Layer.System=Slot.System;Layer.Transform=Slot.Transform;
    Layer.Floats=Slot.Floats;Layer.Colors=Slot.Colors;Layer.Vectors=Slot.Vectors;Layer.Bools=Slot.Bools;
    return Layer;
}

bool UMCPerkVFXProfile::ValidateSlot(const FMCPerkVFXSlot& Slot,FString& OutError)
{
    OutError.Reset();
    if(Slot.Mode!=EMCPerkVFXSlotMode::UseDefault && Slot.Mode!=EMCPerkVFXSlotMode::Replace && Slot.Mode!=EMCPerkVFXSlotMode::Disabled
        && Slot.Mode!=EMCPerkVFXSlotMode::Composition)
    {OutError=TEXT("Unknown VFX slot mode.");return false;}
    if(Slot.Mode!=EMCPerkVFXSlotMode::Replace && Slot.Mode!=EMCPerkVFXSlotMode::Composition) return true;
    if(Slot.Mode==EMCPerkVFXSlotMode::Replace && Slot.System.IsNull()) {OutError=TEXT("Replacement needs a Niagara System.");return false;}
    if(Slot.Mode==EMCPerkVFXSlotMode::Composition && Slot.Composition.IsNull()) {OutError=TEXT("Composition slot needs a runtime VFX composition.");return false;}
    if(Slot.Transform.ContainsNaN() || !Slot.Transform.GetRotation().IsNormalized() || Slot.Transform.GetScale3D().GetAbsMin()<.0001)
    {OutError=TEXT("Offset must be finite with normalized rotation and nonzero scale.");return false;}
    if(Slot.Floats.Num()+Slot.Colors.Num()+Slot.Vectors.Num()+Slot.Bools.Num()>64)
    {OutError=TEXT("A VFX slot accepts at most 64 User parameter entries.");return false;}
    TSet<FName> Names;
    using namespace MCPerkVFXProfilePrivate;
    if(!ValidateNames(Slot.Floats,Names,OutError) || !ValidateNames(Slot.Colors,Names,OutError)
        || !ValidateNames(Slot.Vectors,Names,OutError) || !ValidateNames(Slot.Bools,Names,OutError)) return false;
    for(const auto& Item:Slot.Floats) if(Item.bOverride && !FMath::IsFinite(Item.Value))
    {OutError=TEXT("Float override must be finite.");return false;}
    for(const auto& Item:Slot.Colors) if(Item.bOverride && (!FMath::IsFinite(Item.Value.R) || !FMath::IsFinite(Item.Value.G)
        || !FMath::IsFinite(Item.Value.B) || !FMath::IsFinite(Item.Value.A)))
    {OutError=TEXT("Color override must be finite.");return false;}
    for(const auto& Item:Slot.Vectors) if(Item.bOverride && Item.Value.ContainsNaN())
    {OutError=TEXT("Vector override must be finite.");return false;}
    return true;
}

bool UMCPerkVFXProfile::Validate(FString& OutError) const
{
    OutError.Reset();
    if(Slots.Num()>5) {OutError=TEXT("This action contract supports five named VFX stages.");return false;}
    for(const auto& Pair:Slots) {
        if(!IsSupportedStage(Pair.Key)) {OutError=FString::Printf(TEXT("Unknown action VFX stage: %s"),*Pair.Key.ToString());return false;}
        if(!ValidateSlot(Pair.Value,OutError)) {OutError=Pair.Key.ToString()+TEXT(": ")+OutError;return false;}
    }
    return true;
}

bool UMCPerkVFXProfile::DiscoverParameters(FName Stage,FString& OutError)
{
    auto* Slot=Slots.Find(Stage);
    if(!IsSupportedStage(Stage) || !Slot || Slot->Mode!=EMCPerkVFXSlotMode::Replace)
    {OutError=TEXT("Choose a replacement System in a supported slot first.");return false;}
    auto* Scratch=NewObject<UMCVFXComposition>(GetTransientPackage());
    Scratch->Layers.Add(AsLayer(*Slot));
    if(!Scratch->DiscoverParameters(0,OutError)) return false;
    const auto& Layer=Scratch->Layers[0];
    Slot->Floats=Layer.Floats;Slot->Colors=Layer.Colors;Slot->Vectors=Layer.Vectors;Slot->Bools=Layer.Bools;
    return true;
}
