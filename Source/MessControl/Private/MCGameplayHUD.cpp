#include "MCGameplayHUD.h"
#include "MCBoosterWidget.h"
#include "MCBossHealthWidget.h"
#include "MCUIDataLibrary.h"
#include "MCGameplayUIFactory.h"
#include "MCUIVisualStyle.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MCGamepadHints.h"
#include "MCStaminaWidget.h"
#include "MCGameplayAlertWidget.h"
#include "MCColdCola.h"
#include "MCIceEvent.h"
#include "MCNutRainEvent.h"
#include "MCNutBoss.h"
#include "MCBossCharacter.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCSingleDayDirector.h"
#include "MCProgressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCGripComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "MCThroat.h"
#include "MCCoffeeFlood.h"
#include "MCCataclysmEvent.h"
#include "MCCataclysmRaft.h"
#include "MCFogBrawlEvent.h"
#include "MCRewardChest.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/PrimitiveComponent.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Internationalization/Internationalization.h"
#include "HAL/IConsoleManager.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/GameStateBase.h"
#include "Widgets/SWidget.h"
#include "Layout/Clipping.h"

#define LOCTEXT_NAMESPACE "MCGameplayHUD"

// Unity builds combine this file with the prototype widgets and mouth renderer.
namespace MCGameplayHUDPrivate
{
TAutoConsoleVariable<int32> DirectorDebug(TEXT("MC.Director.Debug"),0,
    TEXT("1: show Director observations, candidate weights/probabilities and recent decisions; 0: compact monitor."),ECVF_Default);
const FLinearColor Ink(.012f,.020f,.033f,.90f),White(.98f,.97f,.93f),Muted(.56f,.66f,.72f),Mint(.26f,.91f,.71f),Amber(1.f,.64f,.23f),Blue(.22f,.65f,1.f);
FString PacingName(EMCGameDirectorPacing Pacing)
{
    switch(Pacing) {
    case EMCGameDirectorPacing::Build:return TEXT("НАРАСТАНИЕ");
    case EMCGameDirectorPacing::Drain:return TEXT("РАЗГРУЗКА");
    case EMCGameDirectorPacing::Rest:return TEXT("ПЕРЕДЫШКА");
    case EMCGameDirectorPacing::FinalCleanup:return TEXT("ФИНАЛЬНАЯ УБОРКА");
    default:return TEXT("ПЕРЕРЫВ"); }
}
FString ShortLine(FString Value,int32 Limit)
{
    Value.ReplaceInline(TEXT("\r"),TEXT(" "));Value.ReplaceInline(TEXT("\n"),TEXT(" "));
    return Value.Len()>Limit?Value.Left(Limit-1)+TEXT("…"):Value;
}
FString CandidateName(EMCGameDirectorEvent Kind)
{
    switch(Kind) {
    case EMCGameDirectorEvent::Food:return TEXT("Еда");
    case EMCGameDirectorEvent::Coffee:return TEXT("Пятна кофе");
    case EMCGameDirectorEvent::CoffeeFlood:return TEXT("Катаклизм");
    case EMCGameDirectorEvent::ColdCola:return TEXT("Кола");
    case EMCGameDirectorEvent::Yawn:return TEXT("Зевание");
    case EMCGameDirectorEvent::Pepper:return TEXT("Перец");
    case EMCGameDirectorEvent::StuckFood:return TEXT("Застр. еда");
    case EMCGameDirectorEvent::LooseTooth:return TEXT("Шаткий зуб");
    case EMCGameDirectorEvent::Reward:return TEXT("Сундук");
    default:return TEXT("Босс"); }
}
float CandidateProbability(const FMCGameDirectorCandidate& Candidate)
{
    return Candidate.EffectiveWeight>0?FMath::Clamp(Candidate.Probability,0.f,1.f):0.f;
}
int32 Percent(float Value) { return FMath::RoundToInt(FMath::Clamp(Value,0.f,1.f)*100); }
FText StepName(EMCDayStep Step,UDataTable* Table,const FString& Culture=FString())
{
    auto Copy=[&](FName Key,const FText& Fallback){return UMCUIDataLibrary::ResolveText(Table,Key,Fallback,Culture);};
    switch(Step) {
    case EMCDayStep::BrushLesson:return Copy(TEXT("UI.HUD.Step.BrushLesson"),LOCTEXT("StepBrushLesson","ВРЕМЯ ЧИСТИТЬ"));
    case EMCDayStep::DiscardBrushes:return Copy(TEXT("UI.HUD.Step.DiscardBrushes"),LOCTEXT("StepDiscardBrushes","ЩЁТКИ ЗА БОРТ"));
    case EMCDayStep::BreakfastRain:return Copy(TEXT("UI.HUD.Step.BreakfastRain"),LOCTEXT("StepBreakfastRain","ЗАВТРАК ПАДАЕТ"));
    case EMCDayStep::BreakfastCleanup:return Copy(TEXT("UI.HUD.Step.BreakfastCleanup"),LOCTEXT("StepBreakfastCleanup","УБРАТЬ ОСТАТКИ"));
    case EMCDayStep::CoffeeWaves:return Copy(TEXT("UI.HUD.Step.CoffeeWaves"),LOCTEXT("StepCoffeeWaves","ЦУНАМИ"));
    case EMCDayStep::CoffeeCleanup:return Copy(TEXT("UI.HUD.Step.CoffeeCleanup"),LOCTEXT("StepCoffeeCleanup","СМЫТЬ КОФЕ"));
    case EMCDayStep::ColdCola:return Copy(TEXT("UI.HUD.Step.ColdCola"),LOCTEXT("StepColdCola","ХОЛОДНАЯ КОЛА"));
    case EMCDayStep::StuckFood:return Copy(TEXT("UI.HUD.Step.StuckFood"),LOCTEXT("StepStuckFood","МЕЖДУ ЗУБАМИ"));
    default:return Copy(TEXT("UI.HUD.Step.Complete"),LOCTEXT("StepComplete","ДЕНЬ ЗАВЕРШЁН")); }
}
FName CataclysmTitleKey(const AMCCataclysmEvent& Event)
{
    switch(Event.Phase) {
    case EMCCataclysmPhase::Complete:return TEXT("UI.Event.Cataclysm.Complete.Title");
    case EMCCataclysmPhase::Cleanup:return TEXT("UI.Event.Cataclysm.Cleanup.Title");
    case EMCCataclysmPhase::JellyFight:return TEXT("UI.Event.Cataclysm.Jellies.Title");
    case EMCCataclysmPhase::Flooding:case EMCCataclysmPhase::BranchWarning:return TEXT("UI.Event.Cataclysm.Rafts.Title");
    default:return Event.bCola?TEXT("UI.Event.Cataclysm.ColaWave.Title"):TEXT("UI.Event.Cataclysm.CoffeeWave.Title");
    }
}
FName CataclysmInstructionKey(const AMCCataclysmEvent& Event)
{
    switch(Event.Phase) {
    case EMCCataclysmPhase::Complete:return TEXT("UI.Event.Cataclysm.Complete.Instruction");
    case EMCCataclysmPhase::Cleanup:return TEXT("UI.Event.Cataclysm.Cleanup.Instruction");
    case EMCCataclysmPhase::JellyFight:return TEXT("UI.Event.Cataclysm.Jellies.Instruction");
    case EMCCataclysmPhase::Flooding:case EMCCataclysmPhase::BranchWarning:return TEXT("UI.Event.Cataclysm.Rafts.Instruction");
    default:return TEXT("UI.Event.Cataclysm.Warning.Instruction");
    }
}
struct FHUDPainter
{
    const FGeometry& G; FSlateWindowElementList& E; int32 L; float S;
    void Box(float X,float Y,float W,float H,FLinearColor C,float Radius=12) const {
        if(W<=0 || H<=0) return;
        const FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius*S);
        FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(FVector2D(W*S,H*S),FSlateLayoutTransform(FVector2D(X*S,Y*S))),&Brush,ESlateDrawEffect::None,C);
    }
    void Text(float X,float Y,const FString& T,int32 Size,FLinearColor C=White) const {
        FSlateDrawElement::MakeText(E,L+1,G.ToPaintGeometry(FVector2D(1000*S,80*S),FSlateLayoutTransform(FVector2D(X*S,Y*S))),T,FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(8,FMath::RoundToInt(Size*S))),ESlateDrawEffect::None,C);
    }
    void Line(TArray<FVector2D> Points,FLinearColor C=White,float Thickness=3) const {
        for(auto& P:Points) P*=S;
        FSlateDrawElement::MakeLines(E,L+2,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Thickness*S);
    }
    void Bar(float X,float Y,float W,float H,float Value,FLinearColor C) const {
        Box(X,Y,W,H,FLinearColor(.004f,.008f,.013f,.95f),H/2);
        Box(X+2,Y+2,(W-4)*FMath::Clamp(Value,0.f,1.f),H-4,C,(H-4)/2);
    }
    void Tool(float X,float Y,int32 Slot,FLinearColor C=White,float Size=1,FLinearColor DetailInk=Ink) const {
        auto Path=[&](std::initializer_list<FVector2D> P,float Width=4) { TArray<FVector2D> V; for(auto Q:P) V.Add(FVector2D(X,Y)+Q*Size); Line(V,C,Width*Size); };
        if(Slot==0) {
            Path({{-13,20},{9,-17},{18,-11},{-5,24},{-13,20}},4);
            for(int I=0;I<4;++I) Path({{7.f+I*3,-14.f+I*2},{12.f+I*3,-23.f+I*2}},3);
        } else if(Slot==1) {
            Path({{-14,22},{9,-13}},7); Path({{-22,-8},{-13,-17},{1,-21},{17,-15},{25,-5}},6);
        } else if(Slot==2) {
            Path({{-16,23},{-4,6}},7); Path({{-4,6},{14,-25},{20,-8},{5,10},{-4,6}},4);
        } else {
            Box(X-13*Size,Y-4*Size,26*Size,30*Size,C,5*Size);
            Path({{-6,-6},{-6,-16},{9,-16},{9,-8}},5); Path({{9,-16},{19,-16}},4);
            Line({{X-7*Size,Y+10*Size},{X+7*Size,Y+10*Size}},DetailInk,3*Size);
            Line({{X,Y+3*Size},{X,Y+17*Size}},DetailInk,3*Size);
        }
    }
    void Face(float X,float Y,float Size,bool Dead,bool Sad,bool Happy,float Time,FLinearColor PlayerTint,FLinearColor InkColor=FLinearColor(.028f,.030f,.043f)) const {
        const float Bob=Dead?0:Happy?FMath::Sin(Time*9)*2:FMath::Sin(Time*2)*.65f;
        Y+=Bob;
        // A two-crown silhouette with separate roots, kept readable at portrait size.
        const FLinearColor BodyTint=Dead?PlayerTint.Desaturate(.8f)*.6f:PlayerTint;
        Box(X-22*Size,Y-22*Size,44*Size,41*Size,BodyTint,13*Size);
        Box(X-19*Size,Y+3*Size,15*Size,32*Size,BodyTint,7*Size);
        Box(X+4*Size,Y+3*Size,15*Size,32*Size,BodyTint,7*Size);
        const FLinearColor Eye=InkColor;
        for(float DX:{-9.f,9.f}) {
            if(Dead) { Line({{X+(DX-4)*Size,Y-6*Size},{X+(DX+4)*Size,Y+2*Size}},Eye,3*Size); Line({{X+(DX+4)*Size,Y-6*Size},{X+(DX-4)*Size,Y+2*Size}},Eye,3*Size); }
            else if(Happy) Line({{X+(DX-4)*Size,Y-2*Size},{X+DX*Size,Y-6*Size},{X+(DX+4)*Size,Y-2*Size}},Eye,2.5f*Size);
            else Box(X+(DX-2)*Size,Y-5*Size,4*Size,5*Size,Eye,2*Size);
        }
        Line({{X-6*Size,Y+(Sad?12:6)*Size},{X,Y+(Sad?8:Happy?14:10)*Size},{X+6*Size,Y+(Sad?12:6)*Size}},Eye,2.4f*Size);
    }
};
}
void UMCHUDIcon::NativePreConstruct()
{
    Super::NativePreConstruct();
    if(!GlyphMaterial) if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/UI/Materials/M_UI_Phase1Glyph.M_UI_Phase1Glyph"),nullptr,LOAD_NoWarn))
    {
        Material->EnsureIsComplete();
        GlyphMaterial=UMaterialInstanceDynamic::Create(Material,this);
    }
    if(GlyphMaterial)
    {
        GlyphBrush.SetResourceObject(GlyphMaterial);GlyphBrush.DrawAs=ESlateBrushDrawType::Image;
        GlyphBrush.ImageSize=FVector2D(80);GlyphBrush.TintColor=FLinearColor::White;
    }
}

int32 UMCHUDIcon::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    if(GlyphMaterial)
    {
        const auto Size=Geometry.GetLocalSize();
        GlyphMaterial->SetVectorParameterValue(TEXT("PanelSize"),FLinearColor(Size.X,Size.Y,0,0));
        GlyphMaterial->SetVectorParameterValue(TEXT("SurfaceColor"),Tint);
        GlyphMaterial->SetVectorParameterValue(TEXT("InkColor"),InkTint);
        GlyphMaterial->SetVectorParameterValue(TEXT("EmptyColor"),EmptyTint);
        GlyphMaterial->SetVectorParameterValue(TEXT("HostColor"),HostTint);
        GlyphMaterial->SetVectorParameterValue(TEXT("HighlightColor"),FLinearColor(1.f,1.f,.95f,Tint.A*.7f));
        GlyphMaterial->SetScalarParameterValue(TEXT("GlyphKind"),bReserveTooth?1.f:bTooth?0.f:ToolSlot+2.f);
        GlyphMaterial->SetScalarParameterValue(TEXT("HealthFraction"),HealthFraction);
        GlyphMaterial->SetScalarParameterValue(TEXT("IsLost"),bLost?1.f:0.f);
        GlyphMaterial->SetScalarParameterValue(TEXT("IsDead"),bDead?1.f:0.f);
        GlyphMaterial->SetScalarParameterValue(TEXT("IsHost"),bHost?1.f:0.f);
        GlyphMaterial->SetScalarParameterValue(TEXT("IsSad"),bSad?1.f:0.f);
        GlyphMaterial->SetScalarParameterValue(TEXT("IsHappy"),bHappy?1.f:0.f);
        FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),&GlyphBrush,ESlateDrawEffect::None,FLinearColor(1,1,1,Style.GetColorAndOpacityTint().A));
        return Layer+1;
    }
    const float S=FMath::Min(Geometry.GetLocalSize().X/80,Geometry.GetLocalSize().Y/80);
    MCGameplayHUDPrivate::FHUDPainter P{Geometry,Elements,Layer,S};
    if(bReserveTooth) {
        P.Face(40,34,.95f,false,false,false,0,EmptyTint,InkTint);
        if(!bLost && HealthFraction>0.f) {
            const FVector2D Min=Geometry.LocalToAbsolute(FVector2D(0,Geometry.GetLocalSize().Y*(1-FMath::Clamp(HealthFraction,0.f,1.f))));
            const FVector2D Max=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
            Elements.PushClip(FSlateClippingZone(FSlateRect(Min.X,Min.Y,Max.X,Max.Y)));
            P.Face(40,34,.95f,false,false,false,0,Tint,InkTint);Elements.PopClip();
        }
        if(bLost) {P.Line({{18,14},{62,65}},InkTint,5);P.Line({{62,14},{18,65}},InkTint,5);}
        return Layer+3;
    }
    if(bTooth) {
        P.Face(40,34,.95f,bDead,bSad,bHappy,GetWorld()?GetWorld()->GetTimeSeconds():0,Tint,InkTint);
        if(bHost) P.Line({{26,9},{23,1},{32,6},{40,0},{48,6},{57,1},{54,9},{26,9}},HostTint,3);
    }
    else P.Tool(40,40,ToolSlot,Tint,1.15f,InkTint);
    return Layer+3;
}
namespace MCGameplayHUDPresentationPrivate
{
    UCanvasPanelSlot* Place(UCanvasPanel* Parent,UWidget* Child,FVector2D Position,FVector2D Size,FVector2D Anchor=FVector2D::ZeroVector)
    {
        auto* CanvasSlot=Parent->AddChildToCanvas(Child);CanvasSlot->SetAnchors(FAnchors(Anchor.X,Anchor.Y));CanvasSlot->SetPosition(Position);CanvasSlot->SetSize(Size);return CanvasSlot;
    }
}

void UMCReserveToothWidget::BuildPresentationTree()
{
    if(!WidgetTree || Glyph) return;
    if(WidgetTree->RootWidget) {Glyph=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("ReserveGlyph")));return;}
    Glyph=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("ReserveGlyph"));
    Glyph->bReserveTooth=true;WidgetTree->RootWidget=Glyph;ApplyTheme();
}
void UMCReserveToothWidget::ApplyPresentation(float CurrentHealth,float MaximumHealth,bool bIsLost)
{
    Current=FMath::Max(0.f,CurrentHealth);Maximum=FMath::Max(1.f,MaximumHealth);bLost=bIsLost;
    BuildPresentationTree();if(!Glyph) return;Glyph->bReserveTooth=true;Glyph->HealthFraction=FMath::Clamp(Current/Maximum,0.f,1.f);Glyph->bLost=bLost;
    Glyph->InvalidateLayoutAndVolatility();
    SetToolTipText(FText::Format(Localized(TEXT("UI.HUD.ReserveHealth"),LOCTEXT("ReserveHealth","{Current} / {Maximum}")),FFormatNamedArguments{{TEXT("Current"),FText::AsNumber(FMath::CeilToInt(Current))},{TEXT("Maximum"),FText::AsNumber(FMath::CeilToInt(Maximum))}}));
}
void UMCReserveToothWidget::ApplyTheme()
{
    if(Glyph) {const auto& P=GetResolvedTheme()->Palette;Glyph->Tint=P.Accent;Glyph->EmptyTint=P.Muted;Glyph->InkTint=P.Ink;Glyph->HostTint=P.Warning;Glyph->InvalidateLayoutAndVolatility();}
    Super::ApplyTheme();
}
void UMCReserveToothWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();ApplyPresentation(Current,Maximum,bLost);ApplyTheme();
}

FText UMCGameplayHUD::Copy(FName Key,const FText& Fallback) const
{
    return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,IsDesignTime() || bApplyingPreview?PreviewCulture:FString());
}
FText UMCGameplayHUD::LiteralCopy(const FString& Value)
{
    // Legacy FString gameplay payloads use the preserved source alias, never editable RU copy.
    // Known event states route directly by semantic key below.
    if(LocalizationTable!=GetResolvedTranslationTable()) {
        LocalizationTable=GetResolvedTranslationTable();bSourceKeysCached=false;SourceTextKeys.Reset();
    }
    if(!bSourceKeysCached && LocalizationTable && LocalizationTable->GetRowStruct()==FMCUILocalizationRow::StaticStruct()) {
        TSet<FString> Ambiguous;
        for(const auto& Pair:LocalizationTable->GetRowMap()) {
            const auto* Row=reinterpret_cast<const FMCUILocalizationRow*>(Pair.Value);
            const FString* NativeSource=FTextInspector::GetSourceString(Row->SourceText);
            const FString Source=NativeSource?*NativeSource:Row->SourceText.ToString();
            if(SourceTextKeys.Contains(Source)) Ambiguous.Add(Source);else SourceTextKeys.Add(Source,Pair.Key);
        }
        for(const auto& Source:Ambiguous) SourceTextKeys.Remove(Source);bSourceKeysCached=true;
    }
    const FName* Key=SourceTextKeys.Find(Value);
    return Key?Copy(*Key,FText::FromString(Value)):FText::AsCultureInvariant(Value);
}
void UMCGameplayHUD::SetCopy(FName WidgetName,const FText& Text)
{
    if(auto* Label=Cast<UTextBlock>(Find(WidgetName))) if(!Label->GetText().EqualTo(Text)) Label->SetText(Text);
}
void UMCGameplayHUD::CachePresentationWidgets()
{
    Widgets.Reset();if(!WidgetTree) return;TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);
    for(auto* Widget:All) Widgets.Add(Widget->GetFName(),Widget);
}
void UMCGameplayHUD::BindAuthoredPresentation()
{
    if(!WidgetTree) return;
    if(!LocalizationTable) {
        const auto* T=GetResolvedTheme();LocalizationTable=!T->LocalizationTable.IsNull()?T->LocalizationTable.LoadSynchronous():UMCUIDataLibrary::GetDefaultLocalizationTable();
    }
    if(auto* Panel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("TeamProgression"))))
        if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ProgressionText"))))
            if(auto* Bar=Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("ProgressionBar")))) {ProgressionPanel=Panel;ProgressionText=Label;ProgressionBar=Bar;}
    if(auto* Panel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("CataclysmPanel")))) {
        CataclysmPanel=Panel;CataclysmInstruction=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("CataclysmInstruction")));
    }
    ReserveTeeth.Reset();
    for(int32 Index=1;Index<=8;++Index) ReserveTeeth.Add(Cast<UMCReserveToothWidget>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("ReserveTooth%d"),Index)))));
    if(auto* Booster=Cast<UMCBoosterWidget>(WidgetTree->FindWidget(TEXT("BoosterWidget")))) BoosterWidget=Booster;
    if(auto* Resource=Cast<UMCStaminaWidget>(WidgetTree->FindWidget(TEXT("StaminaWidget")))) StaminaWidget=Resource;
    if(auto* Resource=Cast<UMCStaminaWidget>(WidgetTree->FindWidget(TEXT("SwimEnergyWidget")))) SwimEnergyWidget=Resource;
    CachePresentationWidgets();
}
void UMCGameplayHUD::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {BindAuthoredPresentation();return;}
    using namespace MCGameplayHUDPresentationPrivate;
    // Viewport DPI is applied by Unreal. Anchors use its entire logical area,
    // so corner panels remain at the screen edges on wide and tall displays.
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HUDRoot"));WidgetTree->RootWidget=Root;
    auto Panel=[&](FName Name,UCanvasPanel* Parent,FVector2D Pos,FVector2D Size,FVector2D Anchor=FVector2D::ZeroVector) {
        auto* C=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Name);Place(Parent,C,Pos,Size,Anchor);return C;
    };
    auto Box=[&](FName Name,UCanvasPanel* Parent,FVector2D Pos,FVector2D Size,EMCUIColorToken Token) {
        auto* I=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);I->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,8.f));Place(Parent,I,Pos,Size);BindStyle(Name,Token,EMCUIStyleTarget::Image);return I;
    };
    auto Text=[&](FName Name,UCanvasPanel* Parent,FVector2D Pos,FVector2D Size,const FText& Value,int32 FontSize,EMCUIColorToken Token) {
        auto* T=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);T->SetText(Value);T->SetAutoWrapText(true);Place(Parent,T,Pos,Size);BindStyle(Name,Token,EMCUIStyleTarget::Text,FontSize,true);return T;
    };
    auto Bar=[&](FName Name,UCanvasPanel* Parent,FVector2D Pos,FVector2D Size,EMCUIColorToken Token) {
        auto* B=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name);B->SetPercent(.7f);Place(Parent,B,Pos,Size);BindStyle(Name,Token,EMCUIStyleTarget::Progress);return B;
    };
    auto* Objectives=Panel(TEXT("ObjectivesPanel"),Root,{22,20},{304,248});
    Box(TEXT("ReserveBG"),Objectives,{0,0},{304,88},EMCUIColorToken::Marine);
    Text(TEXT("ReserveLabel"),Objectives,{10,5},{225,24},Localized(TEXT("UI.HUD.ReserveTitle"),LOCTEXT("ReserveTitle","ЗДОРОВЬЕ ЗУБОВ")),15,EMCUIColorToken::OnMarine);
    Text(TEXT("ReserveValue"),Objectives,{247,5},{52,24},FText::GetEmpty(),16,EMCUIColorToken::OnMarine);
    for(int32 Index=0;Index<8;++Index) {
        auto* Tooth=WidgetTree->ConstructWidget<UMCReserveToothWidget>(ReserveToothWidgetClass?ReserveToothWidgetClass.Get():UMCGameplayUIFactory::ResolveClass(TEXT("ReserveTooth"),UMCReserveToothWidget::StaticClass()),FName(*FString::Printf(TEXT("ReserveTooth%d"),Index+1)));
        Place(Objectives,Tooth,{8.f+Index*36.f,32},{33,45});
    }
    Box(TEXT("CleanBG"),Objectives,{0,174},{304,58},EMCUIColorToken::Marine);
    Text(TEXT("CleanLabel"),Objectives,{10,179},{208,24},Localized(TEXT("UI.HUD.Cleanliness"),LOCTEXT("Cleanliness","ЧИСТОТА РТА")),14,EMCUIColorToken::OnMarine);
    Text(TEXT("CleanValue"),Objectives,{250,179},{50,24},FText::GetEmpty(),17,EMCUIColorToken::Accent);
    Bar(TEXT("CleanProgress"),Objectives,{10,209},{283,13},EMCUIColorToken::Cleanliness);
    auto* Timer=Panel(TEXT("TimerPanel"),Root,{22,114},{304,74});Box(TEXT("TimerBG"),Timer,{0,0},{304,74},EMCUIColorToken::LCDBackground);
    Text(TEXT("TimerLabel"),Timer,{12,5},{138,25},Localized(TEXT("UI.HUD.UntilEvent"),LOCTEXT("UntilEvent","ДО СОБЫТИЯ")),11,EMCUIColorToken::LCDInk);
    auto* Clock=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("TimerClock"));Clock->ToolSlot=4;Place(Timer,Clock,{8,30},{36,36});
    Text(TEXT("TimerValue"),Timer,{49,29},{102,34},FText::GetEmpty(),27,EMCUIColorToken::LCDInk);
    Text(TEXT("TaskLabel"),Timer,{165,5},{131,26},Localized(TEXT("UI.HUD.Task"),LOCTEXT("Task","ЗАДАЧА")),11,EMCUIColorToken::LCDInk);
    Text(TEXT("TaskValue"),Timer,{165,35},{131,26},FText::GetEmpty(),20,EMCUIColorToken::LCDInk);Bar(TEXT("TaskProgress"),Timer,{165,64},{125,5},EMCUIColorToken::LCDInk);
    Box(TEXT("TimerDivider"),Timer,{154,8},{1,58},EMCUIColorToken::LCDInk);
    auto* TimerTrack=Bar(TEXT("TimerProgress"),Timer,{12,64},{132,3},EMCUIColorToken::LCDInk);TimerTrack->SetVisibility(ESlateVisibility::Collapsed);

    auto* XP=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TeamProgression"));XP->SetPadding(FMargin(10,6));Place(Root,XP,{-326,20},{304,40},{1,0});
    auto* XPColumn=WidgetTree->ConstructWidget<UVerticalBox>();XP->SetContent(XPColumn);
    XPColumn->AddChildToVerticalBox(WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ProgressionText")));
    XPColumn->AddChildToVerticalBox(WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("ProgressionBar")));
    BindStyle(TEXT("ProgressionText"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,13,true);BindStyle(TEXT("ProgressionBar"),EMCUIColorToken::Accent,EMCUIStyleTarget::Progress);
    auto* Players=Panel(TEXT("PlayersPanel"),Root,{-326,65},{304,116},{1,0});
    Box(TEXT("PlayersBG"),Players,{0,0},{304,116},EMCUIColorToken::Marine);
    for(int32 Index=0;Index<4;++Index) {
        const FString N=FString::Printf(TEXT("Player%d"),Index+1);auto* Card=Panel(FName(N),Players,{76.f*Index,0},{76,110});
        auto* Frame=Box(FName(N+TEXT("Frame")),Card,{4,5},{68,105},EMCUIColorToken::Marine);Frame->SetVisibility(ESlateVisibility::Collapsed);
        auto* Back=Box(FName(N+TEXT("BG")),Card,{3,3},{60,59},EMCUIColorToken::SurfaceAlt);Back->SetVisibility(ESlateVisibility::Collapsed);
        if(Index>0) Box(FName(N+TEXT("Divider")),Card,{0,10},{1,96},EMCUIColorToken::Border);
        auto* Face=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),FName(N+TEXT("Face")));Face->bTooth=true;Place(Card,Face,{8,9},{60,65});
        auto* Name=Text(FName(N+TEXT("Name")),Card,{6,76},{64,20},FText::AsCultureInvariant(FString::Printf(TEXT("P%d"),Index+1)),12,EMCUIColorToken::Accent);Name->SetJustification(ETextJustify::Center);
        Bar(FName(N+TEXT("Health")),Card,{10,99},{56,10},EMCUIColorToken::Accent);
    }
    auto* Events=Panel(TEXT("EventsPanel"),Root,{-282,173},{260,164},{1,0});
    for(int32 Index=0;Index<3;++Index) {
        const FString N=FString::Printf(TEXT("Timeline%d"),Index);auto* Card=Panel(FName(N),Events,{0,54.f*Index},{260,49});
        Box(FName(N+TEXT("BG")),Card,{0,0},{260,49},EMCUIColorToken::Marine);Bar(FName(N+TEXT("Fill")),Card,{0,46},{260,3},EMCUIColorToken::Mint);
        Box(FName(N+TEXT("Accent")),Card,{0,7},{3,33},EMCUIColorToken::Accent);
        Text(FName(N+TEXT("Label")),Card,{11,4},{240,17},FText::GetEmpty(),10,EMCUIColorToken::OnMarine);
        Text(FName(N+TEXT("Title")),Card,{11,23},{240,23},FText::GetEmpty(),13,EMCUIColorToken::OnMarine);
    }
    auto* Tools=Panel(TEXT("InventoryPanel"),Root,{22,-266},{230,244},{0,1});
    Box(TEXT("ToolTitleBG"),Tools,{0,218},{78,25},EMCUIColorToken::Marine);Text(TEXT("ToolTitle"),Tools,{8,222},{62,20},FText::GetEmpty(),12,EMCUIColorToken::Accent);
    const FVector2D Positions[]={{3,92},{70,31},{137,92},{70,153}};
    for(int32 Index=0;Index<4;++Index) {
        const FString N=FString::Printf(TEXT("Tool%d"),Index+1);auto* Card=Panel(FName(N),Tools,Positions[Index],{62,62});
        Box(FName(N+TEXT("Frame")),Card,{0,0},{62,62},EMCUIColorToken::Marine);
        auto* ToolIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),FName(N+TEXT("Icon")));ToolIcon->ToolSlot=Index;Place(Card,ToolIcon,{7,6},{48,48});
        Box(FName(N+TEXT("KeyBG")),Card,{0,0},{18,19},EMCUIColorToken::Cream);Text(FName(N+TEXT("Key")),Card,{4,1},{17,18},FText::AsCultureInvariant(FString::FromInt(Index+1)),11,EMCUIColorToken::Ink);
    }
    auto* Cooldown=Panel(TEXT("SprayCooldown"),Tools,{136,177},{82,40});Box(TEXT("CooldownBG"),Cooldown,{0,0},{82,29},EMCUIColorToken::Marine);
    Text(TEXT("CooldownValue"),Cooldown,{6,3},{76,24},FText::GetEmpty(),13,EMCUIColorToken::OnMarine);Bar(TEXT("CooldownProgress"),Cooldown,{1,32},{80,4},EMCUIColorToken::Accent);
    auto* Hint=Panel(TEXT("HintPanel"),Root,{252,-120},{420,76},{0,1});Box(TEXT("HintBG"),Hint,{0,0},{420,76},EMCUIColorToken::Marine);
    auto* Mouse=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("ActionMouse"));Mouse->ToolSlot=5;Place(Hint,Mouse,{8,12},{29,39});
    Text(TEXT("ActionHint"),Hint,{45,10},{360,48},FText::GetEmpty(),13,EMCUIColorToken::OnMarine);Bar(TEXT("ContactProgress"),Hint,{45,65},{360,5},EMCUIColorToken::Mint);
    auto* Results=Panel(TEXT("ResultsPanel"),Root,{-245,-50},{490,111},{.5f,.44f});Box(TEXT("ResultsBG"),Results,{0,0},{490,111},EMCUIColorToken::Marine);
    Text(TEXT("ResultTitle"),Results,{20,14},{450,36},FText::GetEmpty(),24,EMCUIColorToken::OnMarine);Text(TEXT("ResultDetail"),Results,{20,56},{450,45},FText::GetEmpty(),13,EMCUIColorToken::OnMarine);Results->SetVisibility(ESlateVisibility::Collapsed);
    CataclysmPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("CataclysmPanel"));CataclysmPanel->SetPadding(FMargin(12,10));Place(Root,CataclysmPanel,{22,224},{304,112});
    CataclysmInstruction=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CataclysmInstruction"));CataclysmInstruction->SetAutoWrapText(true);CataclysmPanel->SetContent(CataclysmInstruction);CataclysmPanel->SetVisibility(ESlateVisibility::Collapsed);
    StaminaWidget=WidgetTree->ConstructWidget<UMCStaminaWidget>(StaminaWidgetClass?StaminaWidgetClass.Get():UMCGameplayUIFactory::ResolveClass(TEXT("Stamina"),UMCStaminaWidget::StaticClass()),TEXT("StaminaWidget"));
    auto* StaminaHost=Place(Root,StaminaWidget,{0,-20},{336,56},{.5f,1});StaminaHost->SetAlignment(FVector2D(.5f,1));StaminaHost->SetAutoSize(true);
    SwimEnergyWidget=WidgetTree->ConstructWidget<UMCStaminaWidget>(UMCGameplayUIFactory::ResolveClass(TEXT("Stamina"),UMCStaminaWidget::StaticClass()),TEXT("SwimEnergyWidget"));SwimEnergyWidget->bShowSwimEnergy=true;
    auto* SwimHost=Place(Root,SwimEnergyWidget,{0,-98},{336,56},{.5f,1});SwimHost->SetAlignment(FVector2D(.5f,1));SwimHost->SetAutoSize(true);SwimEnergyWidget->SetVisibility(ESlateVisibility::Collapsed);
    BindAuthoredPresentation();ApplyTheme();
}

void UMCGameplayHUD::ApplyTheme()
{
    const auto* T=GetResolvedTheme();if(!T || !WidgetTree) return;CachePresentationWidgets();const auto& P=T->Palette;
    LocalizationTable=GetResolvedTranslationTable();bSourceKeysCached=false;SourceTextKeys.Reset();
    SetCopy(TEXT("ReserveLabel"),Copy(TEXT("UI.HUD.ReserveTitle"),LOCTEXT("ReserveTitle","ЗДОРОВЬЕ ЗУБОВ")));
    SetCopy(TEXT("CleanLabel"),Copy(TEXT("UI.HUD.Cleanliness"),LOCTEXT("Cleanliness","ЧИСТОТА РТА")));
    const FName MarineBoxes[]={TEXT("ReserveBG"),TEXT("CleanBG"),TEXT("HintBG"),TEXT("ResultsBG"),TEXT("ToolTitleBG"),TEXT("CooldownBG"),TEXT("PreviewResourceBG")};
    for(FName Name:MarineBoxes) if(auto* Image=Cast<UImage>(Find(Name))) {Image->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,T->CornerRadius));Image->SetColorAndOpacity(P.Marine);}
    if(auto* Image=Cast<UImage>(Find(TEXT("TimerBG")))) Image->SetColorAndOpacity(P.LCDBackground);
    for(FName Name:{FName(TEXT("ReserveLabel")),FName(TEXT("CleanLabel")),FName(TEXT("ToolTitle")),FName(TEXT("ActionHint")),FName(TEXT("ResultDetail")),FName(TEXT("PreviewResourceValue"))})
        if(auto* Label=Cast<UTextBlock>(Find(Name))) {Label->SetFont(T->MakeGameplayFont(Name==TEXT("ToolTitle")?14:13));Label->SetColorAndOpacity(P.OnMarine);}
    if(ProgressionPanel) {ProgressionPanel->SetBrush(FSlateRoundedBoxBrush(P.Marine,T->CornerRadius,P.Border,T->OutlineWidth));ProgressionPanel->SetBrushColor(FLinearColor::White);}
    if(ProgressionText) {ProgressionText->SetFont(T->MakeGameplayFont(13));ProgressionText->SetColorAndOpacity(P.OnMarine);}
    if(ProgressionBar) ProgressionBar->SetFillColorAndOpacity(P.Accent);
    if(CataclysmPanel) {CataclysmPanel->SetBrush(FSlateRoundedBoxBrush(P.Marine,T->CornerRadius,P.Border,T->OutlineWidth));CataclysmPanel->SetBrushColor(FLinearColor::White);}
    if(CataclysmInstruction) {CataclysmInstruction->SetFont(T->MakeGameplayFont(13,false));CataclysmInstruction->SetColorAndOpacity(P.OnMarine);}
    for(int32 Index=1;Index<=4;++Index) {
        const FString Player=FString::Printf(TEXT("Player%d"),Index),Tool=FString::Printf(TEXT("Tool%d"),Index);
        if(auto* Image=Cast<UImage>(Find(FName(Player+TEXT("BG"))))) Image->SetColorAndOpacity(P.SurfaceAlt);
        for(const auto& Name:{Player+TEXT("Face"),Tool+TEXT("Icon")}) if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(Name)))) {
            Icon->InkTint=P.Ink;Icon->EmptyTint=P.Muted;Icon->HostTint=P.Warning;
            if(!Icon->bTooth) Icon->Tint=P.OnMarine;Icon->InvalidateLayoutAndVolatility();
        }
    }
    for(const auto& Tooth:ReserveTeeth) if(Tooth) Tooth->ApplyTheme();
    if(BoosterWidget) BoosterWidget->ApplyTheme();
    if(EncounterPreviewWidget) EncounterPreviewWidget->ApplyTheme();
    ApplyResourceTheme(StaminaWidget);ApplyResourceTheme(SwimEnergyWidget);
    Super::ApplyTheme();
    if(ProgressionPanel) MCUIVisualStyle::Panel(ProgressionPanel,T,P.Marine);
    if(ProgressionBar) MCUIVisualStyle::Progress(ProgressionBar,T,P.Accent);
    if(CataclysmPanel) MCUIVisualStyle::Panel(CataclysmPanel,T,P.Marine);
    for(int32 Index=1;Index<=4;++Index)
    {
        const FString Player=FString::Printf(TEXT("Player%d"),Index),Tool=FString::Printf(TEXT("Tool%d"),Index);
        if(auto* Health=Cast<UProgressBar>(Find(FName(Player+TEXT("Health"))))) MCUIVisualStyle::Progress(Health,T,P.Accent,5.f);
        const bool Selected=SelectedToolIndex==Index-1;
        if(auto* Frame=Cast<UImage>(Find(FName(Tool+TEXT("Frame"))))) MCUIVisualStyle::Image(Frame,T,P.Marine,true,Selected);
        if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(Tool+TEXT("Icon"))))) Icon->Tint=Selected?P.Accent:P.OnMarine;
    }
    if(auto* Clock=Cast<UMCHUDIcon>(Find(TEXT("TimerClock")))) {Clock->Tint=P.LCDInk;Clock->InkTint=P.LCDInk;}
    if(auto* Mouse=Cast<UMCHUDIcon>(Find(TEXT("ActionMouse")))) {Mouse->Tint=P.OnMarine;Mouse->InkTint=P.Ink;}
    Phase1LayoutSignature.Reset();UpdatePhase1Layout();
}

void UMCGameplayHUD::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();
    // Refresh text in the current live state without restarting alerts or gameplay.
    if(!IsDesignTime() && !bApplyingPreview && WidgetTree && WidgetTree->RootWidget) RefreshState();
}

void UMCGameplayHUD::ApplyResourceTheme(UMCStaminaWidget* Resource)
{
    if(Resource) Resource->ApplyTheme();
}

void UMCGameplayHUD::UpdatePhase1Layout()
{
    if(!WidgetTree || !Find(TEXT("TimerClock"))) return; // Preserve historical authored trees.
    const auto& PaintGeometry=GetPaintSpaceGeometry();
    if(PaintGeometry.GetLocalSize().X>1.f) LayoutLogicalWidth=PaintGeometry.GetLocalSize().X;
    const float GeometryScale=PaintGeometry.GetAccumulatedLayoutTransform().GetScale();
    const float LayoutScale=FMath::IsFinite(GeometryScale) && GeometryScale>0.f?GeometryScale:1.f;
    FString Signature=FString::Printf(TEXT("%.0f|scale%.4f|players%d"),LayoutLogicalWidth,LayoutScale,DisplayedPlayerCount);
    for(FName Name:{FName(TEXT("ReserveLabel")),FName(TEXT("CleanLabel")),FName(TEXT("TimerLabel")),FName(TEXT("TaskLabel")),FName(TEXT("ToolTitle")),FName(TEXT("ActionHint")),FName(TEXT("ResultTitle")),FName(TEXT("ResultDetail"))})
        if(const auto* Label=Cast<UTextBlock>(Find(Name))) Signature+=TEXT("\x1f")+Label->GetText().ToString();
    if(ProgressionText) Signature+=ProgressionText->GetText().ToString();
    for(int32 Index=0;Index<3;++Index)
        for(const TCHAR* Suffix:{TEXT("Label"),TEXT("Title")})
            if(const auto* Label=Cast<UTextBlock>(Find(FName(*FString::Printf(TEXT("Timeline%d%s"),Index,Suffix))))) Signature+=TEXT("\x1f")+Label->GetText().ToString();
    const float StaminaHeight=StaminaWidget?FMath::Max(56.f,static_cast<float>(StaminaWidget->GetDesiredSize().Y)):56.f;
    Signature+=FString::Printf(TEXT("|stamina%.0f"),StaminaHeight);
    if(Signature==Phase1LayoutSignature) return;Phase1LayoutSignature=Signature;
    if(SwimEnergyWidget) if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(SwimEnergyWidget->Slot)) CanvasSlot->SetPosition(FVector2D(0,-(20+StaminaHeight+12)));
    auto Place=[&](FName Name,FVector2D Position,FVector2D Size)
    {if(auto* Widget=Find(Name)) if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Widget->Slot)) {CanvasSlot->SetPosition(Position);CanvasSlot->SetSize(Size);}};
    auto TextHeight=[&](FName Name,float Width,float Minimum)
    {
        auto* Label=Cast<UTextBlock>(Find(Name));if(!Label) return Minimum;
        Label->SetAutoWrapText(false);Label->SetWrapTextAt(Width);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
        const auto Slate=Label->TakeWidget();Slate->MarkPrepassAsDirty();Slate->SlatePrepass(LayoutScale);
        return FMath::Max(Minimum,FMath::CeilToFloat(static_cast<float>(Label->GetDesiredSize().Y))+1.f);
    };
    const float ReserveTitleHeight=TextHeight(TEXT("ReserveLabel"),225,24);
    const float ReserveHeight=FMath::Max(88.f,ReserveTitleHeight+58.f);
    Place(TEXT("ReserveLabel"),{10,5},{225,ReserveTitleHeight});Place(TEXT("ReserveBG"),{0,0},{304,ReserveHeight});
    for(int32 Index=0;Index<8;++Index) Place(FName(*FString::Printf(TEXT("ReserveTooth%d"),Index+1)),{8.f+36.f*Index,ReserveTitleHeight+8.f},{33,45});
    const float TimerLabelHeight=FMath::Max(TextHeight(TEXT("TimerLabel"),138,22),TextHeight(TEXT("TaskLabel"),131,22));
    const float TimerHeight=FMath::Max(74.f,TimerLabelHeight+49.f);
    Place(TEXT("TimerPanel"),{22,20+ReserveHeight+6},{304,TimerHeight});Place(TEXT("TimerBG"),{0,0},{304,TimerHeight});
    Place(TEXT("TimerLabel"),{12,5},{138,TimerLabelHeight});Place(TEXT("TaskLabel"),{165,5},{131,TimerLabelHeight});
    Place(TEXT("TimerClock"),{8,TimerLabelHeight+7},{36,36});Place(TEXT("TimerValue"),{49,TimerLabelHeight+6},{102,34});
    Place(TEXT("TaskValue"),{165,TimerLabelHeight+10},{131,26});Place(TEXT("TaskProgress"),{165,TimerHeight-10},{125,5});
    Place(TEXT("TimerDivider"),{154,8},{1,TimerHeight-16});
    const float CleanY=ReserveHeight+6.f+TimerHeight+6.f;
    const float CleanLabelHeight=TextHeight(TEXT("CleanLabel"),208,24),CleanHeight=FMath::Max(58.f,CleanLabelHeight+31.f);
    Place(TEXT("CleanBG"),{0,CleanY},{304,CleanHeight});Place(TEXT("CleanLabel"),{10,CleanY+5},{208,CleanLabelHeight});
    Place(TEXT("CleanValue"),{250,CleanY+5},{50,24});Place(TEXT("CleanProgress"),{10,CleanY+CleanLabelHeight+13},{283,13});
    Place(TEXT("ObjectivesPanel"),{22,20},{304,CleanY+CleanHeight});
    if(CataclysmPanel) if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(CataclysmPanel->Slot))
    {
        CanvasSlot->SetPosition({22,20+CleanY+CleanHeight+12});CanvasSlot->SetAutoSize(true);
        if(CataclysmInstruction) {CataclysmInstruction->SetAutoWrapText(false);CataclysmInstruction->SetWrapTextAt(280);}
    }
    const float ToolHeight=TextHeight(TEXT("ToolTitle"),214,20);
    const auto* ToolLabel=Cast<UTextBlock>(Find(TEXT("ToolTitle")));
    const float ToolWidth=FMath::Clamp(ToolLabel?ToolLabel->GetDesiredSize().X+16.f:78.f,78.f,230.f);
    constexpr float ToolTitleTop=238.f;
    Place(TEXT("ToolTitleBG"),{0,ToolTitleTop},{ToolWidth,ToolHeight+10});Place(TEXT("ToolTitle"),{8,ToolTitleTop+4},{ToolWidth-16,ToolHeight});
    Place(TEXT("InventoryPanel"),{22,-(ToolTitleTop+ToolHeight+10+18)},{230,ToolTitleTop+ToolHeight+10});
    // Keycaps sit outside the tool silhouettes, so even the smallest viewport
    // retains a distinct icon and shortcut instead of a tiny corner badge.
    const FVector2D KeyPositions[]={{-10,20},{20,-12},{49,20},{20,49}};
    for(int32 Index=0;Index<4;++Index)
    {
        const FString Tool=FString::Printf(TEXT("Tool%d"),Index+1);
        Place(FName(Tool+TEXT("KeyBG")),KeyPositions[Index],{25,26});
        Place(FName(Tool+TEXT("Key")),KeyPositions[Index]+FVector2D(1,2),{23,23});
        if(auto* Label=Cast<UTextBlock>(Find(FName(Tool+TEXT("Key")))))
        {
            Label->SetJustification(ETextJustify::Center);Label->SetFont(GetResolvedTheme()->MakeGameplayFont(14));
            Label->SetAutoWrapText(false);Label->SetWrapTextAt(0);
        }
        Place(FName(Tool+TEXT("Icon")),{4,3},{54,54});
    }
    const float HintMaximum=FMath::Clamp((LayoutLogicalWidth-336.f)*.5f-252.f-32.f,220.f,420.f);
    TextHeight(TEXT("ActionHint"),HintMaximum-60.f,28);
    const auto* HintLabel=Cast<UTextBlock>(Find(TEXT("ActionHint")));
    const float HintWidth=FMath::Clamp(HintLabel?HintLabel->GetDesiredSize().X+60.f:200.f,180.f,HintMaximum);
    const float HintTextHeight=TextHeight(TEXT("ActionHint"),HintWidth-60.f,24),HintHeight=FMath::Max(52.f,HintTextHeight+22.f);
    Place(TEXT("HintPanel"),{252,-(HintHeight+22)},{HintWidth,HintHeight});Place(TEXT("HintBG"),{0,0},{HintWidth,HintHeight});
    Place(TEXT("ActionHint"),{45,10},{HintWidth-60,HintTextHeight});Place(TEXT("ContactProgress"),{45,HintHeight-9},{HintWidth-60,5});
    if(ProgressionText && ProgressionPanel)
    {
        ProgressionText->SetAutoWrapText(false);ProgressionText->SetWrapTextAt(284);ProgressionText->TakeWidget()->MarkPrepassAsDirty();ProgressionText->TakeWidget()->SlatePrepass(LayoutScale);
        const float Height=FMath::Max(40.f,ProgressionText->GetDesiredSize().Y+22.f);
        const float PlayersWidth=76.f*FMath::Clamp(DisplayedPlayerCount,1,4);
        Place(TEXT("TeamProgression"),{-326,20},{304,Height});
        Place(TEXT("PlayersPanel"),{-22-PlayersWidth,20+Height+5},{PlayersWidth,116});
        Place(TEXT("PlayersBG"),{0,0},{PlayersWidth,116});
        if(auto* Strip=Find(TEXT("PlayersPanel"))) Strip->SetRenderTranslation(FVector2D::ZeroVector);
        Place(TEXT("EventsPanel"),{-282,20+Height+5+116+12},{260,164});
    }
    // Event names and their status captions may take several lines in another
    // language. Keep the progress at the bottom of each measured row.
    float EventY=0.f;
    for(int32 Index=0;Index<3;++Index)
    {
        const FString Row=FString::Printf(TEXT("Timeline%d"),Index);
        const float LabelHeight=TextHeight(FName(Row+TEXT("Label")),238,17);
        const float TitleHeight=TextHeight(FName(Row+TEXT("Title")),238,23);
        const float RowHeight=LabelHeight+TitleHeight+12;
        Place(FName(Row),{0,EventY},{260,RowHeight});
        Place(FName(Row+TEXT("BG")),{0,0},{260,RowHeight});
        Place(FName(Row+TEXT("Label")),{11,4},{238,LabelHeight});
        Place(FName(Row+TEXT("Title")),{11,LabelHeight+5},{238,TitleHeight});
        Place(FName(Row+TEXT("Fill")),{0,RowHeight-3},{260,3});
        Place(FName(Row+TEXT("Accent")),{0,7},{3,RowHeight-14});
        EventY+=RowHeight+5;
    }
    if(auto* Events=Find(TEXT("EventsPanel"))) if(auto* EventsSlot=Cast<UCanvasPanelSlot>(Events->Slot)) EventsSlot->SetSize(FVector2D(260,EventY));
    const float ResultTitleHeight=TextHeight(TEXT("ResultTitle"),450,36),ResultDetailHeight=TextHeight(TEXT("ResultDetail"),450,40);
    const float ResultHeight=ResultTitleHeight+ResultDetailHeight+40;
    Place(TEXT("ResultsPanel"),{-245,-ResultHeight*.5f},{490,ResultHeight});Place(TEXT("ResultsBG"),{0,0},{490,ResultHeight});
    Place(TEXT("ResultTitle"),{20,14},{450,ResultTitleHeight});Place(TEXT("ResultDetail"),{20,ResultTitleHeight+24},{450,ResultDetailHeight});
}

void UMCGameplayHUD::EnsureBoosterWidget()
{
    if(BoosterWidget || !WidgetTree) return;
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);if(!Root) return;
    BoosterWidget=WidgetTree->ConstructWidget<UMCBoosterWidget>(BoosterWidgetClass?BoosterWidgetClass.Get():UMCGameplayUIFactory::ResolveClass(TEXT("Booster"),UMCBoosterWidget::StaticClass()),TEXT("BoosterWidget"));
    auto* BoosterHost=MCGameplayHUDPresentationPrivate::Place(Root,BoosterWidget,{22,-282},{304,120},{0,1});
    BoosterHost->SetAlignment(FVector2D(0,1));BoosterHost->SetAutoSize(true);
}

void UMCGameplayHUD::RefreshBooster(const AMCToothCharacter* Hero)
{
    EnsureBoosterWidget();if(!BoosterWidget) return;
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const bool Visible=Hero && Hero->Status && Hero->Status->IsAlive() && PC && !PC->bShowMouseCursor;
    BoosterWidget->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);if(!Visible) return;
    FMCUIBoosterViewData Data;
    if(const auto* Perks=UMCPerkComponent::ForActor(Hero)) {
        Data.QueueCount=Perks->BoosterQueue.Num();Data.bFuryActive=Perks->IsFuryActive();Data.bGrenadeArmed=Perks->bGrenadeArmed;
        for(FName ID:Perks->BoosterQueue) if(ID!=TEXT("Fallback_Grenade") || !Perks->bGrenadeArmed) {Data.GameplayPerkID=ID;break;}
        Data.bActivationBlocked=!Hero->CanWork() || Hero->HasFrozenLegs() || (Hero->Inventory && Hero->Inventory->bChainsawRunning) || Data.bFuryActive || PC->IsRewardMenuOpen() || Data.GameplayPerkID.IsNone();
    }
    Data.ActivationInput=FText::AsCultureInvariant(PC->IsUsingGamepad()?TEXT("B"):TEXT("G"));BoosterWidget->ApplyPresentation(Data);
}

void UMCGameplayHUD::RefreshReserveTeeth(const AMCGameState& State)
{
    TArray<const AMCArenaTooth*> Ordered;
    // ArenaTeeth already excludes player characters. Keep unavailable records in
    // this pool so damage/respawn consumption cannot refill or shift lost slots.
    for(const auto& Tooth:State.ArenaTeeth) if(IsValid(Tooth)) Ordered.Add(Tooth.Get());
    Ordered.Sort([](const AMCArenaTooth& A,const AMCArenaTooth& B){return A.State.ToothId<B.State.ToothId;});
    int32 Available=0;
    for(const auto* Tooth:Ordered) if(Tooth->IsAvailable()) ++Available;
    for(int32 Index=0;Index<ReserveTeeth.Num();++Index) if(auto* Display=ReserveTeeth[Index].Get()) {
        const auto* Tooth=Ordered.IsValidIndex(Index)?Ordered[Index]:nullptr;
        const bool bAvailable=Tooth && Tooth->IsAvailable();
        Display->ApplyPresentation(bAvailable?Tooth->State.Health:0.f,Tooth?Tooth->Settings.MaxHealth:100.f,!bAvailable);
    }
    SetCopy(TEXT("ReserveValue"),FText::Format(Copy(TEXT("UI.HUD.ReserveCount"),LOCTEXT("ReserveCount","{Available} / {Maximum}")),FFormatNamedArguments{{TEXT("Available"),FText::AsNumber(Available)},{TEXT("Maximum"),FText::AsNumber(Ordered.Num())}}));
}

void UMCGameplayHUD::ApplyEditorPreview(float Time)
{
    PreviewTime=FMath::IsFinite(Time)?FMath::Max(0.f,Time):0.f;
    TGuardValue<bool> Guard(bApplyingPreview,true);BuildPresentationTree();CachePresentationWidgets();ApplyTheme();
    const bool Empty=PreviewScenario==EMCGameplayUIPreviewScenario::Empty || PreviewState==TEXT("Empty");
    const bool PolishStress=PreviewState==TEXT("PolishStress");
    const bool Low=PolishStress || PreviewScenario==EMCGameplayUIPreviewScenario::LowResources || PreviewState==TEXT("LowResources");
    const bool Long=PolishStress || PreviewScenario==EMCGameplayUIPreviewScenario::LongText || PreviewState==TEXT("LongText");
    DisplayedPlayerCount=4;
    // Designer intentionally reveals collapsed authored controls for editing.
    // This fixture has no cataclysm instruction, so keep its empty panel absent.
    if(CataclysmPanel) CataclysmPanel->SetRenderOpacity(0.f);
    for(int32 Index=0;Index<ReserveTeeth.Num();++Index) if(auto* Tooth=ReserveTeeth[Index].Get()) Tooth->ApplyPresentation(Empty?0.f:Low?(Index<2?25.f:0.f):Index<4?100.f:Index<6?50.f:0.f,100.f,Empty || (Low?Index>=2:Index>=6));
    SetCopy(TEXT("ReserveValue"),FText::AsCultureInvariant(Empty?TEXT("0 / 8"):Low?TEXT("2 / 8"):TEXT("6 / 8")));
    SetCopy(TEXT("CleanValue"),FText::AsCultureInvariant(Empty?TEXT("0%"):Low?TEXT("15%"):TEXT("90%")));
    SetCopy(TEXT("TimerValue"),FText::AsCultureInvariant(Low?TEXT("00:08"):TEXT("02:48")));
    SetCopy(TEXT("TaskValue"),FText::AsCultureInvariant(Empty?TEXT("0 / 0"):TEXT("420 / 1440")));
    SetCopy(TEXT("TaskLabel"),Copy(TEXT("UI.HUD.Task"),LOCTEXT("Task","ЗАДАЧА")));
    SetCopy(TEXT("TimerLabel"),Copy(TEXT("UI.HUD.UntilEvent"),LOCTEXT("UntilEvent","ДО СОБЫТИЯ")));
    SetCopy(TEXT("ToolTitle"),Copy(TEXT("UI.HUD.Tool.Brush"),LOCTEXT("Brush","ЩЁТКА")));
    SetCopy(TEXT("ActionHint"),Long?Copy(TEXT("UI.HUD.Preview.LongHint"),LOCTEXT("LongHint","Удерживай E · вытащить товарища из мимика и помочь вернуться к команде")):Copy(TEXT("UI.HUD.BrushAction"),LOCTEXT("BrushAction","ЛКМ · ЧИСТИТЬ")));
    if(ProgressionText) ProgressionText->SetText(Copy(TEXT("UI.HUD.Preview.TeamXP"),LOCTEXT("PreviewTeamXP","Ур. 2 · 35 / 75 XP")));
    if(ProgressionBar) ProgressionBar->SetPercent(35.f/75.f);
    auto Bar=[&](FName Name,float Value){if(auto* B=Cast<UProgressBar>(Find(Name))) B->SetPercent(Value);};
    Bar(TEXT("CleanProgress"),Empty?0.f:Low?.15f:.9f);Bar(TEXT("TaskProgress"),Empty?0.f:420.f/1440.f);Bar(TEXT("TimerProgress"),.45f);
    if(StaminaWidget) {StaminaWidget->PreviewCulture=PreviewCulture;StaminaWidget->PreviewScenario=Empty?EMCGameplayUIPreviewScenario::Empty:Low?EMCGameplayUIPreviewScenario::LowResources:PreviewScenario;StaminaWidget->ApplyEditorPreview(Time);}
    if(SwimEnergyWidget) {SwimEnergyWidget->PreviewCulture=PreviewCulture;SwimEnergyWidget->PreviewScenario=PreviewScenario;SwimEnergyWidget->PreviewState=PreviewState;SwimEnergyWidget->ApplyEditorPreview(Time);SwimEnergyWidget->SetVisibility(!Empty && (PreviewState==TEXT("Swim") || PreviewState==TEXT("Drowning"))?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
    for(int32 Index=0;Index<4;++Index) {
        const FString N=FString::Printf(TEXT("Player%d"),Index+1);if(auto* W=Find(FName(N))) W->SetVisibility(Empty?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
        Bar(FName(N+TEXT("Health")),Low?.2f:1.f-Index*.16f);
        if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(N+TEXT("Face"))))) {Icon->bHost=Index==0;Icon->bDead=Low && Index==3;Icon->Tint=GetResolvedTheme()->Palette.Accent;}
    }
    for(FName Name:{FName(TEXT("DayTitle")),FName(TEXT("ResultsPanel")),FName(TEXT("SprayCooldown")),FName(TEXT("ContactProgress")),FName(TEXT("EventsPanel"))}) if(auto* W=Find(Name)) W->SetVisibility(ESlateVisibility::Collapsed);
    const bool ResultsVictory=PreviewState==TEXT("ResultsVictory") || PreviewScenario==EMCGameplayUIPreviewScenario::Victory;
    const bool ResultsDefeat=PreviewState==TEXT("ResultsDefeat") || PreviewScenario==EMCGameplayUIPreviewScenario::Defeat;
    if(ResultsVictory || ResultsDefeat)
    {
        if(auto* Panel=Find(TEXT("ResultsPanel"))) Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
        SetCopy(TEXT("ResultTitle"),ResultsDefeat?Copy(TEXT("UI.HUD.RunLost"),LOCTEXT("RunLost","РОТ НЕ СПАСЁН")):Copy(TEXT("UI.HUD.RunComplete"),LOCTEXT("RunComplete","ЗАБЕГ ЗАВЕРШЁН")));
        SetCopy(TEXT("ResultDetail"),UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.HUD.RunResultDetail"),LOCTEXT("RunResultDetail","Незавершённых событий: {Failed} · R — новый забег")),FFormatNamedArguments{{TEXT("Failed"),ResultsDefeat?3:0}},PreviewCulture));
    }
    EnsureBoosterWidget();if(BoosterWidget) {BoosterWidget->PreviewCulture=PreviewCulture;FMCUIBoosterViewData Data;Data.GameplayPerkID=TEXT("Fallback_Mouthwash");Data.QueueCount=Empty?0:3;Data.bFuryActive=PolishStress || PreviewScenario==EMCGameplayUIPreviewScenario::ActiveEffect;Data.bGrenadeArmed=PolishStress || PreviewScenario==EMCGameplayUIPreviewScenario::Armed;BoosterWidget->ApplyPresentation(Data);}
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!EncounterPreviewWidget && Root) {
        EncounterPreviewWidget=WidgetTree->ConstructWidget<UMCGameplayUIWidget>(EncounterPreviewWidgetClass?EncounterPreviewWidgetClass.Get():UMCGameplayUIFactory::ResolveClass(TEXT("BossHealth"),UMCBossHealthWidget::StaticClass()),TEXT("EncounterPreview"));
        auto* EncounterSlot=Root->AddChildToCanvas(EncounterPreviewWidget);EncounterSlot->SetAnchors(FAnchors(0,0,1,1));EncounterSlot->SetOffsets(FMargin(0));EncounterSlot->SetZOrder(20);
    }
    if(EncounterPreviewWidget) {
        EncounterPreviewWidget->PreviewCulture=PreviewCulture;EncounterPreviewWidget->PreviewScenario=PreviewScenario;EncounterPreviewWidget->PreviewState=PreviewState;
        if(PreviewState==TEXT("OneBoss")) EncounterPreviewWidget->PreviewScenario=EMCGameplayUIPreviewScenario::OneBoss;
        else if(PreviewState==TEXT("TwoBosses")) EncounterPreviewWidget->PreviewScenario=EMCGameplayUIPreviewScenario::TwoBosses;
        else if(PreviewState==TEXT("ThreeBosses")) EncounterPreviewWidget->PreviewScenario=EMCGameplayUIPreviewScenario::ThreeBosses;
        EncounterPreviewWidget->ApplyEditorPreview(Time);EncounterPreviewWidget->SetVisibility(Empty || PolishStress || PreviewState==TEXT("ZeroBosses")?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    EnsureAlertWidget();if(AlertWidget) {
        AlertWidget->PreviewCulture=PreviewCulture;AlertWidget->PreviewScenario=PreviewScenario;AlertWidget->ApplyEditorPreview(Time);
        if(const auto* Boss=Cast<UMCBossHealthWidget>(EncounterPreviewWidget)) AlertWidget->SetBossStackCount(Empty || PolishStress || PreviewState==TEXT("ZeroBosses")?0:Boss->GetDisplayedBossCount(),PolishStress?0:Boss->GetDisplayedStackHeight());
        else AlertWidget->SetBossStackCount(Empty?0:PreviewScenario==EMCGameplayUIPreviewScenario::OneBoss?1:PreviewScenario==EMCGameplayUIPreviewScenario::TwoBosses?2:3);
        AlertWidget->SetVisibility(!Empty && (Low || Long || PreviewState==TEXT("Alert"))?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    UpdatePhase1Layout();
}
void UMCGameplayHUD::ResetEditorPreview() {FoodReticleAlpha=0;SetRenderOpacity(1.f);ApplyEditorPreview(0.f);}

void UMCGameplayHUD::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    BuildPresentationTree();
    if(IsDesignTime()) return;
    if(GetOwningPlayer() && !StaminaWidget)
    {
        auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
        if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
        if(!Root)
        {
            UWidget* Existing=WidgetTree->RootWidget;
            Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
            if(Existing) {auto* Full=Root->AddChildToCanvas(Existing);Full->SetAnchors(FAnchors(0,0,1,1));Full->SetOffsets(FMargin(0));}
        }
        StaminaWidget=CreateWidget<UMCStaminaWidget>(GetOwningPlayer(),StaminaWidgetClass?StaminaWidgetClass.Get():UMCGameplayUIFactory::ResolveClass(TEXT("Stamina"),UMCStaminaWidget::StaticClass()));
        auto* StaminaSlot=Root->AddChildToCanvas(StaminaWidget);StaminaSlot->SetAnchors(FAnchors(.5f,1));StaminaSlot->SetAlignment(FVector2D(.5f,1));
        StaminaSlot->SetPosition(FVector2D(0,-20));StaminaSlot->SetSize(FVector2D(336,56));StaminaSlot->SetAutoSize(true);
    }
    if(GetOwningPlayer() && !SwimEnergyWidget)
        if(auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot"))))
        {
            SwimEnergyWidget=CreateWidget<UMCStaminaWidget>(GetOwningPlayer(),UMCGameplayUIFactory::ResolveClass(TEXT("Stamina"),UMCStaminaWidget::StaticClass()));
            if(SwimEnergyWidget) {
                SwimEnergyWidget->bShowSwimEnergy=true;
                auto* SwimSlot=Root->AddChildToCanvas(SwimEnergyWidget);
                SwimSlot->SetAnchors(FAnchors(.5f,1)); SwimSlot->SetAlignment(FVector2D(.5f,1));
                SwimSlot->SetPosition(FVector2D(0,-98)); SwimSlot->SetSize(FVector2D(336,56));SwimSlot->SetAutoSize(true);
                SwimEnergyWidget->Refresh();
            }
        }
    EnsureDirectorMonitor();
    EnsureProgressionPanel();
    EnsureCataclysmPanel();
    EnsureAlertWidget();
    EnsureBoosterWidget();
    if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->RegisterGameplayHUD(this);
}
void UMCGameplayHUD::EnsureAlertWidget()
{
    if(AlertWidget || !WidgetTree || (!IsDesignTime() && !bApplyingPreview && !GetOwningPlayer())) return;
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root) return;
    AlertWidget=WidgetTree->ConstructWidget<UMCGameplayAlertWidget>(AlertWidgetClass?AlertWidgetClass.Get():UMCGameplayUIFactory::ResolveClass(TEXT("GameplayAlert"),UMCGameplayAlertWidget::StaticClass()));
    if(!AlertWidget) return;
    auto* AlertSlot=Root->AddChildToCanvas(AlertWidget);
    AlertSlot->SetAnchors(FAnchors(0,0,1,1)); AlertSlot->SetOffsets(FMargin(0)); AlertSlot->SetZOrder(50);
}
void UMCGameplayHUD::SetBossStackCount(int32 Count,float StackHeight)
{
    EnsureAlertWidget();if(AlertWidget) AlertWidget->SetBossStackCount(Count,StackHeight);
}
void UMCGameplayHUD::QueueGameplayAlert(const FText& Message,float HoldSeconds,FName EventId)
{
    EnsureAlertWidget();
    if(AlertWidget) AlertWidget->QueueAlert(Message,HoldSeconds,EventId);
}
void UMCGameplayHUD::ResetGameplayAlerts()
{
    if(AlertWidget) AlertWidget->ClearAlerts();
    ObservedAlertStates.Reset(); LastAlertDayEvent.Reset(); LastAlertStepStartedAt=-1;
    LastDrowningHero.Reset(); bObservedDrowning=false; DrowningAlertSerial=0;
}
void UMCGameplayHUD::RefreshGameplayAlerts()
{
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    if(!GS || GS->bLobbyWaiting || GS->bTutorialActive) return;
    if(GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost) {
        ResetGameplayAlerts();
        return;
    }
    // Observe the saved gameplay classes' replicated timelines. Presentation never starts events.
    auto Observe=[&](AActor* Actor,FName Channel,bool Active,double Serial,const FText& Message,float Hold=3.f) {
        if(!IsValid(Actor) || !FMath::IsFinite(Serial)) return;
        auto& Timelines=ObservedAlertStates.FindOrAdd(Actor);
        if(!Active) { Timelines.Remove(Channel); return; }
        const double* Previous=Timelines.Find(Channel);
        if(Previous && *Previous==Serial) return;
        Timelines.Add(Channel,Serial);
        const FName EventId(*FString::Printf(TEXT("%s.%d.%.3f"),*Channel.ToString(),Actor->GetUniqueID(),Serial));
        QueueGameplayAlert(Message,Hold,EventId);
    };
    for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It)
        Observe(*It,TEXT("Cola"),It->bActive,It->StartedAt,Copy(TEXT("UI.Alert.Event.ColdCola"),LOCTEXT("AlertColdCola","ХОЛОДНАЯ КОЛА · БЕРЕГИСЬ ЛЬДА!")));
    for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It)
        Observe(*It,TEXT("CoffeeWave"),It->bActive && It->bRiverFlood && !Cast<AMCCataclysmEvent>(It->GetOwner()),It->StartedAt,Copy(TEXT("UI.Alert.Event.CoffeeWave"),LOCTEXT("AlertCoffeeWave","КОФЕЙНАЯ ВОЛНА · ДЕРЖИСЬ!")));
    for(TActorIterator<AMCCataclysmEvent> It(GetWorld());It;++It) {
        Observe(*It,TEXT("CataclysmWarning"),It->Phase==EMCCataclysmPhase::Warning,It->StartedAt,
            UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.Alert.Event.CataclysmWarning"),LOCTEXT("AlertCataclysmWarning","{Title} · ВОЛНА! ДЕРЖИСЬ ЗА ОПОРУ!")),
                FFormatNamedArguments{{TEXT("Title"),It->bCola
                    ?Copy(TEXT("UI.Event.Cataclysm.ColaWave.Title"),LOCTEXT("CataclysmColaTitle","КАТАКЛИЗМ · ВОЛНА КОЛЫ"))
                    :Copy(TEXT("UI.Event.Cataclysm.CoffeeWave.Title"),LOCTEXT("CataclysmCoffeeTitle","КАТАКЛИЗМ · КОФЕЙНАЯ ВОЛНА"))}}),3.f);
        Observe(*It,TEXT("CataclysmBranch"),It->Phase==EMCCataclysmPhase::BranchWarning,It->PhaseStartedAt,
            Copy(TEXT("UI.Alert.Event.CataclysmBranch"),LOCTEXT("AlertCataclysmBranch","РОТ ЗАТОПИТ · НА ПЛОТЫ! ПО ДВОЕ НА КАЖДЫЙ!")),3.5f);
        Observe(*It,TEXT("CataclysmJellies"),It->Phase==EMCCataclysmPhase::JellyFight,It->PhaseStartedAt,
            Copy(TEXT("UI.Alert.Event.CataclysmJellies"),LOCTEXT("AlertCataclysmJellies","ЖИВОЕ ЖЕЛЕ · ПОБЕДИ ЕГО И СМОЙ СЛЕДЫ!")),3.5f);
        Observe(*It,TEXT("CataclysmCleanup"),It->Phase==EMCCataclysmPhase::Cleanup,It->PhaseStartedAt,
            Copy(TEXT("UI.Alert.Event.CataclysmCleanup"),LOCTEXT("AlertCataclysmCleanup","ОПАСНОСТЬ ПРОШЛА · ОЧИСТИ ПОСЛЕДСТВИЯ!")));
        Observe(*It,TEXT("CataclysmComplete"),It->Phase==EMCCataclysmPhase::Complete,It->PhaseStartedAt,
            Copy(TEXT("UI.Alert.Event.CataclysmComplete"),LOCTEXT("AlertCataclysmComplete","КАТАКЛИЗМ ПРЕОДОЛЁН · ВСЁ ЧИСТО!")));
    }
    auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();
    const auto* Move=Hero?Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()):nullptr;
    const bool Drowning=Hero && Hero->Status && Hero->Status->IsAlive() && Move && Move->IsDrowning();
    if(LastDrowningHero.Get()!=Hero) Observe(LastDrowningHero.Get(),TEXT("Drowning"),false,0,FText::GetEmpty());
    if(Drowning && (!bObservedDrowning || LastDrowningHero.Get()!=Hero)) DrowningAlertSerial=GS->GetServerWorldTimeSeconds();
    Observe(Hero,TEXT("Drowning"),Drowning,DrowningAlertSerial,Copy(TEXT("UI.Alert.Event.Drowning"),LOCTEXT("AlertDrowning","ТОНЕШЬ · СРОЧНО НА ПЛОТ ИЛИ СУШУ!")),2.5f);
    LastDrowningHero=Hero; bObservedDrowning=Drowning;
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It) {
        const bool Active=It->IsActive();
        Observe(*It,TEXT("Ice"),Active,It->StartedAt,It->UsesColdThroneContract()
            ?Copy(TEXT("UI.Alert.Event.EternalWinter"),LOCTEXT("AlertEternalWinter","ВЕЧНАЯ ЗИМА · БЕРЕГИСЬ АУРЫ БОССА!"))
            :Copy(TEXT("UI.Alert.Event.Frost"),LOCTEXT("AlertFrost","МОРОЗ · ДЕРЖИСЬ В ТЁПЛОЙ ЗОНЕ!")));
        Observe(*It,TEXT("ColdThrone"),Active && It->UsesColdThroneContract() && It->bColdThroneActive,
            It->ColdThroneEndsAt,Copy(TEXT("UI.Alert.Event.ColdThrone"),LOCTEXT("AlertColdThrone","ХОЛОДНЫЙ ТРОН II · ВСЯ АРЕНА ЗАМЕРЗАЕТ!")),3.5f);
    }
    for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It)
        Observe(*It,TEXT("Fog"),It->IsActive(),It->StartedAt,Copy(TEXT("UI.Alert.Event.FogBrawl"),LOCTEXT("AlertFogBrawl","ДРАКА В ТУМАНЕ · ЗАЩИЩАЙТЕ ЗУБ!")));
    for(TActorIterator<AMCNutRainEvent> It(GetWorld());It;++It) {
        const bool Active=It->Stage!=EMCNutRainStage::Idle && It->Stage!=EMCNutRainStage::Complete;
        // The opening rain is continuous; settling/enemy phases do not repeat the alarm.
        Observe(*It,TEXT("NutRain"),Active,It->Settings.bBossEncounter?0:It->SeriesIndex,Copy(TEXT("UI.Alert.Event.NutRain"),LOCTEXT("AlertNutRain","ОРЕХОГЕДДОН · СМОТРИ ВВЕРХ!")));
    }
    for(TActorIterator<AMCBossCharacter> It(GetWorld());It;++It) {
        const bool Active=It->Runtime.AnimationPreview==EMCBossAnimationPreview::None
            && It->Runtime.State!=EMCBossState::Dormant && It->IsBossAlive();
        Observe(*It,TEXT("Boss"),Active,0,Copy(TEXT("UI.Alert.Event.Boss"),LOCTEXT("AlertBoss","БОСС ВО РТУ · ПРИГОТОВЬТЕСЬ!")),3.5f);
    }
    for(TActorIterator<AMCNutBoss> It(GetWorld());It;++It) {
        const FText Message=It->BossRole==EMCNutBossRole::Tank
            ?Copy(TEXT("UI.Alert.Event.NutKnight"),LOCTEXT("AlertNutKnight","БОСС · ОРЕХОВЫЙ РЫЦАРЬ!"))
            :Copy(TEXT("UI.Alert.Event.NutMage"),LOCTEXT("AlertNutMage","БОСС · ОРЕХОВЫЙ КОЛДУН!"));
        Observe(*It,TEXT("NutBoss"),It->IsEncounterAlive(),int32(It->BossRole),Message,3.5f);
    }
    if(GS->CurrentEvent && (LastAlertDayEvent.Get()!=GS->CurrentEvent || LastAlertStepStartedAt!=GS->StepStartedAt)) {
        LastAlertDayEvent=GS->CurrentEvent; LastAlertStepStartedAt=GS->StepStartedAt;
        const FName EventId(*FString::Printf(TEXT("DayEvent.%d.%.3f"),GS->Day,GS->StepStartedAt));
        if(!AMCCataclysmEvent::FindActive(GetWorld())) {
            // Prefer an authored semantic row; a missing UI row retains the asset's own localized FText.
            FText EventTitle=GS->CurrentEvent->Title;
            const FPrimaryAssetId AssetId=GS->CurrentEvent->GetPrimaryAssetId();
            if(AssetId.IsValid() && LocalizationTable && LocalizationTable->GetRowStruct()==FMCUILocalizationRow::StaticStruct()) {
                const FName TitleKey(*(TEXT("UI.Event.")+AssetId.ToString().Replace(TEXT(":"),TEXT("."))+TEXT(".Title")));
                if(LocalizationTable->FindRow<FMCUILocalizationRow>(TitleKey,TEXT("HUD event title"),false))
                    EventTitle=Copy(TitleKey,EventTitle);
            }
            QueueGameplayAlert(UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.Alert.Event.DayEvent"),LOCTEXT("AlertDayEvent","СОБЫТИЕ · {Title}")),
                FFormatNamedArguments{{TEXT("Title"),EventTitle}}),3.f,EventId);
        }
    }
    for(auto It=ObservedAlertStates.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
}
void UMCGameplayHUD::EnsureCataclysmPanel()
{
    if(CataclysmPanel || !WidgetTree) return;
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root) return;
    CataclysmPanel=WidgetTree->ConstructWidget<UBorder>();
    CataclysmPanel->SetPadding(FMargin(12,10)); CataclysmPanel->SetBrushColor(MCGameplayHUDPrivate::Ink);
    CataclysmPanel->SetVisibility(ESlateVisibility::Collapsed);
    CataclysmInstruction=WidgetTree->ConstructWidget<UTextBlock>();
    CataclysmInstruction->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),13));
    CataclysmInstruction->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::White));
    CataclysmInstruction->SetWrapTextAt(280); CataclysmInstruction->SetAutoWrapText(true);
    CataclysmPanel->SetContent(CataclysmInstruction);
    auto* CataclysmSlot=Root->AddChildToCanvas(CataclysmPanel);
    CataclysmSlot->SetPosition(FVector2D(22,338)); CataclysmSlot->SetSize(FVector2D(304,128));
}
void UMCGameplayHUD::EnsureProgressionPanel()
{
    if(ProgressionPanel || !WidgetTree) return;
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root) return;
    ProgressionPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TeamProgression"));
    ProgressionPanel->SetPadding(FMargin(14,8)); ProgressionPanel->SetBrushColor(MCGameplayHUDPrivate::Ink);
    ProgressionPanel->SetVisibility(ESlateVisibility::Collapsed);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(); ProgressionPanel->SetContent(Column);
    ProgressionText=WidgetTree->ConstructWidget<UTextBlock>();
    ProgressionText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14));
    ProgressionText->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::White)); Column->AddChildToVerticalBox(ProgressionText);
    ProgressionBar=WidgetTree->ConstructWidget<UProgressBar>(); ProgressionBar->SetFillColorAndOpacity(MCGameplayHUDPrivate::Mint);
    Column->AddChildToVerticalBox(ProgressionBar);
    auto* XPPanelSlot=Root->AddChildToCanvas(ProgressionPanel); XPPanelSlot->SetAnchors(FAnchors(.5f,0));
    XPPanelSlot->SetAlignment(FVector2D(.5f,0)); XPPanelSlot->SetPosition(FVector2D(0,116)); XPPanelSlot->SetSize(FVector2D(330,58));
}
void UMCGameplayHUD::EnsureDirectorMonitor()
{
    if((DirectorPanel && DirectorCandidatesPanel && DirectorLogButton) || !WidgetTree) return;
    // Use the authored design canvas so the sidebar follows its viewport scale.
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root) return;
    auto AddPanel=[&](FName Name,TObjectPtr<UBorder>& Panel,TObjectPtr<UTextBlock>& TextWidget,float Width,float RightOffset,float Top)
    {
        Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);
        Panel->SetPadding(FMargin(12,10));Panel->SetBrushColor(MCGameplayHUDPrivate::Ink);
        Panel->SetVisibility(ESlateVisibility::Collapsed);
        auto* PanelSize=WidgetTree->ConstructWidget<USizeBox>();
        PanelSize->SetWidthOverride(Width);PanelSize->SetMaxDesiredHeight(600);
        PanelSize->SetClipping(EWidgetClipping::ClipToBounds);Panel->SetContent(PanelSize);
        TextWidget=WidgetTree->ConstructWidget<UTextBlock>();
        TextWidget->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11));
        TextWidget->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::White));
        TextWidget->SetWrapTextAt(Width);TextWidget->SetAutoWrapText(false);
        PanelSize->SetContent(TextWidget);
        auto* PanelSlot=Root->AddChildToCanvas(Panel);PanelSlot->SetAnchors(FAnchors(1,0));
        PanelSlot->SetAlignment(FVector2D(1,0));PanelSlot->SetPosition(FVector2D(RightOffset,Top));PanelSlot->SetAutoSize(true);
        return PanelSize;
    };
    if(!DirectorPanel) DirectorPanelSize=AddPanel(TEXT("DirectorMonitor"),DirectorPanel,DirectorText,280,-22,338);
    // The second column ends before the observation column and stays right of
    // the arena centre, leaving the player portraits and bottom controls clear.
    if(!DirectorCandidatesPanel) AddPanel(TEXT("DirectorCandidates"),DirectorCandidatesPanel,DirectorCandidatesText,306,-362,144);
    if (!DirectorLogButton) {
        DirectorLogButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("DirectorLogButton"));
        DirectorLogButton->SetBackgroundColor(MCGameplayHUDPrivate::Ink);
        DirectorLogButton->SetToolTipText(FText::FromString(TEXT("Открыть общий журнал забега и решения директора. F3 доступен также с клавиатуры.")));
        DirectorLogButtonText=WidgetTree->ConstructWidget<UTextBlock>();
        DirectorLogButtonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11));
        DirectorLogButtonText->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::Mint));
        DirectorLogButtonText->SetText(FText::FromString(TEXT("F3 · ЖУРНАЛ ДИРЕКТОРА")));
        auto* Content=CastChecked<UButtonSlot>(DirectorLogButton->AddChild(DirectorLogButtonText));
        Content->SetPadding(FMargin(12,8));
        DirectorLogButton->OnClicked.AddDynamic(this,&UMCGameplayHUD::OpenDirectorLog);
        auto* LogSlot=Root->AddChildToCanvas(DirectorLogButton);LogSlot->SetAnchors(FAnchors(1,0));
        LogSlot->SetAlignment(FVector2D(1,0));LogSlot->SetPosition(FVector2D(-22,96));LogSlot->SetAutoSize(true);
        LogSlot->SetZOrder(10);
    }
}
void UMCGameplayHUD::OpenDirectorLog()
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->ToggleDevPanel();
}
void UMCGameplayHUD::RefreshDirectorMonitor(const FMCGameDirectorState& State)
{
    EnsureDirectorMonitor();
    if(!DirectorPanel || !DirectorText) return;
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    const bool Debug=(State.bEnabled || (GS && !GS->DirectorDecisionLog.IsEmpty())) && MCGameplayHUDPrivate::DirectorDebug.GetValueOnGameThread()>0;
    if (DirectorLogButton) {
#if UE_BUILD_SHIPPING
        DirectorLogButton->SetVisibility(ESlateVisibility::Collapsed);
#else
        DirectorLogButton->SetVisibility(ESlateVisibility::Visible);
#endif
        const bool FragmentComplete=GS && GS->SingleDayDirector && GS->SingleDayDirector->bAuthoredFragmentComplete;
        DirectorLogButtonText->SetText(FText::FromString(FragmentComplete?TEXT("ФРАГМЕНТ ГОТОВ · F3 / ЖУРНАЛ"):TEXT("F3 · ЖУРНАЛ ДИРЕКТОРА")));
    }
    DirectorPanel->SetVisibility(State.bEnabled || Debug?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(DirectorCandidatesPanel) DirectorCandidatesPanel->SetVisibility(Debug?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Events=Find(TEXT("EventsPanel")))
        Events->SetVisibility(Debug?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(!State.bEnabled) return;
    const float Width=Debug?306.f:280.f;
    DirectorPanelSize->SetWidthOverride(Width);DirectorText->SetWrapTextAt(Width);
    if(auto* PanelSlot=Cast<UCanvasPanelSlot>(DirectorPanel->Slot)) PanelSlot->SetPosition(FVector2D(-22,Debug?144:338));
    if(GS && GS->bSingleDayLoop && !Debug) {
        if(State.Instruction.IsEmpty()) DirectorPanel->SetVisibility(ESlateVisibility::Collapsed);
        DirectorText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
        DirectorText->SetText(FText::FromString(State.Instruction));
        return;
    }
    DirectorText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11));
    using namespace MCGameplayHUDPrivate;
    FString Value=PacingName(State.Pacing)+FString::Printf(TEXT(" · сложность %.2f\nНагрузка %.2f · цель %.2f"),State.Difficulty,State.Pressure,State.TargetPressure);
    if(!State.Instruction.IsEmpty()) Value+=TEXT("\n")+ShortLine(State.Instruction,92);
    Value+=(State.bNextReserved?TEXT("\nОкно зарезервировано: "):TEXT("\nКандидат: "))
        +(State.NextTitle.IsEmpty()?TEXT("ожидание"):ShortLine(State.NextTitle,70));
    if(!State.DecisionReason.IsEmpty()) Value+=TEXT("\n")+ShortLine(State.DecisionReason,120);
    if(Debug)
    {
        Value+=FString::Printf(TEXT("\n\nПредел %.2f · работа ≈ %.0f чел·с\nТемп %.2f чел·с/с/доступного\nЗдоровье %d%% · запас сил %d%%\nСтресс %d%% · игроков %d · доступны %d\nДень прошёл на %d%%\nИсходная еда %d/%d · готово %d%%\nзавершено / фактически подано\nЦелых %d · кусочков %d · несут %d\nГлотка: %d в очереди · движение %s\nУборка %d · лёд %d · огонь %d · язвы %d\nОчередь событий %d · срочно: %s"),
            State.PressureLimit,State.WorkSeconds,State.Throughput,Percent(State.TeamHealth),Percent(State.TeamStamina),Percent(State.Stress),
            State.LivingPlayers,State.AvailablePlayers,Percent(State.DayProgress),State.FinishedFood,State.SpawnedFood,Percent(State.CompletionProgress),
            State.WholeFood,State.Fragments,State.CarriedFood,State.ThroatQueued,State.bGlobalMovement?TEXT("да"):TEXT("нет"),
            State.CleaningTasks,State.Ice,State.Fires,State.Ulcers,State.QueuedEvents,State.bUrgent?TEXT("да"):TEXT("нет"));
        if(!State.LastDecision.IsEmpty()) Value+=TEXT("\nРешение: ")+ShortLine(State.LastDecision,100);
        Value+=TEXT("\nMC.Director.Debug 0 — свернуть\n\nПОСЛЕДНИЕ РЕШЕНИЯ · НОВЫЕ СВЕРХУ");
        // Keep the newest decisions visible within the observation panel's height.
        const auto& History=GS?GS->DirectorDecisionLog:State.DecisionLog;
        for(int32 I=History.Num()-1;I>=FMath::Max(0,History.Num()-4);--I)
            Value+=TEXT("\n")+ShortLine(History[I],40);

        TArray<const FMCGameDirectorCandidate*,TInlineAllocator<16>> Candidates;
        for(const auto& Candidate:State.Candidates) Candidates.Add(&Candidate);
        Candidates.Sort([](const FMCGameDirectorCandidate& A,const FMCGameDirectorCandidate& B)
        {
            const float AP=CandidateProbability(A),BP=CandidateProbability(B);
            return AP==BP?uint8(A.Kind)<uint8(B.Kind):AP>BP;
        });
        FString Weights=TEXT("КАНДИДАТЫ · ВЕРОЯТНОСТЬ ВЫБОРА\nВес базовый → итоговый · шанс\nP — прогноз нагрузки после события");
        for(const auto* Candidate:Candidates)
        {
            Weights+=FString::Printf(TEXT("\n%s  %.1f→%.1f · %.1f%%\nP %.2f · %s"),*CandidateName(Candidate->Kind),
                Candidate->BaseWeight,Candidate->EffectiveWeight,CandidateProbability(*Candidate)*100,Candidate->ForecastPressure,
                *ShortLine(Candidate->BlockReason.IsEmpty()?TEXT("доступно"):Candidate->BlockReason,60));
        }
        Weights+=FString::Printf(TEXT("\n\nЖдать: вес %.1f · %.1f%%\nВыбор включает ожидание."),State.WaitWeight,
            State.WaitWeight>0?FMath::Clamp(State.WaitProbability,0.f,1.f)*100:0.f);
        if(DirectorCandidatesText && !DirectorCandidatesText->GetText().ToString().Equals(Weights))
            DirectorCandidatesText->SetText(FText::FromString(Weights));
    }
    else Value+=TEXT("\nMC.Director.Debug 1 — наблюдения и веса");
    if(!DirectorText->GetText().ToString().Equals(Value)) DirectorText->SetText(FText::FromString(Value));
}
void UMCGameplayHUD::NativeConstruct()
{
    Super::NativeConstruct();
    if(IsDesignTime()) {ApplyEditorPreview(PreviewTime);return;}
    if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->RegisterGameplayHUD(this);
    Widgets.Reset();
    TArray<UWidget*> All; WidgetTree->GetAllWidgets(All);
    for(auto* Widget:All) {
        Widgets.Add(Widget->GetFName(),Widget);
        // Tooth portraits bob in paint; cache their surrounding layout without freezing the animation.
        if(auto* Icon=Cast<UMCHUDIcon>(Widget);Icon && Icon->bTooth) Icon->ForceVolatile(true);
    }
    RefreshState();
}
void UMCGameplayHUD::NativeDestruct()
{
    if(!IsDesignTime()) if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->UnregisterGameplayHUD(this);
    if(AlertWidget) AlertWidget->ClearAlerts();
    Super::NativeDestruct();
}
UWidget* UMCGameplayHUD::Find(FName Name) const
{ const auto* Widget=Widgets.Find(Name); return Widget?Widget->Get():WidgetTree?WidgetTree->FindWidget(Name):nullptr; }
void UMCGameplayHUD::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); RefreshElapsed+=Dt;
    if(Geometry.GetLocalSize().X>1.f && !FMath::IsNearlyEqual(LayoutLogicalWidth,Geometry.GetLocalSize().X,.5f)) {LayoutLogicalWidth=Geometry.GetLocalSize().X;Phase1LayoutSignature.Reset();}
    UpdatePhase1Layout();
    if(AlertWidget) if(const auto* Boss=Cast<UMCBossHealthWidget>(EncounterPreviewWidget)) AlertWidget->SetBossStackCount(Boss->GetVisibility()==ESlateVisibility::Collapsed?0:Boss->GetDisplayedBossCount(),Boss->GetDisplayedStackHeight());
    if(IsDesignTime() || bApplyingPreview) return;
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const bool Spray=Hero && Hero->Inventory && Hero->Inventory->Selected==EMCToolSlot::Spray;
    const auto* Player=GetOwningPlayer();
    const float FoodAim=Hero && Player && !Player->bShowMouseCursor && !Player->IsLookInputIgnored()?Hero->FoodIntakeAimStrength():0;
    const bool HadFoodReticle=FoodReticleAlpha>.001f;
    FoodReticleAlpha=FMath::FInterpTo(FoodReticleAlpha,FoodAim,Dt,12.f);
    if(FoodAim<=0 && FoodReticleAlpha<.005f) FoodReticleAlpha=0;
    // Charge and cooldown animate every frame; ordinary HUD data remains at 10 Hz.
    if(Spray || bHadSprayReticle || HadFoodReticle || FoodReticleAlpha>0) if(const auto Cached=GetCachedWidget()) Cached->Invalidate(EInvalidateWidgetReason::Paint);
    bHadSprayReticle=Spray;
    if(RefreshElapsed>=.1f) { RefreshElapsed=0; RefreshState();UpdatePhase1Layout(); }
}
int32 UMCGameplayHUD::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    if(IsDesignTime() || bApplyingPreview) return Top;
    const auto* ThemeData=GetResolvedTheme();const auto& Palette=ThemeData->Palette;
    const auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* Inv=Hero?Hero->Inventory.Get():nullptr;
    if(PC && Hero && Hero->Status->IsAlive() && !PC->bShowMouseCursor) if(const auto* Perks=UMCPerkComponent::ForActor(Hero)) {
        FVector Point;
        if(Perks->GetFogNavigation(Point)) {
            FVector View;FRotator Rotation;PC->GetPlayerViewPoint(View,Rotation);
            const FVector Delta=(Point-Hero->GetActorLocation()).GetSafeNormal2D();
            const FVector Forward=Rotation.Vector().GetSafeNormal2D(),Right=FVector::CrossProduct(FVector::UpVector,Forward);
            const FVector2D Direction=FVector2D(FVector::DotProduct(Delta,Right),-FVector::DotProduct(Delta,Forward)).GetSafeNormal();
            const FVector2D Center=Geometry.GetLocalSize()*.5,Tip=Center+Direction*130,Side(-Direction.Y,Direction.X);
            FSlateDrawElement::MakeLines(Elements,++Top,Geometry.ToPaintGeometry(),{Tip-Direction*20+Side*10,Tip,Tip-Direction*20-Side*10},ESlateDrawEffect::None,Palette.Mint,true,4);
        }
    }
    if(PC && PC->IsLocalController() && !PC->bShowMouseCursor && !PC->IsLookInputIgnored() && !PC->IsSpectating()
        && Hero && Hero->CanWork() && FoodReticleAlpha>.005f) {
        // Paint in this HUD's allotted space. Player-screen geometry can belong
        // to a different Slate window while PIE uses a floating game viewport.
        const FVector2D AimCenter=Geometry.GetLocalSize()*.5f;
        const float A=FMath::Clamp(FoodReticleAlpha,0.f,1.f),Radius=FMath::Lerp(5.f,25.f,A),Arm=FMath::Lerp(3.f,10.f,A);
        for(int32 I=0;I<4;++I) {
            const FVector2D Sign(I<2?-1:1,(I%2)?-1:1),Corner=AimCenter+Sign*Radius;
            const TArray<FVector2D> Points={Corner-FVector2D(Sign.X*Arm,0),Corner,Corner-FVector2D(0,Sign.Y*Arm)};
            FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Palette.Ink.CopyWithNewOpacity(Palette.Ink.A*A*.85f),true,4);
            FSlateDrawElement::MakeLines(Elements,Top+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Palette.OnMarine.CopyWithNewOpacity(Palette.OnMarine.A*A),true,2);
        }
        FSlateDrawElement::MakeText(Elements,Top+2,
            Geometry.ToPaintGeometry(FVector2D(180,24),FSlateLayoutTransform(AimCenter+FVector2D(-58,Radius+12))),
            Copy(TEXT("UI.HUD.FoodThrow"),LOCTEXT("FoodThrow","Q · БРОСИТЬ")),ThemeData->MakeGameplayFont(12),ESlateDrawEffect::None,Palette.OnMarine.CopyWithNewOpacity(Palette.OnMarine.A*A));
        Top+=2;
    }
    if(!PC || !PC->IsLocalController() || PC->bShowMouseCursor || PC->IsLookInputIgnored() || PC->IsSpectating()
        || !Inv || Inv->Selected!=EMCToolSlot::Spray || !Hero->CanWork() || Hero->bInCoffee || !Inv->ShouldPresentTool()) return Top;
    const FVector2D Center=Geometry.GetLocalSize()*.5f;
    const bool Pressure=Inv->SelectedUpgrade()==EMCToolUpgrade::Watergun && Inv->bPressureMode;
    const float Cool=Pressure?Inv->SpraySecondsLeft():0;
    const FLinearColor Tint=Cool>0?Palette.Muted:Pressure?Palette.OnMarine:Palette.Accent;
    auto Stroke=[&](const TArray<FVector2D>& Points,FLinearColor Color,float Width=2.f) {
        FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Palette.Ink.CopyWithNewOpacity(Palette.Ink.A*.8f),true,Width+2);
        FSlateDrawElement::MakeLines(Elements,Top+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
    };
    auto Arc=[&](float Radius,float Fraction,FLinearColor Color) {
        if(Fraction<=0) return;
        TArray<FVector2D> Points;
        const int32 Steps=FMath::Max(2,FMath::CeilToInt(40*Fraction));
        for(int32 I=0;I<=Steps;++I) {
            const float A=-PI/2+2*PI*Fraction*I/Steps;
            Points.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);
        }
        Stroke(Points,Color);
    };
    const float Radius=Pressure?7.f:10.f;
    Arc(Radius,1,Tint);
    for(const FVector2D Axis:{FVector2D(1,0),FVector2D(-1,0),FVector2D(0,1),FVector2D(0,-1)})
        Stroke({Center+Axis*(Radius+4),Center+Axis*(Radius+9)},Tint);
    const FSlateRoundedBoxBrush Dot(FLinearColor::White,2.f);
    FSlateDrawElement::MakeBox(Elements,Top+2,Geometry.ToPaintGeometry(FVector2D(4,4),FSlateLayoutTransform(Center-FVector2D(2,2))),&Dot,ESlateDrawEffect::None,Tint);
    if(Pressure) {
        Arc(18,1,Palette.SurfaceAlt.CopyWithNewOpacity(Palette.SurfaceAlt.A*.65f));
        Arc(18,Inv->bWaterReloading?1-Cool/Inv->CooldownSeconds():float(Inv->WaterAmmo)/Inv->WaterMagazineCapacity(),Palette.Shield);
    }
    return Top+3;
}
void UMCGameplayHUD::RefreshState()
{
    if(IsDesignTime() || bApplyingPreview) return;
    namespace HUD = MCGameplayHUDPrivate;
    const auto& Palette=GetResolvedTheme()->Palette;
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr; if(!GS) return;
    RefreshGameplayAlerts();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* InputController=GetOwningPlayer<AMCPlayerController>();
    RefreshReserveTeeth(*GS);RefreshBooster(Hero);
    if(StaminaWidget) StaminaWidget->Refresh();
    // HUD refresh wakes the collapsed gauge when water or recovery begins.
    if(SwimEnergyWidget) SwimEnergyWidget->Refresh();
    ApplyResourceTheme(StaminaWidget);ApplyResourceTheme(SwimEnergyWidget);
    auto Text=[&](FName Name,const FString& Value) { SetCopy(Name,LiteralCopy(Value)); };
    auto Bar=[&](FName Name,float Value) { if(auto* B=Cast<UProgressBar>(Find(Name))) B->SetPercent(FMath::Clamp(Value,0.f,1.f)); };
    auto Show=[&](FName Name,bool Visible) { if(auto* W=Find(Name)) W->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed); };
    auto Color=[&](FName Name,FLinearColor Value) { if(auto* I=Cast<UImage>(Find(Name))) I->SetColorAndOpacity(Value); };
    const double Now=GS->GetServerWorldTimeSeconds();
    EnsureProgressionPanel();
    if(ProgressionPanel) {
        ProgressionPanel->SetVisibility(GS->bSingleDayLoop && !GS->bTutorialActive?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(GS->Progression && ProgressionText && ProgressionBar) {
            const auto* XP=GS->Progression.Get();
            ProgressionText->SetText(UMCUIDataLibrary::FormatTextForCulture(XP->ExperienceToNextLevel>0
                ? Copy(TEXT("UI.HUD.TeamXP"),LOCTEXT("TeamXP","Ур. {Level} · {Current} / {Required} XP"))
                : Copy(TEXT("UI.HUD.TeamXPMaximum"),LOCTEXT("TeamXPMaximum","Ур. {Level} · МАКСИМУМ")),
                FFormatNamedArguments{{TEXT("Level"),FText::AsNumber(XP->TeamLevel)},{TEXT("Current"),FText::AsNumber(XP->ExperienceInLevel)},{TEXT("Required"),FText::AsNumber(XP->ExperienceToNextLevel)}}));
            ProgressionBar->SetPercent(XP->ExperienceToNextLevel>0?FMath::Clamp(float(XP->ExperienceInLevel)/XP->ExperienceToNextLevel,0.f,1.f):1.f);
        }
    }
    const auto& Director=GS->DirectorState;const bool Directed=Director.bEnabled;
    RefreshDirectorMonitor(Director);
    const bool HasStep=!Directed && GS->DayPlan && GS->DayPlan->Steps.IsValidIndex(GS->StepIndex);
    const bool Finished=(!Directed && GS->bDayOneComplete) || GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost;
    const auto* Cataclysm=AMCCataclysmEvent::FindActive(GetWorld());
    if(!Cataclysm && GS->bDevManualEvents)
        for(TActorIterator<AMCCataclysmEvent> It(GetWorld());It;++It)
            if(!It->IsActorBeingDestroyed() && It->IsComplete() && Now-It->PhaseStartedAt<8.
                && (!Cataclysm || It->PhaseStartedAt>Cataclysm->PhaseStartedAt)) Cataclysm=*It;
    EnsureCataclysmPanel();
    if(CataclysmPanel) {
        CataclysmPanel->SetVisibility(Cataclysm && !Finished?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(Cataclysm && CataclysmInstruction) {
            const FText Instruction=Copy(HUD::CataclysmInstructionKey(*Cataclysm),FText::FromString(Cataclysm->Instruction()));
            if(!CataclysmInstruction->GetText().EqualTo(Instruction)) CataclysmInstruction->SetText(Instruction);
        }
    }
    const FString StartingSoon=Copy(TEXT("UI.HUD.StartingSoon"),LOCTEXT("StartingSoon","СКОРО НАЧНЁМ")).ToString();
    FString Title=Directed?(Director.CurrentTitle.IsEmpty()?StartingSoon:Director.CurrentTitle):HasStep?HUD::StepName(GS->DayPlan->Steps[GS->StepIndex].Step,LocalizationTable).ToString():GS->CurrentEvent?GS->CurrentEvent->Title.ToString():StartingSoon;
    const AMCCoffeeFlood* ActiveFlood=nullptr;
    for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) if(It->IsActive()) { ActiveFlood=*It; break; }
    if(!Directed && ActiveFlood) Title=ActiveFlood->bRiverFlood?Copy(TEXT("UI.HUD.Step.CoffeeWaves"),LOCTEXT("StepCoffeeWaves","ЦУНАМИ")).ToString():ActiveFlood->GetPhase()==EMCCoffeePhase::Holding?Copy(TEXT("UI.HUD.CoffeeSwimming"),LOCTEXT("CoffeeSwimming","КОФЕ · ПЛАВАНИЕ")).ToString():TEXT("old_flood");
    if(Directed && GS->Phase==EMCShiftPhase::Intermission) Title=Copy(TEXT("UI.HUD.Intermission"),LOCTEXT("Intermission","ПЕРЕДЫШКА")).ToString();
    if(Cataclysm) Title=Copy(HUD::CataclysmTitleKey(*Cataclysm),FText::FromString(Cataclysm->Title())).ToString();
    if(Finished) Title=(GS->Phase==EMCShiftPhase::Lost?Copy(TEXT("UI.HUD.RunLost"),LOCTEXT("RunLost","РОТ НЕ СПАСЁН")):Copy(TEXT("UI.HUD.RunComplete"),LOCTEXT("RunComplete","ЗАБЕГ ЗАВЕРШЁН"))).ToString();
    Show(TEXT("DayTitle"),false);Text(TEXT("EventTitle"),Title);
    const float Done=Directed?FMath::Clamp(Director.CompletionProgress,0.f,1.f):GS->TasksTotal>0?1-float(GS->TasksLeft)/GS->TasksTotal:0;
    int32 Surfaces=0; float Clean=0;bool HasDirt=false;
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        if(const auto* Patch=Cast<AMCMouthSurface>(*It)) {
            if(!Patch->bUlcer) {
                const bool IsClean=Patch->IsClean();HasDirt|=!IsClean;++Surfaces;
                Clean+=IsClean?1.f:1-FMath::Clamp(Patch->RemainingLiquid(),0.f,1.f);
            }
            continue;
        }
        if(const auto* Tooth=Cast<AMCArenaTooth>(*It);Tooth && !Tooth->IsAvailable()) continue;
        if(const auto* Care=It->FindComponentByClass<UMCToothStatusComponent>();Care && Care->IsAlive()) {
            ++Surfaces;Clean+=1-Care->CoffeeAmount();HasDirt|=Care->State.CoffeeLeft>0;
        }
    }
    Clean=Surfaces?Clean/Surfaces:1;
    // An almost erased tile must not round up to "100%" while the same live
    // residue still holds the meal/cleanup gate open.
    if(HasDirt) Clean=FMath::Min(Clean,.99f);
    const float Health=GS->MouthHealth/FMath::Max(1.f,GS->RunSettings.MaxMouthHealth);
    SetCopy(TEXT("TaskLabel"),Directed && !GS->bSingleDayLoop?Copy(TEXT("UI.HUD.FoodCompleted"),LOCTEXT("FoodCompleted","ЕДА ЗАВЕРШЕНА")):Copy(TEXT("UI.HUD.Task"),LOCTEXT("Task","ЗАДАЧА")));
    SetCopy(TEXT("TaskValue"),FText::Format(Copy(TEXT("UI.HUD.CountPair"),LOCTEXT("CountPair","{Current} / {Maximum}")),FFormatNamedArguments{{TEXT("Current"),FText::AsNumber(Directed?Director.FinishedFood:FMath::Max(0,GS->TasksTotal-GS->TasksLeft))},{TEXT("Maximum"),FText::AsNumber(Directed?Director.SpawnedFood:GS->TasksTotal)}})); Bar(TEXT("TaskProgress"),Done);
    if(Cataclysm) {
        SetCopy(TEXT("TaskLabel"),Cataclysm->Variant==EMCCataclysmVariant::Rafts
            ?Cataclysm->Phase==EMCCataclysmPhase::Flooding?UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.HUD.Cataclysm.Survive"),LOCTEXT("CataclysmSurvive","ВЫЖИТЬ · {Seconds} С")),{{TEXT("Seconds"),UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt64(Cataclysm->HazardSecondsRemaining()))}}):Copy(TEXT("UI.HUD.Cataclysm.Dirt"),LOCTEXT("CataclysmDirt","ПОСЛЕДСТВИЯ · ГРЯЗЬ"))
            :UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.HUD.Cataclysm.Jellies"),LOCTEXT("CataclysmJellies","ЖЕЛЕ {Count} · ГРЯЗЬ")),{{TEXT("Count"),Cataclysm->JelliesLeft}}));
        SetCopy(TEXT("TaskValue"),UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.HUD.AmmoCount"),LOCTEXT("AmmoCount","{Current}/{Maximum}")),{{TEXT("Current"),Cataclysm->DirtLeft},{TEXT("Maximum"),Cataclysm->DirtTotal}}));
        Bar(TEXT("TaskProgress"),Cataclysm->DirtTotal>0?1-float(Cataclysm->DirtLeft)/Cataclysm->DirtTotal:0.f);
    }
    SetCopy(TEXT("CleanValue"),FText::Format(Copy(TEXT("UI.HUD.Percent"),LOCTEXT("Percent","{Value}%")),FFormatNamedArguments{{TEXT("Value"),FText::AsNumber(FMath::RoundToInt(Clean*100))}})); Bar(TEXT("CleanProgress"),Clean);
    SetCopy(TEXT("HealthValue"),FText::Format(Copy(TEXT("UI.HUD.Percent"),LOCTEXT("Percent","{Value}%")),FFormatNamedArguments{{TEXT("Value"),FText::AsNumber(FMath::RoundToInt(Health*100))}})); Bar(TEXT("HealthProgress"),Health);
    const double EndAt=Directed && GS->Phase==EMCShiftPhase::Working?Director.DayEndAt:GS->PhaseEndsAt;
    const bool Timed=EndAt>0 && !Finished && (Directed || !GS->bDevManualEvents);
    const float Remaining=float(FMath::Max(0.,EndAt-Now));const int32 Seconds=FMath::CeilToInt(Remaining);
    const float Duration=FMath::Max(1.f,float(EndAt-GS->StepStartedAt));
    const float Progress=Directed && GS->Phase==EMCShiftPhase::Working?FMath::Clamp(Director.DayProgress,0.f,1.f)
        :Timed?FMath::Clamp(1-Remaining/Duration,0.f,1.f):Done;
    Text(TEXT("TimerValue"),Timed?FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60):TEXT("--:--"));
    SetCopy(TEXT("TimerLabel"),Finished?Copy(TEXT("UI.HUD.RunResults"),LOCTEXT("RunResults","ИТОГИ ЗАБЕГА")):GS->bSingleDayLoop?Timed?Copy(TEXT("UI.HUD.UntilEvent"),LOCTEXT("UntilEvent","ДО СОБЫТИЯ")):Copy(TEXT("UI.HUD.Endure"),LOCTEXT("Endure","ПРОДЕРЖИСЬ И ПРОКАЧАЙСЯ")):Timed?Directed?GS->Phase==EMCShiftPhase::Working?Copy(TEXT("UI.HUD.UntilRunEnd"),LOCTEXT("UntilRunEnd","ДО КОНЦА ЗАБЕГА")):Copy(TEXT("UI.HUD.UntilStart"),LOCTEXT("UntilStart","ДО НАЧАЛА")):Copy(TEXT("UI.HUD.UntilEvent"),LOCTEXT("UntilEvent","ДО СОБЫТИЯ")):Copy(TEXT("UI.HUD.NoTimeLimit"),LOCTEXT("NoTimeLimit","БЕЗ ЛИМИТА ВРЕМЕНИ"))); Bar(TEXT("TimerProgress"),Finished?1:Progress);
    if(auto* T=Cast<UTextBlock>(Find(TEXT("TimerValue")))) T->SetColorAndOpacity(FSlateColor(Timed && Seconds<10?Palette.Warning:Palette.LCDInk));
    TArray<APlayerState*> Players;
    for(const auto& Player:GS->PlayerArray)
        if(IsValid(Player) && !Player->IsSpectator() && !Player->IsOnlyASpectator()) Players.Add(Player.Get());
    Players.Sort([](const APlayerState& A,const APlayerState& B){return A.GetPlayerId()<B.GetPlayerId();});
    DisplayedPlayerCount=FMath::Min(4,Players.Num());
    if(!Find(TEXT("TimerClock")))
        if(auto* Strip=Find(TEXT("PlayersPanel"))) Strip->SetRenderTranslation(FVector2D(76*(4-DisplayedPlayerCount),0));
    for(int32 I=0;I<4;++I) {
        const FString N=FString::Printf(TEXT("Player%d"),I+1); Show(FName(N),Players.IsValidIndex(I)); if(!Players.IsValidIndex(I)) continue;
        const auto* Tooth=Cast<AMCToothCharacter>(Players[I]->GetPawn()); const bool Dead=Tooth && !Tooth->Status->IsAlive();
        const auto* State=Cast<AMCPlayerState>(Players[I]);
        const FLinearColor PlayerTint=State?State->PlayerColor:Tooth?Tooth->GetPlayerColor():Palette.OnMarine;
        Color(FName(N+TEXT("Frame")),Dead?PlayerTint.Desaturate(.8f)*.6f:PlayerTint);
        if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(N+TEXT("Face"))))) {
            const bool Host=State && State->bSessionHost;
            const bool Sad=Tooth && (Tooth->MimicCaptor || Now-Tooth->TaskFailureAt<2.5 || Tooth->Status->State.Health<Tooth->Status->State.MaxHealth*.35f);
            const bool Happy=Tooth && Now-Tooth->TaskSuccessAt<2 && !Sad;
            if(!Icon->Tint.Equals(PlayerTint) || Icon->bHost!=Host || Icon->bDead!=Dead || Icon->bSad!=Sad || Icon->bHappy!=Happy) {
                Icon->Tint=PlayerTint;Icon->bHost=Host;Icon->bDead=Dead;Icon->bSad=Sad;Icon->bHappy=Happy;
                if(const auto Cached=Icon->GetCachedWidget()) Cached->Invalidate(EInvalidateWidgetReason::Paint);
            }
        }
        Bar(FName(N+TEXT("Health")),Tooth?Tooth->Status->State.Health/FMath::Max(1.f,Tooth->Status->State.MaxHealth):0);
    }
    TArray<int32,TInlineAllocator<16>> TimelineSteps;
    if(HasStep) for(int32 I=0;I<GS->DayPlan->Steps.Num();++I)
        if(GS->DayPlan->Steps[I].Step!=EMCDayStep::DiscardBrushes) TimelineSteps.Add(I);
    const int32 TimelineStart=FMath::Max(0,TimelineSteps.IndexOfByKey(GS->StepIndex)-1);
    for(int32 I=0;I<3;++I) {
        const FString N=FString::Printf(TEXT("Timeline%d"),I);
        if(Directed)
        {
            const bool Visible=I==0 || I==1 && !Director.NextTitle.IsEmpty();Show(FName(N),Visible);
            if(!Visible) continue;
            SetCopy(FName(N+TEXT("Label")),I==0?Copy(TEXT("UI.HUD.Timeline.Now"),LOCTEXT("TimelineNow","СЕЙЧАС")):GS->bSingleDayLoop?Copy(TEXT("UI.HUD.Timeline.Next"),LOCTEXT("TimelineNext","ДАЛЕЕ")):Director.bNextReserved?Copy(TEXT("UI.HUD.Timeline.Reserved"),LOCTEXT("TimelineReserved","ЗАРЕЗЕРВИРОВАНО")):Copy(TEXT("UI.HUD.Timeline.Candidate"),LOCTEXT("TimelineCandidate","КАНДИДАТ")));
            Text(FName(N+TEXT("Title")),I==0?Title:Director.NextTitle);
            Bar(FName(N+TEXT("Fill")),I==0?Finished?1:Progress:0);
            if(auto* B=Cast<UProgressBar>(Find(FName(N+TEXT("Fill"))))) MCUIVisualStyle::Progress(B,GetResolvedTheme(),Palette.Mint);
            Color(FName(N+TEXT("Accent")),I==0?Palette.Mint:Palette.Muted);
            continue;
        }
        const bool Visible=HasStep?TimelineSteps.IsValidIndex(TimelineStart+I):I==0; Show(FName(N),Visible); if(!Visible) continue;
        const int32 Index=HasStep?TimelineSteps[TimelineStart+I]:INDEX_NONE;
        const bool Past=HasStep && Index<GS->StepIndex,Current=!HasStep || Index==GS->StepIndex,Failed=Past && GS->PreviousStepFailed;
        SetCopy(FName(N+TEXT("Label")),Past?Failed?Copy(TEXT("UI.HUD.Timeline.Incomplete"),LOCTEXT("TimelineIncomplete","ПРОШЛО · НЕ ПОЛНОСТЬЮ")):Copy(TEXT("UI.HUD.Timeline.Completed"),LOCTEXT("TimelineCompleted","ВЫПОЛНЕНО")):Current?Copy(TEXT("UI.HUD.Timeline.Now"),LOCTEXT("TimelineNow","СЕЙЧАС")):Copy(TEXT("UI.HUD.Timeline.Next"),LOCTEXT("TimelineNext","ДАЛЕЕ")));
        Text(FName(N+TEXT("Title")),Current?Title:HasStep?HUD::StepName(GS->DayPlan->Steps[Index].Step,LocalizationTable).ToString():Title);
        Bar(FName(N+TEXT("Fill")),Past?1:Current?Finished?1:Progress:0);
        if(auto* B=Cast<UProgressBar>(Find(FName(N+TEXT("Fill"))))) MCUIVisualStyle::Progress(B,GetResolvedTheme(),Failed?Palette.Warning:Palette.Mint);
        Color(FName(N+TEXT("Accent")),Current?Palette.Mint:Past?Failed?Palette.Warning:Palette.Muted:Palette.Marine);
    }
    Show(TEXT("InventoryPanel"),Hero!=nullptr); Show(TEXT("HintPanel"),Hero!=nullptr);
    if(Hero && Hero->Inventory) {
        const auto* Inv=Hero->Inventory.Get();
        auto Format=[&](FName Key,const FText& Fallback,const FFormatNamedArguments& Arguments){return UMCUIDataLibrary::FormatTextForCulture(Copy(Key,Fallback),Arguments);};
        auto Decimal=[](double Value){return UMCUIDataLibrary::DecimalForCulture(Value,1);};
        auto Whole=[](double Value){return UMCUIDataLibrary::NumberForCulture(FMath::RoundToInt64(Value));};
        FText ToolName;
        switch(Inv->Selected) {
        case EMCToolSlot::Pickaxe: ToolName=Inv->HasUpgrade(EMCToolUpgrade::Buffer)?Copy(TEXT("UI.HUD.Tool.Buffer"),LOCTEXT("ToolBuffer","ПЕРФОРАТОР")):Copy(TEXT("UI.HUD.Tool.Pickaxe"),LOCTEXT("Pickaxe","КИРКА"));break;
        case EMCToolSlot::Knife: ToolName=Inv->HasUpgrade(EMCToolUpgrade::Chainsaw)?Copy(TEXT("UI.HUD.Tool.Chainsaw"),LOCTEXT("ToolChainsaw","БЕНЗОПИЛА")):Copy(TEXT("UI.HUD.Tool.Knife"),LOCTEXT("Knife","НОЖ"));break;
        case EMCToolSlot::Spray: ToolName=Inv->HasUpgrade(EMCToolUpgrade::Watergun)?(Inv->bPressureMode?Copy(TEXT("UI.HUD.Tool.WatergunShoot"),LOCTEXT("ToolWatergunShoot","ВОДЯНОЙ ПИСТОЛЕТ · ВЫСТРЕЛ")):Copy(TEXT("UI.HUD.Tool.WatergunHeal"),LOCTEXT("ToolWatergunHeal","ВОДЯНОЙ ПИСТОЛЕТ · ЛЕЧЕНИЕ"))):Copy(TEXT("UI.HUD.Tool.Spray"),LOCTEXT("Spray","СПРЕЙ"));break;
        default: ToolName=Inv->HasUpgrade(EMCToolUpgrade::MeshaBrush)?Copy(TEXT("UI.HUD.Tool.MegaBrush"),LOCTEXT("ToolMegaBrush","МЕГАЩЁТКА")):Inv->bWaterJetUnlocked?Copy(TEXT("UI.HUD.Tool.WaterJet"),LOCTEXT("ToolWaterJet","ВОДОМЁТ")):Copy(TEXT("UI.HUD.Tool.Brush"),LOCTEXT("Brush","ЩЁТКА"));break;
        }
        SetCopy(TEXT("ToolTitle"),ToolName);
        SelectedToolIndex=static_cast<int32>(Inv->Selected);
        for(int32 I=0;I<4;++I) {
            const FString N=FString::Printf(TEXT("Tool%d"),I+1); const bool Selected=uint8(Inv->Selected)==I;
            if(auto* Frame=Cast<UImage>(Find(FName(N+TEXT("Frame"))))) MCUIVisualStyle::Image(Frame,GetResolvedTheme(),Palette.Marine,true,Selected);
            if(auto* Key=Cast<UImage>(Find(FName(N+TEXT("KeyBG"))))) MCUIVisualStyle::Image(Key,GetResolvedTheme(),Selected?Palette.Mint:Palette.Cream);
            if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(N+TEXT("Icon")))))
            {
                const FLinearColor NewTint=Selected?Palette.Accent:Palette.OnMarine;
                if(!Icon->Tint.Equals(NewTint)) {Icon->Tint=NewTint;Icon->InvalidateLayoutAndVolatility();}
            }
        }
        const bool Automatic=Inv->Selected==EMCToolSlot::Spray && Inv->HasUpgrade(EMCToolUpgrade::Watergun) && Inv->bPressureMode;
        const float Cool=Inv->SpraySecondsLeft(); Show(TEXT("SprayCooldown"),Automatic || Cool>0);
        SetCopy(TEXT("CooldownValue"),Automatic?(Inv->bWaterReloading?Format(TEXT("UI.HUD.CooldownSeconds"),LOCTEXT("CooldownSeconds","{Seconds} с"),{{TEXT("Seconds"),Decimal(Cool)}}):Format(TEXT("UI.HUD.AmmoCount"),LOCTEXT("AmmoCount","{Current}/{Maximum}"),{{TEXT("Current"),Inv->WaterAmmo},{TEXT("Maximum"),Inv->WaterMagazineCapacity()}})):Decimal(Cool));
        Bar(TEXT("CooldownProgress"),Automatic && !Inv->bWaterReloading?float(Inv->WaterAmmo)/Inv->WaterMagazineCapacity():1-Cool/Inv->CooldownSeconds());
        FText Hint=Inv->Selected==EMCToolSlot::Pickaxe?Copy(TEXT("UI.HUD.Hint.Pickaxe"),LOCTEXT("HintPickaxe","ЛКМ · ДРОБИТЬ · ЕДА → XP")):Inv->Selected==EMCToolSlot::Knife?Copy(TEXT("UI.HUD.Hint.Knife"),LOCTEXT("HintKnife","ЛКМ · РАЗБИТЬ ЕДУ → XP")):Inv->Selected==EMCToolSlot::Spray?Copy(TEXT("UI.HUD.Hint.Spray"),LOCTEXT("HintSpray","УДЕРЖИВАЙ ЛКМ · ЛЕЧИТЬ ЯЗВУ")):Copy(TEXT("UI.HUD.BrushAction"),LOCTEXT("BrushAction","ЛКМ · ЧИСТИТЬ"));
        if(Inv->Selected==EMCToolSlot::Pickaxe || Inv->Selected==EMCToolSlot::Knife) {
            for(TActorIterator<AMCNutRainEvent> It(GetWorld());It;++It) {
                if(It->Settings.bBossEncounter && It->Stage!=EMCNutRainStage::Idle && It->Stage!=EMCNutRainStage::Complete) {
                    Hint=Copy(TEXT("UI.HUD.Hint.NutsNoXP"),LOCTEXT("HintNutsNoXP","ЛКМ · ДРОБИТЬ · ОРЕХИ БЕЗ XP"));
                    break;
                }
            }
        }
        if(Inv->Selected==EMCToolSlot::Knife && Inv->HasUpgrade(EMCToolUpgrade::Chainsaw)) Hint=Inv->IsChainsawRunning()?Copy(TEXT("UI.HUD.Hint.ChainsawRunning"),LOCTEXT("HintChainsawRunning","БЕНЗОПИЛА ТЯНЕТ ВПЕРЁД · СТЕНА ОСТАНОВИТ · 30 УРОНА")):Copy(TEXT("UI.HUD.Hint.ChainsawStart"),LOCTEXT("HintChainsawStart","ЛКМ · ЗАПУСТИТЬ · ОСТАНОВКА ТОЛЬКО ОБ СТЕНУ"));
        if(Inv->Selected==EMCToolSlot::Spray && Inv->HasUpgrade(EMCToolUpgrade::Watergun)) Hint=Inv->bPressureMode?(Inv->bWaterReloading?Copy(TEXT("UI.HUD.Hint.WatergunReload"),LOCTEXT("HintWatergunReload","ПЕРЕЗАРЯДКА · УДЕРЖИВАЙ ЛКМ ДЛЯ ПРОДОЛЖЕНИЯ     4 · РЕЖИМ")):Copy(TEXT("UI.HUD.Hint.WatergunShoot"),LOCTEXT("HintWatergunShoot","УДЕРЖИВАЙ ЛКМ · АВТОМАТИЧЕСКАЯ СТРЕЛЬБА     4 · РЕЖИМ"))):Copy(TEXT("UI.HUD.Hint.WatergunHeal"),LOCTEXT("HintWatergunHeal","УДЕРЖИВАЙ ЛКМ · ТУШИТЬ И ЛЕЧИТЬ     4 · РЕЖИМ"));
        if(!Inv->IsChainsawRunning()) Hint=Format(TEXT("UI.HUD.Hint.WithBrace"),LOCTEXT("HintWithBrace","{Action}     ПКМ · ДЕРЖАТЬСЯ"),{{TEXT("Action"),Hint}});
        const auto IsFreshOrdinaryFood=[](const AMCFoodActor* Food) {
            return IsValid(Food) && Food->FoodData.Kind==EMCFoodKind::Food && !Food->bBrushTool && !Food->IsWrongIngredient();
        };
        bool HasOrdinaryPieces=false,HasExitPieces=false;
        for(const auto& Piece:Hero->FoodCollection->Pieces) if(IsValid(Piece)) {
            HasOrdinaryPieces|=IsFreshOrdinaryFood(Piece);HasExitPieces|=!IsFreshOrdinaryFood(Piece);
        }
        const bool HeldNeedsExit=IsValid(Hero->HeldFood) && !IsFreshOrdinaryFood(Hero->HeldFood);
        const bool ExplicitFoodTransport=GS->bSingleDayLoop
            ?!HeldNeedsExit && !HasExitPieces && (IsFreshOrdinaryFood(Hero->HeldFood) || Hero->FoodCollection->bCollecting || HasOrdinaryPieces)
            :IsValid(Hero->HeldFood) || Hero->FoodCollection->bCollecting || !Hero->FoodCollection->Pieces.IsEmpty();
        if(Hero->FoodCollection->bCollecting)
        {
            bool HasWrong=GS->bSingleDayLoop && HasExitPieces;
            for(const auto& Piece:Hero->FoodCollection->Pieces) if(IsValid(Piece) && Piece->IsWrongIngredient()) { HasWrong=true; break; }
            Hint=HasWrong?Format(TEXT("UI.HUD.Hint.WasteStack"),LOCTEXT("HintWasteStack","СТОПКА {Current}/{Maximum} · МУСОР ЗА БОРТ · Q ПО ОДНОМУ"),{{TEXT("Current"),Hero->FoodCollection->Pieces.Num()},{TEXT("Maximum"),Hero->FoodCollection->EffectiveCapacity()}})
                :Format(TEXT("UI.HUD.Hint.FoodStack"),LOCTEXT("HintFoodStack","СТОПКА {Current}/{Maximum} · ЦЕЛЬСЯ В ГЛОТКУ · Q ПО ОДНОМУ"),{{TEXT("Current"),Hero->FoodCollection->Pieces.Num()},{TEXT("Maximum"),Hero->FoodCollection->EffectiveCapacity()}});
        }
        if(Hero->IsYawning()) Hint=Copy(TEXT("UI.HUD.Hint.Yawn"),LOCTEXT("HintYawn","ЗЕВАНИЕ · WASD + SHIFT · БЕГИ ПРОТИВ ПОТОКА"));
        if(Hero->HeldFood) Hint=GS->bSingleDayLoop && Hero->HeldFood->FoodData.Kind==EMCFoodKind::Spicy?Copy(TEXT("UI.HUD.Hint.PepperThrow"),LOCTEXT("HintPepperThrow","ПЕРЕЦ · Q БРОСИТЬ ЗА БОРТ"))
            :Hero->HeldFood->IsWrongIngredient()?Copy(TEXT("UI.HUD.Hint.WasteThrow"),LOCTEXT("HintWasteThrow","МУСОР · Q БРОСИТЬ ЗА БОРТ")):Copy(TEXT("UI.HUD.Hint.HeldFood"),LOCTEXT("HintHeldFood","E · ДЕРЖАТЬ     Q · БРОСИТЬ"));
        if(Hero->bInCoffee) Hint=ActiveFlood && ActiveFlood->bRiverFlood?
            Copy(TEXT("UI.HUD.Hint.CoffeeRiver"),LOCTEXT("HintCoffeeRiver","WASD + SHIFT · БЕЖАТЬ     ПКМ · ДЕРЖАТЬСЯ ЗА ОПОРУ")):
            Copy(TEXT("UI.HUD.Hint.CoffeeSwim"),LOCTEXT("HintCoffeeSwim","WASD · ПЛЫТЬ     ЛКМ · ЗАЦЕПИТЬСЯ"));
        if(const auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()); Move && Move->IsClimbing()) Hint=Copy(TEXT("UI.HUD.Hint.Climb"),LOCTEXT("HintClimb","WASD · ЛАЗАТЬ     E · ДЕРЖАТЬСЯ     SPACE · ОТПРЫГНУТЬ"));
        for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
            if(It->CanOrderJump(Hero) && (!GS->bSingleDayLoop || ExplicitFoodTransport)) Hint=Copy(TEXT("UI.HUD.Hint.ThroatJump"),LOCTEXT("HintThroatJump","SPACE · ПРЫГНУТЬ НА ЯЗЫЧОК"));
            if(It->ContainsPlayer(Hero)) {
                if(It->ThroatPhase==EMCThroatPhase::Anticipation) {
                    const double UntilSuction=FMath::Max(0.,It->PhaseStartedAt+It->AnticipationSeconds-Now);
                    Hint=GS->bSingleDayLoop && !ExplicitFoodTransport
                        ?Format(TEXT("UI.HUD.Hint.ThroatLeave"),LOCTEXT("HintThroatLeave","ЗАСАСЫВАНИЕ ЧЕРЕЗ {Seconds} С · ВЫЙДИ ИЗ ЗОНЫ"),{{TEXT("Seconds"),Decimal(UntilSuction)}})
                        :Format(TEXT("UI.HUD.Hint.ThroatDeliver"),LOCTEXT("HintThroatDeliver","ДОСТАВЛЯЙ ЕЩЁ · ЗАСАСЫВАНИЕ ЧЕРЕЗ {Seconds} С"),{{TEXT("Seconds"),Decimal(UntilSuction)}});
                }
                else if(It->ThroatPhase==EMCThroatPhase::Swallowing) Hint=GS->bSingleDayLoop && !ExplicitFoodTransport?Copy(TEXT("UI.HUD.Hint.ThroatHold"),LOCTEXT("HintThroatHold","ГЛОТКА ЗАСАСЫВАЕТ · ДЕРЖИСЬ")):Copy(TEXT("UI.HUD.Hint.ThroatAccepting"),LOCTEXT("HintThroatAccepting","ГЛОТКА ЗАСАСЫВАЕТ · СЛЕДУЮЩАЯ ПАРТИЯ ПРИНИМАЕТСЯ"));
                else if(It->ThroatPhase==EMCThroatPhase::Collecting && (!GS->bSingleDayLoop || ExplicitFoodTransport)) Hint=Copy(TEXT("UI.HUD.Hint.ThroatCollect"),LOCTEXT("HintThroatCollect","ВНЕСИ СТОПКУ В ЗОНУ · ЕДА ОТПРАВИТСЯ САМА"));
            }
        }
        if(Hero->Grip && Hero->Grip->IsBracing())
            Hint=Format(TEXT("UI.HUD.Hint.BraceLoad"),LOCTEXT("HintBraceLoad","ДЕРЖИ ПКМ · {Target} · НАГРУЗКА {Mass} КГ"),
                {{TEXT("Target"),Cast<AMCToothCharacter>(Hero->Grip->BraceTarget())?Copy(TEXT("UI.HUD.Hint.BracePlayer"),LOCTEXT("HintBracePlayer","ТЯНЕШЬ ИГРОКА")):Hero->Grip->IsWorldAnchored()?Copy(TEXT("UI.HUD.Hint.BraceWorld"),LOCTEXT("HintBraceWorld","ОПОРА")):Copy(TEXT("UI.HUD.Hint.BraceFood"),LOCTEXT("HintBraceFood","ДЕРЖИШЬСЯ ЗА ЕДУ"))},{TEXT("Mass"),Whole(Hero->Grip->TotalChainMass())}});
        else if(Hero->Grip && Hero->Grip->IncomingChainMass()>0)
            Hint=Format(TEXT("UI.HUD.Hint.IncomingLoad"),LOCTEXT("HintIncomingLoad","ТЕБЯ ДЕРЖАТ · ДОПОЛНИТЕЛЬНЫЙ ВЕС {Mass} КГ"),{{TEXT("Mass"),Whole(Hero->Grip->IncomingChainMass())}});
        if(const auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());PC && PC->IsSpectating())
        {
            const auto* Target=PC->GetSpectatorTarget();
            const auto* Player=Target?Target->GetPlayerState():nullptr;
            const FText Name=Player?FText::AsCultureInvariant(Player->GetPlayerName()):Copy(TEXT("UI.Spectator.WaitingForPlayer"),LOCTEXT("SpectatorWaiting","ожидание игрока"));
            const float Respawn=PC->GetRespawnSecondsRemaining();
            Hint=Respawn>0?Format(TEXT("UI.HUD.Hint.SpectatingRespawn"),LOCTEXT("HintSpectatingRespawn","НАБЛЮДАЕШЬ: {Name} · ЛКМ / → СЛЕДУЮЩИЙ · ПКМ / ← ПРЕДЫДУЩИЙ · ВОЗРОЖДЕНИЕ {Seconds} С"),{{TEXT("Name"),Name},{TEXT("Seconds"),Whole(FMath::CeilToFloat(Respawn))}})
                :Format(TEXT("UI.HUD.Hint.Spectating"),LOCTEXT("HintSpectating","НАБЛЮДАЕШЬ: {Name} · ЛКМ / → СЛЕДУЮЩИЙ · ПКМ / ← ПРЕДЫДУЩИЙ"),{{TEXT("Name"),Name}});
        }
        else if(!Hero->Status->IsAlive()) Hint=GS->AvailableArenaTeeth()>0?Format(TEXT("UI.HUD.Hint.Respawn"),LOCTEXT("HintRespawn","ВОЗРОЖДЕНИЕ ЧЕРЕЗ {Seconds} С"),{{TEXT("Seconds"),Whole(FMath::Max(0.,Hero->RespawnAt-Now))}}):Copy(TEXT("UI.HUD.Hint.NoReserve"),LOCTEXT("HintNoReserve","НЕТ ЗАПАСНЫХ ЗУБОВ"));
        float Contact=Hero->ContactProgress;
        if (const auto* Mimic=Hero->MimicCaptor.Get(); IsValid(Mimic))
        {
            Hint=Copy(TEXT("UI.HUD.Hint.MimicCaptured"),LOCTEXT("HintMimicCaptured","ТЕБЯ ПРОГЛОТИЛ МИМИК · ТОВАРИЩ ДОЛЖЕН УДЕРЖИВАТЬ E У СУНДУКА"));
            Contact=Mimic->RescueProgress();
        }
        else if (Hero->Status->IsAlive())
        {
            const AMCRewardChest* Nearest=nullptr;
            double Distance=DBL_MAX;
            for (TActorIterator<AMCRewardChest> It(GetWorld());It;++It) if (It->CanRescue(Hero))
            {
                const double Candidate=FVector::DistSquared(Hero->GetActorLocation(),It->GetActorLocation());
                if (Candidate<Distance) { Nearest=*It; Distance=Candidate; }
            }
            if (Nearest)
            {
                const auto* Captive=Nearest->GetCapturedPlayer();
                const auto* Player=Captive?Captive->GetPlayerState():nullptr;
                const FText Name=Player?FText::AsCultureInvariant(Player->GetPlayerName()):Copy(TEXT("UI.HUD.Hint.Teammate"),LOCTEXT("HintTeammate","товарища"));
                Hint=Format(TEXT("UI.HUD.Hint.MimicRescue"),LOCTEXT("HintMimicRescue","УДЕРЖИВАЙ E · ВЫТАЩИТЬ {Name} ИЗ МИМИКА"),{{TEXT("Name"),Name}});
                Contact=Nearest->RescueProgress();
            }
        }
        if(Hero->CanWork()) for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It)
        {
            if(!It->IsActive()) continue;
            if(It->Stage==EMCFogBrawlStage::Warning)
            {
                const bool Guarding=It->IsGuarding(Hero);
                FText Direction;
                if(auto* Nearby=It->NearestTarget(Hero,true))
                {
                    FVector ViewLocation; FRotator ViewRotation=Hero->GetControlRotation();
                    if(const auto* Owner=GetOwningPlayer()) Owner->GetPlayerViewPoint(ViewLocation,ViewRotation);
                    const FVector Offset=(Nearby->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D();
                    FVector Forward=ViewRotation.Vector().GetSafeNormal2D();
                    if(Forward.IsNearlyZero()) Forward=FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Z).GetSafeNormal2D();
                    const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
                    const float Side=FVector::DotProduct(Offset,Right),Ahead=FVector::DotProduct(Offset,Forward);
                    Direction=FMath::Abs(Side)>FMath::Abs(Ahead)?Side>0?Copy(TEXT("UI.HUD.Direction.Right"),LOCTEXT("DirectionRight","СПРАВА")):Copy(TEXT("UI.HUD.Direction.Left"),LOCTEXT("DirectionLeft","СЛЕВА"))
                        :Ahead>0?Copy(TEXT("UI.HUD.Direction.Forward"),LOCTEXT("DirectionForward","ВПЕРЕДИ")):Copy(TEXT("UI.HUD.Direction.Back"),LOCTEXT("DirectionBack","СЗАДИ"));
                }
                const FText Side=It->CurrentSide==EMCFogBrawlSide::Right?Copy(TEXT("UI.HUD.Direction.Right"),LOCTEXT("DirectionRight","СПРАВА")):Copy(TEXT("UI.HUD.Direction.Left"),LOCTEXT("DirectionLeft","СЛЕВА"));
                const FText Action=Guarding?Copy(TEXT("UI.HUD.Hint.FogGuard"),LOCTEXT("HintFogGuard","ДЕРЖИ ПКМ")):Format(TEXT("UI.HUD.Hint.FogReach"),LOCTEXT("HintFogReach","К ЗУБУ {Direction} · ДЕРЖИ ПКМ"),{{TEXT("Direction"),Direction}});
                Hint=Format(TEXT("UI.HUD.Hint.FogStrike"),LOCTEXT("HintFogStrike","УДАР {Current}/{Total} {Side} · ЦЕЛЕЙ {Targets} · {Action} · {Seconds} С"),
                    {{TEXT("Current"),FMath::Min(It->StrikesResolved+1,It->TotalStrikes)},{TEXT("Total"),FMath::Max(1,It->TotalStrikes)},{TEXT("Side"),Side},{TEXT("Targets"),It->CurrentTargetCount()},
                    {TEXT("Action"),Action},{TEXT("Seconds"),Decimal(It->WarningRemaining())}});
                if(It->IsFinalWarning()) Hint=Format(TEXT("UI.HUD.Hint.FogFinalWarning"),LOCTEXT("HintFogFinalWarning","ПОСЛЕДНЕЕ ПРЕДУПРЕЖДЕНИЕ · {Message}"),{{TEXT("Message"),Hint}});
                else if(It->CanMarkTarget(Hero)) Hint=Format(TEXT("UI.HUD.Hint.FogCanMark"),LOCTEXT("HintFogCanMark","{Message} · E — МАЯК КОМАНДЕ"),{{TEXT("Message"),Hint}});
                else if(It->bTargetMarked) Hint=Format(TEXT("UI.HUD.Hint.FogMarked"),LOCTEXT("HintFogMarked","{Message} · ЗУБ ОТМЕЧЕН МАЯКОМ"),{{TEXT("Message"),Hint}});
            }
            else if(It->Stage==EMCFogBrawlStage::Selection)
                Hint=Format(TEXT("UI.HUD.Hint.FogSelection"),LOCTEXT("HintFogSelection","МИГАЮТ ВСЕ ДОСТУПНЫЕ ЗУБЫ · ВЫБОР УСКОРЯЕТСЯ · {Seconds} С"),{{TEXT("Seconds"),Decimal(FMath::Max(0.,It->StageEndsAt-Now))}});
            else if(It->Stage==EMCFogBrawlStage::Recovery)
                Hint=It->StrikesResolved>=It->TotalStrikes?Copy(TEXT("UI.HUD.Hint.FogComplete"),LOCTEXT("HintFogComplete","ВСЕ УДАРЫ ПЕРЕЖИТЫ"))
                    :It->LastImpactAt<0?Copy(TEXT("UI.HUD.Hint.FogWaiting"),LOCTEXT("HintFogWaiting","СОБЕРИТЕСЬ · СОБЫТИЕ ЖДЁТ ГОТОВНОСТИ КОМАНДЫ"))
                    :It->CurrentTargetCount()>0?Copy(TEXT("UI.HUD.Hint.FogNextSide"),LOCTEXT("HintFogNextSide","СПРАВА УДАР ЗАВЕРШЁН · ПРИГОТОВЬТЕСЬ К УДАРУ СЛЕВА"))
                    :Copy(TEXT("UI.HUD.Hint.FogNextSeries"),LOCTEXT("HintFogNextSeries","СЕРИЯ ЗАВЕРШЕНА · СЕЙЧАС НОВЫЙ ВЫБОР ЗУБОВ"));
            else Hint=Copy(TEXT("UI.HUD.Hint.FogGathering"),LOCTEXT("HintFogGathering","ТУМАН СГУЩАЕТСЯ · СКОРО НАЧНЁТСЯ ПЕРЕБОР ВСЕХ ЗУБОВ"));
            break;
        }
        if(Hero->HasFrozenLegs() && Hero->CanWork()) {
            Hint=Inv->Selected==EMCToolSlot::Pickaxe
                ?Copy(TEXT("UI.HUD.Hint.FrozenLegsPickaxe"),LOCTEXT("HintFrozenLegsPickaxe","НОГИ ВО ЛЬДУ · УДЕРЖИВАЙ ЛКМ КИРКОЙ"))
                :Copy(TEXT("UI.HUD.Hint.FrozenLegsSwitch"),LOCTEXT("HintFrozenLegsSwitch","НОГИ ВО ЛЬДУ · 2 — КИРКА · УДЕРЖИВАЙ ЛКМ"));
            double PrisonImpactAt=MAX_dbl;
            for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
                for(const auto& Strike:It->Strikes)
                    if(Strike.bPrisonPunishment && Strike.Prisoner==Hero && !Strike.bImpacted && !Strike.bCancelled)
                        PrisonImpactAt=FMath::Min(PrisonImpactAt,Strike.ImpactAt);
            if(PrisonImpactAt<MAX_dbl)
                Hint=Format(TEXT("UI.HUD.Hint.PrisonIcicles"),LOCTEXT("HintPrisonIcicles","{Message} · СОСУЛЬКИ ЧЕРЕЗ {Seconds} С"),{{TEXT("Message"),Hint},{TEXT("Seconds"),Decimal(FMath::Max(0.,PrisonImpactAt-Now))}});
        }
        if(Cataclysm && Hero->CanWork()) {
            Hint=Cataclysm->Phase==EMCCataclysmPhase::Cleanup?Copy(TEXT("UI.HUD.Hint.CataclysmCleanup"),LOCTEXT("HintCataclysmCleanup","ЩЁТКА · УБОРКА · C + ЛКМ — САМООЧИСТКА"))
                :Cataclysm->Phase==EMCCataclysmPhase::JellyFight?Copy(TEXT("UI.HUD.Hint.CataclysmJellies"),LOCTEXT("HintCataclysmJellies","КИРКА / НОЖ · ПОБЕДИ ЖЕЛЕ · ЩЁТКА — УБОРКА"))
                :Cataclysm->Phase==EMCCataclysmPhase::Complete?Copy(TEXT("UI.HUD.Hint.CataclysmComplete"),LOCTEXT("HintCataclysmComplete","КАТАКЛИЗМ ПРЕОДОЛЁН · ВСЁ ЧИСТО"))
                :Cataclysm->Phase==EMCCataclysmPhase::Flooding || Cataclysm->Phase==EMCCataclysmPhase::BranchWarning
                    ?Copy(TEXT("UI.HUD.Hint.CataclysmRafts"),LOCTEXT("HintCataclysmRafts","ПКМ У ПЛОТА · ЗАЛЕЗТЬ · ПО ДВОЕ"))
                    :Copy(TEXT("UI.HUD.Hint.CataclysmWave"),LOCTEXT("HintCataclysmWave","ВОЛНА · ДЕРЖИ ПКМ У ОПОРЫ"));
            const auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement());
            const AMCCataclysmRaft* Nearby=nullptr;
            double Best=FMath::Square(280.);
            for(const auto& Raft:Cataclysm->Rafts) if(IsValid(Raft)) {
                const bool OnDeck=Raft->IsRider(Hero);
                const double Distance=OnDeck?-1.:FVector::DistSquared2D(Hero->GetActorLocation(),Raft->GetActorLocation());
                if(Distance<Best) {Best=Distance;Nearby=Raft;}
            }
            if(Nearby) {
                const FText RaftState=Nearby->IsSubmerging()?Copy(TEXT("UI.HUD.Hint.RaftSubmerged"),LOCTEXT("HintRaftSubmerged","ПОД ВОДОЙ · СКОРО ВСПЛЫВЁТ")):Nearby->IsOverloaded()?Copy(TEXT("UI.HUD.Hint.RaftOverloaded"),LOCTEXT("HintRaftOverloaded","БОЛЬШОЙ ВЕС · СОЙДИ!"))
                    :Hero->IsSeatedOnRaft() && Hero->SeatedRaft==Nearby
                        ?Copy(TEXT("UI.HUD.Hint.RaftSeatedControls"),LOCTEXT("HintRaftSeatedControls","WASD — ГРЕСТИ ПО ПЛОТУ · ПРОБЕЛ — СПРЫГНУТЬ · УКЛОНЯЙТЕСЬ ОТ МУСОРА"))
                    :Nearby->IsRider(Hero)
                        ?Copy(TEXT("UI.HUD.Hint.RaftTakeSeat"),LOCTEXT("HintRaftTakeSeat","ПКМ — ЗАНЯТЬ МЕСТО · ДВА ГРЕБЦА СИЛЬНЕЕ ОДНОГО"))
                    :Nearby->IsBoardable()?Copy(TEXT("UI.HUD.Hint.RaftBoard"),LOCTEXT("HintRaftBoard","ДЕРЖИ ПКМ У КРАЯ · ЗАЛЕЗТЬ")):Copy(TEXT("UI.HUD.Hint.RaftOccupied"),LOCTEXT("HintRaftOccupied","ЗАНЯТ · ИЩИ ВТОРОЙ ПЛОТ"));
                Hint=Format(TEXT("UI.HUD.Hint.RaftStatus"),LOCTEXT("HintRaftStatus","ПЛОТ {Current}/{Maximum} · {State}"),{{TEXT("Current"),Nearby->OccupantCount},{TEXT("Maximum"),2},{TEXT("State"),RaftState}});
            }
            if(Move && Move->IsDrowning()) Hint=Format(TEXT("UI.HUD.Hint.Drowning"),LOCTEXT("HintDrowning","ТОНЕШЬ · {Seconds} С · НА ПЛОТ ИЛИ СУШУ!"),{{TEXT("Seconds"),Decimal(Move->GetDrowningSecondsRemaining())}});
        }
        SetCopy(TEXT("ActionHint"),InputController && InputController->IsUsingGamepad()?FText::AsCultureInvariant(MCGamepadHints::Adapt(Hint.ToString())):Hint); Show(TEXT("ContactProgress"),Contact>0); Bar(TEXT("ContactProgress"),Contact);
    }
    if(auto* Results=Find(TEXT("ResultsPanel")))
        Results->SetRenderTranslation(FVector2D(Directed && HUD::DirectorDebug.GetValueOnGameThread()>0?-186.f:0.f,0));
    Show(TEXT("ResultsPanel"),Finished); Text(TEXT("ResultTitle"),Title);
    SetCopy(TEXT("ResultDetail"),UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.HUD.RunResultDetail"),LOCTEXT("RunResultDetail","Незавершённых событий: {Failed} · R — новый забег")),FFormatNamedArguments{{TEXT("Failed"),FText::AsNumber(GS->FailedEvents)}}));
    // Keep the actual equipped tools and controls during the lesson; its own
    // fairy card supplies the objective instead of the ordinary day overview.
    const bool Overview=!GS->bTutorialActive;
    Show(TEXT("ObjectivesPanel"),Overview);
    Show(TEXT("TimerPanel"),Overview);
    Show(TEXT("PlayersPanel"),Overview);
    if (!Overview) {
        Show(TEXT("EventsPanel"),false);
        Show(TEXT("ResultsPanel"),false);
        if (DirectorPanel) DirectorPanel->SetVisibility(ESlateVisibility::Collapsed);
        if (DirectorCandidatesPanel) DirectorCandidatesPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
}


#undef LOCTEXT_NAMESPACE
