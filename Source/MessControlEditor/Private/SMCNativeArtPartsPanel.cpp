#include "SMCNativeArtPartsPanel.h"
#include "MCVFXNativePresentationSettings.h"
#include "AssetRegistry/AssetData.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Misc/Paths.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    bool Supports(const FMCVFXNativePartDescriptor& D,FName Key)
    {return D.EditableProperties.Contains(Key);}
    FMCNativeArtControlView Control(const TCHAR* Key,const TCHAR* Label,EMCNativeArtControlKind Kind)
    {FMCNativeArtControlView C;C.ID=FName(Key);C.Label=FText::FromString(Label);C.Kind=Kind;C.bWritable=true;return C;}
}

TArray<FMCNativeArtPartView> MCNativeArtParts::BuildViews(const UMCVFXNativePresentationSettings* Draft,FMCNativeArtScope Scope)
{
    TArray<FMCNativeArtPartView> Result;if(!Draft)return Result;
    const FName Event=Scope.bPersistent?NAME_None:Scope.Event;
    for(const auto& D:UMCVFXNativePresentationSettings::DescribeParts(Draft->SourceID,Event))
    {
        const auto* P=Draft->FindPart(Event,D.PartID);if(!P)continue;
        FMCNativeArtPartView V;V.PartID=D.PartID;V.Label=D.Label;V.bProtectedGeometry=D.bProtectedGeometry;
        if(D.Kind==EMCVFXNativePartKind::PassiveLightning)V.Summary=NSLOCTEXT("MCNativeArt","LightningOriginalSummary","Настоящие две ленты используют один материал. Концы и seed определяет принятый игровой контакт; размер ниже меняет только толщину.");
        EMCPassiveFeedbackKind FeedbackKind;
        if(UMCVFXNativePresentationSettings::GetPassiveFeedbackKind(Draft->SourceID,Event,FeedbackKind))
            V.Summary=Supports(D,TEXT("Feedback.Scale"))?
                NSLOCTEXT("MCNativeArt","PassiveOriginalSummary","Исходные родные формы. Масштаб и цвет меняют только рисунок эффекта; событие, лечение, урон и игровые размеры определяет механика."):
                NSLOCTEXT("MCNativeArt","PassiveLinkSummary","Связь между настоящими принятыми точками. Концы и траектория защищены; доступны материал, цвет и косметический темп.");
        if(D.bProtectedGeometry)V.Summary=NSLOCTEXT("MCNativeArt","ProtectedShapeSummary","Настоящая игровая форма. Её размеры, положение и момент действия определяет механика.");
        if(D.bMaskedCutout)V.AvailabilityReason=NSLOCTEXT("MCNativeArt","MaskedSummary","Материал использует отсечение: прозрачность меняет границу видимых участков, а не мягкость растворения.");
        auto AddResource=[&](const TCHAR* Key,const TCHAR* Label,const FSoftObjectPath& Asset,UClass* Class)
        {
            if(!Asset.IsNull()){FMCNativeArtResourceView Source;Source.Role=FText::FromString(Label);Source.Asset=Asset;V.Sources.Add(Source);}
            if(Supports(D,FName(Key))){auto C=Control(Key,Label,EMCNativeArtControlKind::Resource);C.Resource=Asset;C.AllowedResourceClass=Class;V.Controls.Add(C);}
        };
        AddResource(TEXT("System"),TEXT("Niagara"),P->System.ToSoftObjectPath(),UNiagaraSystem::StaticClass());
        AddResource(TEXT("Material"),TEXT("Материал"),P->Material.ToSoftObjectPath(),UMaterialInterface::StaticClass());
        AddResource(TEXT("Mesh"),TEXT("Меш"),P->Mesh.ToSoftObjectPath(),UStaticMesh::StaticClass());
        if(Supports(D,TEXT("bEnabled"))){auto C=Control(TEXT("bEnabled"),TEXT("Показывать часть"),EMCNativeArtControlKind::Toggle);C.Toggle=P->bEnabled;V.Controls.Insert(C,0);}
        auto AddColor=[&](const TCHAR* Key,const TCHAR* Label,FLinearColor Value){if(Supports(D,FName(Key))){auto C=Control(Key,Label,EMCNativeArtControlKind::Color);C.Color=Value;V.Controls.Add(C);}};
        AddColor(TEXT("Tint"),TEXT("Цвет"),P->Tint);
        AddColor(TEXT("Cracks.CoreColor"),TEXT("Цвет трещин"),P->Cracks.CoreColor);
        AddColor(TEXT("Cracks.LipColor"),TEXT("Цвет кромок"),P->Cracks.LipColor);
        AddColor(TEXT("Cracks.RimColor"),TEXT("Цвет края щита"),P->Cracks.RimColor);
        auto AddScalar=[&](const TCHAR* Key,const TCHAR* Label,double Value)
        {
            if(!Supports(D,FName(Key)))return;
            auto C=Control(Key,Label,EMCNativeArtControlKind::Scalar);C.Scalar=Value;
            if(const auto* Bounds=D.NumericBounds.Find(FName(Key))){C.Min=Bounds->X;C.Max=Bounds->Y;}
            if(C.ID==TEXT("Feedback.DurationScale"))C.Hint=NSLOCTEXT("MCNativeArt","FeedbackClockHint","1 — исходное движение. Меняет только время родной анимации эффекта; не время действия пассивки.");
            V.Controls.Add(C);
        };
        AddScalar(TEXT("OpacityScale"),TEXT("Плотность"),P->OpacityScale);
        AddScalar(TEXT("SystemScale"),TEXT("Размер Niagara"),P->SystemScale);
        AddScalar(TEXT("Feedback.Scale"),D.Kind==EMCVFXNativePartKind::PassiveLightning?TEXT("Толщина молнии ×"):TEXT("Размер родных форм"),P->Feedback.Scale);
        AddScalar(TEXT("Feedback.DurationScale"),TEXT("Длительность движения ×"),P->Feedback.DurationScale);
        auto AddCurve=[&](const TCHAR* Key,const TCHAR* Label,const FSoftObjectPath& Asset){if(Supports(D,FName(Key))){auto C=Control(Key,Label,EMCNativeArtControlKind::Curve);C.Resource=Asset;C.AllowedResourceClass=UCurveFloat::StaticClass();V.Controls.Add(C);}};
        AddCurve(TEXT("OpacityCurve"),TEXT("Изменение плотности"),P->OpacityCurve.ToSoftObjectPath());
        AddCurve(TEXT("ExpansionCurve"),TEXT("Изменение размера"),P->ExpansionCurve.ToSoftObjectPath());
        // Whole typed Flame support includes the actual native SelfGlow field.
        // Flame/core renderers with no Tint binding deliberately get no color control.
        if(Supports(D,TEXT("Flame"))){auto C=Control(TEXT("Flame.SelfGlow"),TEXT("Свечение огня"),EMCNativeArtControlKind::Scalar);C.Scalar=P->Flame.SelfGlow;C.Min=0.;C.Max=4.;V.Controls.Add(C);}
        Result.Add(MoveTemp(V));
    }
    return Result;
}

bool MCNativeArtParts::ApplyEdit(const FMCNativeArtEdit& Edit,const UMCVFXNativePresentationSettings* Draft,FMCVFXNativePartSettings& Out,FString& Error)
{
    Error.Reset();if(!Draft||Draft->SourceID!=Edit.SourceID){Error=TEXT("Контекст родной основы изменился. Выберите часть заново.");return false;}
    const auto Views=BuildViews(Draft,Edit.Scope);
    const auto* View=Views.FindByPredicate([&](const auto& V){return V.PartID==Edit.PartID;});
    const auto* C=View?View->Controls.FindByPredicate([&](const auto& V){return V.ID==Edit.ControlID&&V.Kind==Edit.Kind&&V.bWritable;}):nullptr;
    const auto* Part=Draft->FindPart(Edit.Scope.bPersistent?NAME_None:Edit.Scope.Event,Edit.PartID);
    if(!C||!Part||View->bProtectedGeometry){Error=TEXT("Эта настройка не поддерживается выбранной родной частью.");return false;}
    Out=*Part;const FName Key=Edit.ControlID;
    if(Key==TEXT("bEnabled"))Out.bEnabled=Edit.Toggle;
    else if(Key==TEXT("Material"))Out.Material=Edit.Resource;
    else if(Key==TEXT("System"))Out.System=Edit.Resource;
    else if(Key==TEXT("Mesh"))Out.Mesh=Edit.Resource;
    else if(Key==TEXT("OpacityCurve"))Out.OpacityCurve=Edit.Resource;
    else if(Key==TEXT("ExpansionCurve"))Out.ExpansionCurve=Edit.Resource;
    else if(Key==TEXT("Tint"))Out.Tint=Edit.Color;
    else if(Key==TEXT("Cracks.CoreColor"))Out.Cracks.CoreColor=Edit.Color;
    else if(Key==TEXT("Cracks.LipColor"))Out.Cracks.LipColor=Edit.Color;
    else if(Key==TEXT("Cracks.RimColor"))Out.Cracks.RimColor=Edit.Color;
    else if(Key==TEXT("OpacityScale"))Out.OpacityScale=float(Edit.Scalar);
    else if(Key==TEXT("SystemScale"))Out.SystemScale=float(Edit.Scalar);
    else if(Key==TEXT("Feedback.Scale"))Out.Feedback.Scale=float(Edit.Scalar);
    else if(Key==TEXT("Feedback.DurationScale"))Out.Feedback.DurationScale=float(Edit.Scalar);
    else if(Key==TEXT("Flame.SelfGlow"))Out.Flame.SelfGlow=float(Edit.Scalar);
    else {Error=TEXT("Для этой настройки ещё нет редактируемого элемента Composer.");return false;}
    return true;
}

void SMCNativeArtPartsPanel::Construct(const FArguments& Args)
{
    Context=Args._Context;
    TSharedRef<SVerticalBox> Parts=SNew(SVerticalBox);
    Parts->AddSlot().AutoHeight().Padding(0,0,0,8)
        [SNew(STextBlock).Text(Context.Scope.bPersistent?NSLOCTEXT("MCNativeArt","PersistentParts","Состояние объекта"):NSLOCTEXT("MCNativeArt","NativeParts","Родная основа"))
            .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))];
    if(Context.bCanEdit)Parts->AddSlot().AutoHeight().Padding(0,0,0,8)
        [SNew(STextBlock).Text(NSLOCTEXT("MCNativeArt","DraftOnly","Настройки меняют рабочую копию. Общие исходные ресурсы сохраняются отдельно."))
            .ColorAndOpacity(FLinearColor(.63f,.76f,.79f)).AutoWrapText(true)];
    if(!Context.bCanEdit && !Context.Reason.IsEmpty())
        Parts->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text(Context.Reason).AutoWrapText(true)];
    for(const auto& Part:Context.Parts)Parts->AddSlot().AutoHeight().Padding(0,0,0,8)[PartCard(Part)];
    if(Context.Parts.IsEmpty())
        Parts->AddSlot().AutoHeight()[SNew(STextBlock)
            .Text(NSLOCTEXT("MCNativeArt","NoDeclaredParts","Этот renderer ещё не объявил редактируемые родные части. Слои Niagara доступны отдельно."))
            .AutoWrapText(true)];
    ChildSlot[Parts];
}

TSharedRef<SWidget> SMCNativeArtPartsPanel::PartCard(const FMCNativeArtPartView& Part)
{
    TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);
    Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(Part.Label).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle")).AutoWrapText(true)];
    if(!Part.Summary.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text(Part.Summary).AutoWrapText(true)];
    if(Part.bProtectedGeometry)
        Rows->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock)
            .Text(NSLOCTEXT("MCNativeArt","Protected","Привязка к игре · форма и момент действия защищены"))
            .ColorAndOpacity(FLinearColor(.68f,.79f,.82f)).AutoWrapText(true)];
    for(const auto& Source:Part.Sources)
    {
        const FText Name=FText::FromString(Source.Asset.IsNull()?FPaths::GetCleanFilename(Source.GeometrySource):Source.Asset.GetAssetName());
        Rows->AddSlot().AutoHeight().Padding(0,3)[SNew(SButton).ContentPadding(FMargin(6,4))
            .ToolTipText(NSLOCTEXT("MCNativeArt","OpenSource","Открыть настоящий ресурс. Его редактирование меняет общий исходник, а выбор другого ресурса ниже меняет только эту часть."))
            .OnClicked_Lambda([Open=Context.OpenSource,Source]{if(Open)Open(Source);return FReply::Handled();})
            [SNew(STextBlock).Text(FText::Format(NSLOCTEXT("MCNativeArt","SourceName","{0} · {1}"),Source.Role,Name)).AutoWrapText(true)]];
    }
    for(const auto& Control:Part.Controls)Rows->AddSlot().AutoHeight().Padding(0,5)[ControlRow(Part.PartID,Control)];
    if(!Part.AvailabilityReason.IsEmpty())Rows->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Text(Part.AvailabilityReason).AutoWrapText(true)];
    Rows->AddSlot().AutoHeight().Padding(0,7,0,0)[SNew(SButton)
        .Text(NSLOCTEXT("MCNativeArt","ResetPart","Вернуть исходный вид этой части"))
        .Visibility(Part.bProtectedGeometry?EVisibility::Collapsed:EVisibility::Visible)
        .IsEnabled(Context.bCanEdit && !Part.bProtectedGeometry && bool(Context.ResetPart))
        .OnClicked_Lambda([Reset=Context.ResetPart,Status=Context.ShowStatus,ID=Part.PartID]{FString Error;if(Reset&&!Reset(ID,Error)&&Status)Status(Error);return FReply::Handled();})];
    static const FSlateRoundedBoxBrush Card(FLinearColor::White,7.f,FLinearColor(.12f,.22f,.25f),1.f);
    TSharedRef<SWidget> Body=SNew(SBorder).BorderImage(&Card).BorderBackgroundColor(FLinearColor(.025f,.055f,.068f)).Padding(10)[Rows];
    if(Part.bProtectedGeometry)return SNew(SExpandableArea).InitiallyCollapsed(true)
        .HeaderContent()[SNew(STextBlock).Text(Part.Label).AutoWrapText(true).ColorAndOpacity(FLinearColor(.63f,.76f,.79f))]
        .BodyContent()[Body];
    return Body;
}

FMCNativeArtEdit SMCNativeArtPartsPanel::MakeEdit(FName PartID,const FMCNativeArtControlView& C)const
{
    FMCNativeArtEdit E;E.SourceID=Context.SourceID;E.DraftInstanceID=Context.DraftInstanceID;E.Scope=Context.Scope;
    E.PartID=PartID;E.ControlID=C.ID;E.Kind=C.Kind;E.Scalar=C.Scalar;E.Toggle=C.Toggle;E.Color=C.Color;E.Resource=C.Resource;return E;
}
void SMCNativeArtPartsPanel::Commit(FMCNativeArtEdit Edit)
{
    if(!Context.bCanEdit || !Context.Commit)return;
    FString Error;if(!Context.Commit(Edit,Error)&&Context.ShowStatus)Context.ShowStatus(Error);
}

TSharedRef<SWidget> SMCNativeArtPartsPanel::ControlRow(FName PartID,const FMCNativeArtControlView& C)
{
    TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
    Row->AddSlot().FillWidth(.45f).VAlign(VAlign_Center)[SNew(STextBlock).Text(C.Label).ToolTipText(C.Hint).AutoWrapText(true)];
    const bool Writable=Context.bCanEdit && C.bWritable;
    const TWeakPtr<SMCNativeArtPartsPanel> Weak=SharedThis(this);auto E=MakeEdit(PartID,C);
    if(C.Kind==EMCNativeArtControlKind::Scalar)
        Row->AddSlot().FillWidth(.55f)[SNew(SSpinBox<double>).Value(C.Scalar).MinValue(C.Min).MaxValue(C.Max)
            .IsEnabled(Writable).ToolTipText(C.Hint)
            .OnValueCommitted_Lambda([Weak,E](double Value,ETextCommit::Type)mutable{if(auto Panel=Weak.Pin()){E.Scalar=Value;Panel->Commit(E);}})];
    else if(C.Kind==EMCNativeArtControlKind::Toggle)
        Row->AddSlot().FillWidth(.55f)[SNew(SCheckBox).IsChecked(C.Toggle?ECheckBoxState::Checked:ECheckBoxState::Unchecked).IsEnabled(Writable)
            .OnCheckStateChanged_Lambda([Weak,E](ECheckBoxState Value)mutable{if(auto Panel=Weak.Pin()){E.Toggle=Value==ECheckBoxState::Checked;Panel->Commit(E);}})];
    else if(C.Kind==EMCNativeArtControlKind::Color)
        Row->AddSlot().FillWidth(.55f)[SNew(SColorBlock).Color(C.Color).Size(FVector2D(60,23)).IsEnabled(Writable)
            .OnMouseButtonDown_Lambda([Weak,E](const FGeometry&,const FPointerEvent&)
            {
                if(auto Panel=Weak.Pin()) {
                    FColorPickerArgs Args;Args.InitialColor=E.Color;Args.bOnlyRefreshOnOk=true;Args.bUseAlpha=false;Args.ParentWidget=Panel;
                    Args.OnColorCommitted=FOnLinearColorValueChanged::CreateLambda([Weak,E](FLinearColor Value)mutable
                        {if(auto Current=Weak.Pin()){E.Color=Value;Current->Commit(E);}});
                    OpenColorPicker(Args);
                }
                return FReply::Handled();
            })];
    else if(C.Kind==EMCNativeArtControlKind::Resource || C.Kind==EMCNativeArtControlKind::Curve)
        Row->AddSlot().FillWidth(.55f)[SNew(SObjectPropertyEntryBox).AllowedClass(C.AllowedResourceClass)
            .ObjectPath(C.Resource.ToString()).IsEnabled(Writable)
            .OnObjectChanged_Lambda([Weak,E](const FAssetData& Asset)mutable{if(auto Panel=Weak.Pin()){E.Resource=Asset.GetSoftObjectPath();Panel->Commit(E);}})];
    else
        Row->AddSlot().FillWidth(.55f)[SNew(STextBlock).Text(C.ReadOnlyValue).ColorAndOpacity(FLinearColor(.68f,.79f,.82f)).AutoWrapText(true)];
    return Row;
}
