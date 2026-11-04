#include "MCUnifiedHUDExtrasWidget.h"
#include "MCUnifiedReticleWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUniversalUIWidget.h"
#include "MCUIDataLibrary.h"
#include "MCPerkComponent.h"
#include "GameFramework/Pawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "MCUnifiedHUDExtras"

namespace MCUnifiedHUDExtrasPrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    const TCHAR* Groups[]={TEXT("ExtrasResults"),TEXT("ExtrasSwim"),TEXT("ExtrasBooster"),TEXT("ExtrasPassives"),TEXT("ExtrasCataclysm"),TEXT("ExtrasStatusPreview")};
    FName Bounds(FName Name) {return FName(*(Name.ToString()+TEXT("Bounds")));}
    bool SameTexts(const TArray<FText>& A,const TArray<FText>& B)
    {
        if(A.Num()!=B.Num()) return false;
        for(int32 I=0;I<A.Num();++I) if(!A[I].EqualTo(B[I])) return false;
        return true;
    }
}
UMCUnifiedHUDExtrasWidget::UMCUnifiedHUDExtrasWidget()
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
}
UMCUniversalUIWidget* UMCUnifiedHUDExtrasWidget::Shape(FName Name,bool bBlue)
{
    auto* View=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    View->Label=FText::GetEmpty();View->LabelKey=NAME_None;
    View->bUseNativeLayout=false;View->bUseThemeShape=false;View->bCustomColors=true;
    View->CustomFill=MCUnifiedHUDExtrasPrivate::Color(bBlue?TEXT("4C76AE"):TEXT("374F56"));
    View->CustomOutline=MCUnifiedHUDExtrasPrivate::Color(TEXT("FFF9F8"));View->CustomShadow=MCUnifiedHUDExtrasPrivate::Color(TEXT("C7C7C7"));
    View->CornerRadius=18;View->OutlineWidth=3;View->ShadowOffset=FVector2D(8,9);View->ContentPadding=FMargin(16,12);
    View->SetVisibility(ESlateVisibility::HitTestInvisible);View->Initialize();View->BuildPresentationTree();View->ApplyTheme();return View;
}
UTextBlock* UMCUnifiedHUDExtrasWidget::Text(FName Name,int32 Size)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetAutoWrapText(true);Label->SetWrapTextAt(600);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Label->SetColorAndOpacity(MCUnifiedHUDExtrasPrivate::Color(TEXT("FFF9F8")));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(auto* T=GetResolvedTheme()) Label->SetFont(T->MakeFont(Size,true));return Label;
}
UVerticalBox* UMCUnifiedHUDExtrasWidget::AddGroup(FName Name,FVector2D Anchor,FVector2D Position,FVector2D Alignment,bool bBlue)
{
    auto* Canvas=Cast<UCanvasPanel>(WidgetTree->RootWidget);if(!Canvas) return nullptr;
    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),MCUnifiedHUDExtrasPrivate::Bounds(Name));Width->SetWidthOverride(400);
    auto* Frame=Shape(Name,bBlue);Width->SetContent(Frame);
    auto* CanvasCell=Canvas->AddChildToCanvas(Width);CanvasCell->SetAnchors(FAnchors(Anchor.X,Anchor.Y));CanvasCell->SetAlignment(Alignment);CanvasCell->SetPosition(Position);CanvasCell->SetAutoSize(true);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(*(Name.ToString()+TEXT("Content"))));
    Frame->SetContentForSlot(TEXT("Content"),Column);Width->SetVisibility(ESlateVisibility::Collapsed);return Column;
}
void UMCUnifiedHUDExtrasWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {ClearShapeLabels();EnsureSupplementalFlow();EnsureReticleView();return;} // Retain saved nodes and shape options.
    WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HUDExtrasRoot"));
    auto Add=[&](UVerticalBox* Column,FName Name,int32 Size) {
        auto* Label=Text(Name,Size);Column->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,0,0,6));return Label;
    };
    auto* Results=AddGroup(TEXT("ExtrasResults"),FVector2D(.5,.46),FVector2D::ZeroVector,FVector2D(.5,.5),true);
    Add(Results,TEXT("ExtrasResultTitle"),30)->SetJustification(ETextJustify::Center);
    Add(Results,TEXT("ExtrasResultDetail"),21)->SetJustification(ETextJustify::Center);
    auto* Swim=AddGroup(TEXT("ExtrasSwim"),FVector2D(.5,1),FVector2D(0,-276),FVector2D(.5,1));
    Add(Swim,TEXT("ExtrasSwimCaption"),23)->SetJustification(ETextJustify::Center);
    Add(Swim,TEXT("ExtrasSwimValue"),20)->SetJustification(ETextJustify::Center);
    auto* Track=Shape(TEXT("ExtrasSwimProgress"));Track->Kind=EMCUniversalUIKind::Progress;Track->ContentPadding=FMargin(0);Track->ShadowOffset=FVector2D::ZeroVector;
    Track->OutlineWidth=2;Track->CornerRadius=8;Track->ProgressFill=EMCUIColorToken::Mint;Track->ApplyTheme();
    auto* TrackHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ExtrasSwimProgressHeight"));TrackHeight->SetHeightOverride(14);TrackHeight->SetContent(Track);Swim->AddChildToVerticalBox(TrackHeight);
    auto* Booster=AddGroup(TEXT("ExtrasBooster"),FVector2D(0,1),FVector2D(38,-285),FVector2D(0,1));
    Add(Booster,TEXT("ExtrasBoosterName"),22);Add(Booster,TEXT("ExtrasBoosterCount"),18);Add(Booster,TEXT("ExtrasBoosterKey"),20);Add(Booster,TEXT("ExtrasBoosterDetail"),18);
    auto* Cataclysm=AddGroup(TEXT("ExtrasCataclysm"),FVector2D(0,.27),FVector2D(38,0),FVector2D::ZeroVector);
    Add(Cataclysm,TEXT("ExtrasCataclysmInstruction"),21);
    auto* Passives=AddGroup(TEXT("ExtrasPassives"),FVector2D(1,1),FVector2D(-50,-195),FVector2D(1,1),true);
    auto* Height=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ExtrasPassiveHeight"));Height->SetMaxDesiredHeight(MaximumPassiveHeight);Passives->AddChildToVerticalBox(Height);
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ExtrasPassiveScroll"));Scroll->SetAllowOverscroll(false);Height->SetContent(Scroll);
    auto* Chips=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("ExtrasPassiveChips"));Chips->SetExplicitWrapSize(true);Chips->SetWrapSize(400);Chips->SetInnerSlotPadding(FVector2D(8,8));Scroll->AddChild(Chips);
    // Standalone actor-free status fixtures only. Runtime uses the primary V2ActionHint once.
    auto* Status=AddGroup(TEXT("ExtrasStatusPreview"),FVector2D(.5,1),FVector2D(0,-190),FVector2D(.5,1));
    Add(Status,TEXT("ExtrasStatusPreviewText"),21)->SetJustification(ETextJustify::Center);
    ClearShapeLabels();EnsureSupplementalFlow();EnsureReticleView();UpdateLayout();
}
UMCUnifiedReticleWidget* UMCUnifiedHUDExtrasWidget::EnsureReticleView()
{
    if(!WidgetTree) return nullptr;
    if(auto* Existing=WidgetTree->FindWidget<UMCUnifiedReticleWidget>(TEXT("UnifiedReticle"))) return Existing;
    if(WidgetTree->FindWidget(TEXT("UnifiedReticle"))) return nullptr;
    auto* Canvas=Cast<UCanvasPanel>(WidgetTree->RootWidget);if(!Canvas) return nullptr;
    auto* Class=UMCGameplayUIFactory::ResolveClassForTheme(TEXT("UnifiedReticle"),UMCUnifiedReticleWidget::StaticClass(),GetResolvedTheme());
    auto* View=WidgetTree->ConstructWidget<UMCUnifiedReticleWidget>(Class,TEXT("UnifiedReticle"));
    if(!View->Theme) View->Theme=GetResolvedTheme();View->Initialize();View->BuildPresentationTree();
    auto* Cell=Canvas->AddChildToCanvas(View);Cell->SetAnchors(FAnchors(0,0,1,1));Cell->SetOffsets(FMargin(0));Cell->SetZOrder(100);
    View->SetVisibility(ESlateVisibility::HitTestInvisible);View->SetBackend(Backend.Get());return View;
}
void UMCUnifiedHUDExtrasWidget::ClearShapeLabels()
{
    if(!WidgetTree) return;
    WidgetTree->ForEachWidget([](UWidget* Widget) {
        if(auto* ShapeView=Cast<UMCUniversalUIWidget>(Widget);ShapeView && ShapeView->GetName().StartsWith(TEXT("Extras"))) {
            if(ShapeView->LabelKey.IsNone() && (ShapeView->Label.ToString()==TEXT("Панель") || ShapeView->Label.ToString()==TEXT("Panel"))) {
                ShapeView->Label=FText::GetEmpty();ShapeView->LabelKey=NAME_None;ShapeView->ApplyTheme();
            }
        }
    });
}
void UMCUnifiedHUDExtrasWidget::EnsureSupplementalFlow()
{
    if(!WidgetTree) return;
    auto* Canvas=WidgetTree->FindWidget<UCanvasPanel>(TEXT("HUDExtrasRoot"));if(!Canvas) return;
    auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasFlowBounds"));
    auto* Rows=WidgetTree->FindWidget<UWrapBox>(TEXT("ExtrasFlowRows"));
    if(!Bounds) {
        Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ExtrasFlowBounds"));
        auto* CanvasCell=Canvas->AddChildToCanvas(Bounds);CanvasCell->SetAnchors(FAnchors(0,1));CanvasCell->SetAlignment(FVector2D(0,1));CanvasCell->SetPosition(FVector2D(38,-SupplementalBottomInset));CanvasCell->SetAutoSize(true);
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ExtrasFlowScroll"));
        Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness(FVector2D(6,6));Bounds->SetContent(Scroll);
        Rows=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("ExtrasFlowRows"));Rows->SetExplicitWrapSize(true);Rows->SetInnerSlotPadding(FVector2D(16,16));Rows->SetHorizontalAlignment(HAlign_Left);Scroll->AddChild(Rows);
    }
    if(!Rows) return;
    // Move existing objects, preserving their identities and all authored inner shapes.
    for(const TCHAR* Name:{TEXT("ExtrasCataclysm"),TEXT("ExtrasSwim"),TEXT("ExtrasPassives"),TEXT("ExtrasBooster")})
        if(auto* Group=WidgetTree->FindWidget<USizeBox>(MCUnifiedHUDExtrasPrivate::Bounds(FName(Name))))
            if(Group->GetParent()!=Rows) {Group->RemoveFromParent();Rows->AddChildToWrapBox(Group)->SetVerticalAlignment(VAlign_Top);}
}
void UMCUnifiedHUDExtrasWidget::UpdateLayout()
{
    if(!WidgetTree || bUpdatingLayout) return;TGuardValue<bool> Guard(bUpdatingLayout,true);
    EnsureSupplementalFlow();
    FVector2D Area=GetPaintSpaceGeometry().GetLocalSize();if(Area.X<1) Area=FVector2D(1920,1080);
    FString Signature=FString::Printf(TEXT("%.1f|%.1f|%.1f|%.1f|%.1f|%.1f|%.1f"),Area.X,Area.Y,MaximumCenterWidth,MaximumSideWidth,MaximumCriticalWidth,MaximumPassiveHeight,SupplementalBottomInset);
    WidgetTree->ForEachWidget([&](UWidget* Widget) {
        if(auto* Label=Cast<UTextBlock>(Widget)) Signature+=FString::Printf(TEXT("|%s:%s:%.1f"),*Label->GetName(),*Label->GetText().ToString(),Label->GetFont().Size);
    });
    for(const TCHAR* Name:MCUnifiedHUDExtrasPrivate::Groups) if(auto* Group=WidgetTree->FindWidget(MCUnifiedHUDExtrasPrivate::Bounds(FName(Name)))) Signature+=FString::FromInt(int32(Group->GetVisibility()));
    if(Signature==LastLayoutSignature) return;LastLayoutSignature=Signature;LastArea=Area;
    const float AvailableWidth=FMath::Max(240.f,float(Area.X)-76.f),WrapWidth=FMath::Max(220.f,AvailableWidth-16.f);
    const float Center=FMath::Max(240.f,FMath::Min(MaximumCenterWidth,WrapWidth));
    const float Side=FMath::Max(180.f,FMath::Min(MaximumSideWidth,float(Area.X)*.29f));
    const float Swim=FMath::Min(Center,FMath::Max(280.f,float(Area.X)*.42f));
    float Critical=Side;
    if(auto* Label=WidgetTree->FindWidget<UTextBlock>(TEXT("ExtrasCataclysmInstruction"));Label && !Label->GetText().IsEmpty() && FSlateApplication::IsInitialized())
        if(const auto Renderer=FSlateApplication::Get().GetRenderer()) {
            const float Unwrapped=Renderer->GetFontMeasureService()->Measure(Label->GetText(),Label->GetFont()).X;
            // Real warnings target two readable lines before anything is allowed to overflow.
            Critical=FMath::Clamp(Unwrapped*.5f+32.f,Side,FMath::Max(Side,FMath::Min(MaximumCriticalWidth,WrapWidth)));
        }
    for(const TCHAR* Name:MCUnifiedHUDExtrasPrivate::Groups) {
        const FName Key(Name);const bool bSide=Key==TEXT("ExtrasBooster") || Key==TEXT("ExtrasPassives");
        const float Width=Key==TEXT("ExtrasCataclysm")?Critical:bSide?Side:Key==TEXT("ExtrasSwim")?Swim:Center;
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(MCUnifiedHUDExtrasPrivate::Bounds(Key))) Bounds->SetWidthOverride(Width);
        if(auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(Key)) {
            const float Inner=FMath::Max(80.f,Width-Frame->ContentPadding.Left-Frame->ContentPadding.Right);
            const FString Prefix=Key.ToString();
            WidgetTree->ForEachWidget([&](UWidget* Widget) {
                if(auto* Label=Cast<UTextBlock>(Widget);Label && Label->GetName().StartsWith(Prefix)) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(Inner);}
            });
            if(Key==TEXT("ExtrasResults")) for(const TCHAR* LabelName:{TEXT("ExtrasResultTitle"),TEXT("ExtrasResultDetail")})
                if(auto* Label=WidgetTree->FindWidget<UTextBlock>(LabelName)) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(Inner);}
            if(Key==TEXT("ExtrasPassives")) if(auto* Chips=WidgetTree->FindWidget<UWrapBox>(TEXT("ExtrasPassiveChips"))) {
                Chips->SetWrapSize(Inner);
                for(UWidget* Child:Chips->GetAllChildren()) if(auto* Size=Cast<USizeBox>(Child)) Size->SetMaxDesiredWidth(Inner);
                for(const auto& Label:PassiveLabels) if(Label) {Label->SetAutoWrapText(false);Label->SetWrapTextAt(FMath::Max(60.f,Inner-18.f));}
            }
        }
    }
    if(auto* Height=WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasPassiveHeight"))) Height->SetMaxDesiredHeight(MaximumPassiveHeight);
    auto* Flow=WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasFlowBounds"));auto* Rows=WidgetTree->FindWidget<UWrapBox>(TEXT("ExtrasFlowRows"));auto* Scroll=WidgetTree->FindWidget<UScrollBox>(TEXT("ExtrasFlowScroll"));
    if(!Flow || !Rows || !Scroll) return;
    bool Any=false;for(UWidget* Child:Rows->GetAllChildren()) Any|=Child->GetVisibility()!=ESlateVisibility::Collapsed && Child->GetVisibility()!=ESlateVisibility::Hidden;
    Flow->SetVisibility(Any?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Any) {
        bLastCriticalVisible=false;
        if(auto* ResultBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasResultsBounds")))
            if(auto* CanvasCell=Cast<UCanvasPanelSlot>(ResultBounds->Slot)) CanvasCell->SetPosition(FVector2D::ZeroVector);
        return;
    }
    const float Bottom=FMath::Min(SupplementalBottomInset,float(Area.Y)*.40f);
    // Grow upward before using the Designer stress fallback. Critical gameplay copy does not depend on mouse scrolling.
    const float AvailableHeight=FMath::Max(100.f,float(Area.Y)-Bottom-24.f);
    auto* CriticalLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("ExtrasCataclysmInstruction"));
    auto* CriticalBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasCataclysmBounds"));
    const bool CriticalVisible=CriticalBounds && CriticalBounds->GetVisibility()!=ESlateVisibility::Collapsed;
    if(CriticalVisible && CriticalLabel && (!bLastCriticalVisible || !LastCriticalCopy.EqualTo(CriticalLabel->GetText()))) Scroll->ScrollToStart();
    bLastCriticalVisible=CriticalVisible;LastCriticalCopy=CriticalLabel?CriticalLabel->GetText():FText::GetEmpty();
    Flow->SetWidthOverride(AvailableWidth);Rows->SetWrapSize(WrapWidth);
    if(auto* CanvasCell=Cast<UCanvasPanelSlot>(Flow->Slot)) CanvasCell->SetPosition(FVector2D(38,-Bottom));
    // Explicit wrap widths avoid the previous allotted width feeding back into text measurement.
    const auto SlateRows=Rows->TakeWidget();SlateRows->MarkPrepassAsDirty();SlateRows->SlatePrepass(FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale()));
    const float RequiredHeight=FMath::CeilToFloat(Rows->GetDesiredSize().Y)+10.f;
    const bool Overflow=RequiredHeight>AvailableHeight+.5f;
    Scroll->SetScrollBarVisibility(Overflow?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Scroll->SetAlwaysShowScrollbar(Overflow);
    Flow->SetHeightOverride(FMath::Min(RequiredHeight,AvailableHeight));Flow->SetMaxDesiredHeight(AvailableHeight);
    // Results can coexist with a remaining swim gauge; reserve a gap instead of painting over it.
    if(auto* ResultBounds=WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasResultsBounds"));ResultBounds && ResultBounds->GetVisibility()!=ESlateVisibility::Collapsed)
        if(auto* CanvasCell=Cast<UCanvasPanelSlot>(ResultBounds->Slot)) {
            const auto SlateResult=ResultBounds->TakeWidget();SlateResult->MarkPrepassAsDirty();SlateResult->SlatePrepass();
            const float Half=float(ResultBounds->GetDesiredSize().Y)*.5f;
            const float Target=FMath::Max(Half+20.f,FMath::Min(float(Area.Y)*.46f,float(Area.Y)-Bottom-Flow->GetHeightOverride()-16.f-Half));
            CanvasCell->SetPosition(FVector2D(0,Target-float(Area.Y)*.46f));
        }
}
void UMCUnifiedHUDExtrasWidget::PreparePresentation()
{
    if(bPrepared || bPreparing) return;TGuardValue<bool> Guard(bPreparing,true);BuildPresentationTree();
    bool Ready=WidgetTree && GetResolvedTheme() && FSlateApplication::IsInitialized();
    if(!Ready) return;
    for(const TCHAR* Name:MCUnifiedHUDExtrasPrivate::Groups) Ready&=WidgetTree->FindWidget<USizeBox>(MCUnifiedHUDExtrasPrivate::Bounds(FName(Name)))!=nullptr;
    const TCHAR* Required[]={TEXT("ExtrasResultTitle"),TEXT("ExtrasResultDetail"),TEXT("ExtrasSwimCaption"),TEXT("ExtrasSwimValue"),TEXT("ExtrasBoosterName"),TEXT("ExtrasBoosterCount"),TEXT("ExtrasBoosterKey"),TEXT("ExtrasBoosterDetail"),TEXT("ExtrasCataclysmInstruction"),TEXT("ExtrasStatusPreviewText")};
    const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    const auto Measure=Renderer->GetFontMeasureService();
    // Known supplemental states prepare their localized glyphs while the loading cover is up.
    TArray<FText> Warm;
    const FName Keys[]={TEXT("UI.Swim.Title"),TEXT("UI.Swim.Low"),TEXT("UI.Swim.Recovery"),TEXT("UI.Swim.Drowning"),
        TEXT("UI.Swim.TimeToDeath"),TEXT("UI.Stamina.Value"),TEXT("UI.HUD.RunLost"),TEXT("UI.HUD.RunComplete"),
        TEXT("UI.HUD.RunResultDetail"),TEXT("UI.Booster.GenericName"),TEXT("UI.Booster.Charges"),
        TEXT("UI.Booster.FuryActive"),TEXT("UI.Booster.GrenadeArmed"),TEXT("UI.Event.Cataclysm.Rafts.Instruction")};
    UDataTable* CopyTable=Backend.IsValid()?Backend->GetResolvedTranslationTable():GetResolvedTranslationTable();
    const FString CopyCulture=IsDesignTime()?PreviewCulture:FString();
    for(FName Key:Keys) Warm.Add(UMCUIDataLibrary::ResolveText(CopyTable,Key,FText::GetEmpty(),CopyCulture));
    if(auto* Source=Backend.Get()) {
        const auto Snapshot=Source->GetUnifiedExtrasSnapshot();
        for(const FText& Copy:{Snapshot.StatusHint,Snapshot.ResultTitle,Snapshot.ResultDetail,Snapshot.SwimCaption,Snapshot.SwimValue,
            Snapshot.BoosterName,Snapshot.BoosterCount,Snapshot.BoosterKey,Snapshot.BoosterFury,Snapshot.BoosterArmed,Snapshot.BoosterDetail,Snapshot.CataclysmInstruction}) Warm.Add(Copy);
        Warm.Append(Snapshot.PassiveNames);
        // Only the source owner's already-loaded canonical catalog is read. No
        // catalog load, offer eligibility, RNG or private list is introduced.
        const auto* Perks=UMCPerkComponent::ForActor(Source->GetOwningPlayerPawn());
        const UDataTable* Catalog=Perks?Perks->GetPerkTable():nullptr;
        if(Catalog && Catalog->GetRowStruct()==FMCPerkDefinition::StaticStruct())
            for(const auto& Entry:Catalog->GetRowMap()) {
                const auto* Definition=reinterpret_cast<const FMCPerkDefinition*>(Entry.Value);
                if(Definition && UMCPerkComponent::ResolvePerkKind(*Definition,Entry.Key)==EMCPerkKind::Passive)
                    Warm.Add(UMCUIDataLibrary::ResolveText(CopyTable,Definition->DisplayNameKey,Definition->DisplayName,CopyCulture));
            }
    }
    const FText WarmCopy=FText::Join(FText::AsCultureInvariant(TEXT(" ")),Warm);
    const float FontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    for(const TCHAR* Name:Required) {
        auto* Label=WidgetTree->FindWidget<UTextBlock>(Name);Ready&=Label && Label->GetFont().HasValidFont();
        if(Label) {Measure->Measure(Label->GetText(),Label->GetFont(),FontScale);Measure->Measure(WarmCopy,Label->GetFont(),FontScale);Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789 / .,+")),Label->GetFont(),FontScale);}
    }
    // Passive rows do not exist yet in an empty loadout. Prime their real
    // fixed font now, before the first accepted reward adds a 17px chip.
    const FSlateFontInfo PassiveFont=GetResolvedTheme()->MakeFont(17,true);Ready&=PassiveFont.HasValidFont();
    Measure->Measure(WarmCopy,PassiveFont,FontScale);
    Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789 / .,+")),PassiveFont,FontScale);
    Ready&=WidgetTree->FindWidget<UWrapBox>(TEXT("ExtrasPassiveChips")) && WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ExtrasSwimProgress"));
    Ready&=WidgetTree->FindWidget<UWrapBox>(TEXT("ExtrasFlowRows")) && WidgetTree->FindWidget<USizeBox>(TEXT("ExtrasFlowBounds"));
    auto* Reticle=EnsureReticleView();
    if(Reticle) {Reticle->SetBackend(Backend.Get());Reticle->PreparePresentation();}
    Ready&=Reticle && Reticle->IsPresentationPrepared();
    bPrepared=Ready;UpdateLayout();
}
void UMCUnifiedHUDExtrasWidget::SetBackend(UMCGameplayHUD* Source)
{
    if(Backend.Get()!=Source) {if(auto* Previous=Backend.Get()) Previous->SetUnifiedExtrasActive(false);Backend=Source;bPrepared=false;}
    if(!IsDesignTime()) {PreparePresentation();RefreshFromBackend();}
}
void UMCUnifiedHUDExtrasWidget::HideGroups()
{
    if(WidgetTree) {
        for(const TCHAR* Name:MCUnifiedHUDExtrasPrivate::Groups)
            if(auto* Bounds=WidgetTree->FindWidget(MCUnifiedHUDExtrasPrivate::Bounds(FName(Name)))) Bounds->SetVisibility(ESlateVisibility::Collapsed);
        if(auto* Flow=WidgetTree->FindWidget(TEXT("ExtrasFlowBounds"))) Flow->SetVisibility(ESlateVisibility::Collapsed);
    }
}
void UMCUnifiedHUDExtrasWidget::UpdatePassives(const TArray<FText>& Names)
{
    if(MCUnifiedHUDExtrasPrivate::SameTexts(LastPassiveNames,Names)) return;
    auto* Chips=WidgetTree->FindWidget<UWrapBox>(TEXT("ExtrasPassiveChips"));if(!Chips) return;
    // Retain rows when copy changes; never replace same-named live UObjects during localization.
    if(PassiveRows.IsEmpty()) for(UWidget* Child:Chips->GetAllChildren()) if(auto* Row=Cast<USizeBox>(Child)) {
        const int32 Index=PassiveRows.Num();PassiveRows.Add(Row);
        PassiveLabels.Add(WidgetTree->FindWidget<UTextBlock>(FName(*FString::Printf(TEXT("ExtrasPassiveName%d"),Index))));
    }
    LastPassiveNames=Names;Chips->ClearChildren();
    for(int32 I=0;I<Names.Num();++I) {
        if(!PassiveRows.IsValidIndex(I)) {
            auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*FString::Printf(TEXT("ExtrasPassiveBounds%d"),I)));
            Bounds->SetMaxDesiredWidth(FMath::Max(120.f,MaximumSideWidth-32));
            auto* Chip=Shape(FName(*FString::Printf(TEXT("ExtrasPassiveChip%d"),I)));Chip->ContentPadding=FMargin(9,6);Chip->CornerRadius=12;Chip->OutlineWidth=2;Chip->ShadowOffset=FVector2D(3,4);Chip->ApplyTheme();Bounds->SetContent(Chip);
            auto* Label=Text(FName(*FString::Printf(TEXT("ExtrasPassiveName%d"),I)),17);Chip->SetContentForSlot(TEXT("Content"),Label);
            PassiveRows.Add(Bounds);PassiveLabels.Add(Label);
        }
        if(PassiveLabels.IsValidIndex(I) && PassiveLabels[I]) PassiveLabels[I]->SetText(Names[I]);
        Chips->AddChildToWrapBox(PassiveRows[I]);
    }
}
void UMCUnifiedHUDExtrasWidget::Present(const FMCGameplayHUDExtrasSnapshot& Snapshot)
{
    if(!bPrepared || !WidgetTree || !Snapshot.bSourceVisible) {HideGroups();return;}
    auto Set=[&](FName Name,const FText& Copy) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) {
        if(!Label->GetText().EqualTo(Copy)) Label->SetText(Copy);Label->SetVisibility(Copy.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }};
    auto Show=[&](FName Name,bool Visible) {if(auto* Bounds=WidgetTree->FindWidget(MCUnifiedHUDExtrasPrivate::Bounds(Name))) Bounds->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);};
    Show(TEXT("ExtrasResults"),Snapshot.bResultsVisible);Set(TEXT("ExtrasResultTitle"),Snapshot.ResultTitle);Set(TEXT("ExtrasResultDetail"),Snapshot.ResultDetail);
    Show(TEXT("ExtrasSwim"),Snapshot.bSwimVisible);Set(TEXT("ExtrasSwimCaption"),Snapshot.SwimCaption);Set(TEXT("ExtrasSwimValue"),Snapshot.SwimValue);
    if(auto* Track=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("ExtrasSwimProgress"))) {
        const auto Fill=Snapshot.bDrowning?EMCUIColorToken::Cost:EMCUIColorToken::Mint;
        if(Track->ProgressFill!=Fill) {Track->ProgressFill=Fill;Track->ApplyTheme();}Track->SetProgressValue(Snapshot.SwimFraction);
    }
    Show(TEXT("ExtrasCataclysm"),Snapshot.bCataclysmVisible);Set(TEXT("ExtrasCataclysmInstruction"),Snapshot.CataclysmInstruction);
    Show(TEXT("ExtrasBooster"),Snapshot.bBoosterVisible);Set(TEXT("ExtrasBoosterName"),Snapshot.BoosterName);Set(TEXT("ExtrasBoosterCount"),Snapshot.BoosterCount);Set(TEXT("ExtrasBoosterKey"),Snapshot.BoosterKey);
    TArray<FText> BoosterLines;for(const FText& Line:{Snapshot.BoosterFury,Snapshot.BoosterArmed,Snapshot.BoosterDetail}) if(!Line.IsEmpty()) BoosterLines.Add(Line);
    Set(TEXT("ExtrasBoosterDetail"),FText::Join(FText::AsCultureInvariant(TEXT("\n")),BoosterLines));
    UpdatePassives(Snapshot.PassiveNames);Show(TEXT("ExtrasPassives"),Snapshot.bPassivesVisible && !Snapshot.PassiveNames.IsEmpty());
    const bool PreviewStatus=IsDesignTime() && (Snapshot.bDead || Snapshot.bSpectating);
    Show(TEXT("ExtrasStatusPreview"),PreviewStatus);if(PreviewStatus) Set(TEXT("ExtrasStatusPreviewText"),Snapshot.StatusHint);
    UpdateLayout();
}
void UMCUnifiedHUDExtrasWidget::RefreshFromBackend()
{
    if(IsDesignTime()) return;auto* Source=Backend.Get();
    if(!Source || Source->GetWorld()!=GetWorld()) {HideGroups();return;}
    PreparePresentation();Present(Source->GetUnifiedExtrasSnapshot());
}
void UMCUnifiedHUDExtrasWidget::ApplyTheme()
{
    Super::ApplyTheme();if(WidgetTree) if(auto* T=GetResolvedTheme()) WidgetTree->ForEachWidget([&](UWidget* Widget) {
        if(auto* Label=Cast<UTextBlock>(Widget)) Label->SetFont(T->MakeFont(FMath::Max(8,FMath::RoundToInt(Label->GetFont().Size)),true));
    });
    LastLayoutSignature.Reset();bPrepared=false;PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();
}
void UMCUnifiedHUDExtrasWidget::NativeConstruct() {Super::NativeConstruct();PreparePresentation();RefreshFromBackend();}
void UMCUnifiedHUDExtrasWidget::NativeDestruct() {if(auto* Source=Backend.Get()) Source->SetUnifiedExtrasActive(false);if(WidgetTree) if(auto* Reticle=WidgetTree->FindWidget<UMCUnifiedReticleWidget>(TEXT("UnifiedReticle"))) Reticle->SetBackend(nullptr);bPrepared=false;Super::NativeDestruct();}
void UMCUnifiedHUDExtrasWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);if(!LastArea.Equals(Geometry.GetLocalSize(),.5f)) UpdateLayout();
    if(IsDesignTime()) return;RefreshElapsed+=DeltaSeconds;if(RefreshElapsed>=.1f) {RefreshElapsed=0;RefreshFromBackend();}
}
void UMCUnifiedHUDExtrasWidget::OnLocalizationChanged()
{
    LastPassiveNames.Reset();bPrepared=false;PreparePresentation();
    if(IsDesignTime()) PresentPreview(PreviewTime);else RefreshFromBackend();
}
void UMCUnifiedHUDExtrasWidget::PresentPreview(float Time)
{
    if(!IsDesignTime()) return;PreparePresentation();FMCGameplayHUDExtrasSnapshot Sample;Sample.bSourceVisible=true;
    auto Copy=[&](FName Key,const FText& Fallback) {return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);};
    auto Format=[&](FName Key,const FText& Fallback,const FFormatNamedArguments& Arguments) {return UMCUIDataLibrary::FormatTextForCulture(Copy(Key,Fallback),Arguments,PreviewCulture);};
    const bool Long=PreviewState==TEXT("LongText") || PreviewScenario==EMCGameplayUIPreviewScenario::LongText;
    const bool CataclysmCombo=PreviewState==TEXT("CataclysmSwim") || PreviewState==TEXT("CataclysmDrowning");
    Sample.bDead=PreviewState==TEXT("Death") || PreviewState==TEXT("Respawn") || PreviewState==TEXT("Spectator");Sample.bSpectating=PreviewState==TEXT("Spectator");Sample.RespawnSeconds=4;
    Sample.StatusHint=Sample.bSpectating?Format(TEXT("UI.HUD.Hint.SpectatingRespawn"),LOCTEXT("Spectating","НАБЛЮДАЕШЬ: {Name} · ЛКМ / → СЛЕДУЮЩИЙ · ПКМ / ← ПРЕДЫДУЩИЙ · ВОЗРОЖДЕНИЕ {Seconds} С"),{{TEXT("Name"),FText::AsCultureInvariant(TEXT("Player"))},{TEXT("Seconds"),4}}):
        PreviewState==TEXT("Respawn")?Format(TEXT("UI.HUD.Hint.Respawn"),LOCTEXT("Respawn","ВОЗРОЖДЕНИЕ ЧЕРЕЗ {Seconds} С"),{{TEXT("Seconds"),4}}):Copy(TEXT("UI.HUD.Hint.NoReserve"),LOCTEXT("NoReserve","НЕТ ЗАПАСНЫХ ЗУБОВ"));
    const bool Lost=PreviewState==TEXT("ResultsLose");Sample.bResultsVisible=Lost || PreviewState==TEXT("ResultsWin");
    Sample.ResultTitle=Copy(Lost?TEXT("UI.HUD.RunLost"):TEXT("UI.HUD.RunComplete"),Lost?LOCTEXT("Lost","РОТ НЕ СПАСЁН"):LOCTEXT("Win","ЗАБЕГ ЗАВЕРШЁН"));
    Sample.ResultDetail=Format(TEXT("UI.HUD.RunResultDetail"),LOCTEXT("Results","Незавершённых событий: {Failed} · R — новый забег"),{{TEXT("Failed"),Lost?3:0}});
    Sample.bDrowning=PreviewState==TEXT("Drowning") || PreviewState==TEXT("CataclysmDrowning");Sample.bSwimVisible=Sample.bDrowning || PreviewState==TEXT("Swimming") || PreviewState==TEXT("Default") || Long || CataclysmCombo;
    Sample.SwimCaption=Copy(Sample.bDrowning?TEXT("UI.Swim.Drowning"):TEXT("UI.Swim.Title"),Sample.bDrowning?LOCTEXT("Drowning","ТОНЕШЬ"):LOCTEXT("Swimming","ПЛАВАНИЕ"));
    Sample.SwimFraction=Sample.bDrowning?.35f:.72f;Sample.SwimValue=Sample.bDrowning?Format(TEXT("UI.Swim.TimeToDeath"),LOCTEXT("TimeToDeath","ДО ГИБЕЛИ {Seconds} С"),{{TEXT("Seconds"),UMCUIDataLibrary::DecimalForCulture(2.1,1,PreviewCulture)}}):
        Format(TEXT("UI.Stamina.Value"),LOCTEXT("StaminaValue","{Current} / {Maximum}"),{{TEXT("Current"),72},{TEXT("Maximum"),100}});
    Sample.bBoosterVisible=PreviewState==TEXT("Booster") || Long;Sample.BoosterName=Copy(TEXT("UI.Booster.GenericName"),LOCTEXT("Booster","Бустер"));Sample.BoosterKey=FText::AsCultureInvariant(TEXT("G"));
    Sample.BoosterCount=Format(TEXT("UI.Booster.Charges"),LOCTEXT("Charges","Зарядов: {Count}"),{{TEXT("Count"),3}});Sample.BoosterArmed=Copy(TEXT("UI.Booster.GrenadeArmed"),LOCTEXT("Armed","Граната взорвётся при падении"));
    Sample.bPassivesVisible=PreviewState==TEXT("Passive") || Long || CataclysmCombo;
    if(Sample.bPassivesVisible) Sample.PassiveNames={Copy(TEXT("UI.V2.Preview.Passive"),LOCTEXT("Passive","Пассивный навык")),Copy(TEXT("UI.V2.Preview.Passive"),LOCTEXT("Passive","Пассивный навык"))};
    Sample.bCataclysmVisible=PreviewState==TEXT("Cataclysm") || Long || CataclysmCombo;
    Sample.CataclysmInstruction=Copy(TEXT("UI.Event.Cataclysm.Rafts.Instruction"),LOCTEXT("CataclysmRafts","ПКМ у плота — занять левое или правое место. WASD — грести относительно плота; усилия обоих игроков складываются. Пробел — спрыгнуть. Договаривайтесь о направлении и уклоняйтесь от плывущего мусора!"));
    if(Long) {
        auto Extend=[](const FText& Text) {return FText::Format(FText::AsCultureInvariant(TEXT("{0} · {0} · {0}")),Text);};
        Sample.SwimCaption=Extend(Sample.SwimCaption);Sample.BoosterName=Extend(Sample.BoosterName);Sample.CataclysmInstruction=Extend(Sample.CataclysmInstruction);
        for(FText& Name:Sample.PassiveNames) Name=Extend(Name);
    }
    Present(Sample);
    if(auto* Reticle=EnsureReticleView()) {Reticle->PreviewCulture=PreviewCulture;Reticle->PreviewScenario=PreviewScenario;Reticle->PreviewState=PreviewState;Reticle->ApplyEditorPreview(Time);}
}
void UMCUnifiedHUDExtrasWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();if(IsDesignTime()) PresentPreview(Time);else RefreshFromBackend();
}

#undef LOCTEXT_NAMESPACE
