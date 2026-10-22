#include "MCGameplayMenuWidget.h"

#include "MCSteamSessionSubsystem.h"
#include "MCUIDataLibrary.h"
#include "MCGameplayHUD.h"
#include "MCHubGameState.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "MCGameplayMenu"

namespace MCGameplayMenuPrivate
{
    UMCSteamSessionSubsystem* Sessions(const UUserWidget* Widget)
    {
        auto* Instance=Widget->GetGameInstance();
        return Instance?Instance->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;
    }
    bool WithinActivePage(const UWidget* Target,const UWidgetSwitcher* Pages)
    {
        if(!Target || !Pages || !Target->GetIsEnabled() || !Target->IsVisible()) return false;
        const UWidget* Current=Target;
        while(Current && Current!=Pages->GetActiveWidget()) {
            if(!Current->IsVisible()) return false;
            Current=Current->GetParent();
        }
        return Current==Pages->GetActiveWidget();
    }
}

void UMCGameplayMenuActionButton::Configure(UMCGameplayMenuWidget* InMenu)
{
    Menu=InMenu;OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
}
void UMCGameplayMenuActionButton::Clicked() {if(Menu) Menu->HandleAction(MenuAction);}

FText UMCGameplayMenuWidget::Copy(FName Key,const FText& Fallback) const
{
    return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,IsPreviewing()?PreviewCulture:FString());
}
void UMCGameplayMenuWidget::NativeOnInitialized() {Super::NativeOnInitialized();SetIsFocusable(true);}
void UMCGameplayMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(!IsPreviewing()) {ShowPage(Page);FocusMenu();}
}
void UMCGameplayMenuWidget::NativeDestruct()
{
    if(bVideoConfirmationPending) RevertVideoSettings();
    GeneratedPickerLabels.Reset();
    Super::NativeDestruct();
}

UTextBlock* UMCGameplayMenuWidget::AddText(UVerticalBox* Parent,FName Name,const FText& Text,int32 Size)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetText(Text);Label->SetAutoWrapText(true);Label->SetFont(GetResolvedTheme()->MakeGameplayFont(Size));
    Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
    Parent->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,3,0,10));return Label;
}
UMCGameplayMenuActionButton* UMCGameplayMenuWidget::AddButton(UVerticalBox* Parent,FName Name,const FText& Text,EMCGameplayMenuAction Action)
{
    auto* Button=WidgetTree->ConstructWidget<UMCGameplayMenuActionButton>(UMCGameplayMenuActionButton::StaticClass(),Name);
    Button->MenuAction=Action;Button->Configure(this);
    auto* ButtonInset=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*(Name.ToString()+TEXT("Padding"))));
    ButtonInset->SetBrushColor(FLinearColor::Transparent);ButtonInset->SetPadding(FMargin(16,10));ButtonInset->SetVerticalAlignment(VAlign_Center);
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*(Name.ToString()+TEXT("Label"))));
    Label->SetText(Text);Label->SetAutoWrapText(true);Label->SetJustification(ETextJustify::Center);
    ButtonInset->SetContent(Label);auto* ContentSlot=Cast<UButtonSlot>(Button->SetContent(ButtonInset));ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);ContentSlot->SetPadding(FMargin(0));
    Parent->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,4));return Button;
}
UComboBoxString* UMCGameplayMenuWidget::AddPicker(UVerticalBox* Parent,FName Name,FName LabelName,const FText& Label)
{
    auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*(Name.ToString()+TEXT("Frame"))));Frame->SetPadding(FMargin(16,10));
    BindStyle(Frame->GetFName(),EMCUIColorToken::Surface,EMCUIStyleTarget::Background);
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Frame->SetContent(Row);
    auto* Caption=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),LabelName);Caption->SetText(Label);Caption->SetAutoWrapText(true);
    auto* CaptionSize=WidgetTree->ConstructWidget<USizeBox>();CaptionSize->SetWidthOverride(238);CaptionSize->SetContent(Caption);
    auto* CaptionSlot=Row->AddChildToHorizontalBox(CaptionSize);CaptionSlot->SetVerticalAlignment(VAlign_Center);CaptionSlot->SetPadding(FMargin(0,0,16,0));
    auto* Picker=WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),Name);
    Picker->SetContentPadding(FMargin(12,8));Picker->SetMaxListHeight(260);Picker->SetEnableGamepadNavigationMode(true);
    auto* PickerSlot=Row->AddChildToHorizontalBox(Picker);PickerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));PickerSlot->SetVerticalAlignment(VAlign_Center);
    Parent->AddChildToVerticalBox(Frame)->SetPadding(FMargin(0,0,0,10));return Picker;
}

void UMCGameplayMenuWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {BindPresentationTree();return;}
    Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("GameplayMenuBackdrop"));
    WidgetTree->RootWidget=Backdrop;Backdrop->SetPadding(FMargin(28));
    Backdrop->SetHorizontalAlignment(HAlign_Center);Backdrop->SetVerticalAlignment(VAlign_Center);
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("GameplayMenuScale"));
    Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);Backdrop->SetContent(Scale);
    auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("GameplayMenuSize"));
    Size->SetWidthOverride(500);Size->SetHeightOverride(650);Scale->SetContent(Size);
    Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("GameplayMenuCard"));
    Card->SetPadding(FMargin(20,18));Size->SetContent(Card);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("GameplayMenuColumn"));Card->SetContent(Column);
    auto* Header=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("GameplayMenuHeader"));Header->SetPadding(FMargin(14,10));
    auto* HeaderRow=WidgetTree->ConstructWidget<UHorizontalBox>();Header->SetContent(HeaderRow);
    auto* HeaderTooth=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("GameplayMenuTooth"));HeaderTooth->bTooth=true;HeaderTooth->bHappy=true;
    auto* ToothSize=WidgetTree->ConstructWidget<USizeBox>();ToothSize->SetWidthOverride(46);ToothSize->SetHeightOverride(50);ToothSize->SetContent(HeaderTooth);
    HeaderRow->AddChildToHorizontalBox(ToothSize)->SetPadding(FMargin(0,0,14,0));
    Heading=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("GameplayMenuHeading"));Heading->SetAutoWrapText(true);
    auto* HeadingSlot=HeaderRow->AddChildToHorizontalBox(Heading);HeadingSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));HeadingSlot->SetVerticalAlignment(VAlign_Center);
    Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,12));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("GameplayMenuScroll"));
    Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);Scroll->SetScrollbarPadding(FMargin(8,0,0,0));
    Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Pages=WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("GameplayMenuPages"));Scroll->AddChild(Pages);
    auto MakePage=[&](const TCHAR* Name){auto* Panel=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),Name);Pages->AddChild(Panel);return Panel;};
    auto* Pause=MakePage(TEXT("MenuPausePage"));
    AddButton(Pause,TEXT("MenuResume"),FText::GetEmpty(),EMCGameplayMenuAction::Resume);
    AddButton(Pause,TEXT("MenuPauseInvite"),FText::GetEmpty(),EMCGameplayMenuAction::Invite);
    AddButton(Pause,TEXT("MenuEmotes"),FText::GetEmpty(),EMCGameplayMenuAction::Emotes);
    AddButton(Pause,TEXT("MenuSettings"),FText::GetEmpty(),EMCGameplayMenuAction::Settings);
    AddButton(Pause,TEXT("MenuCredits"),FText::GetEmpty(),EMCGameplayMenuAction::Credits);
    AddButton(Pause,TEXT("MenuLeave"),FText::GetEmpty(),EMCGameplayMenuAction::Leave);
    AddButton(Pause,TEXT("MenuExit"),FText::GetEmpty(),EMCGameplayMenuAction::Exit);
    AddText(Column,TEXT("MenuNetworkNote"),FText::GetEmpty(),16);
    auto* Settings=MakePage(TEXT("MenuSettingsPage"));
    ResolutionPicker=AddPicker(Settings,TEXT("MenuResolutionPicker"),TEXT("MenuResolutionLabel"),FText::GetEmpty());
    ModePicker=AddPicker(Settings,TEXT("MenuModePicker"),TEXT("MenuModeLabel"),FText::GetEmpty());
    QualityPicker=AddPicker(Settings,TEXT("MenuQualityPicker"),TEXT("MenuQualityLabel"),FText::GetEmpty());
    VSyncCheck=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("MenuVSync"));
    auto* VSyncLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("MenuVSyncLabel"));
    VSyncLabel->SetAutoWrapText(true);VSyncCheck->SetContent(VSyncLabel);
    Settings->AddChildToVerticalBox(VSyncCheck)->SetPadding(FMargin(0,3,0,14));
    auto* ApplyButton=AddButton(Settings,TEXT("MenuApply"),FText::GetEmpty(),EMCGameplayMenuAction::ApplySettings);
    auto* SettingsBack=AddButton(Settings,TEXT("MenuSettingsBack"),FText::GetEmpty(),EMCGameplayMenuAction::Back);
    Settings->RemoveChild(ApplyButton);Settings->RemoveChild(SettingsBack);
    auto* SettingsActions=WidgetTree->ConstructWidget<UHorizontalBox>();Settings->AddChildToVerticalBox(SettingsActions)->SetPadding(FMargin(0,8,0,4));
    auto* ApplySlot=SettingsActions->AddChildToHorizontalBox(ApplyButton);ApplySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ApplySlot->SetPadding(FMargin(0,0,12,0));
    SettingsActions->AddChildToHorizontalBox(SettingsBack)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Credits=MakePage(TEXT("MenuCreditsPage"));
    AddText(Credits,TEXT("MenuCreditsBody"),FText::GetEmpty());AddText(Credits,TEXT("MenuCreditsEngine"),FText::GetEmpty(),16);
    AddButton(Credits,TEXT("MenuCreditsBack"),FText::GetEmpty(),EMCGameplayMenuAction::Back);
    auto* Leave=MakePage(TEXT("MenuLeavePage"));
    AddText(Leave,TEXT("MenuLeaveBody"),FText::GetEmpty());
    AddButton(Leave,TEXT("MenuLeaveCancel"),FText::GetEmpty(),EMCGameplayMenuAction::Back);
    AddButton(Leave,TEXT("MenuLeaveConfirm"),FText::GetEmpty(),EMCGameplayMenuAction::ConfirmLeave);
    auto* Exit=MakePage(TEXT("MenuExitPage"));
    AddButton(Exit,TEXT("MenuExitCancel"),FText::GetEmpty(),EMCGameplayMenuAction::Back);
    AddButton(Exit,TEXT("MenuExitConfirm"),FText::GetEmpty(),EMCGameplayMenuAction::ConfirmExit);
    auto* Confirm=MakePage(TEXT("MenuVideoConfirmPage"));
    AddText(Confirm,TEXT("MenuVideoConfirmBody"),FText::GetEmpty());
    AddButton(Confirm,TEXT("MenuVideoKeep"),FText::GetEmpty(),EMCGameplayMenuAction::KeepSettings);
    AddButton(Confirm,TEXT("MenuVideoRevert"),FText::GetEmpty(),EMCGameplayMenuAction::RevertSettings);
    auto* Connection=MakePage(TEXT("MenuConnectionPage"));
    AddText(Connection,TEXT("MenuConnectionBody"),FText::GetEmpty(),16);
    AddButton(Connection,TEXT("MenuHost"),FText::GetEmpty(),EMCGameplayMenuAction::Host);
    AddButton(Connection,TEXT("MenuFindRooms"),FText::GetEmpty(),EMCGameplayMenuAction::FindRooms);
    RoomPicker=AddPicker(Connection,TEXT("MenuRoomPicker"),TEXT("MenuRoomLabel"),FText::GetEmpty());
    AddButton(Connection,TEXT("MenuJoinRoom"),FText::GetEmpty(),EMCGameplayMenuAction::JoinRoom);
    AddButton(Connection,TEXT("MenuInvite"),FText::GetEmpty(),EMCGameplayMenuAction::Invite);
    AddText(Connection,TEXT("MenuAddressLabel"),FText::GetEmpty(),16);
    AddressBox=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("MenuAddress"));
    AddressBox->SetText(FText::AsCultureInvariant(TEXT("127.0.0.1")));Connection->AddChildToVerticalBox(AddressBox)->SetPadding(FMargin(0,0,0,8));
    AddButton(Connection,TEXT("MenuJoinAddress"),FText::GetEmpty(),EMCGameplayMenuAction::JoinAddress);
    AddButton(Connection,TEXT("MenuConnectionBack"),FText::GetEmpty(),EMCGameplayMenuAction::Back);
    Status=AddText(Column,TEXT("GameplayMenuStatus"),FText::GetEmpty(),15);
    Hint=AddText(Column,TEXT("GameplayMenuHint"),FText::GetEmpty(),13);
    BindPresentationTree();UpdatePageCopy();ApplyTheme();
}

void UMCGameplayMenuWidget::BindPresentationTree()
{
    if(!WidgetTree) return;
    Backdrop=Cast<UBorder>(WidgetTree->FindWidget(TEXT("GameplayMenuBackdrop")));
    Card=Cast<UBorder>(WidgetTree->FindWidget(TEXT("GameplayMenuCard")));
    Heading=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("GameplayMenuHeading")));
    Status=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("GameplayMenuStatus")));
    Hint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("GameplayMenuHint")));
    Pages=Cast<UWidgetSwitcher>(WidgetTree->FindWidget(TEXT("GameplayMenuPages")));
    // Preserve the saved pause layout; the Hub invitation is one appended
    // Designer-editable branch when an older WBP has not been migrated yet.
    if(!WidgetTree->FindWidget(TEXT("MenuPauseInvite")))
        if(auto* Pause=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("MenuPausePage"))))
            AddButton(Pause,TEXT("MenuPauseInvite"),FText::GetEmpty(),EMCGameplayMenuAction::Invite);
    ResolutionPicker=Cast<UComboBoxString>(WidgetTree->FindWidget(TEXT("MenuResolutionPicker")));
    ModePicker=Cast<UComboBoxString>(WidgetTree->FindWidget(TEXT("MenuModePicker")));
    QualityPicker=Cast<UComboBoxString>(WidgetTree->FindWidget(TEXT("MenuQualityPicker")));
    VSyncCheck=Cast<UCheckBox>(WidgetTree->FindWidget(TEXT("MenuVSync")));
    RoomPicker=Cast<UComboBoxString>(WidgetTree->FindWidget(TEXT("MenuRoomPicker")));
    AddressBox=Cast<UEditableTextBox>(WidgetTree->FindWidget(TEXT("MenuAddress")));
    Buttons.Reset();FocusTargets.Reset();TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);
    for(auto* Widget:Widgets) {
        if(auto* Button=Cast<UMCGameplayMenuActionButton>(Widget)) {Button->Configure(this);Buttons.Add(Button);FocusTargets.Add(Button);}
        else if(auto* Picker=Cast<UComboBoxString>(Widget)) {Picker->OnGenerateWidgetEvent.BindDynamic(this,&ThisClass::GeneratePickerItem);FocusTargets.Add(Picker);}
        else if(Cast<UCheckBox>(Widget) || Cast<UEditableTextBox>(Widget)) FocusTargets.Add(Widget);
    }
    if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("GameplayMenuScroll")))) {Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);}
    // The multiplayer note belongs above the persistent navigation footer.
    // Keeping it in the scrolling page can hide its second translated line.
    if(auto* Column=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("GameplayMenuColumn"))))
        if(auto* Note=WidgetTree->FindWidget(TEXT("MenuNetworkNote"));Note && Note->GetParent()!=Column) {
            Note->RemoveFromParent();Column->AddChildToVerticalBox(Note)->SetPadding(FMargin(0,8,0,6));
            for(UTextBlock* Footer:{Status.Get(),Hint.Get()}) if(Footer) {Footer->RemoveFromParent();Column->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,3,0,8));}
        }
    NormalizePresentationLayout();
}

void UMCGameplayMenuWidget::NormalizePresentationLayout()
{
    if(!bUseNativeLayout || !WidgetTree) return;
    auto* Settings=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("MenuSettingsPage")));
    if(Settings && VSyncCheck && VSyncCheck->GetParent()==Settings && !WidgetTree->FindWidget(TEXT("MenuVSyncFrame")))
    {
        // Replace only the known settings row. Detaching/re-appending the tail
        // updates live Slate too; InsertChildAt would only reorder the UObject tree.
        struct FTailEntry {UWidget* Widget;FMargin Padding;FSlateChildSize Size;};
        TArray<FTailEntry> Tail;
        const int32 RowIndex=Settings->GetChildIndex(VSyncCheck);
        for(int32 Index=RowIndex+1;Index<Settings->GetChildrenCount();++Index) {
            UWidget* Child=Settings->GetChildAt(Index);const auto* ChildSlot=Cast<UVerticalBoxSlot>(Child->Slot);
            Tail.Add({Child,ChildSlot?ChildSlot->GetPadding():FMargin(0),ChildSlot?ChildSlot->GetSize():FSlateChildSize(ESlateSizeRule::Automatic)});
        }
        for(auto& Entry:Tail) Entry.Widget->RemoveFromParent();
        VSyncCheck->RemoveFromParent();
        auto* Caption=Cast<UTextBlock>(VSyncCheck->GetContent());VSyncCheck->SetContent(nullptr);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("MenuVSyncFrame"));Frame->SetPadding(FMargin(16,10));
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Frame->SetContent(Row);
        if(Caption) {
            auto* CaptionSize=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("MenuVSyncCaptionSize"));CaptionSize->SetWidthOverride(300);CaptionSize->SetContent(Caption);
            auto* CaptionSlot=Row->AddChildToHorizontalBox(CaptionSize);CaptionSlot->SetPadding(FMargin(0,0,16,0));CaptionSlot->SetVerticalAlignment(VAlign_Center);
        }
        auto* CheckSize=WidgetTree->ConstructWidget<USizeBox>();CheckSize->SetWidthOverride(32);CheckSize->SetHeightOverride(32);CheckSize->SetContent(VSyncCheck);
        Row->AddChildToHorizontalBox(CheckSize)->SetVerticalAlignment(VAlign_Center);
        Settings->AddChildToVerticalBox(Frame)->SetPadding(FMargin(0,0,0,10));
        for(auto& Entry:Tail) {auto* ChildSlot=Settings->AddChildToVerticalBox(Entry.Widget);ChildSlot->SetPadding(Entry.Padding);ChildSlot->SetSize(Entry.Size);}
    }
    BindStyle(TEXT("MenuVSyncFrame"),EMCUIColorToken::Surface,EMCUIStyleTarget::Background);
    if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("GameplayMenuScroll")))) {Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness(FVector2D(5,5));}
    bLayoutDirty=true;
}

void UMCGameplayMenuWidget::UpdatePresentationLayout(FVector2D Area)
{
    if(!bUseNativeLayout) return;
    if(!WidgetTree || !Card || !Pages) return;
    auto* Size=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("GameplayMenuSize")));
    auto* Column=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("GameplayMenuColumn")));
    auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("GameplayMenuScroll")));
    UWidget* ActivePage=Pages->GetActiveWidget();
    if(!Size || !Column || !Scroll || !ActivePage) return;
    if(Area.IsNearlyZero()) Area=GetCachedGeometry().GetLocalSize();
    if(Area.X<=1 || Area.Y<=1) Area=FVector2D(1920,1080);
    const float PreferredWidth=Page==EPage::Settings?840.f:Page==EPage::Credits?760.f:Page==EPage::Pause?500.f:640.f;
    const float Width=FMath::Min(PreferredWidth,FMath::Max(280.f,float(Area.X)-56.f));
    const float MaximumHeight=FMath::Max(240.f,float(Area.Y)-56.f);
    const FVector2D ArrangedSize=Size->GetCachedGeometry().GetLocalSize();
    const float ArrangedScrollHeight=Scroll->GetCachedGeometry().GetLocalSize().Y;
    // Auto-wrap can change desired height during paint, after the first
    // prepass. These cached-size reads are cheap once the layout has settled.
    const bool ContentOutgrewViewport=ArrangedScrollHeight>1.f && ActivePage->GetDesiredSize().Y>ArrangedScrollHeight+1.f && Size->GetHeightOverride()<MaximumHeight-.5f;
    if(!bLayoutDirty && Area.Equals(LastLayoutArea,.5f) && !ContentOutgrewViewport) return;
    LastLayoutArea=Area;
    Size->SetWidthOverride(Width);
    const float InnerWidth=Width-40.f;
    const bool UseArrangedWidths=FMath::IsNearlyEqual(float(ArrangedSize.X),Width,.5f);
    auto WrapLabel=[&](UTextBlock* Label,float AvailableWidth) {
        if(!Label) return;
        const float ArrangedWidth=Label->GetCachedGeometry().GetLocalSize().X;
        if(UseArrangedWidths && ArrangedWidth>1.f) AvailableWidth=FMath::Min(AvailableWidth,ArrangedWidth);
        // An explicit width makes prepass and paint use the same wrapping
        // input. AutoWrapText would also consult the previous paint's width.
        Label->SetAutoWrapText(false);Label->SetWrapTextAt(FMath::Max(64.f,AvailableWidth));
        Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    };
    WrapLabel(Heading,FMath::Max(140.f,InnerWidth-88.f));
    for(const TCHAR* Name:{TEXT("MenuNetworkNote"),TEXT("GameplayMenuHint"),TEXT("GameplayMenuStatus"),TEXT("MenuCreditsBody"),TEXT("MenuCreditsEngine"),TEXT("MenuLeaveBody"),TEXT("MenuVideoConfirmBody"),TEXT("MenuConnectionBody"),TEXT("MenuAddressLabel")})
        WrapLabel(Cast<UTextBlock>(WidgetTree->FindWidget(Name)),InnerWidth-8.f);
    for(const TCHAR* Name:{TEXT("MenuResolutionLabel"),TEXT("MenuModeLabel"),TEXT("MenuQualityLabel"),TEXT("MenuRoomLabel"),TEXT("MenuVSyncLabel")})
        if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) {
            const float CaptionWidth=FMath::Clamp(InnerWidth*.39f,160.f,300.f);
            if(auto* CaptionSize=Cast<USizeBox>(Label->GetParent())) CaptionSize->SetWidthOverride(CaptionWidth);
            WrapLabel(Label,CaptionWidth);
        }
    for(const auto& Button:Buttons) if(Button) if(auto* Inset=Cast<UBorder>(Button->GetContent())) if(auto* Label=Cast<UTextBlock>(Inset->GetContent())) {
        const bool ActionRow=Cast<UHorizontalBox>(Button->GetParent())!=nullptr;
        WrapLabel(Label,ActionRow?(InnerWidth-12.f)*.5f-40.f:InnerWidth-40.f);
    }
    // Measure the active page and fixed footer separately. A Fill ScrollBox
    // should consume only the bounded remainder, not create a blank modal body.
    const float GeometryScale=GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale();
    const float LayoutScale=FMath::IsFinite(GeometryScale) && GeometryScale>0.f?GeometryScale:1.f;
    Column->TakeWidget()->MarkPrepassAsDirty();Column->TakeWidget()->SlatePrepass(LayoutScale);
    ActivePage->TakeWidget()->MarkPrepassAsDirty();ActivePage->TakeWidget()->SlatePrepass(LayoutScale);
    float Height=Card->GetPadding().GetDesiredSize().Y+8.f;
    const float PageHeight=ActivePage->GetDesiredSize().Y;
    for(UWidget* Child:Column->GetAllChildren()) {
        if(Child==Status && (!Status || Status->GetText().IsEmpty())) continue;
        if(Child->GetFName()==TEXT("MenuNetworkNote") && Page!=EPage::Pause) continue;
        const auto* ChildSlot=Cast<UVerticalBoxSlot>(Child->Slot);
        Height+=(Child==Scroll?PageHeight:Child->GetDesiredSize().Y)+(ChildSlot?ChildSlot->GetPadding().GetDesiredSize().Y:0.f);
    }
    if(PageHeight<1.f) {bLayoutDirty=true;return;}
    const float TargetHeight=FMath::Clamp(FMath::CeilToFloat(Height),220.f,MaximumHeight);
    if(!FMath::IsNearlyEqual(Size->GetHeightOverride(),TargetHeight,.5f)) Size->SetHeightOverride(TargetHeight);
    Scroll->SetScrollBarVisibility(Height>MaximumHeight?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // Keep measuring across the first arranged frames, then stop doing
    // prepasses. A page/theme/viewport change starts the same settle again.
    bLayoutDirty=!UseArrangedWidths || !FMath::IsNearlyEqual(float(ArrangedSize.Y),TargetHeight,.5f);
}

void UMCGameplayMenuWidget::UpdatePageCopy()
{
    if(!WidgetTree) return;
    auto Set=[&](const TCHAR* Name,FName Key,const FText& Fallback){if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) Label->SetText(Copy(Key,Fallback));};
    Set(TEXT("MenuResumeLabel"),TEXT("UI.Menu.Resume"),LOCTEXT("Resume","Продолжить"));
    Set(TEXT("MenuPauseInviteLabel"),TEXT("hub.invite"),LOCTEXT("HubInvite","ПРИГЛАСИТЬ ДРУЗЕЙ"));
    if(auto* Invite=WidgetTree->FindWidget(TEXT("MenuPauseInvite"))) {
        auto* Sessions=IsPreviewing()?nullptr:MCGameplayMenuPrivate::Sessions(this);
        const bool Available=IsPreviewing()?bPreviewSteamAvailable
            :Sessions && Sessions->CanUseSteam() && Sessions->HasRoom() && GetWorld() && GetWorld()->GetGameState<AMCHubGameState>();
        Invite->SetVisibility(Available?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        Invite->SetIsEnabled(Available && (!Sessions || !Sessions->IsBusy()));
    }
    Set(TEXT("MenuEmotesLabel"),TEXT("UI.Menu.Emotes"),LOCTEXT("Emotes","Эмоции и сигналы"));
    Set(TEXT("MenuSettingsLabel"),TEXT("UI.Menu.Settings"),LOCTEXT("Settings","Настройки"));
    Set(TEXT("MenuCreditsLabel"),TEXT("UI.Menu.Credits"),LOCTEXT("Credits","Авторы"));
    Set(TEXT("MenuLeaveLabel"),TEXT("UI.Menu.Leave"),LOCTEXT("Leave","В главное меню"));
    Set(TEXT("MenuExitLabel"),TEXT("UI.Menu.Exit"),LOCTEXT("Exit","Выйти из игры"));
    Set(TEXT("MenuNetworkNote"),TEXT("UI.Menu.NetworkNote"),LOCTEXT("NetworkNote","В сетевой игре смена продолжается, пока это меню открыто."));
    Set(TEXT("MenuResolutionLabel"),TEXT("UI.Menu.Resolution"),LOCTEXT("Resolution","Разрешение"));
    Set(TEXT("MenuModeLabel"),TEXT("UI.Menu.WindowMode"),LOCTEXT("WindowMode","Режим экрана"));
    Set(TEXT("MenuQualityLabel"),TEXT("UI.Menu.Quality"),LOCTEXT("Quality","Качество графики"));
    Set(TEXT("MenuVSyncLabel"),TEXT("UI.Menu.VSync"),LOCTEXT("VSync","VSync"));
    Set(TEXT("MenuApplyLabel"),TEXT("UI.Menu.Apply"),LOCTEXT("Apply","Применить и сохранить"));
    for(const TCHAR* Name:{TEXT("MenuSettingsBackLabel"),TEXT("MenuCreditsBackLabel"),TEXT("MenuConnectionBackLabel")})
        Set(Name,TEXT("UI.Common.Back"),LOCTEXT("Back","Назад"));
    for(const TCHAR* Name:{TEXT("MenuLeaveCancelLabel"),TEXT("MenuExitCancelLabel")})
        Set(Name,TEXT("UI.Common.Cancel"),LOCTEXT("Cancel","Отмена"));
    Set(TEXT("MenuCreditsBody"),TEXT("UI.Menu.CreditsBody"),LOCTEXT("CreditsBody","Mess Control\nКооперативная игра про маленькие зубы и большой беспорядок во рту."));
    Set(TEXT("MenuCreditsEngine"),TEXT("UI.Menu.CreditsEngine"),LOCTEXT("CreditsEngine","Движок: Unreal Engine\nUnreal Engine — товарный знак Epic Games, Inc.\nИмена команды и авторов материалов будут добавлены после утверждения титров."));
    const bool bHost=IsPreviewing()?bPreviewHost:GetOwningPlayer() && GetOwningPlayer()->HasAuthority();
    Set(TEXT("MenuLeaveBody"),bHost?TEXT("UI.Menu.LeaveHostBody"):TEXT("UI.Menu.LeaveClientBody"),bHost?
        LOCTEXT("LeaveHostBody","Ты — хост. При выходе соединение остальных игроков с комнатой закроется."):
        LOCTEXT("LeaveClientBody","Ты вернёшься в главное меню."));
    Set(TEXT("MenuLeaveConfirmLabel"),TEXT("UI.Menu.LeaveConfirm"),LOCTEXT("LeaveConfirm","Выйти в главное меню"));
    Set(TEXT("MenuExitConfirmLabel"),TEXT("UI.Menu.Exit"),LOCTEXT("Exit","Выйти из игры"));
    Set(TEXT("MenuVideoConfirmBody"),TEXT("UI.Menu.VideoConfirmBody"),LOCTEXT("VideoConfirmBody","Если изображение пропало, предыдущий режим вернётся автоматически через 15 секунд."));
    Set(TEXT("MenuVideoKeepLabel"),TEXT("UI.Menu.VideoKeep"),LOCTEXT("VideoKeep","Сохранить"));
    Set(TEXT("MenuVideoRevertLabel"),TEXT("UI.Menu.VideoRevert"),LOCTEXT("VideoRevert","Вернуть прежний режим"));
    Set(TEXT("MenuConnectionBody"),TEXT("UI.Menu.ConnectionBody"),LOCTEXT("ConnectionBody","До четырёх игроков. Создай комнату или присоединись к друзьям. Shift+Tab — друзья Steam."));
    Set(TEXT("MenuHostLabel"),TEXT("UI.Menu.Host"),LOCTEXT("Host","Создать комнату"));
    Set(TEXT("MenuFindRoomsLabel"),TEXT("UI.Menu.FindRooms"),LOCTEXT("FindRooms","Найти комнаты"));
    Set(TEXT("MenuRoomLabel"),TEXT("UI.Menu.Room"),LOCTEXT("Room","Комната друзей"));
    Set(TEXT("MenuJoinRoomLabel"),TEXT("UI.Menu.JoinRoom"),LOCTEXT("JoinRoom","Подключиться"));
    Set(TEXT("MenuInviteLabel"),TEXT("UI.Menu.Invite"),LOCTEXT("Invite","Пригласить друга в Steam"));
    Set(TEXT("MenuAddressLabel"),TEXT("UI.Menu.Address"),LOCTEXT("Address","IP или имя компьютера · порт 7777"));
    Set(TEXT("MenuJoinAddressLabel"),TEXT("UI.Menu.JoinAddress"),LOCTEXT("JoinAddress","Подключиться по IP"));
    if(Heading) {
        switch(Page) {
        case EPage::Settings: Heading->SetText(Copy(TEXT("UI.Menu.Settings"),LOCTEXT("Settings","Настройки")));break;
        case EPage::Credits: Heading->SetText(Copy(TEXT("UI.Menu.Credits"),LOCTEXT("Credits","Авторы")));break;
        case EPage::Leave: Heading->SetText(Copy(TEXT("UI.Menu.LeaveHeading"),LOCTEXT("LeaveHeading","Выйти из комнаты?")));break;
        case EPage::Exit: Heading->SetText(Copy(TEXT("UI.Menu.ExitHeading"),LOCTEXT("ExitHeading","Выйти из игры?")));break;
        case EPage::VideoConfirm: Heading->SetText(Copy(TEXT("UI.Menu.VideoHeading"),LOCTEXT("VideoHeading","Сохранить режим экрана?")));break;
        case EPage::Connection: Heading->SetText(Copy(TEXT("UI.Menu.ConnectionHeading"),LOCTEXT("ConnectionHeading","Игра с друзьями")));break;
        default:Heading->SetText(Copy(TEXT("UI.Menu.PauseHeading"),LOCTEXT("PauseHeading","Меню")));break;
        }
    }
    if(Hint) Hint->SetText(Copy(bConnectionRoot?TEXT("UI.Menu.ConnectionHint"):TEXT("UI.Menu.NavigationHint"),bConnectionRoot?
        LOCTEXT("ConnectionHint","F2 / Esc — закрыть · ↑↓ / D-pad — выбрать · Enter / A — открыть"):
        LOCTEXT("NavigationHint","↑↓ / D-pad — выбрать · Enter / A — открыть · Esc / B — назад")));
}

void UMCGameplayMenuWidget::ShowPage(EPage NewPage)
{
    BuildPresentationTree();Page=NewPage;NextStickNavigationAt=0;SetStatus(FText::GetEmpty());
    if(Pages) {
        const TCHAR* Names[]={TEXT("MenuPausePage"),TEXT("MenuSettingsPage"),TEXT("MenuCreditsPage"),TEXT("MenuLeavePage"),TEXT("MenuExitPage"),TEXT("MenuVideoConfirmPage"),TEXT("MenuConnectionPage")};
        if(auto* Panel=WidgetTree->FindWidget(Names[static_cast<int32>(Page)])) Pages->SetActiveWidget(Panel);
    }
    UpdatePageCopy();
    if(Page==EPage::Settings) PopulateSettings();
    if(Page==EPage::Connection) {LastSearchRevision=INDEX_NONE;RefreshConnection();}
    ApplyTheme();if(!IsPreviewing()) FocusMenu();
}
void UMCGameplayMenuWidget::ShowPause()
{
    if(bVideoConfirmationPending) RevertVideoSettings();bConnectionRoot=false;bCloseRequested=false;ShowPage(EPage::Pause);
}
void UMCGameplayMenuWidget::ShowConnection()
{
    if(bVideoConfirmationPending) RevertVideoSettings();bConnectionRoot=true;bCloseRequested=false;ShowPage(EPage::Connection);
}
void UMCGameplayMenuWidget::SetStatus(const FText& Text)
{
    StatusKey=NAME_None;StatusFallback=Text;
    if(!Text.IsEmpty()) {
        const FString* NativeSource=FTextInspector::GetSourceString(Text);
        const FString Source=NativeSource?*NativeSource:Text.ToString();
        UDataTable* Table=GetResolvedTranslationTable();
        if(Table && Table->GetRowStruct()==FMCUILocalizationRow::StaticStruct()) {
            FName MatchingKey=NAME_None;
            bool bAmbiguous=false;
            for(const auto& Entry:Table->GetRowMap()) {
                if(!Entry.Key.ToString().StartsWith(TEXT("UI.Connection.Status."),ESearchCase::CaseSensitive)) continue;
                const auto* Row=reinterpret_cast<const FMCUILocalizationRow*>(Entry.Value);
                if(!Row) continue;
                // SourceText is the immutable migration alias; editing RU never changes identity.
                const FString* RowNativeSource=FTextInspector::GetSourceString(Row->SourceText);
                const FString RowSource=RowNativeSource?*RowNativeSource:Row->SourceText.ToString();
                if(Source!=RowSource) continue;
                // Keep external details and ambiguous authoring unchanged; matching never uses a regex.
                if(!MatchingKey.IsNone()) {bAmbiguous=true;break;}
                MatchingKey=Entry.Key;
            }
            if(!MatchingKey.IsNone() && !bAmbiguous) StatusKey=MatchingKey;
        }
    }
    RefreshStatusCopy();
}
void UMCGameplayMenuWidget::SetStatusKey(FName Key,const FText& Fallback)
{
    StatusKey=Key;StatusFallback=Fallback;RefreshStatusCopy();
}
void UMCGameplayMenuWidget::RefreshStatusCopy()
{
    if(!Status) return;
    const FText DisplayText=StatusKey.IsNone()?StatusFallback:Copy(StatusKey,StatusFallback);
    if(!Status->GetText().EqualTo(DisplayText)) {Status->SetText(DisplayText);bLayoutDirty=true;}
    Status->SetVisibility(DisplayText.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
bool UMCGameplayMenuWidget::SteamAvailable() const
{
    if(IsPreviewing()) return bPreviewSteamAvailable;
    auto* Steam=MCGameplayMenuPrivate::Sessions(this);return Steam && Steam->CanUseSteam();
}

void UMCGameplayMenuWidget::PopulateSettings()
{
    if(!ResolutionPicker || !ModePicker || !QualityPicker || !VSyncCheck) return;
    auto* Settings=IsPreviewing()?nullptr:UGameUserSettings::GetGameUserSettings();
    if(!Settings && !IsPreviewing()) {
        SetStatusKey(TEXT("UI.Menu.SettingsUnavailable"),LOCTEXT("SettingsUnavailable","Настройки экрана недоступны."));
        if(auto* Button=WidgetTree->FindWidget(TEXT("MenuApply"))) Button->SetIsEnabled(false);return;
    }
    if(auto* Button=WidgetTree->FindWidget(TEXT("MenuApply"))) Button->SetIsEnabled(true);
    GeneratedPickerLabels.Reset();
    const FIntPoint Current=Settings?Settings->GetScreenResolution():FIntPoint(1920,1080);
    Resolutions.Reset();if(Settings) UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
    else {Resolutions.Add(FIntPoint(1280,720));Resolutions.Add(FIntPoint(1920,1080));Resolutions.Add(FIntPoint(2560,1440));}
    Resolutions.RemoveAll([](const FIntPoint& Point){return Point.X<1024 || Point.Y<600;});
    if(Current.X>0 && Current.Y>0) Resolutions.AddUnique(Current);
    if(Resolutions.IsEmpty()) Resolutions.Add(FIntPoint(1280,720));
    Resolutions.Sort([](const FIntPoint& A,const FIntPoint& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;});
    ResolutionPicker->ClearOptions();for(const auto& Resolution:Resolutions) ResolutionPicker->AddOption(FString::Printf(TEXT("%d × %d"),Resolution.X,Resolution.Y));
    ResolutionPicker->SetSelectedIndex(FMath::Max(0,Resolutions.IndexOfByKey(Current)));
    const int32 Quality=Settings?Settings->GetOverallScalabilityLevel():3;
    RefreshSettingsChoiceCopy(Settings?static_cast<int32>(Settings->GetFullscreenMode()):1,Quality<0?5:FMath::Clamp(Quality,0,4),Quality<0);
    VSyncCheck->SetIsChecked(Settings?Settings->IsVSyncEnabled():true);
}
void UMCGameplayMenuWidget::RefreshSettingsChoiceCopy(int32 Mode,int32 Quality,bool bIncludeCustom)
{
    if(!ModePicker || !QualityPicker) return;
    // Only labels change. Preserve both pending selections and their semantic indices.
    ModePicker->ClearOptions();
    ModePicker->AddOption(Copy(TEXT("UI.Menu.Fullscreen"),LOCTEXT("Fullscreen","Полный экран")).ToString());
    ModePicker->AddOption(Copy(TEXT("UI.Menu.Borderless"),LOCTEXT("Borderless","Окно без рамки")).ToString());
    ModePicker->AddOption(Copy(TEXT("UI.Menu.Windowed"),LOCTEXT("Windowed","Оконный")).ToString());
    ModePicker->SetSelectedIndex(FMath::Clamp(Mode,0,2));
    QualityPicker->ClearOptions();
    QualityPicker->AddOption(Copy(TEXT("UI.Menu.QualityLow"),LOCTEXT("QualityLow","Низкое")).ToString());
    QualityPicker->AddOption(Copy(TEXT("UI.Menu.QualityMedium"),LOCTEXT("QualityMedium","Среднее")).ToString());
    QualityPicker->AddOption(Copy(TEXT("UI.Menu.QualityHigh"),LOCTEXT("QualityHigh","Высокое")).ToString());
    QualityPicker->AddOption(Copy(TEXT("UI.Menu.QualityEpic"),LOCTEXT("QualityEpic","Эпическое")).ToString());
    QualityPicker->AddOption(Copy(TEXT("UI.Menu.QualityCinematic"),LOCTEXT("QualityCinematic","Кинематографическое")).ToString());
    if(bIncludeCustom) QualityPicker->AddOption(Copy(TEXT("UI.Menu.QualityCustom"),LOCTEXT("QualityCustom","Свои настройки — сохранить")).ToString());
    QualityPicker->SetSelectedIndex(FMath::Clamp(Quality,0,5));
}

void UMCGameplayMenuWidget::ApplySettings()
{
    if(IsPreviewing()) return;
    auto* Settings=UGameUserSettings::GetGameUserSettings();
    if(!Settings || !ResolutionPicker || !ModePicker || !QualityPicker || !VSyncCheck) return;
    const int32 Index=ResolutionPicker->GetSelectedIndex();if(!Resolutions.IsValidIndex(Index)) return;
    PreviousResolution=Settings->GetScreenResolution();PreviousWindowMode=static_cast<int32>(Settings->GetFullscreenMode());
    const int32 Mode=FMath::Clamp(ModePicker->GetSelectedIndex(),0,2);
    Settings->SetScreenResolution(Resolutions[Index]);Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(Mode));
    const int32 Quality=QualityPicker->GetSelectedIndex();if(Quality>=0 && Quality<=4) Settings->SetOverallScalabilityLevel(Quality);
    Settings->SetVSyncEnabled(VSyncCheck->IsChecked());Settings->ApplySettings(false);Settings->SaveSettings();
    if(PreviousResolution!=Settings->GetScreenResolution() || PreviousWindowMode!=Mode) {
        bVideoConfirmationPending=true;VideoConfirmationEndsAt=FPlatformTime::Seconds()+15.;ShowPage(EPage::VideoConfirm);
    } else SetStatusKey(TEXT("UI.Menu.SettingsSaved"),LOCTEXT("SettingsSaved","Настройки применены и сохранены."));
}
void UMCGameplayMenuWidget::RevertVideoSettings()
{
    const bool bWasPending=bVideoConfirmationPending;bVideoConfirmationPending=false;
    if(!bWasPending || IsPreviewing()) return;
    if(auto* Settings=UGameUserSettings::GetGameUserSettings()) {
        Settings->SetScreenResolution(PreviousResolution);Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PreviousWindowMode));
        Settings->ApplyResolutionSettings(false);Settings->ConfirmVideoMode();Settings->SaveSettings();
    }
}

void UMCGameplayMenuWidget::RefreshConnection()
{
    const bool bSteam=SteamAvailable();auto* Steam=IsPreviewing()?nullptr:MCGameplayMenuPrivate::Sessions(this);
    const bool bBusy=IsPreviewing()?PreviewState==TEXT("Pending") || PreviewScenario==EMCGameplayUIPreviewScenario::Pending:Steam && Steam->IsBusy();
    for(const TCHAR* Name:{TEXT("MenuFindRooms"),TEXT("MenuRoomPicker"),TEXT("MenuRoomLabel"),TEXT("MenuJoinRoom"),TEXT("MenuInvite")})
        if(auto* Widget=WidgetTree->FindWidget(Name)) Widget->SetVisibility(bSteam?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    for(const TCHAR* Name:{TEXT("MenuHost"),TEXT("MenuFindRooms"),TEXT("MenuJoinRoom"),TEXT("MenuInvite"),TEXT("MenuJoinAddress"),TEXT("MenuAddress"),TEXT("MenuRoomPicker")})
        if(auto* Widget=WidgetTree->FindWidget(Name)) Widget->SetIsEnabled(!bBusy);
    if(IsPreviewing()) {
        if(RoomPicker) {GeneratedPickerLabels.Reset();RoomPicker->ClearOptions();RoomPicker->AddOption(TEXT("AfterCoffee · 2 / 4"));RoomPicker->SetSelectedIndex(0);}
        SetStatus(PreviewStatus);return;
    }
    if(Steam) {
        if(RoomPicker && LastSearchRevision!=Steam->GetSearchRevision()) {
            LastSearchRevision=Steam->GetSearchRevision();GeneratedPickerLabels.Reset();RoomPicker->ClearOptions();
            for(const FString& Room:Steam->GetRoomLabels()) RoomPicker->AddOption(Room);
            if(RoomPicker->GetOptionCount()>0) RoomPicker->SetSelectedIndex(0);
        }
        // ShowPage clears the label when reopening; refresh even when the subsystem string is unchanged.
        LastSessionStatus=Steam->GetStatus();SetStatus(FText::FromString(LastSessionStatus));
    }
    if(auto* Join=WidgetTree->FindWidget(TEXT("MenuJoinRoom"))) Join->SetIsEnabled(!bBusy && RoomPicker && RoomPicker->GetSelectedIndex()!=INDEX_NONE);
}

bool UMCGameplayMenuWidget::NormalizeJoinAddress(const FString& Input,FString& OutAddress)
{
    OutAddress.Reset();FString Address=Input.TrimStartAndEnd();
    // The controller validates again at the travel boundary, including our normalized output.
    if(Address.StartsWith(TEXT("unreal://"),ESearchCase::IgnoreCase)) Address.RightChopInline(9);
    if(Address.IsEmpty() || Address.Len()>260) return false;
    FString Host=Address,PortText=TEXT("7777");int32 Colon=INDEX_NONE;
    if(Address.FindChar(TEXT(':'),Colon)) {
        Host=Address.Left(Colon);PortText=Address.Mid(Colon+1);
        if(PortText.IsEmpty() || PortText.Len()>5) return false;
        for(TCHAR C:PortText) if(C<TEXT('0') || C>TEXT('9')) return false;
    }
    if(Host.IsEmpty() || Host.Len()>253) return false;
    const int32 Port=FCString::Atoi(*PortText);if(Port<1 || Port>65535) return false;
    TArray<FString> Labels;Host.ParseIntoArray(Labels,TEXT("."),false);bool bNumeric=true;
    for(const FString& Label:Labels) {
        if(Label.IsEmpty() || Label.Len()>63 || Label.StartsWith(TEXT("-")) || Label.EndsWith(TEXT("-"))) return false;
        for(TCHAR C:Label) {
            const bool bDigit=C>=TEXT('0') && C<=TEXT('9');
            const bool bLetter=(C>=TEXT('a') && C<=TEXT('z')) || (C>=TEXT('A') && C<=TEXT('Z'));
            if(!bDigit && !bLetter && C!=TEXT('-')) return false;bNumeric&=bDigit;
        }
    }
    if(bNumeric && Labels.Num()>1) {
        if(Labels.Num()!=4) return false;
        for(const FString& Label:Labels) if(Label.Len()>3 || FCString::Atoi(*Label)>255) return false;
    }
    OutAddress=FString::Printf(TEXT("unreal://%s:%d"),*Host,Port);return true;
}

void UMCGameplayMenuWidget::HandleAction(EMCGameplayMenuAction Action)
{
    if(IsPreviewing() || bCloseRequested) return;
    auto* Steam=MCGameplayMenuPrivate::Sessions(this);
    switch(Action) {
    case EMCGameplayMenuAction::Resume:bCloseRequested=true;OnResumeRequested.Broadcast();break;
    case EMCGameplayMenuAction::Emotes:bCloseRequested=true;OnEmotesRequested.Broadcast();break;
    case EMCGameplayMenuAction::Settings:ShowPage(EPage::Settings);break;
    case EMCGameplayMenuAction::Credits:ShowPage(EPage::Credits);break;
    case EMCGameplayMenuAction::Leave:ShowPage(EPage::Leave);break;
    case EMCGameplayMenuAction::Exit:ShowPage(EPage::Exit);break;
    case EMCGameplayMenuAction::Back:RequestBack();break;
    case EMCGameplayMenuAction::ConfirmLeave:bCloseRequested=true;OnLeaveRequested.Broadcast();break;
    case EMCGameplayMenuAction::ConfirmExit:bCloseRequested=true;OnExitRequested.Broadcast();break;
    case EMCGameplayMenuAction::ApplySettings:ApplySettings();break;
    case EMCGameplayMenuAction::KeepSettings:
        if(!bVideoConfirmationPending) break;
        bVideoConfirmationPending=false;
        if(auto* Settings=UGameUserSettings::GetGameUserSettings()) {Settings->ConfirmVideoMode();Settings->SaveSettings();}
        ShowPage(EPage::Settings);SetStatusKey(TEXT("UI.Menu.SettingsSaved"),LOCTEXT("SettingsSaved","Настройки применены и сохранены."));break;
    case EMCGameplayMenuAction::RevertSettings:
        RevertVideoSettings();ShowPage(EPage::Settings);SetStatusKey(TEXT("UI.Menu.VideoRestored"),LOCTEXT("VideoRestored","Предыдущий режим экрана восстановлен."));break;
    case EMCGameplayMenuAction::Host:if(!Steam || !Steam->IsBusy()) OnHostRequested.Broadcast();break;
    case EMCGameplayMenuAction::FindRooms:if(Steam && !Steam->IsBusy()) Steam->FindRooms();break;
    case EMCGameplayMenuAction::JoinRoom:
        if(Steam && !Steam->IsBusy() && RoomPicker && RoomPicker->GetSelectedIndex()!=INDEX_NONE) Steam->JoinRoom(RoomPicker->GetSelectedIndex());
        else SetStatusKey(TEXT("UI.Menu.ChooseRoom"),LOCTEXT("ChooseRoom","Сначала найди и выбери комнату."));break;
    case EMCGameplayMenuAction::Invite:if(Steam && !Steam->IsBusy()) Steam->InviteFriends();break;
    case EMCGameplayMenuAction::JoinAddress:
        if(AddressBox && (!Steam || !Steam->IsBusy())) {
            FString Address;if(NormalizeJoinAddress(AddressBox->GetText().ToString(),Address)) OnJoinAddressRequested.Broadcast(Address);
            else SetStatusKey(TEXT("UI.Menu.InvalidAddress"),LOCTEXT("InvalidAddress","Введи IP или имя компьютера, при необходимости с портом. Например: 192.168.1.10:7777."));
        }break;
    }
}
void UMCGameplayMenuWidget::RequestBack()
{
    if(IsPreviewing() || bCloseRequested) return;
    if(bVideoConfirmationPending) {RevertVideoSettings();ShowPage(EPage::Settings);return;}
    const EPage Root=bConnectionRoot?EPage::Connection:EPage::Pause;
    if(Page!=Root) {ShowPage(Root);return;}
    bCloseRequested=true;
    if(bConnectionRoot) OnConnectionCloseRequested.Broadcast();else OnResumeRequested.Broadcast();
}
void UMCGameplayMenuWidget::FocusMenu()
{
    if(IsPreviewing() || bCloseRequested || !GetOwningPlayer()) return;
    for(UWidget* Target:FocusTargets) if(MCGameplayMenuPrivate::WithinActivePage(Target,Pages)) {Target->SetUserFocus(GetOwningPlayer());if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("GameplayMenuScroll")))) Scroll->ScrollWidgetIntoView(Target,false,EDescendantScrollDestination::IntoView,8);return;}
    SetUserFocus(GetOwningPlayer());
}
bool UMCGameplayMenuWidget::MoveFocus(int32 Direction)
{
    if(IsPreviewing() || !GetOwningPlayer() || FocusTargets.IsEmpty()) return false;
    int32 Index=FocusTargets.IndexOfByPredicate([&](const auto& Item){return Item && (Item->HasUserFocus(GetOwningPlayer()) || Item->HasFocusedDescendants());});
    if(FocusTargets.IsValidIndex(Index)) {
        if(const auto* Picker=Cast<UComboBoxString>(FocusTargets[Index]);Picker && Picker->IsOpen()) return false;
        if(Cast<UEditableTextBox>(FocusTargets[Index])) return false;
    }
    for(int32 Attempt=0;Attempt<FocusTargets.Num();++Attempt) {
        Index=(Index+Direction+FocusTargets.Num())%FocusTargets.Num();auto* Target=FocusTargets[Index].Get();
        if(MCGameplayMenuPrivate::WithinActivePage(Target,Pages)) {Target->SetUserFocus(GetOwningPlayer());if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("GameplayMenuScroll")))) Scroll->ScrollWidgetIntoView(Target,true,EDescendantScrollDestination::IntoView,8);return true;}
    }
    return false;
}

void UMCGameplayMenuWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);UpdatePresentationLayout(Geometry.GetLocalSize());if(IsPreviewing()) return;
    UWidget* Focused=nullptr;
    for(UWidget* Target:FocusTargets) if(Target && (Target->HasUserFocus(GetOwningPlayer()) || Target->HasFocusedDescendants())) {Focused=Target;break;}
    if(LastFocusTarget.Get()!=Focused) {LastFocusTarget=Focused;ApplyTheme();}
    if(bVideoConfirmationPending) {
        if(FPlatformTime::Seconds()>=VideoConfirmationEndsAt) {
            RevertVideoSettings();ShowPage(EPage::Settings);SetStatusKey(TEXT("UI.Menu.VideoRestored"),LOCTEXT("VideoRestored","Предыдущий режим экрана восстановлен."));
        } else {
            const FText Pattern=Copy(TEXT("UI.Menu.VideoCountdown"),LOCTEXT("VideoCountdown","Автоматический возврат: {Seconds} сек."));
            SetStatus(UMCUIDataLibrary::FormatTextForCulture(Pattern,FFormatNamedArguments{{TEXT("Seconds"),UMCUIDataLibrary::NumberForCulture(FMath::CeilToInt(VideoConfirmationEndsAt-FPlatformTime::Seconds()))}}));
        }
    }
    RefreshAccumulator+=DeltaSeconds;if(RefreshAccumulator<.2f) return;RefreshAccumulator=0;
    if(Page==EPage::Connection) RefreshConnection();
    if(Page==EPage::Pause) UpdatePageCopy();
}
FReply UMCGameplayMenuWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(IsPreviewing()) return Super::NativeOnPreviewKeyDown(Geometry,Event);
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right || (bConnectionRoot && Key==EKeys::F2) || (!bConnectionRoot && Key==EKeys::Gamepad_Special_Right)) {
        if(!Event.IsRepeat()) RequestBack();return FReply::Handled();
    }
    if((Key==EKeys::Up || Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Down) &&
        MoveFocus(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up?-1:1)) return FReply::Handled();
    if(Key==EKeys::Gamepad_FaceButton_Bottom && !Event.IsRepeat()) {
        for(UMCGameplayMenuActionButton* Button:Buttons) if(MCGameplayMenuPrivate::WithinActivePage(Button,Pages) && Button->HasUserFocus(GetOwningPlayer())) {Button->OnClicked.Broadcast();return FReply::Handled();}
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCGameplayMenuWidget::NativeOnAnalogValueChanged(const FGeometry& Geometry,const FAnalogInputEvent& Event)
{
    if(!IsPreviewing() && Event.GetKey()==EKeys::Gamepad_LeftY && FMath::Abs(Event.GetAnalogValue())>.55f) {
        if(FPlatformTime::Seconds()<NextStickNavigationAt) return FReply::Handled();
        if(MoveFocus(Event.GetAnalogValue()>0?-1:1)) {NextStickNavigationAt=FPlatformTime::Seconds()+.22;return FReply::Handled();}
    }
    return Super::NativeOnAnalogValueChanged(Geometry,Event);
}
UWidget* UMCGameplayMenuWidget::GeneratePickerItem(FString Item)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(Item));
    Label->SetAutoWrapText(true);Label->SetWrapTextAt(340);Label->SetFont(GetResolvedTheme()->MakeGameplayFont(20));Label->SetColorAndOpacity(GetResolvedTheme()->Palette.Ink);GeneratedPickerLabels.Add(Label);return Label;
}

void UMCGameplayMenuWidget::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();
    if(ModePicker && QualityPicker && ModePicker->GetOptionCount()>0 && QualityPicker->GetOptionCount()>0)
        RefreshSettingsChoiceCopy(ModePicker->GetSelectedIndex(),QualityPicker->GetSelectedIndex(),QualityPicker->GetOptionCount()>5);
    RefreshStatusCopy();
}

void UMCGameplayMenuWidget::ApplyTheme()
{
    const auto* ThemeData=GetResolvedTheme();if(!ThemeData || !WidgetTree) return;const auto& P=ThemeData->Palette;
    if(Backdrop) {Backdrop->SetBrush(FSlateRoundedBoxBrush(P.Backdrop,0));Backdrop->SetBrushColor(FLinearColor::White);}
    BindStyle(TEXT("GameplayMenuCard"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    BindStyle(TEXT("GameplayMenuHeader"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    if(Card) Card->SetPadding(FMargin(20,18));
    const float PanelWidth=Page==EPage::Settings?840:Page==EPage::Credits?760:Page==EPage::Pause?500:640;
    if(auto* Note=WidgetTree->FindWidget(TEXT("MenuNetworkNote"))) Note->SetVisibility(Page==EPage::Pause?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    UpdatePageCopy();TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);
    for(auto* Widget:Widgets) {
        if(auto* Label=Cast<UTextBlock>(Widget)) {
            const bool bButton=Label->GetName().EndsWith(TEXT("Label")) && Cast<UBorder>(Label->GetParent()) && Cast<UButton>(Label->GetParent()->GetParent());
            const bool PickerCaption=Cast<USizeBox>(Label->GetParent()) && Cast<UHorizontalBox>(Label->GetParent()->GetParent());
            Label->SetAutoWrapText(true);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
            const bool NetworkNote=Label->GetFName()==TEXT("MenuNetworkNote");
            Label->SetFont(ThemeData->MakeGameplayFont(Label==Heading?30:Label==Hint?14:Label==Status || NetworkNote?16:bButton?22:PickerCaption?21:18,bButton || Label==Heading || PickerCaption));
            if(NetworkNote || Label==Hint) Label->SetWrapTextAt(PanelWidth-40.f);
            Label->SetColorAndOpacity(Label==Heading?(WidgetTree->FindWidget(TEXT("GameplayMenuHeader"))?P.Ink:P.OnMarine):Label==Hint?P.OnMarine:Label==Status?P.Mint:bButton?P.ButtonText:PickerCaption?P.Ink:P.OnMarine);
            Label->SetShadowOffset(FVector2D::ZeroVector);
        }
        if(auto* Button=Cast<UMCGameplayMenuActionButton>(Widget)) {
            const bool Primary=Button->MenuAction==EMCGameplayMenuAction::Resume || Button->MenuAction==EMCGameplayMenuAction::ApplySettings || Button->MenuAction==EMCGameplayMenuAction::KeepSettings;
            const bool Danger=Button->MenuAction==EMCGameplayMenuAction::Exit || Button->MenuAction==EMCGameplayMenuAction::ConfirmLeave || Button->MenuAction==EMCGameplayMenuAction::ConfirmExit;
            const FLinearColor Normal=GetOwningPlayer() && Button->HasUserFocus(GetOwningPlayer())?P.ButtonHover:Primary?P.Mint:Danger?P.Cost:P.Button;
            MCUIVisualStyle::Button(Button,ThemeData,Normal);
            if(auto* Inset=Cast<UBorder>(Button->GetContent())) {
                Inset->SetPadding(FMargin(16,10));Inset->SetVerticalAlignment(VAlign_Center);
                if(auto* ContentSlot=Cast<UButtonSlot>(Inset->Slot)) {ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);ContentSlot->SetPadding(FMargin(0));}
                if(auto* Label=Cast<UTextBlock>(Inset->GetContent())) {
                    const bool ActionRow=Cast<UHorizontalBox>(Button->GetParent())!=nullptr;
                    Label->SetWrapTextAt(FMath::Max(160.f,ActionRow?(PanelWidth-52.f)*.5f-52.f:PanelWidth-94.f));
                }
            }
        } else if(auto* Picker=Cast<UComboBoxString>(Widget)) {
            auto Style=Picker->GetWidgetStyle();Style.ComboButtonStyle.ButtonStyle.SetNormal(FSlateRoundedBoxBrush(P.SurfaceAlt,8,P.Border,ThemeData->OutlineWidth));
            Style.ComboButtonStyle.ButtonStyle.SetHovered(FSlateRoundedBoxBrush(P.SurfaceAlt,8,P.Focus,ThemeData->OutlineWidth));
            Style.ComboButtonStyle.ButtonStyle.SetPressed(FSlateRoundedBoxBrush(P.SurfaceAlt,8,P.Focus,ThemeData->OutlineWidth));
            Style.ComboButtonStyle.DownArrowImage.TintColor=P.Marine;
            Style.ComboButtonStyle.SetMenuBorderBrush(FSlateRoundedBoxBrush(P.Surface,8));Picker->SetWidgetStyle(Style);
            auto ItemStyle=Picker->GetItemStyle();
            ItemStyle.SetTextColor(P.Ink);ItemStyle.SetSelectedTextColor(P.Ink);
            ItemStyle.SetEvenRowBackgroundBrush(FSlateRoundedBoxBrush(P.Surface,4));ItemStyle.SetOddRowBackgroundBrush(FSlateRoundedBoxBrush(P.Surface,4));
            ItemStyle.SetEvenRowBackgroundHoveredBrush(FSlateRoundedBoxBrush(P.ButtonHover,4));ItemStyle.SetOddRowBackgroundHoveredBrush(FSlateRoundedBoxBrush(P.ButtonHover,4));
            ItemStyle.SetActiveBrush(FSlateRoundedBoxBrush(P.ButtonPressed,4));ItemStyle.SetInactiveBrush(FSlateRoundedBoxBrush(P.ButtonPressed,4));
            ItemStyle.SetActiveHoveredBrush(FSlateRoundedBoxBrush(P.ButtonHover,4));ItemStyle.SetInactiveHoveredBrush(FSlateRoundedBoxBrush(P.ButtonHover,4));
            ItemStyle.SetSelectorFocusedBrush(FSlateRoundedBoxBrush(P.SurfaceAlt,4,P.Focus,ThemeData->OutlineWidth));Picker->SetItemStyle(ItemStyle);
        } else if(auto* Check=Cast<UCheckBox>(Widget)) {
            auto Style=Check->GetWidgetStyle();const bool Enabled=Check->GetIsEnabled();
            // Retain the authored check/indeterminate glyphs; only their ink changes.
            auto TintGlyph=[&](FSlateBrush& Brush,const FLinearColor& Color){Brush.TintColor=Enabled?Color:P.Muted;};
            TintGlyph(Style.UncheckedImage,P.Ink);TintGlyph(Style.UncheckedHoveredImage,P.Focus);TintGlyph(Style.UncheckedPressedImage,P.ButtonText);
            TintGlyph(Style.CheckedImage,P.ButtonText);TintGlyph(Style.CheckedHoveredImage,P.ButtonText);TintGlyph(Style.CheckedPressedImage,P.ButtonText);
            TintGlyph(Style.UndeterminedImage,P.ButtonText);TintGlyph(Style.UndeterminedHoveredImage,P.ButtonText);TintGlyph(Style.UndeterminedPressedImage,P.ButtonText);
            const FVector2D CheckSize(28,28);
            for(FSlateBrush* Glyph:{&Style.UncheckedImage,&Style.UncheckedHoveredImage,&Style.UncheckedPressedImage,&Style.CheckedImage,&Style.CheckedHoveredImage,&Style.CheckedPressedImage,&Style.UndeterminedImage,&Style.UndeterminedHoveredImage,&Style.UndeterminedPressedImage}) Glyph->ImageSize=CheckSize;
            Style.SetBackgroundImage(FSlateRoundedBoxBrush(Enabled?P.Button:P.ButtonDisabled,4,P.Border,ThemeData->OutlineWidth,CheckSize));
            Style.SetBackgroundHoveredImage(FSlateRoundedBoxBrush(Enabled?P.ButtonHover:P.ButtonDisabled,4,P.Focus,ThemeData->OutlineWidth,CheckSize));
            Style.SetBackgroundPressedImage(FSlateRoundedBoxBrush(Enabled?P.ButtonPressed:P.ButtonDisabled,4,P.Focus,ThemeData->OutlineWidth,CheckSize));
            Style.SetForegroundColor(P.Ink);Style.SetHoveredForegroundColor(P.Ink);Style.SetPressedForegroundColor(P.Ink);
            Style.SetCheckedForegroundColor(P.Ink);Style.SetCheckedHoveredForegroundColor(P.Ink);Style.SetCheckedPressedForegroundColor(P.Ink);Style.SetUndeterminedForegroundColor(P.Ink);
            Style.SetBorderBackgroundColor(Enabled?P.Button:P.ButtonDisabled);Check->SetWidgetStyle(Style);
        } else if(auto* Edit=Cast<UEditableTextBox>(Widget)) {
            auto Style=Edit->GetWidgetStyle();Style.SetFont(ThemeData->MakeGameplayFont(17));
            Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(P.SurfaceAlt,8,P.Border,ThemeData->OutlineWidth));
            Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(P.SurfaceAlt,8,P.Focus,ThemeData->OutlineWidth));
            Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(P.SurfaceAlt,8,P.Focus,ThemeData->OutlineWidth));
            Edit->SetWidgetStyle(Style);
            // UE 5.8 forwards the argument address to Slate. Rebind to the
            // persistent widget-owned copy before this local style expires.
            Edit->SetWidgetStyle(Edit->GetWidgetStyle());
            Edit->SetForegroundColor(P.Ink);
        }
    }
    for(UTextBlock* Label:GeneratedPickerLabels) if(Label) {Label->SetFont(ThemeData->MakeGameplayFont(20));Label->SetColorAndOpacity(P.Ink);}
    if(auto* Icon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("GameplayMenuTooth")))) {Icon->Tint=P.Accent;Icon->InkTint=P.Ink;Icon->InvalidateLayoutAndVolatility();}
    Super::ApplyTheme();
    bLayoutDirty=true;UpdatePresentationLayout();
}
void UMCGameplayMenuWidget::ApplyEditorPreview(float Time)
{
    if(bVideoConfirmationPending) RevertVideoSettings();bEditorFixture=true;bCloseRequested=false;
    BuildPresentationTree();for(UMCGameplayMenuActionButton* Button:Buttons) if(Button) Button->SetIsEnabled(true);
    EPage Fixture=EPage::Pause;bConnectionRoot=false;
    if(PreviewState==TEXT("Settings")) Fixture=EPage::Settings;
    else if(PreviewState==TEXT("Credits") || PreviewScenario==EMCGameplayUIPreviewScenario::LongText) Fixture=EPage::Credits;
    else if(PreviewState==TEXT("Leave")) Fixture=EPage::Leave;
    else if(PreviewState==TEXT("Exit")) Fixture=EPage::Exit;
    else if(PreviewState==TEXT("VideoConfirm")) Fixture=EPage::VideoConfirm;
    else if(PreviewState==TEXT("Connection") || PreviewState==TEXT("Pending") || PreviewScenario==EMCGameplayUIPreviewScenario::Pending) {Fixture=EPage::Connection;bConnectionRoot=true;}
    ShowPage(Fixture);
    if(Fixture==EPage::VideoConfirm) {
        const FString Culture=PreviewCulture;const auto Pattern=Copy(TEXT("UI.Menu.VideoCountdown"),LOCTEXT("VideoCountdown","Автоматический возврат: {Seconds} сек."));
        SetStatus(UMCUIDataLibrary::FormatTextForCulture(Pattern,FFormatNamedArguments{{TEXT("Seconds"),UMCUIDataLibrary::NumberForCulture(FMath::Clamp(PreviewVideoSeconds-FMath::FloorToInt(Time),0,15),Culture)}},Culture));
    } else if(!PreviewStatus.IsEmpty()) SetStatus(PreviewStatus);
    if(PreviewScenario==EMCGameplayUIPreviewScenario::Disabled || PreviewState==TEXT("Disabled"))
        for(UMCGameplayMenuActionButton* Button:Buttons) if(Button) Button->SetIsEnabled(false);
    SetRenderOpacity(PreviewScenario==EMCGameplayUIPreviewScenario::Empty || PreviewState==TEXT("Empty")?0.f:1.f);
}
void UMCGameplayMenuWidget::ResetEditorPreview()
{
    bEditorFixture=false;bCloseRequested=false;SetRenderOpacity(1);
    for(UMCGameplayMenuActionButton* Button:Buttons) if(Button) Button->SetIsEnabled(true);
}

#undef LOCTEXT_NAMESPACE
