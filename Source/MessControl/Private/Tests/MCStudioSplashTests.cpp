#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MCStudioSplashWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace MCStudioSplashTests
{
    UMCStudioSplashWidget* DesignerWidget()
    {
        auto* Widget=NewObject<UMCStudioSplashWidget>();Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
        Widget->WidgetTree=NewObject<UWidgetTree>(Widget);
        // Test the presenter before logo authoring, without loading optional game
        // packages or compiling a test shader. Only transient CPU-side art is used.
        auto* Texture=UTexture2D::CreateTransient(4,4);
        Widget->WordmarkTexture=Texture;Widget->BranchTexture=Texture;Widget->LeavesTexture=Texture;
        Widget->RevealMaterial=UMaterial::GetDefaultMaterial(MD_UI);
        Widget->BuildPresentationTree();
        return Widget;
    }
    UMaterialInstanceDynamic* LayerMaterial(UMCStudioSplashWidget* Widget,const TCHAR* Name)
    {
        auto* Image=Widget->WidgetTree->FindWidget<UImage>(Name);
        return Image?Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject()):nullptr;
    }
    float Parameter(UMaterialInstanceDynamic* Material,const TCHAR* Name)
    {
        float Value=-1.f;
        if(Material) Material->GetScalarParameterValue(FMaterialParameterInfo(Name),Value,true);
        return Value;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStudioSplashSessionTest,
    "MessControl.UI.StudioSplash.OncePerGameInstance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStudioSplashSessionTest::RunTest(const FString&)
{
    // Do not initialize unrelated GameInstance subsystems or microphone devices.
    auto* FirstSession=NewObject<UGameInstance>();auto* SecondSession=NewObject<UGameInstance>();
    auto* First=NewObject<UMCStudioSplashSubsystem>(FirstSession);
    auto* Second=NewObject<UMCStudioSplashSubsystem>(SecondSession);
    TestEqual(TEXT("Presentation state belongs to its GameInstance"),First->GetGameInstance(),FirstSession);
    TestTrue(TEXT("First startup in this application session can show the ident"),First->ClaimStartupPresentation());
    TestFalse(TEXT("A later controller or return to menu cannot replay it"),First->ClaimStartupPresentation());
    TestFalse(TEXT("Repeated front-end refresh cannot replay it"),First->ClaimStartupPresentation());
    TestTrue(TEXT("A fresh GameInstance starts a fresh application session"),Second->ClaimStartupPresentation());
    TestEqual(TEXT("Default studio presentation lasts three seconds"),GetDefault<UMCStudioSplashWidget>()->Duration,3.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStudioSplashCurveTest,
    "MessControl.UI.StudioSplash.RevealCurvesStayBounded",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStudioSplashCurveTest::RunTest(const FString&)
{
    using FCurve=float (*)(float,float,float);
    const FCurve Curves[]={&UMCStudioSplashWidget::RevealAt,&UMCStudioSplashWidget::CubicRevealAt};
    const float NaN=std::numeric_limits<float>::quiet_NaN(),Infinity=std::numeric_limits<float>::infinity();
    for(const FCurve Curve:Curves)
    {
        TestEqual(TEXT("No reveal before the authored interval"),Curve(-10.f,0.f,1.f),0.f);
        TestEqual(TEXT("Reveal starts at zero"),Curve(0.f,0.f,1.f),0.f);
        TestEqual(TEXT("Reveal completes at the authored end"),Curve(1.f,0.f,1.f),1.f);
        TestEqual(TEXT("Late sampling never overshoots"),Curve(1000.f,0.f,1.f),1.f);
        float Previous=0.f;
        for(int32 Index=0;Index<=20;++Index)
        {
            const float Value=Curve(Index/20.f,0.f,1.f);
            TestTrue(TEXT("The reveal progresses monotonically and stays normalized"),FMath::IsFinite(Value) && Value>=Previous && Value>=0.f && Value<=1.f);
            Previous=Value;
        }
        TestEqual(TEXT("NaN scrub time produces a safe hidden pose"),Curve(NaN,0.f,1.f),0.f);
        TestEqual(TEXT("Invalid authored start cannot poison UI transforms"),Curve(.5f,NaN,1.f),0.f);
        TestEqual(TEXT("Invalid authored end cannot poison UI transforms"),Curve(.5f,0.f,Infinity),0.f);
        TestTrue(TEXT("Zero-length interval remains finite"),FMath::IsFinite(Curve(.5f,.5f,.5f)));
        TestTrue(TEXT("Reversed authored interval remains finite"),FMath::IsFinite(Curve(.5f,1.f,0.f)));
    }
    TestTrue(TEXT("Cubic reveal makes the intended quick initial sweep"),UMCStudioSplashWidget::CubicRevealAt(.25f,0.f,1.f)>.25f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStudioSplashDesignerTest,
    "MessControl.UI.StudioSplash.EditableArtAndScrubbingStayLocal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStudioSplashDesignerTest::RunTest(const FString&)
{
    using namespace MCStudioSplashTests;
    auto* Widget=DesignerWidget();
    UWidget* OriginalRoot=Widget->WidgetTree->RootWidget;
    auto* Canvas=Cast<UCanvasPanel>(OriginalRoot);
    if(!TestNotNull(TEXT("Native presentation creates the editable composition"),Canvas)) return false;
    auto* ArtistLabel=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ArtistSentinel"));
    ArtistLabel->SetText(FText::AsCultureInvariant(TEXT("KEEP ARTIST CONTENT")));Canvas->AddChild(ArtistLabel);
    const int32 ChildCount=Canvas->GetChildrenCount();
    auto* Wordmark=Widget->WidgetTree->FindWidget<UImage>(TEXT("StudioWordmark"));
    auto* Backdrop=Widget->WidgetTree->FindWidget<UBorder>(TEXT("StudioBackdrop"));
    auto* OriginalMID=LayerMaterial(Widget,TEXT("StudioWordmark"));
    TestNotNull(TEXT("A layer uses an independently editable dynamic material"),OriginalMID);
    Widget->ApplyEditorPreview(.4f);Widget->ApplyEditorPreview(.8f);
    TestEqual(TEXT("Scrubbing does not allocate another layer MID"),LayerMaterial(Widget,TEXT("StudioWordmark")),OriginalMID);

    auto* ChangedTexture=UTexture2D::CreateTransient(8,4);
    Widget->WordmarkTexture=ChangedTexture;Widget->LogoColor=FLinearColor(.2f,.6f,.9f,.8f);
    Widget->BackgroundColor=FLinearColor(.1f,.03f,.02f,1.f);Widget->ApplyEditorPreview(1.f);
    TestEqual(TEXT("Property edits preserve the artist's existing root"),Widget->WidgetTree->RootWidget.Get(),OriginalRoot);
    TestEqual(TEXT("Property edits preserve other designer widgets"),Widget->WidgetTree->FindWidget(TEXT("ArtistSentinel")),static_cast<UWidget*>(ArtistLabel));
    TestEqual(TEXT("Property edits never append duplicate native layers"),Canvas->GetChildrenCount(),ChildCount);
    TestEqual(TEXT("The layer widget itself remains editable by identity"),Widget->WidgetTree->FindWidget(TEXT("StudioWordmark")),static_cast<UWidget*>(Wordmark));
    if(TestNotNull(TEXT("The edited layer retains its dynamic material"),LayerMaterial(Widget,TEXT("StudioWordmark"))))
    {
        auto* EditedMID=LayerMaterial(Widget,TEXT("StudioWordmark"));UTexture* Texture=nullptr;FLinearColor Color;
        TestTrue(TEXT("Texture parameter is authored on the layer"),EditedMID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("LogoTexture")),Texture,true));
        TestEqual(TEXT("New texture is reflected on the same designer root"),Texture,static_cast<UTexture*>(ChangedTexture));
        TestTrue(TEXT("Color parameter is authored on the layer"),EditedMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("LogoColor")),Color,true));
        TestEqual(TEXT("Edited brand color reaches the reveal material"),Color,Widget->LogoColor);
    }
    if(TestNotNull(TEXT("Saved presentation has an editable backdrop"),Backdrop))
        TestEqual(TEXT("Edited background color reaches the saved backdrop"),Backdrop->GetBrushColor(),Widget->BackgroundColor);

    auto* Observer=NewObject<UMCGameplayUIWidget>();Observer->PreviewTime=7.f;
    Widget->OnFinished.AddDynamic(Observer,&UMCGameplayUIWidget::ResetEditorPreview);
    Widget->ApplyEditorPreview(1000.f);Widget->SetTimelineTime(1000.f);Widget->ApplyEditorPreview(std::numeric_limits<float>::quiet_NaN());
    TestFalse(TEXT("Designer scrubbing never finishes the application session"),Widget->IsFinished());
    TestEqual(TEXT("Scrubbing never fires the runtime completion event"),Observer->PreviewTime,7.f);
    Widget->OnFinished.RemoveAll(Observer);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCStudioSplashArtistTimelineTest,
    "MessControl.UI.StudioSplash.ArtistTimelineStartsWithVisibleArt",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCStudioSplashArtistTimelineTest::RunTest(const FString&)
{
    using namespace MCStudioSplashTests;
    auto* Widget=DesignerWidget();Widget->bUseNativeTimeline=false;
    for(float Duration:{3.f,.5f})
    {
        Widget->Duration=Duration;Widget->ApplyEditorPreview(0.f);Widget->ResetEditorPreview();
        for(const TCHAR* Layer:{TEXT("StudioWordmark"),TEXT("StudioBranch"),TEXT("StudioLeaves")})
        {
            auto* Material=LayerMaterial(Widget,Layer);
            TestEqual(TEXT("Artist timeline receives fully revealed art even at preview zero"),Parameter(Material,TEXT("Reveal")),1.f);
            TestEqual(TEXT("Native exit does not hide art when the artist owns animation"),Parameter(Material,TEXT("Opacity")),1.f);
        }
    }
    return true;
}
#endif
