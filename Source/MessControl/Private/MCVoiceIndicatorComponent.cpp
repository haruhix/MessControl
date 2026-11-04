#include "MCVoiceIndicatorComponent.h"
#include "MCVoiceChatSubsystem.h"
#include "MCGameplayUIFactory.h"
#include "MCToothCharacter.h"
#include "MCUIVisualStyle.h"
#include "MCWorldUIShapes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CapsuleComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerState.h"
#include "Rendering/DrawElements.h"

UMCVoiceIndicatorWidget::UMCVoiceIndicatorWidget() {bUseVersionedWorldPresentation=true;}

void UMCVoiceIndicatorWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(false);BuildPresentationTree();ApplyTheme();
    SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UMCVoiceIndicatorWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(!WidgetTree->RootWidget)
    {
        auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("VoiceIndicatorRoot"));
        Size->SetWidthOverride(92);Size->SetHeightOverride(34);WidgetTree->RootWidget=Size;
        auto* Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("VoiceIndicatorPanel"));Panel->SetPadding(FMargin(8,5));Size->SetContent(Panel);
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("VoiceIndicatorRow"));Panel->SetContent(Row);
        auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(24);IconSize->SetHeightOverride(24);
        auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("VoiceMicrophone"));Icon->SetOpacity(0);IconSize->SetContent(Icon);
        Row->AddChildToHorizontalBox(IconSize)->SetPadding(FMargin(0,0,7,0));
        for(int32 Index=0;Index<3;++Index)
        {
            auto* BarSize=WidgetTree->ConstructWidget<USizeBox>();BarSize->SetWidthOverride(7);BarSize->SetHeightOverride(22);
            auto* Bar=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("VoiceBar%d"),Index)));
            Bar->SetPadding(FMargin(0));Bar->SetRenderTransformPivot(FVector2D(.5f,1));BarSize->SetContent(Bar);
            auto* BarSlot=Row->AddChildToHorizontalBox(BarSize);BarSlot->SetPadding(FMargin(2,0));BarSlot->SetVerticalAlignment(VAlign_Bottom);
        }
    }
    Microphone=WidgetTree->FindWidget<UImage>(TEXT("VoiceMicrophone"));Bars.Reset();
    for(int32 Index=0;Index<3;++Index) Bars.Add(WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("VoiceBar%d"),Index))));
    BuildWorldPresentationTree();
}
void UMCVoiceIndicatorWidget::BuildWorldPresentationTree()
{
    if(!bUseVersionedWorldPresentation || !WidgetTree) return;
    auto* NewRoot=WidgetTree->FindWidget<USizeBox>(TEXT("WorldVoiceV2Root"));
    if(!NewRoot) {
        NewRoot=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldVoiceV2Root"),92,34);
        auto* Plate=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldVoiceV2Plate"),GetNewWorldPresentationTheme(),EMCUIColorToken::Cream,FMargin(8,5));NewRoot->SetContent(Plate);
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WorldVoiceV2Row"));Plate->SetContentForSlot(TEXT("Content"),Row);
        auto* IconBounds=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldVoiceV2MicBounds"),24,24);IconBounds->SetHeightOverride(24);
        auto* Mic=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("WorldVoiceV2Mic"));Mic->SetOpacity(0);IconBounds->SetContent(Mic);
        Row->AddChildToHorizontalBox(IconBounds)->SetPadding(FMargin(0,0,7,0));
        for(int32 Index=0;Index<3;++Index) {
            auto* BarBounds=MCWorldUIShapes::Bounds(WidgetTree,FName(*FString::Printf(TEXT("WorldVoiceV2BarBounds%d"),Index)),7,22);BarBounds->SetHeightOverride(22);
            auto* Bar=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("WorldVoiceV2Bar%d"),Index)));
            Bar->SetPadding(FMargin(0));Bar->SetRenderTransformPivot(FVector2D(.5f,1));BarBounds->SetContent(Bar);
            auto* BarCell=Row->AddChildToHorizontalBox(BarBounds);BarCell->SetPadding(FMargin(2,0));BarCell->SetVerticalAlignment(VAlign_Bottom);
        }
    }
    InstallWorldPresentation(NewRoot);
    NewMicrophone=WidgetTree->FindWidget<UImage>(TEXT("WorldVoiceV2Mic"));NewBars.Reset();
    for(int32 Index=0;Index<3;++Index) NewBars.Add(WidgetTree->FindWidget<UBorder>(FName(*FString::Printf(TEXT("WorldVoiceV2Bar%d"),Index))));
}

void UMCVoiceIndicatorWidget::ApplyTheme()
{
    BuildPresentationTree();
    BindStyle(TEXT("VoiceIndicatorPanel"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    for(int32 Index=0;Index<3;++Index) BindStyle(FName(*FString::Printf(TEXT("VoiceBar%d"),Index)),EMCUIColorToken::Accent,EMCUIStyleTarget::Background);
    Super::ApplyTheme();
    // A designer can assign any texture/material to replace the default vector mic.
    if(Microphone) {Microphone->SetColorAndOpacity(GetResolvedTheme()->Palette.Accent);Microphone->SetOpacity(Microphone->GetBrush().GetResourceObject()?1.f:0.f);}
    if(const auto* NewTheme=GetNewWorldPresentationTheme()) {
        for(const auto& Bar:NewBars) if(Bar) {Bar->SetBrush(FSlateRoundedBoxBrush(NewTheme->Palette.Marine,2.f));Bar->SetBrushColor(FLinearColor::White);}
        if(NewMicrophone) {
            if(Microphone) NewMicrophone->SetBrush(Microphone->GetBrush());
            NewMicrophone->SetColorAndOpacity(NewTheme->Palette.Marine);NewMicrophone->SetOpacity(NewMicrophone->GetBrush().GetResourceObject()?1.f:0.f);
        }
    }
    SetVoiceState(bPresentedSpeaking,PresentedLevel,PresentedVoiceTime);
}
void UMCVoiceIndicatorWidget::SetVoiceState(bool bSpeaking,float Level,float Time)
{
    bPresentedSpeaking=bSpeaking;PresentedLevel=Level;PresentedVoiceTime=Time;
    if(Bars.IsEmpty()) BuildPresentationTree();
    SetVisibility(bSpeaking?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    const float Envelope=FMath::Clamp(Level,0.f,1.f);
    for(int32 Index=0;Index<Bars.Num();++Index) if(Bars[Index])
        Bars[Index]->SetRenderScale(FVector2D(1,FMath::Clamp(.18f+Envelope*(.55f+.27f*FMath::Sin(Time*15+Index*2)),.18f,1.f)));
    for(int32 Index=0;Index<NewBars.Num();++Index) if(NewBars[Index])
        NewBars[Index]->SetRenderScale(FVector2D(1,FMath::Clamp(.18f+Envelope*(.55f+.27f*FMath::Sin(Time*15+Index*2)),.18f,1.f)));
}
void UMCVoiceIndicatorWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);BuildPresentationTree();
    SetVoiceState(PreviewScenario!=EMCGameplayUIPreviewScenario::Empty,.65f+.25f*FMath::Sin(Time*4),Time);
}
int32 UMCVoiceIndicatorWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const
{
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,bParentEnabled);
    const auto* VisibleMic=IsNewWorldPresentation()?NewMicrophone.Get():Microphone.Get();
    if(!VisibleMic || VisibleMic->GetBrush().GetResourceObject()) return Top;
    const FGeometry IconGeometry=VisibleMic->GetCachedGeometry();const FVector2D S=IconGeometry.GetLocalSize()/FVector2D(24,24);
    const FLinearColor MicTint=IsNewWorldPresentation()?GetNewWorldPresentationTheme()->Palette.Marine:GetResolvedTheme()->Palette.Accent;
    const FLinearColor Color=MicTint*Style.GetColorAndOpacityTint();
    auto Line=[&](TArray<FVector2D> Points) {for(auto& P:Points) P*=S;FSlateDrawElement::MakeLines(Elements,Top+1,IconGeometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,2);};
    Line({{9,4},{9,11},{10,13},{14,13},{15,11},{15,4},{14,2},{10,2},{9,4}});
    Line({{5,10},{5,13},{8,17},{16,17},{19,13},{19,10}});
    Line({{12,17},{12,22}});Line({{8,22},{16,22}});
    return Top+1;
}

UMCVoiceIndicatorComponent::UMCVoiceIndicatorComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;PrimaryComponentTick.TickInterval=.05f;
    SetWidgetSpace(EWidgetSpace::Screen);SetWidgetClass(UMCVoiceIndicatorWidget::StaticClass());SetDrawSize(FVector2D(92,34));
    SetDrawAtDesiredSize(true);SetPivot(FVector2D(.5f,1));SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);SetCanEverAffectNavigation(false);SetCastShadow(false);SetVisibility(false);
}
void UMCVoiceIndicatorComponent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer) {SetComponentTickEnabled(false);return;}
    if(GetWidgetClass()==UMCVoiceIndicatorWidget::StaticClass()) SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("VoiceIndicator"),UMCVoiceIndicatorWidget::StaticClass()));
    if(auto* Hero=Cast<AMCToothCharacter>(GetOwner())) AddTickPrerequisiteComponent(Hero->GetMesh());
}
void UMCVoiceIndicatorComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());auto* State=Hero?Hero->GetPlayerState():nullptr;
    auto* Voice=GetWorld() && GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UMCVoiceChatSubsystem>():nullptr;
    const bool Speaking=Voice && State && State->GetPawn()==Hero && Voice->IsTalking(State);
    SetVisibility(Speaking);
    if(Speaking && Hero)
    {
        const auto* Mesh=Hero->GetMesh();FName Head=Hero->RigBone(TEXT("gaze_head"));if(!Mesh->DoesSocketExist(Head)) Head=TEXT("head");
        const FVector Anchor=Mesh->DoesSocketExist(Head)?Mesh->GetSocketLocation(Head):Hero->GetActorLocation()+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        SetWorldLocation(Anchor+FVector(0,0,HeightAboveHead));
    }
    if(auto* Label=Cast<UMCVoiceIndicatorWidget>(GetUserWidgetObject())) Label->SetVoiceState(Speaking,Voice?Voice->GetPlayerVoiceLevel(State):0.f,float(FMath::Fmod(FPlatformTime::Seconds(),1024.)));
    Super::TickComponent(Dt,TickType,ThisTickFunction);
}
