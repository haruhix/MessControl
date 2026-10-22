#include "MCHubFrontEndEditorLibrary.h"

#include "MCHubWidget.h"
#include "MCMainMenuWidget.h"
#include "MCGameplayMenuWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCScoreboardWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"

namespace
{
bool Editable(UWidgetBlueprint* Blueprint)
{
    return IsInGameThread() && GEditor && !GEditor->PlayWorld && !IsRunningCommandlet()
        && IsValid(Blueprint) && Blueprint->ParentClass && Blueprint->WidgetTree
        && Blueprint->GeneratedClass && !Blueprint->GetPackage()->IsDirty();
}
}

bool UMCHubFrontEndEditorLibrary::RoutePlayButtonToHub(UWidgetBlueprint* Blueprint)
{
    if(!Editable(Blueprint) || Blueprint->GetPathName()!=TEXT("/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D")
        || !Cast<UButton>(Blueprint->WidgetTree->FindWidget(TEXT("PlayButton")))) return false;
    TStrongObjectPtr<UWidgetBlueprint> Guard(Blueprint);
    UK2Node_ComponentBoundEvent* Event=nullptr;
    for(UEdGraph* Graph:Blueprint->UbergraphPages) if(Graph)
        for(UEdGraphNode* Node:Graph->Nodes)
            if(auto* Candidate=Cast<UK2Node_ComponentBoundEvent>(Node);Candidate
                && Candidate->ComponentPropertyName==TEXT("PlayButton") && Candidate->DelegatePropertyName==TEXT("OnClicked"))
            {
                if(Event) return false;
                Event=Candidate;
            }
    UEdGraph* Graph=Event?Event->GetGraph():nullptr;
    auto* Then=Event?Event->FindPin(UEdGraphSchema_K2::PN_Then,EGPD_Output):nullptr;
    const auto* Schema=Graph?Cast<UEdGraphSchema_K2>(Graph->GetSchema()):nullptr;
    UFunction* Function=UMCMainMenuWidget::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UMCMainMenuWidget,PlayToHub));
    if(!Then || !Schema || !Function || !Function->HasAnyFunctionFlags(FUNC_Static)) return false;
    if(Then->LinkedTo.Num()==1)
        if(const auto* Call=Cast<UK2Node_CallFunction>(Then->LinkedTo[0]->GetOwningNode());Call && Call->GetTargetFunction()==Function) return true;
    const TArray<UEdGraphPin*> Previous=Then->LinkedTo;
    const FScopedTransaction Transaction(NSLOCTEXT("MCHubEditor","DirectHubPlay","Route Play directly to Hub"));
    Blueprint->Modify();Graph->Modify();Event->Modify();
    FGraphNodeCreator<UK2Node_CallFunction> CallCreator(*Graph);
    auto* Call=CallCreator.CreateNode();Call->SetFromFunction(Function);
    Call->NodePosX=Event->NodePosX+320;Call->NodePosY=Event->NodePosY;
    Call->NodeComment=TEXT("MC_PlayDirectHub_v1: entry only; shared Hub selector remains authoritative.");
    CallCreator.Finalize();
    FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);
    auto* Self=SelfCreator.CreateNode();Self->NodePosX=Call->NodePosX-120;Self->NodePosY=Call->NodePosY+150;SelfCreator.Finalize();
    auto* Context=Call->FindPin(TEXT("WorldContextObject"),EGPD_Input);
    auto* Execute=Call->GetExecPin();
    auto RollBack=[&] {
        Then->BreakAllPinLinks();
        for(auto* Pin:Previous) if(Pin) Then->MakeLinkTo(Pin);
        Graph->RemoveNode(Call);Graph->RemoveNode(Self);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    };
    if(!Context || !Execute || !Schema->TryCreateConnection(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),Context)) {RollBack();return false;}
    Schema->BreakPinLinks(*Then,true);
    if(!Schema->TryCreateConnection(Then,Execute)) {RollBack();return false;}
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error) {RollBack();return false;}
    Blueprint->MarkPackageDirty();
    UE_LOG(LogTemp,Display,TEXT("MC_HUB_PLAY Rewired PlayButton only: %s"),*Blueprint->GetPathName());
    return true;
}

bool UMCHubFrontEndEditorLibrary::AddPlayStatusPresentation(UWidgetBlueprint* Blueprint)
{
    if(!Editable(Blueprint) || Blueprint->GetPathName()!=TEXT("/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D")) return false;
    auto* Canvas=Blueprint->WidgetTree->FindWidget<UCanvasPanel>(TEXT("RootCanvas"));
    if(!Canvas) return false;
    if(auto* Existing=Blueprint->WidgetTree->FindWidget(TEXT("HubPlayStatus")))
        return Cast<UMCHubPlayStatusWidget>(Existing) && Existing->GetParent()==Canvas;
    TStrongObjectPtr<UWidgetBlueprint> Guard(Blueprint);
    Blueprint->Modify();Blueprint->WidgetTree->Modify();Canvas->Modify();
    auto* Status=Blueprint->WidgetTree->ConstructWidget<UMCHubPlayStatusWidget>(UMCHubPlayStatusWidget::StaticClass(),TEXT("HubPlayStatus"));
    Status->SetDesignerFlags(EWidgetDesignFlags::Designing);Status->Initialize();
    Status->Theme=UMCGameplayUIFactory::GetMasterTheme();Status->BuildPresentationTree();
    Status->ClearFlags(RF_Transient);Status->SetFlags(RF_Transactional);Status->bIsVariable=false;
    if(!Status->WidgetTree || !Status->WidgetTree->RootWidget) return false;
    Status->WidgetTree->ClearFlags(RF_Transient);Status->WidgetTree->SetFlags(RF_Transactional);
    Status->WidgetTree->ForEachWidget([](UWidget* Widget){Widget->ClearFlags(RF_Transient);Widget->SetFlags(RF_Transactional);Widget->bIsVariable=false;});
    if(auto* Label=Status->WidgetTree->FindWidget<UTextBlock>(TEXT("HubPlayStatusMessage"));Label && Status->Theme)
        Label->SetFont(Status->Theme->MakeGameplayFont(20,true));
    auto* Slot=Canvas->AddChildToCanvas(Status);Slot->SetAnchors(FAnchors(.5f,1.f));Slot->SetAlignment(FVector2D(.5f,1.f));
    // A fixed allocation keeps the pending/error observer tickable while its
    // inner card is collapsed, and reserves no visible screen space when idle.
    Slot->SetAutoSize(false);Slot->SetSize(FVector2D(640,108));Slot->SetPosition(FVector2D(0,-90));Slot->SetZOrder(50);
    Blueprint->OnVariableAdded(Status->GetFName());
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error) {Status->RemoveFromParent();return false;}
    Blueprint->MarkPackageDirty();return true;
}

bool UMCHubFrontEndEditorLibrary::AddHubGuidancePresentation(UWidgetBlueprint* Blueprint)
{
    if(!Editable(Blueprint) || !Blueprint->GetPathName().StartsWith(TEXT("/Game/Gameplay/UI/"))) {
        UE_LOG(LogTemp,Error,TEXT("MC_GUIDANCE_GUARD Preflight failed asset=%s editor=%d play=%d commandlet=%d dirty=%d tree=%d generated=%d"),
            *GetPathNameSafe(Blueprint),GEditor!=nullptr,GEditor && GEditor->PlayWorld!=nullptr,IsRunningCommandlet(),
            Blueprint && Blueprint->GetPackage()->IsDirty(),Blueprint && Blueprint->WidgetTree!=nullptr,Blueprint && Blueprint->GeneratedClass!=nullptr);
        return false;
    }
    const bool Hub=Blueprint->ParentClass->IsChildOf(UMCHubWidget::StaticClass());
    const bool Menu=Blueprint->ParentClass->IsChildOf(UMCMainMenuWidget::StaticClass());
    const bool Pause=Blueprint->ParentClass->IsChildOf(UMCGameplayMenuWidget::StaticClass());
    const bool Scoreboard=Blueprint->ParentClass->IsChildOf(UMCScoreboardWidget::StaticClass());
    if(!Hub && !Menu && !Pause && !Scoreboard) return false;
    TStrongObjectPtr<UWidgetBlueprint> Guard(Blueprint);
    UWidgetTree* Target=Blueprint->WidgetTree;
    auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Defaults) return false;
    const TArray<FName> Branches=Scoreboard?TArray<FName>{TEXT("ScoreVoiceHeader"),TEXT("ScorePlayer1Mute"),TEXT("ScorePlayer2Mute"),TEXT("ScorePlayer3Mute"),TEXT("ScorePlayer4Mute")}
        :Hub?TArray<FName>{TEXT("HubInviteFriends"),TEXT("HubInviteHint"),TEXT("HubReadyWaypoint")}
        :Pause?TArray<FName>{TEXT("MenuPauseInvite")}:TArray<FName>{TEXT("MenuPause_Action15")};
    bool Complete=true;for(FName Name:Branches) Complete&=Target->FindWidget(Name)!=nullptr;
    if(Complete) return true;
    TStrongObjectPtr<UMCGameplayUIWidget> Source(NewObject<UMCGameplayUIWidget>(GetTransientPackage(),
        Scoreboard?UMCScoreboardWidget::StaticClass():Hub?UMCHubWidget::StaticClass():Pause?UMCGameplayMenuWidget::StaticClass():UMCMainMenuWidget::StaticClass(),NAME_None,RF_Transient));
    Source->Theme=Defaults->Theme;Source->TranslationTable=Defaults->TranslationTable;
    Source->StyleBindings=Defaults->StyleBindings;Source->TextBindings=Defaults->TextBindings;
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);
    // Scoreboard Initialize caches its row tree. Supply the artist clone first,
    // otherwise its private Players cache would still refer to the native seed.
    if(!Scoreboard) Source->Initialize();
    if(Source->WidgetTree) Source->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    Source->WidgetTree=DuplicateObject<UWidgetTree>(Target,Source.Get());
    if(Scoreboard) Source->Initialize();
    Source->BuildPresentationTree();
    if(Scoreboard) Source->BindStyle(TEXT("ScoreVoiceHeader"),EMCUIColorToken::Ink,EMCUIStyleTarget::Text,20,true);
    if(Menu) CastChecked<UMCMainMenuWidget>(Source.Get())->ShowPause();
    if(Pause) CastChecked<UMCGameplayMenuWidget>(Source.Get())->ShowPause();
    struct FBranch {UWidget* Root;UPanelWidget* Parent;bool Canvas;FMargin Padding;FVector2D Position,Alignment;int32 Z;
        bool Horizontal=false;FSlateChildSize ChildSize=FSlateChildSize(ESlateSizeRule::Automatic);
        EHorizontalAlignment HAlign=HAlign_Fill;EVerticalAlignment VAlign=VAlign_Fill;};
    TArray<FBranch> Missing;TArray<UWidget*> NewWidgets;
    for(FName Name:Branches)
    {
        if(Target->FindWidget(Name)) continue;
        UWidget* Root=Source->WidgetTree->FindWidget(Name);auto* Parent=Root?Root->GetParent():nullptr;
        // The named leaf sits in a new width wrapper. Move that entire missing
        // branch up to its first existing ancestor, never its artist row.
        if(Scoreboard) while(Parent && !Target->FindWidget(Parent->GetFName())) {Root=Parent;Parent=Root->GetParent();}
        UPanelWidget* Destination=Parent?Cast<UPanelWidget>(Target->FindWidget(Parent->GetFName())):nullptr;
        if(!Root || !Destination || (!Cast<UCanvasPanel>(Destination) && !Cast<UVerticalBox>(Destination) && !Cast<UHorizontalBox>(Destination))) {
            UE_LOG(LogTemp,Error,TEXT("MC_GUIDANCE_GUARD Missing branch/destination leaf=%s root=%s parent=%s targetParent=%s sourceTree=%s targetTree=%s"),
                *Name.ToString(),*GetPathNameSafe(Root),*GetPathNameSafe(Parent),*GetPathNameSafe(Destination),*GetPathNameSafe(Source->WidgetTree),*GetPathNameSafe(Target));
            return false;
        }
        FBranch Branch{Root,Destination,false,FMargin(0,4),FVector2D::ZeroVector,FVector2D::ZeroVector,0};
        if(const auto* Slot=Cast<UCanvasPanelSlot>(Root->Slot)) {
            Branch.Canvas=true;Branch.Position=Slot->GetPosition();Branch.Alignment=Slot->GetAlignment();Branch.Z=Slot->GetZOrder();
        }
        if(const auto* Slot=Cast<UVerticalBoxSlot>(Root->Slot)) Branch.Padding=Slot->GetPadding();
        if(const auto* Slot=Cast<UHorizontalBoxSlot>(Root->Slot)) {
            Branch.Horizontal=true;Branch.Padding=Slot->GetPadding();Branch.ChildSize=Slot->GetSize();
            Branch.HAlign=Slot->GetHorizontalAlignment();Branch.VAlign=Slot->GetVerticalAlignment();
        }
        Missing.Add(Branch);
        NewWidgets.AddUnique(Root);
        UWidgetTree::ForWidgetAndChildren(Root,[&](UWidget* Child){NewWidgets.AddUnique(Child);});
    }
    for(UWidget* Widget:NewWidgets)
    {
        if(Widget->GetOuter()!=Source->WidgetTree || StaticFindObjectFast(UObject::StaticClass(),Target,Widget->GetFName())) {
            UE_LOG(LogTemp,Error,TEXT("MC_GUIDANCE_GUARD Branch ownership/name conflict widget=%s outer=%s sourceTree=%s targetCollision=%s"),
                *GetPathNameSafe(Widget),*GetPathNameSafe(Widget->GetOuter()),*GetPathNameSafe(Source->WidgetTree),
                *GetPathNameSafe(StaticFindObjectFast(UObject::StaticClass(),Target,Widget->GetFName())));
            return false;
        }
        Widget->ClearFlags(RF_Transient);Widget->SetFlags(RF_Transactional);Widget->bIsVariable=false;
        if(auto* Button=Cast<UButton>(Widget)) {
            Button->OnClicked.Clear();Button->OnPressed.Clear();
            // The temporary live presenter may own MIDs. Seed only this new
            // button with serializable analytic brushes; gameplay styling
            // applies the shared material palette again on construction.
            if(Source->Theme) {
                const auto& P=Source->Theme->Palette;auto Style=Button->GetStyle();
                Style.SetNormal(FSlateRoundedBoxBrush(P.Button,8.f));
                Style.SetHovered(FSlateRoundedBoxBrush(P.ButtonHover,8.f));
                Style.SetPressed(FSlateRoundedBoxBrush(P.ButtonPressed,8.f));
                Style.SetDisabled(FSlateRoundedBoxBrush(P.ButtonDisabled,8.f));
                Button->SetStyle(Style);
            }
        }
        if(auto* Text=Cast<UTextBlock>(Widget);Text && Source->Theme) {
            const auto* Style=Source->StyleBindings.FindByPredicate([&](const FMCUIStyleBinding& Binding){return Binding.WidgetName==Text->GetFName() && Binding.Target==EMCUIStyleTarget::Text;});
            Text->SetFont(Source->Theme->MakeGameplayFont(Style && Style->FontSize>0?Style->FontSize:18,Style && Style->bBold));
        }
    }
    const auto OldGuids=Blueprint->WidgetVariableNameToGuidMap;
    const auto OldStyles=Defaults->StyleBindings;const auto OldTexts=Defaults->TextBindings;
    auto RollBack=[&] {
        for(auto& Branch:Missing) Branch.Root->RemoveFromParent();
        for(UWidget* Widget:NewWidgets) if(Widget->GetOuter()==Target) UWidgetTree::TryMoveWidgetToNewTree(Widget,Source->WidgetTree);
        Blueprint->WidgetVariableNameToGuidMap=OldGuids;
        if(auto* Current=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass?Blueprint->GeneratedClass->GetDefaultObject():nullptr)) {
            Current->StyleBindings=OldStyles;Current->TextBindings=OldTexts;
            CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
        }
    };
    Blueprint->Modify();Target->Modify();
    for(auto& Branch:Missing) Branch.Root->RemoveFromParent();
    for(UWidget* Widget:NewWidgets) if(!UWidgetTree::TryMoveWidgetToNewTree(Widget,Target)) {
        UE_LOG(LogTemp,Error,TEXT("MC_GUIDANCE_GUARD Could not move widget %s into %s"),*GetPathNameSafe(Widget),*GetPathNameSafe(Target));
        RollBack();return false;
    }
    for(const auto& Branch:Missing)
    {
        Branch.Parent->Modify();
        if(Branch.Canvas) {
            auto* Slot=CastChecked<UCanvasPanel>(Branch.Parent)->AddChildToCanvas(Branch.Root);
            Slot->SetAutoSize(true);Slot->SetPosition(Branch.Position);Slot->SetAlignment(Branch.Alignment);Slot->SetZOrder(Branch.Z);
        } else if(Branch.Horizontal) {
            auto* Slot=CastChecked<UHorizontalBox>(Branch.Parent)->AddChildToHorizontalBox(Branch.Root);
            Slot->SetPadding(Branch.Padding);Slot->SetSize(Branch.ChildSize);
            Slot->SetHorizontalAlignment(Branch.HAlign);Slot->SetVerticalAlignment(Branch.VAlign);
        } else CastChecked<UVerticalBox>(Branch.Parent)->AddChildToVerticalBox(Branch.Root)->SetPadding(Branch.Padding);
    }
    for(UWidget* Widget:NewWidgets) if(!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) Blueprint->OnVariableAdded(Widget->GetFName());
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) {RollBack();return false;}
    auto* Updated=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject());
    if(!Updated) {RollBack();return false;}
    Updated->Modify();
    for(const auto& Binding:Source->StyleBindings)
        if(NewWidgets.Contains(Target->FindWidget(Binding.WidgetName)) && !Updated->StyleBindings.ContainsByPredicate(
            [&](const FMCUIStyleBinding& Existing){return Existing.WidgetName==Binding.WidgetName && Existing.Target==Binding.Target;})) Updated->StyleBindings.Add(Binding);
    for(const auto& Entry:Source->TextBindings)
        if(NewWidgets.Contains(Target->FindWidget(Entry.Key)) && !Updated->TextBindings.Contains(Entry.Key)) Updated->TextBindings.Add(Entry.Key,Entry.Value);
    CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    Blueprint->MarkPackageDirty();
    UE_LOG(LogTemp,Display,TEXT("MC_HUB_GUIDANCE Appended %d branches, preserved existing presentation: %s"),Missing.Num(),*Blueprint->GetPathName());
    return true;
}

// Public narrow name for the same append-only, rollback-capable migration.
bool UMCHubFrontEndEditorLibrary::AddScoreboardVoicePresentation(UWidgetBlueprint* Blueprint)
{
    return Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UMCScoreboardWidget::StaticClass())
        && AddHubGuidancePresentation(Blueprint);
}
