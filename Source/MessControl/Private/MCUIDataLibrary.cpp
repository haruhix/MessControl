#include "MCUIDataLibrary.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextFormatter.h"

namespace MCUIDataPrivate
{
    constexpr const TCHAR* NativeCulture = TEXT("ru");

    FString NativeString(const FText& Text)
    {
        if (const FString* Source = FTextInspector::GetSourceString(Text)) return *Source;
        return FTextInspector::GetDisplayString(Text);
    }

    bool HasCopy(const FText& Text)
    {
        return !NativeString(Text).TrimStartAndEnd().IsEmpty();
    }

    FText DisplayCopy(const FText& Text)
    {
        // Explicit-culture preview copy must not be retranslated by the editor's global language.
        return FText::AsCultureInvariant(NativeString(Text));
    }

    FString CanonicalCulture(const FString& Name)
    {
        FString Result = Name.TrimStartAndEnd().Replace(TEXT("_"), TEXT("-"));
        if (Result.IsEmpty()) return Result;
        if (const FCulturePtr Culture = FInternationalization::Get().GetCulture(Result))
        {
            const FString Canonical = Culture->GetName();
            // UE can silently canonicalize malformed names to en-US-POSIX. Accept case/separator
            // normalization, but never treat that unrelated engine fallback as the requested UI culture.
            if (!Result.Equals(Canonical, ESearchCase::IgnoreCase)) return FString();
            Result = Canonical;
        }
        return Result;
    }

    TArray<FString> CultureChain(const FString& RequestedCulture)
    {
        auto& I18N = FInternationalization::Get();
        FString Requested = CanonicalCulture(RequestedCulture.IsEmpty()
            ? I18N.GetCurrentLanguage()->GetName() : RequestedCulture);
        if (Requested.IsEmpty()) Requested = NativeCulture;
        TArray<FString> Chain;
        Chain.Add(Requested);
        if (I18N.GetCulture(Requested))
            for (const FString& Parent : I18N.GetPrioritizedCultureNames(Requested)) Chain.AddUnique(Parent);
        return Chain;
    }

    FCultureRef FormattingCulture(const FString& RequestedCulture)
    {
        auto& I18N = FInternationalization::Get();
        if (const FCulturePtr Culture = I18N.GetCulture(CultureChain(RequestedCulture)[0])) return Culture.ToSharedRef();
        if (const FCulturePtr Culture = I18N.GetCulture(NativeCulture)) return Culture.ToSharedRef();
        return I18N.GetCurrentLanguage();
    }

    FText LocalNumber(const FFormatArgumentValue& Value, const FCultureRef& Culture, const FNumberFormattingOptions* Options)
    {
        switch (Value.GetType())
        {
        case EFormatArgumentType::Int: return FText::AsNumber(Value.GetIntValue(), Options, Culture);
        case EFormatArgumentType::UInt: return FText::AsNumber(Value.GetUIntValue(), Options, Culture);
        case EFormatArgumentType::Float: return FText::AsNumber(Value.GetFloatValue(), Options, Culture);
        case EFormatArgumentType::Double: return FText::AsNumber(Value.GetDoubleValue(), Options, Culture);
        default: return FText::GetEmpty();
        }
    }

    double NumericDouble(const FFormatArgumentValue& Value)
    {
        switch (Value.GetType())
        {
        case EFormatArgumentType::Int: return static_cast<double>(Value.GetIntValue());
        case EFormatArgumentType::UInt: return static_cast<double>(Value.GetUIntValue());
        case EFormatArgumentType::Float: return Value.GetFloatValue();
        case EFormatArgumentType::Double: return Value.GetDoubleValue();
        default: return 0;
        }
    }

    FFormatArgumentValue LocalArgument(const FFormatArgumentValue& Value, const FCultureRef& Culture)
    {
        if (Value.GetType() <= EFormatArgumentType::Double)
            return LocalNumber(Value, Culture, &FNumberFormattingOptions::DefaultNoGrouping());
        if (Value.GetType() == EFormatArgumentType::Text)
        {
            FHistoricTextNumericData Numeric;
            if (FTextInspector::GetHistoricNumericData(Value.GetTextValue(), Numeric))
            {
                const auto* Options = Numeric.FormatOptions.IsSet() ? &Numeric.FormatOptions.GetValue() : nullptr;
                if (Numeric.FormatType == FHistoricTextNumericData::EType::AsPercent)
                    return FText::AsPercent(NumericDouble(Numeric.SourceValue), Options, Culture);
                return LocalNumber(Numeric.SourceValue, Culture, Options);
            }
        }
        return Value;
    }

    /** Uses the engine's modifier-argument parser and expression evaluator, with an owned explicit culture. */
    class FLocalPluralModifier final : public ITextFormatArgumentModifier
    {
    public:
        FLocalPluralModifier(FCultureRef InCulture, ETextPluralType InType)
            : Culture(MoveTemp(InCulture)), Type(InType) {}

        static TSharedPtr<ITextFormatArgumentModifier> Create(const FTextFormatString& Args,
            FTextFormatPatternDefinitionConstRef PatternDefinition, const FCultureRef& Culture, ETextPluralType Type)
        {
            TSortedMap<FTextFormatString, FTextFormatString> Values;
            if (!ParseKeyValueArgs(Args, Values)) return nullptr;
            auto Result = MakeShared<FLocalPluralModifier>(Culture, Type);
            for (const auto& Value : Values)
            {
                const FString Name = FString::ConstructFromPtrSize(Value.Key.StringPtr, Value.Key.StringLen);
                if (Name != TEXT("zero") && Name != TEXT("one") && Name != TEXT("two")
                    && Name != TEXT("few") && Name != TEXT("many") && Name != TEXT("other")) return nullptr;
                FTextFormat Form = FTextFormat::FromString(FString::ConstructFromPtrSize(Value.Value.StringPtr, Value.Value.StringLen), PatternDefinition);
                if (!Form.IsValid()) return nullptr;
                Result->LongestForm = FMath::Max(Result->LongestForm, Value.Value.StringLen);
                Result->bUsesArguments |= Form.GetExpressionType() == FTextFormat::EExpressionType::Complex;
                Result->Forms.Add(Name, MoveTemp(Form));
            }
            if (!Result->Forms.Contains(TEXT("other"))) return nullptr;
            return Result;
        }

        virtual bool Validate(const FCultureRef&, TArray<FString>& OutErrors) const override
        {
            bool bValid = true;
            for (ETextPluralForm Required : Culture->GetValidPluralForms(Type))
            {
                if (!Forms.Contains(FormName(Required)))
                {
                    OutErrors.Add(TEXT("Missing plural form '") + FString(FormName(Required)) + TEXT("' for ") + Culture->GetName());
                    bValid = false;
                }
            }
            for (const auto& Form : Forms) bValid &= Form.Value.ValidatePattern(Culture, OutErrors);
            return bValid;
        }

        virtual void Evaluate(const FFormatArgumentValue& Value, const FPrivateTextFormatArguments& Args, FString& OutResult) const override
        {
            const FFormatArgumentValue* Number = &Value;
            int32 Multiplier = 1;
            FHistoricTextNumericData Numeric;
            if (Value.GetType() == EFormatArgumentType::Text && FTextInspector::GetHistoricNumericData(Value.GetTextValue(), Numeric))
            {
                Number = &Numeric.SourceValue;
                Multiplier = Numeric.FormatType == FHistoricTextNumericData::EType::AsPercent ? 100 : 1;
            }
            ETextPluralForm Selected = ETextPluralForm::Other;
            switch (Number->GetType())
            {
            case EFormatArgumentType::Int: Selected = Culture->GetPluralForm(Number->GetIntValue() * static_cast<int64>(Multiplier), Type); break;
            case EFormatArgumentType::UInt: Selected = Culture->GetPluralForm(Number->GetUIntValue() * static_cast<uint64>(Multiplier), Type); break;
            case EFormatArgumentType::Float: Selected = Culture->GetPluralForm(Number->GetFloatValue() * Multiplier, Type); break;
            case EFormatArgumentType::Double: Selected = Culture->GetPluralForm(Number->GetDoubleValue() * Multiplier, Type); break;
            default: break;
            }
            const FTextFormat* Form = Forms.Find(FormName(Selected));
            if (!Form) Form = Forms.Find(TEXT("other"));
            if (Form) OutResult += FTextFormatter::Format(*Form, Args);
        }

        virtual void GetFormatArgumentNames(TArray<FString>& OutNames) const override
        {
            for (const auto& Form : Forms) Form.Value.GetFormatArgumentNames(OutNames);
        }

        virtual void EstimateLength(int32& OutLength, bool& OutUsesFormatArgs) const override
        {
            OutLength = LongestForm;
            OutUsesFormatArgs = bUsesArguments;
        }

    private:
        static const TCHAR* FormName(ETextPluralForm Form)
        {
            switch (Form)
            {
            case ETextPluralForm::Zero: return TEXT("zero");
            case ETextPluralForm::One: return TEXT("one");
            case ETextPluralForm::Two: return TEXT("two");
            case ETextPluralForm::Few: return TEXT("few");
            case ETextPluralForm::Many: return TEXT("many");
            default: return TEXT("other");
            }
        }

        FCultureRef Culture;
        ETextPluralType Type;
        TMap<FString, FTextFormat> Forms;
        int32 LongestForm = 0;
        bool bUsesArguments = false;
    };

    /** Unique temporary keywords avoid replacing the engine's global plural/ordinal implementations. */
    class FScopedLocalModifiers
    {
    public:
        explicit FScopedLocalModifiers(const FCultureRef& Culture)
        {
            const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            PluralName = TEXT("MCUIPlural") + Suffix;
            OrdinalName = TEXT("MCUIOrdinal") + Suffix;
            FTextFormatter::Get().RegisterTextArgumentModifier(FTextFormatString(PluralName),
                [Culture](const FTextFormatString& Args, FTextFormatPatternDefinitionConstRef Definition)
                { return FLocalPluralModifier::Create(Args, Definition, Culture, ETextPluralType::Cardinal); });
            FTextFormatter::Get().RegisterTextArgumentModifier(FTextFormatString(OrdinalName),
                [Culture](const FTextFormatString& Args, FTextFormatPatternDefinitionConstRef Definition)
                { return FLocalPluralModifier::Create(Args, Definition, Culture, ETextPluralType::Ordinal); });
        }

        ~FScopedLocalModifiers()
        {
            FTextFormatter::Get().UnregisterTextArgumentModifier(FTextFormatString(PluralName));
            FTextFormatter::Get().UnregisterTextArgumentModifier(FTextFormatString(OrdinalName));
        }

        FString Rewrite(const FString& Pattern) const
        {
            FString Result;
            Result.Reserve(Pattern.Len());
            bool bInsideArgument = false;
            bool bAfterArgument = false;
            for (int32 Index = 0; Index < Pattern.Len(); ++Index)
            {
                // Preserve the engine's backtick escape sequences; expression parsing stays in UE.
                if (Pattern[Index] == TEXT('`') && Index + 1 < Pattern.Len())
                {
                    const TCHAR Next = Pattern[Index + 1];
                    if (Next == TEXT('`') || Next == TEXT('{') || Next == TEXT('}') || Next == TEXT('|'))
                    {
                        Result.AppendChar(Pattern[Index]); Result.AppendChar(Next); ++Index;
                        bAfterArgument = false; continue;
                    }
                }
                if (bAfterArgument && Pattern[Index] == TEXT('|') && Pattern.Mid(Index).StartsWith(TEXT("|plural("), ESearchCase::CaseSensitive))
                {
                    Result += TEXT("|") + PluralName; Index += 6; bAfterArgument = false; continue;
                }
                if (bAfterArgument && Pattern[Index] == TEXT('|') && Pattern.Mid(Index).StartsWith(TEXT("|ordinal("), ESearchCase::CaseSensitive))
                {
                    Result += TEXT("|") + OrdinalName; Index += 7; bAfterArgument = false; continue;
                }
                Result.AppendChar(Pattern[Index]);
                bAfterArgument = Pattern[Index] == TEXT('}') && bInsideArgument;
                if (Pattern[Index] == TEXT('{')) bInsideArgument = true;
                else if (Pattern[Index] == TEXT('}')) bInsideArgument = false;
            }
            return Result;
        }

    private:
        FString PluralName, OrdinalName;
    };

    template<typename TValue>
    const TValue* FindCulture(const TMap<FString, TValue>& Localizations, const FString& Culture)
    {
        const TValue* Found = nullptr;
        for (const auto& Entry : Localizations)
        {
            if (!CanonicalCulture(Entry.Key).Equals(Culture, ESearchCase::IgnoreCase)) continue;
            // Canonically duplicate keys are an authoring error, never a map-order-dependent choice.
            if (Found) return nullptr;
            Found = &Entry.Value;
        }
        return Found;
    }

    const FText* FindTranslation(const FMCUILocalizationRow& Row, const FString& Name)
    {
        const FText* Common=nullptr;
        if(Name.Equals(TEXT("ru"),ESearchCase::IgnoreCase)) Common=&Row.RU;
        else if(Name.Equals(TEXT("en"),ESearchCase::IgnoreCase)) Common=&Row.EN;
        else if(Name.Equals(TEXT("de"),ESearchCase::IgnoreCase)) Common=&Row.DE;
        else if(Name.Equals(TEXT("fr"),ESearchCase::IgnoreCase)) Common=&Row.FR;
        else if(Name.Equals(TEXT("es"),ESearchCase::IgnoreCase)) Common=&Row.ES;
        else if(Name.Equals(TEXT("pt-BR"),ESearchCase::IgnoreCase)) Common=&Row.PT_BR;
        else if(Name.Equals(TEXT("zh-Hans"),ESearchCase::IgnoreCase)) Common=&Row.ZH_HANS;
        else if(Name.Equals(TEXT("zh-Hant"),ESearchCase::IgnoreCase)) Common=&Row.ZH_HANT;
        else if(Name.Equals(TEXT("ja"),ESearchCase::IgnoreCase)) Common=&Row.JA;
        else if(Name.Equals(TEXT("ko"),ESearchCase::IgnoreCase)) Common=&Row.KO;
        if(Common && HasCopy(*Common)) return Common;
        if(const FText* Extra=FindCulture(Row.AdditionalLanguages,Name);Extra && HasCopy(*Extra)) return Extra;
        return FindCulture(Row.Localizations,Name); // Serialized compatibility for unmigrated/custom tables.
    }

    const FMCUILocalizationRow* FindTextRow(UDataTable* Table,FName Key)
    {
        return Table && Table->GetRowStruct()==FMCUILocalizationRow::StaticStruct() && !Key.IsNone()
            ? Table->FindRow<FMCUILocalizationRow>(Key,TEXT("UI localization lookup"),false) : nullptr;
    }

    template<typename TCopyLookup>
    FText ResolveField(const FText& Source, const TArray<FString>& Chain, TCopyLookup Lookup, bool& bUsedFallback)
    {
        for (int32 Index = 0; Index < Chain.Num(); ++Index)
        {
            if (const FText* Candidate = Lookup(Chain[Index]); Candidate && HasCopy(*Candidate))
            {
                bUsedFallback |= Index > 0;
                return DisplayCopy(*Candidate);
            }
            if (Chain[Index].Equals(NativeCulture, ESearchCase::IgnoreCase) && HasCopy(Source))
            {
                bUsedFallback |= Index > 0;
                return DisplayCopy(Source);
            }
        }
        bUsedFallback = true;
        return DisplayCopy(Source);
    }

    FText ResolveCatalogField(UDataTable* Table,FName Key,const FText& Fallback,
        const FString& Culture,bool& bUsedFallback)
    {
        if(Key.IsNone()) return Fallback;
        const auto* Row=FindTextRow(Table,Key);
        if(!Row || !HasCopy(Row->GetNativeText())) {bUsedFallback=true;return Fallback;}
        return ResolveField(Row->GetNativeText(),CultureChain(Culture),[&](const FString& Name)
        {return FindTranslation(*Row,Name);},bUsedFallback);
    }

    template<typename TRow>
    FMCUIResolvedContent ResolveRow(const TRow* Row, const FString& Culture, UDataTable* LocalizationTable)
    {
        FMCUIResolvedContent Result;
        if (!Row)
        {
            Result.bUsedFallback = true;
            return Result;
        }
        Result.bFound = true;
        Result.Image = Row->Image;
        const TArray<FString> Chain = CultureChain(Culture);
        bool bNameFallback=false,bDescriptionFallback=false;
        Result.DisplayName = ResolveField(Row->DisplayName, Chain, [&](const FString& Name) -> const FText*
        {
            const auto* Copy = FindCulture(Row->Localizations, Name);
            return Copy ? &Copy->DisplayName : nullptr;
        }, bNameFallback);
        Result.Description = ResolveField(Row->Description, Chain, [&](const FString& Name) -> const FText*
        {
            const auto* Copy = FindCulture(Row->Localizations, Name);
            return Copy ? &Copy->Description : nullptr;
        }, bDescriptionFallback);
        UDataTable* Catalogue=LocalizationTable?LocalizationTable:UMCUIDataLibrary::GetDefaultLocalizationTable();
        if(const auto* Text=FindTextRow(Catalogue,Row->DisplayNameKey);Text && HasCopy(Text->GetNativeText())) bNameFallback=false;
        if(const auto* Text=FindTextRow(Catalogue,Row->DescriptionKey);Text && HasCopy(Text->GetNativeText())) bDescriptionFallback=false;
        Result.DisplayName=ResolveCatalogField(Catalogue,Row->DisplayNameKey,Result.DisplayName,Culture,bNameFallback);
        Result.Description=ResolveCatalogField(Catalogue,Row->DescriptionKey,Result.Description,Culture,bDescriptionFallback);
        Result.bUsedFallback=bNameFallback || bDescriptionFallback;
        return Result;
    }

    template<typename TRow>
    FMCUIResolvedContent ResolveHandle(const FDataTableRowHandle& Handle, const FString& Culture, UDataTable* LocalizationTable)
    {
        const auto* Row = Handle.DataTable && Handle.DataTable->GetRowStruct() == TRow::StaticStruct() && !Handle.RowName.IsNone()
            ? Handle.DataTable->FindRow<TRow>(Handle.RowName, TEXT("UI presentation lookup"), false) : nullptr;
        return ResolveRow(Row, Culture, LocalizationTable);
    }

    template<typename TRow>
    FMCUIResolvedContent FindGameplayRow(UDataTable* Table, FName GameplayPerkID, const FString& Culture, UDataTable* LocalizationTable)
    {
        const TRow* Found = nullptr;
        if (Table && Table->GetRowStruct() == TRow::StaticStruct() && !GameplayPerkID.IsNone())
        {
            for (const auto& Entry : Table->GetRowMap())
            {
                const auto* Row = reinterpret_cast<const TRow*>(Entry.Value);
                if (!Row || Row->GameplayPerkID != GameplayPerkID) continue;
                if (Found) return ResolveRow<TRow>(nullptr, Culture, LocalizationTable);
                Found = Row;
            }
        }
        return ResolveRow(Found, Culture, LocalizationTable);
    }

    UDataTable* LoadDefault(const TCHAR* Path, const UScriptStruct* ExpectedStruct, TWeakObjectPtr<UDataTable>& Cache)
    {
        UDataTable* Table = Cache.Get();
        if (!Table) Table = LoadObject<UDataTable>(nullptr, Path, nullptr, LOAD_NoWarn);
        if (!Table || Table->GetRowStruct() != ExpectedStruct) return nullptr;
        Cache = Table;
        return Table;
    }

    void ValidatePattern(const FText& Text, const FString& CultureName, const FString& Location, TArray<FString>& Problems)
    {
        const FCulturePtr Culture = FInternationalization::Get().GetCulture(CultureName);
        if (!Culture) return; // The culture-key check reports this once per variant.
        const FScopedLocalModifiers Modifiers(Culture.ToSharedRef());
        TArray<FString> Errors;
        if (!FTextFormat::FromString(Modifiers.Rewrite(NativeString(Text))).ValidatePattern(Culture, Errors))
            for (const FString& Error : Errors) Problems.Add(Location + TEXT(": ") + Error);
    }

    void ValidateParameters(const FText& Source, const FText& Translation, const FString& Location, TArray<FString>& Problems)
    {
        TArray<FString> SourceParameters, TranslatedParameters;
        const FScopedLocalModifiers Modifiers(FormattingCulture(NativeCulture));
        FText::GetFormatPatternParameters(FTextFormat::FromString(Modifiers.Rewrite(NativeString(Source))), SourceParameters);
        FText::GetFormatPatternParameters(FTextFormat::FromString(Modifiers.Rewrite(NativeString(Translation))), TranslatedParameters);
        SourceParameters.Sort();
        TranslatedParameters.Sort();
        if (SourceParameters != TranslatedParameters) Problems.Add(Location + TEXT(": format parameters differ from source"));
    }

    template<typename TValue>
    void ValidateCultures(const TMap<FString, TValue>& Variants, const FString& Location, TArray<FString>& Problems)
    {
        TSet<FString> Seen;
        for (const auto& Entry : Variants)
        {
            const FString Canonical = CanonicalCulture(Entry.Key);
            if (Canonical.IsEmpty() || !FInternationalization::Get().GetCulture(Canonical))
                Problems.Add(Location + TEXT(": invalid culture key '") + Entry.Key + TEXT("'"));
            const FString ComparisonKey = Canonical.ToLower();
            if (Seen.Contains(ComparisonKey)) Problems.Add(Location + TEXT(": duplicate canonical culture '") + Canonical + TEXT("'"));
            Seen.Add(ComparisonKey);
        }
    }

    template<typename TRow>
    void ValidateRows(const UDataTable* Table, TArray<FString>& Problems, UDataTable* LocalizationTable)
    {
        TMap<FName, FName> GameplayRows;
        for (const auto& Entry : Table->GetRowMap())
        {
            const auto* Row = reinterpret_cast<const TRow*>(Entry.Value);
            const FString Location = Entry.Key.ToString();
            if (!Row) { Problems.Add(Location + TEXT(": missing row payload")); continue; }
            if (Row->GameplayPerkID.IsNone()) Problems.Add(Location + TEXT(": GameplayPerkID is empty"));
            else if (const FName* Previous = GameplayRows.Find(Row->GameplayPerkID))
                Problems.Add(Location + TEXT(": duplicate GameplayPerkID '") + Row->GameplayPerkID.ToString()
                    + TEXT("' also used by ") + Previous->ToString());
            else GameplayRows.Add(Row->GameplayPerkID, Entry.Key);
            if(!Row->DisplayNameKey.IsNone() || !Row->DescriptionKey.IsNone())
            {
                UDataTable* Catalogue=LocalizationTable?LocalizationTable:UMCUIDataLibrary::GetDefaultLocalizationTable();
                for(FName Key:{Row->DisplayNameKey,Row->DescriptionKey})
                    if(!Key.IsNone() && !FindTextRow(Catalogue,Key)) Problems.Add(Location+TEXT(": missing catalogue key ")+Key.ToString());
                const auto Copy=ResolveRow(Row,NativeCulture,Catalogue);
                if(!HasCopy(Copy.DisplayName)) Problems.Add(Location+TEXT(": resolved DisplayName is empty"));
                if(!HasCopy(Copy.Description)) Problems.Add(Location+TEXT(": resolved Description is empty"));
                ValidatePattern(Copy.DisplayName,NativeCulture,Location+TEXT(".DisplayName"),Problems);
                ValidatePattern(Copy.Description,NativeCulture,Location+TEXT(".Description"),Problems);
                // The shared catalogue validates keyed translations. An unkeyed field still
                // uses its serialized legacy copy and needs its own validation below.
            }
            const bool bLegacyName=Row->DisplayNameKey.IsNone();
            const bool bLegacyDescription=Row->DescriptionKey.IsNone();
            if(!bLegacyName && !bLegacyDescription) continue;
            if(bLegacyName)
            {
                if (!HasCopy(Row->DisplayName)) Problems.Add(Location + TEXT(": source DisplayName is empty"));
                ValidatePattern(Row->DisplayName, NativeCulture, Location + TEXT(".DisplayName"), Problems);
            }
            if(bLegacyDescription)
            {
                if (!HasCopy(Row->Description)) Problems.Add(Location + TEXT(": source Description is empty"));
                ValidatePattern(Row->Description, NativeCulture, Location + TEXT(".Description"), Problems);
            }
            ValidateCultures(Row->Localizations, Location, Problems);
            for (const auto& Variant : Row->Localizations)
            {
                const FString VariantLocation = Location + TEXT("[") + Variant.Key + TEXT("]");
                if (bLegacyName && bLegacyDescription && !HasCopy(Variant.Value.DisplayName) && !HasCopy(Variant.Value.Description))
                    Problems.Add(VariantLocation + TEXT(": empty localization; source copy will be used"));
                if (bLegacyName && HasCopy(Variant.Value.DisplayName))
                {
                    ValidatePattern(Variant.Value.DisplayName, CanonicalCulture(Variant.Key), VariantLocation + TEXT(".DisplayName"), Problems);
                    ValidateParameters(Row->DisplayName, Variant.Value.DisplayName, VariantLocation + TEXT(".DisplayName"), Problems);
                }
                if (bLegacyDescription && HasCopy(Variant.Value.Description))
                {
                    ValidatePattern(Variant.Value.Description, CanonicalCulture(Variant.Key), VariantLocation + TEXT(".Description"), Problems);
                    ValidateParameters(Row->Description, Variant.Value.Description, VariantLocation + TEXT(".Description"), Problems);
                }
            }
        }
    }
}

UDataTable* UMCUIDataLibrary::GetDefaultLocalizationTable()
{
    static TWeakObjectPtr<UDataTable> Cache;
    return MCUIDataPrivate::LoadDefault(TEXT("/Game/Gameplay/UI/Data/DT_UI_Localization.DT_UI_Localization"), FMCUILocalizationRow::StaticStruct(), Cache);
}

UDataTable* UMCUIDataLibrary::GetDefaultBoosterTable()
{
    static TWeakObjectPtr<UDataTable> Cache;
    return MCUIDataPrivate::LoadDefault(TEXT("/Game/Gameplay/UI/Data/DT_UI_Boosters.DT_UI_Boosters"), FMCUIBoosterRow::StaticStruct(), Cache);
}

UDataTable* UMCUIDataLibrary::GetDefaultCardTable()
{
    static TWeakObjectPtr<UDataTable> Cache;
    return MCUIDataPrivate::LoadDefault(TEXT("/Game/Gameplay/UI/Data/DT_UI_Cards.DT_UI_Cards"), FMCUICardRow::StaticStruct(), Cache);
}

FText UMCUIDataLibrary::ResolveText(UDataTable* Table, FName Key, const FText& Fallback, const FString& Culture)
{
    using namespace MCUIDataPrivate;
    bool bUsedFallback=false;
    return ResolveCatalogField(Table,Key,DisplayCopy(Fallback),Culture,bUsedFallback);
}

FText UMCUIDataLibrary::FormatTextForCulture(const FText& Pattern, const FFormatNamedArguments& Arguments, const FString& Culture)
{
    using namespace MCUIDataPrivate;
    const FCultureRef TargetCulture = FormattingCulture(Culture);
    const FString SourcePattern = FTextInspector::GetDisplayString(Pattern);
    TUniquePtr<FScopedLocalModifiers> Modifiers;
    FString LocalPattern = SourcePattern;
    if (SourcePattern.Contains(TEXT("|plural("), ESearchCase::CaseSensitive)
        || SourcePattern.Contains(TEXT("|ordinal("), ESearchCase::CaseSensitive))
    {
        Modifiers = MakeUnique<FScopedLocalModifiers>(TargetCulture);
        LocalPattern = Modifiers->Rewrite(SourcePattern);
    }
    const FTextFormat Format = FTextFormat::FromString(LocalPattern);
    TArray<FString> Errors;
    if (!Format.IsValid() || !Format.ValidatePattern(TargetCulture, Errors)) return FText::AsCultureInvariant(SourcePattern);
    FFormatNamedArguments LocalArguments;
    for (const auto& Argument : Arguments) LocalArguments.Add(Argument.Key, LocalArgument(Argument.Value, TargetCulture));
    // Materialize immediately: no global-language-dependent text history or temporary modifier names escape this call.
    return FText::AsCultureInvariant(FTextFormatter::FormatStr(Format, LocalArguments, false, false));
}

FText UMCUIDataLibrary::NumberForCulture(int64 Value, const FString& Culture)
{
    return FText::AsNumber(Value, &FNumberFormattingOptions::DefaultNoGrouping(), MCUIDataPrivate::FormattingCulture(Culture));
}

FText UMCUIDataLibrary::DecimalForCulture(double Value, int32 FractionalDigits, const FString& Culture)
{
    FNumberFormattingOptions Options = FNumberFormattingOptions::DefaultNoGrouping();
    const int32 Digits = FMath::Clamp(FractionalDigits, 0, 6);
    Options.SetMinimumFractionalDigits(Digits).SetMaximumFractionalDigits(Digits);
    return FText::AsNumber(Value, &Options, MCUIDataPrivate::FormattingCulture(Culture));
}

FMCUIResolvedContent UMCUIDataLibrary::ResolveBooster(const FDataTableRowHandle& RowHandle, const FString& Culture, UDataTable* LocalizationTable)
{
    return MCUIDataPrivate::ResolveHandle<FMCUIBoosterRow>(RowHandle, Culture, LocalizationTable);
}

FMCUIResolvedContent UMCUIDataLibrary::FindBoosterForGameplayID(UDataTable* Table, FName GameplayPerkID, const FString& Culture, UDataTable* LocalizationTable)
{
    return MCUIDataPrivate::FindGameplayRow<FMCUIBoosterRow>(Table, GameplayPerkID, Culture, LocalizationTable);
}

FMCUIResolvedContent UMCUIDataLibrary::ResolveCard(const FDataTableRowHandle& RowHandle, const FString& Culture, UDataTable* LocalizationTable)
{
    return MCUIDataPrivate::ResolveHandle<FMCUICardRow>(RowHandle, Culture, LocalizationTable);
}

FMCUIResolvedContent UMCUIDataLibrary::FindCardForGameplayID(UDataTable* Table, FName GameplayPerkID, const FString& Culture, UDataTable* LocalizationTable)
{
    return MCUIDataPrivate::FindGameplayRow<FMCUICardRow>(Table, GameplayPerkID, Culture, LocalizationTable);
}

bool UMCUIDataLibrary::ValidateContentTable(UDataTable* Table, TArray<FString>& OutProblems, UDataTable* LocalizationTable)
{
    using namespace MCUIDataPrivate;
    OutProblems.Reset();
    if (!Table) OutProblems.Add(TEXT("No UI data table selected"));
    else if (Table->GetRowStruct() == FMCUIBoosterRow::StaticStruct()) ValidateRows<FMCUIBoosterRow>(Table, OutProblems, LocalizationTable);
    else if (Table->GetRowStruct() == FMCUICardRow::StaticStruct()) ValidateRows<FMCUICardRow>(Table, OutProblems, LocalizationTable);
    else if (Table->GetRowStruct() == FMCUILocalizationRow::StaticStruct())
    {
        for (const auto& Entry : Table->GetRowMap())
        {
            const auto* Row = reinterpret_cast<const FMCUILocalizationRow*>(Entry.Value);
            const FString Location = Entry.Key.ToString();
            if (!Row) { OutProblems.Add(Location + TEXT(": missing row payload")); continue; }
            const FText& Native=Row->GetNativeText();
            if(!HasCopy(Native)) OutProblems.Add(Location+TEXT(": RU/native copy is empty"));
            ValidatePattern(Native,NativeCulture,Location,OutProblems);
            const auto ValidateCopy=[&](const FText& Text,const FString& Culture,const FString& Field)
            {
                if(!HasCopy(Text)) return; // Empty optional languages deliberately fall back to RU.
                ValidatePattern(Text,Culture,Location+TEXT(".")+Field,OutProblems);
                ValidateParameters(Native,Text,Location+TEXT(".")+Field,OutProblems);
            };
            ValidateCopy(Row->EN,TEXT("en"),TEXT("EN"));ValidateCopy(Row->DE,TEXT("de"),TEXT("DE"));
            ValidateCopy(Row->FR,TEXT("fr"),TEXT("FR"));ValidateCopy(Row->ES,TEXT("es"),TEXT("ES"));
            ValidateCopy(Row->PT_BR,TEXT("pt-BR"),TEXT("PT_BR"));
            ValidateCopy(Row->ZH_HANS,TEXT("zh-Hans"),TEXT("ZH_HANS"));ValidateCopy(Row->ZH_HANT,TEXT("zh-Hant"),TEXT("ZH_HANT"));
            ValidateCopy(Row->JA,TEXT("ja"),TEXT("JA"));ValidateCopy(Row->KO,TEXT("ko"),TEXT("KO"));
            ValidateCultures(Row->AdditionalLanguages,Location+TEXT(".AdditionalLanguages"),OutProblems);
            for(const auto& Variant:Row->AdditionalLanguages)
            {
                const FString Field=TEXT("AdditionalLanguages[")+Variant.Key+TEXT("]");
                if(!HasCopy(Variant.Value)) OutProblems.Add(Location+TEXT(".")+Field+TEXT(": translation is empty"));
                else ValidateCopy(Variant.Value,CanonicalCulture(Variant.Key),Field);
            }
            ValidateCultures(Row->Localizations,Location+TEXT(".Legacy"),OutProblems);
            // Validate only still-active legacy translations; migrated columns are authoritative.
            for(const auto& Variant:Row->Localizations)
            {
                const FString Culture=CanonicalCulture(Variant.Key);
                if(FindTranslation(*Row,Culture)!=&Variant.Value) continue;
                if(!HasCopy(Variant.Value)) OutProblems.Add(Location+TEXT("[Legacy ")+Variant.Key+TEXT("]: translation is empty"));
                else ValidateCopy(Variant.Value,Culture,TEXT("Legacy[")+Variant.Key+TEXT("]"));
            }
        }
    }
    else OutProblems.Add(TEXT("Unsupported UI row struct; expected MCUILocalizationRow, MCUIBoosterRow or MCUICardRow"));
    return OutProblems.IsEmpty();
}
