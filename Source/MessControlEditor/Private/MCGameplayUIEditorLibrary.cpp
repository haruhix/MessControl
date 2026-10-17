#include "MCGameplayUIEditorLibrary.h"

#include "MCGameplayUIPreview.h"
#include "MCGameplayUIWidget.h"
#include "MCUIDataLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"
#include "Engine/UserInterfaceSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/UnrealType.h"

bool UMCGameplayUIEditorLibrary::RenderGameplayUIPreview(TSubclassOf<UMCGameplayUIWidget> WidgetClass,
    const FString& OutputFile,FIntPoint DrawSize,const FString& State,const FString& Culture,float Time)
{
    FString Path=FPaths::ConvertRelativePathToFull(OutputFile);FPaths::NormalizeFilename(Path);
    if (!GEditor || GEditor->PlayWorld || !WidgetClass || !Path.StartsWith(TEXT("E:/"),ESearchCase::IgnoreCase)
        || !Path.EndsWith(TEXT(".png"),ESearchCase::IgnoreCase) || DrawSize.X<1 || DrawSize.Y<1 || DrawSize.X>4096 || DrawSize.Y>4096) return false;
    if(auto* Generated=Cast<UWidgetBlueprintGeneratedClass>(WidgetClass.Get()))
        Generated->UpdateCustomPropertyListForPostConstruction();
    TStrongObjectPtr<UMCGameplayUIWidget> Widget(NewObject<UMCGameplayUIWidget>(GetTransientPackage(),WidgetClass,NAME_None,RF_Transient));
    Widget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
    Widget->PreviewState=FName(State);Widget->PreviewCulture=Culture;
    const int64 Scenario=StaticEnum<EMCGameplayUIPreviewScenario>()->GetValueByNameString(State);
    if (Scenario!=INDEX_NONE) Widget->PreviewScenario=static_cast<EMCGameplayUIPreviewScenario>(Scenario);
    Widget->Initialize();Widget->BuildPresentationTree();
    const TSharedRef<SWidget> SlateWidget=Widget->TakeWidget();Widget->ApplyEditorPreview(Time);
    // Only materials used by this fixture need to finish before its first draw.
    TArray<UTexture*> FixtureTextures;
    Widget->WidgetTree->ForEachWidgetAndDescendants([&FixtureTextures](UWidget* Item)
    {
        auto CompleteBrush=[](const FSlateBrush& Brush)
        {if(auto* Material=Cast<UMaterialInterface>(Brush.GetResourceObject())) Material->EnsureIsComplete();};
        if(auto* Border=Cast<UBorder>(Item)) CompleteBrush(Border->Background);
        if(auto* Button=Cast<UButton>(Item)) {const auto& Style=Button->GetStyle();CompleteBrush(Style.Normal);CompleteBrush(Style.Hovered);CompleteBrush(Style.Pressed);CompleteBrush(Style.Disabled);}
        if(auto* Image=Cast<UImage>(Item))
        {
            if(auto* Texture=Cast<UTexture>(Image->GetBrush().GetResourceObject())) FixtureTextures.AddUnique(Texture);
            if(auto* Material=Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject())) Material->EnsureIsComplete();
        }
        if(auto* Bar=Cast<UProgressBar>(Item))
            for(const auto* Brush:{&Bar->GetWidgetStyle().FillImage,&Bar->GetWidgetStyle().BackgroundImage})
                if(auto* Material=Cast<UMaterialInterface>(Brush->GetResourceObject())) Material->EnsureIsComplete();
    });
    FTextureCompilingManager::Get().FinishCompilation(FixtureTextures);
    for(auto* Texture:FixtureTextures) Texture->WaitForPendingInitOrStreaming();
    MCGameplayUIPreview::SynchronizeFixtureTree(Widget.Get());
    SlateWidget->SlatePrepass();
    UE_LOG(LogTemp,Display,TEXT("MC_UI_RENDER %s bindings=%d cdoBindings=%d"),*WidgetClass->GetPathName(),Widget->StyleBindings.Num(),WidgetClass->GetDefaultObject<UMCGameplayUIWidget>()->StyleBindings.Num());
    for(const FName Name:{FName(TEXT("TimerValue")),FName(TEXT("ReserveValue")),FName(TEXT("ThemeHeading"))})
        if(auto* Text=Cast<UTextBlock>(Widget->GetWidgetFromName(Name)))
            UE_LOG(LogTemp,Display,TEXT("MC_UI_RENDER_FONT %s valid=%d value=%s"),*Name.ToString(),Text->GetFont().HasValidFont(),*Text->GetText().ToString());
    const float DPIScale=DrawSize.X>=1000 && DrawSize.Y>=600?GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(DrawSize):1.f;
    const TSharedRef<SWidget> PreviewSurface=SNew(SDPIScaler).DPIScale(DPIScale)[SlateWidget];
    // Render linear colors into a float target, then encode sRGB exactly once.
    // An sRGB target plus Slate gamma correction otherwise brightens PNG previews twice.
    auto* Renderer=new FWidgetRenderer(false,true);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
    Target->ClearColor=FLinearColor::Transparent;
    Target->TargetGamma=1.f;
    Target->InitCustomFormat(DrawSize.X,DrawSize.Y,PF_FloatRGBA,true);
    Target->UpdateResourceImmediate(true);
    Renderer->DrawWidget(Target.Get(),PreviewSurface,FVector2D(DrawSize),0.f,false);
    FlushRenderingCommands();
    // Auto-wrap needs an allotted width before its measured height is final.
    // Warm the actual tree, then export the settled layout.
    Widget->RefreshPresentationGeometry();
    SlateWidget->Invalidate(EInvalidateWidgetReason::Layout);
    PreviewSurface->SlatePrepass();
    Renderer->DrawWidget(Target.Get(),PreviewSurface,FVector2D(DrawSize),0.f,false);
    FlushRenderingCommands();
    TArray<FLinearColor> LinearPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(false);
    TArray64<uint8> Pixels;
    const bool bRead=Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(LinearPixels,ReadFlags);
    TArray<FColor> RawPixels;
    if(bRead)
    {
        RawPixels.Reserve(LinearPixels.Num());
        for(const auto& Color:LinearPixels) RawPixels.Add(Color.ToFColorSRGB());
    }
    const bool bEncoded=bRead && FImageUtils::CompressImage(Pixels,TEXT("PNG"),
        FImageView(RawPixels.GetData(),DrawSize.X,DrawSize.Y,EGammaSpace::sRGB));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const bool bSaved=bEncoded && FFileHelper::SaveArrayToFile(Pixels,*Path);
    BeginCleanup(Renderer);
    return bSaved;
}
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

void UMCGameplayUIEditorLibrary::EnsureGameplayUIPreviewRegistered()
{
    MCGameplayUIPreview::EnsureRegistered();
}

void UMCGameplayUIEditorLibrary::OpenGameplayUITheme(UMCGameplayUITheme* Theme)
{
    MCGameplayUIPreview::EnsureRegistered();
    MCGameplayUIPreview::OpenThemeEditor(Theme);
}

UWidgetBlueprint* UMCGameplayUIEditorLibrary::CreateEditableGameplayWidget(const FString& AssetPath,TSubclassOf<UMCGameplayUIWidget> ParentClass)
{
    if (!GEditor || GEditor->PlayWorld || IsRunningCommandlet() || !ParentClass
        || ParentClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || !AssetPath.StartsWith(TEXT("/Game/Gameplay/UI/")) || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Requires idle editor, concrete gameplay widget class and /Game/Gameplay/UI/ package path: %s"),*AssetPath);
        return nullptr;
    }
    auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    if (!Assets) return nullptr;
    const FString ObjectPath=AssetPath+TEXT(".")+FPackageName::GetLongPackageAssetName(AssetPath);
    UObject* LoadedExisting=FindObject<UObject>(nullptr,*ObjectPath);
    if (LoadedExisting || Assets->DoesAssetExist(AssetPath))
    {
        auto* Existing=Cast<UWidgetBlueprint>(LoadedExisting?LoadedExisting:Assets->LoadAsset(AssetPath));
        if (!Existing || !Existing->ParentClass || !Existing->ParentClass->IsChildOf(UMCGameplayUIWidget::StaticClass()))
        {
            UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Existing unrelated asset preserved: %s"),*AssetPath);
            return nullptr;
        }
        UE_LOG(LogTemp,Display,TEXT("MC_UI_AUTHOR Existing artist tree preserved: %s"),*AssetPath);
        return Existing;
    }

    // A design-time source instance supplies the initial editable tree, without running gameplay initialization.
    // Blueprint compilation can collect garbage. The source owns presentation
    // data copied after compilation, so keep it alive through the entire save.
    TStrongObjectPtr<UMCGameplayUIWidget> Source(NewObject<UMCGameplayUIWidget>(GetTransientPackage(),ParentClass,NAME_None,RF_Transient));
    Source->SetDesignerFlags(EWidgetDesignFlags::Designing);
    Source->Initialize();
    Source->BuildPresentationTree();
    if (!Source->WidgetTree || !Source->WidgetTree->RootWidget)
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Parent produced no presentation tree: %s"),*ParentClass->GetPathName());
        return nullptr;
    }

    UPackage* Package=CreatePackage(*AssetPath);
    auto* Blueprint=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(ParentClass,Package,
        FName(*FPackageName::GetLongPackageAssetName(AssetPath)),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass(),TEXT("MCGameplayUIAuthoring")));
    if (!Blueprint) return nullptr;
    TStrongObjectPtr<UWidgetBlueprint> BlueprintGuard(Blueprint);
    if (Blueprint->WidgetTree)
        Blueprint->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors | REN_NonTransactional);
    Blueprint->WidgetTree=DuplicateObject<UWidgetTree>(Source->WidgetTree,Blueprint,TEXT("WidgetTree"));
    Blueprint->WidgetTree->ClearFlags(RF_Transient);
    Blueprint->WidgetTree->SetFlags(RF_Transactional);
    Blueprint->WidgetTree->ForEachWidget([SourceWidget=Source.Get()](UWidget* Widget)
    {
        Widget->ClearFlags(RF_Transient);
        Widget->SetFlags(RF_Transactional);
        // Native classes resolve named controls themselves. A generated field
        // with the same name as a native cache would be a Blueprint collision.
        Widget->bIsVariable=false;
        // Callbacks to the temporary native owner are rebound by each runtime
        // instance. Saving that owner would leave transient external references.
        if(auto* Button=Cast<UButton>(Widget)) Button->OnClicked.RemoveAll(SourceWidget);
        if(auto* Picker=Cast<UComboBoxString>(Widget);Picker && Picker->OnGenerateWidgetEvent.GetUObject()==SourceWidget)
            Picker->OnGenerateWidgetEvent.Unbind();
        if(auto* Bar=Cast<UProgressBar>(Widget))
        {
            auto Style=Bar->GetWidgetStyle();
            for(FSlateBrush* Brush:{&Style.FillImage,&Style.BackgroundImage})
                if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Brush->GetResourceObject())) Brush->SetResourceObject(Dynamic->GetMaterial());
            Bar->SetWidgetStyle(Style);
        }
        if(auto* Image=Cast<UImage>(Widget))
            if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject()))
                Image->SetBrushFromMaterial(Dynamic->GetMaterial());
        if(auto* Border=Cast<UBorder>(Widget))
        {
            auto Brush=Border->Background;
            if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Brush.GetResourceObject())) {Brush.SetResourceObject(Dynamic->GetMaterial());Border->SetBrush(Brush);}
        }
        if(auto* Button=Cast<UButton>(Widget))
        {
            auto Style=Button->GetStyle();
            for(FSlateBrush* Brush:{&Style.Normal,&Style.Hovered,&Style.Pressed,&Style.Disabled})
                if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Brush->GetResourceObject())) Brush->SetResourceObject(Dynamic->GetMaterial());
            Button->SetStyle(Style);
        }
    });
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status==BS_Error || !Blueprint->GeneratedClass)
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Blueprint compilation failed; no package saved: %s"),*AssetPath);
        return nullptr;
    }
    if (auto* Defaults=Cast<UMCGameplayUIWidget>(Blueprint->GeneratedClass->GetDefaultObject()))
    {
        Defaults->StyleBindings=Source->StyleBindings;
        // Native defaults changed after compilation: refresh the optimized
        // initializer so newly created instances receive those defaults too.
        CastChecked<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass)->UpdateCustomPropertyListForPostConstruction();
    }
    FAssetRegistryModule::AssetCreated(Blueprint);
    Blueprint->MarkPackageDirty();
    if (!Assets->SaveLoadedAsset(Blueprint,false))
    {
        UE_LOG(LogTemp,Error,TEXT("MC_UI_AUTHOR Could not save new gameplay widget: %s"),*AssetPath);
        return nullptr;
    }
    UE_LOG(LogTemp,Display,TEXT("MC_UI_AUTHOR Saved editable gameplay widget: %s"),*AssetPath);
    return Blueprint;
}

bool UMCGameplayUIEditorLibrary::ValidateGameplayUILocalization(TArray<FString>& OutReport)
{
    OutReport.Reset();
    auto& I18N=FInternationalization::Get();
    const FString LanguageBefore=I18N.GetCurrentLanguage()->GetName();
    const FString LocaleBefore=I18N.GetCurrentLocale()->GetName();
    int32 Passed=0,Failed=0;
    auto Check=[&](const FString& Label,const FString& Actual,const FString& Expected)
    {
        const bool bPass=Actual==Expected;
        if(bPass) ++Passed;else ++Failed;
        OutReport.Add(FString::Printf(TEXT("%s %s: expected [%s], got [%s]"),bPass?TEXT("PASS"):TEXT("FAIL"),*Label,*Expected,*Actual));
    };
    auto Format=[&](const TCHAR* Pattern,const FFormatNamedArguments& Arguments,const TCHAR* Culture)
    {
        return UMCUIDataLibrary::FormatTextForCulture(FText::AsCultureInvariant(Pattern),Arguments,Culture).ToString();
    };
    const FText GlobalPattern=FText::AsCultureInvariant(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"));
    const FFormatNamedArguments GlobalArguments{{TEXT("N"),int64(2)}};
    const FString EnginePluralBefore=FText::Format(GlobalPattern,GlobalArguments).ToString();
    const FText GlobalOrdinal=FText::AsCultureInvariant(TEXT("{N}|ordinal(one=one,two=two,few=few,other=other)"));
    const FString EngineOrdinalBefore=FText::Format(GlobalOrdinal,GlobalArguments).ToString();
    struct FCase {double Value;const TCHAR* RU;const TCHAR* EN;const TCHAR* ENOrdinal;};
    const FCase Cases[]=
    {
        {0,TEXT("many"),TEXT("other"),TEXT("other")},
        {1,TEXT("one"),TEXT("one"),TEXT("one")},
        {2,TEXT("few"),TEXT("other"),TEXT("two")},
        {5,TEXT("many"),TEXT("other"),TEXT("other")},
        {11,TEXT("many"),TEXT("other"),TEXT("other")},
        {21,TEXT("one"),TEXT("other"),TEXT("one")},
        {1.5,TEXT("other"),TEXT("other"),TEXT("other")}
    };
    for(const auto& Case:Cases)
    {
        const FFormatArgumentValue Number=Case.Value==1.5?FFormatArgumentValue(Case.Value):FFormatArgumentValue(int64(Case.Value));
        const FFormatNamedArguments Arguments{{TEXT("N"),Number}};
        const FString Label=FString::Printf(TEXT("%g"),Case.Value);
        Check(TEXT("RU cardinal ")+Label,Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),Arguments,TEXT("ru")),Case.RU);
        Check(TEXT("EN cardinal ")+Label,Format(TEXT("{N}|plural(one=one,other=other)"),Arguments,TEXT("en")),Case.EN);
        Check(TEXT("RU ordinal ")+Label,Format(TEXT("{N}|ordinal(other=other)"),Arguments,TEXT("ru")),TEXT("other"));
        Check(TEXT("EN ordinal ")+Label,Format(TEXT("{N}|ordinal(one=one,two=two,few=few,other=other)"),Arguments,TEXT("en")),Case.ENOrdinal);
    }
    Check(TEXT("RU decimal"),UMCUIDataLibrary::DecimalForCulture(1.5,1,TEXT("ru")).ToString(),TEXT("1,5"));
    Check(TEXT("EN decimal"),UMCUIDataLibrary::DecimalForCulture(1.5,1,TEXT("en")).ToString(),TEXT("1.5"));
    Check(TEXT("RU numeric argument"),Format(TEXT("{N}"),FFormatNamedArguments{{TEXT("N"),1.5}},TEXT("ru")),TEXT("1,5"));
    Check(TEXT("EN numeric argument"),Format(TEXT("{N}"),FFormatNamedArguments{{TEXT("N"),1.5}},TEXT("en")),TEXT("1.5"));
    const FText NumericText=UMCUIDataLibrary::NumberForCulture(21,TEXT("en"));
    Check(TEXT("Historic numeric argument"),Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),FFormatNamedArguments{{TEXT("N"),NumericText}},TEXT("ru")),TEXT("one"));
    Check(TEXT("Nested format argument"),Format(TEXT("{N}|plural(one=\"{Who}: один\",few=\"{Who}: несколько\",many=\"{Who}: много\",other=\"{Who}: часть\")"),
        FFormatNamedArguments{{TEXT("N"),int64(2)},{TEXT("Who"),FText::AsCultureInvariant(TEXT("Зуб"))}},TEXT("ru")),TEXT("Зуб: несколько"));
    Check(TEXT("Nested plural/ordinal"),Format(TEXT("{N}|plural(one=\"{M}|ordinal(one=first,two=second,few=third,other=rest)\",other=\"none\")"),
        FFormatNamedArguments{{TEXT("N"),int64(1)},{TEXT("M"),int64(2)}},TEXT("en")),TEXT("second"));
    Check(TEXT("Escaped modifier"),Format(TEXT("{N}`|plural(one=ONE,other=OTHER)"),FFormatNamedArguments{{TEXT("N"),int64(1)}},TEXT("en")),TEXT("1|plural(one=ONE,other=OTHER)"));
    Check(TEXT("Escaped argument"),Format(TEXT("`{N`} {N}"),FFormatNamedArguments{{TEXT("N"),int64(2)}},TEXT("en")),TEXT("{N} 2"));
    Check(TEXT("Literal modifier-like text"),Format(TEXT("literal |plural("),FFormatNamedArguments(),TEXT("en")),TEXT("literal |plural("));
    Check(TEXT("Regional culture"),Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),FFormatNamedArguments{{TEXT("N"),int64(21)}},TEXT("ru-RU")),TEXT("one"));
    Check(TEXT("Invalid culture falls back to RU"),Format(TEXT("{N}|plural(one=one,few=few,many=many,other=other)"),FFormatNamedArguments{{TEXT("N"),int64(2)}},TEXT("invalid-ui-culture")),TEXT("few"));
    Check(TEXT("Editor language unchanged"),I18N.GetCurrentLanguage()->GetName(),LanguageBefore);
    Check(TEXT("Editor locale unchanged"),I18N.GetCurrentLocale()->GetName(),LocaleBefore);
    Check(TEXT("Engine plural unchanged"),FText::Format(GlobalPattern,GlobalArguments).ToString(),EnginePluralBefore);
    Check(TEXT("Engine ordinal unchanged"),FText::Format(GlobalOrdinal,GlobalArguments).ToString(),EngineOrdinalBefore);
    OutReport.Insert(FString::Printf(TEXT("Gameplay UI localization: %d passed, %d failed; language=%s locale=%s"),Passed,Failed,*LanguageBefore,*LocaleBefore),0);
    UE_LOG(LogTemp,Display,TEXT("MC_UI_LOCALIZATION %s"),*OutReport[0]);
    // Python hides OutReport when the bool return is false, so retain every actionable comparison in the log.
    for(const FString& Entry:OutReport)
        if(Entry.StartsWith(TEXT("FAIL "),ESearchCase::CaseSensitive))
            UE_LOG(LogTemp,Warning,TEXT("MC_UI_LOCALIZATION %s"),*Entry);
    return Failed==0;
}

bool UMCGameplayUIEditorLibrary::ConfigureBossNameCJKFallback(UFont* NameFont,UFontFace* FallbackFace)
{
    if(!IsValid(NameFont) || !IsValid(FallbackFace) || NameFont->FontCacheType!=EFontCacheType::Runtime
        || (GEditor && GEditor->PlayWorld) || NameFont->GetInternalCompositeFont().DefaultTypeface.Fonts.IsEmpty())
    {
        UE_LOG(LogTemp,Warning,TEXT("MC_BOSS_CJK_FALLBACK Invalid font/face, empty default typeface, or active PIE"));
        return false;
    }
    // The empty culture list is deliberate: an explicit PreviewCulture does
    // not change global culture, and these scripts need a real glyph face in
    // every preview/editor locale rather than the diagnostic last-resort font.
    static const FName EntryName(TEXT("MC_Boss_CJK_Fallback"));
    FProperty* Property=FindFProperty<FProperty>(UFont::StaticClass(),UFont::GetInternalCompositeFontPropertyName());
    NameFont->Modify();NameFont->PreEditChange(Property);
    auto& Composite=NameFont->GetMutableInternalCompositeFont();
    FCompositeSubFont* SubFont=Composite.SubTypefaces.FindByPredicate([](const FCompositeSubFont& Entry){return Entry.EditorName==EntryName;});
    if(!SubFont) SubFont=&Composite.SubTypefaces.AddDefaulted_GetRef();
    SubFont->EditorName=EntryName;SubFont->Cultures.Reset();SubFont->ScalingFactor=1.f;
    SubFont->Typeface.Fonts.Reset();
    auto& FaceEntry=SubFont->Typeface.Fonts.AddDefaulted_GetRef();FaceEntry.Name=TEXT("Default");FaceEntry.Font=FFontData(FallbackFace);
    SubFont->CharacterRanges.Reset();
    // BMP ranges covered by the bundled Droid Sans Fallback. Latin/Cyrillic,
    // numeric values and punctuation outside these ranges retain the default.
    const int32 Ranges[][2]={{0x1100,0x11FF},{0x2E80,0x2FFF},{0x3000,0x31FF},{0x3200,0x33FF},
        {0x3400,0x4DBF},{0x4E00,0x9FFF},{0xA960,0xA97F},{0xAC00,0xD7AF},{0xD7B0,0xD7FF},
        {0xF900,0xFAFF},{0xFE10,0xFE1F},{0xFE30,0xFE4F},{0xFF00,0xFFEF}};
    for(const auto& Range:Ranges) SubFont->CharacterRanges.Add(FInt32Range::Inclusive(Range[0],Range[1]));
    Composite.MakeDirty();
    FPropertyChangedEvent Changed(Property,EPropertyChangeType::ValueSet);NameFont->PostEditChangeProperty(Changed);
    NameFont->MarkPackageDirty();
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().GetRenderer()->FlushFontCache(TEXT("MC boss CJK sub-typeface configured"));
    UE_LOG(LogTemp,Display,TEXT("MC_BOSS_CJK_FALLBACK %s face=%s ranges=%d default_faces=%d"),
        *NameFont->GetPathName(),*FallbackFace->GetPathName(),SubFont->CharacterRanges.Num(),Composite.DefaultTypeface.Fonts.Num());
    return true;
}
