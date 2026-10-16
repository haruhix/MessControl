(event UserInterface|EventPreConstruct (IsDesignTime))

(event UserInterface|EventConstruct
  (Variables|Default|SetPageIndex 0)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetOpen3 1.0)
  (CallFunction|FindStageActors)
  (CallFunction|ShowPage 0))

(event UserInterface|EventTick (MyGeometry InDeltaTime)
  (CallFunction|UpdateStage))

(event OnClicked(PlayButton)
  (Variables|Default|SetActiveDay 3)
  (CallFunction|ShowPage 1))

(event OnClicked(SettingsButton)
  (CallFunction|ShowPage 2))

(event OnClicked(CreditsButton)
  (CallFunction|ShowPage 3))

(event OnClicked(ExitButton)
  (CallFunction|ShowPage 4))

(event OnClicked(Day1Button)
  (CallFunction|SelectDay 1))

(event OnClicked(Day2Button)
  (CallFunction|SelectDay 2))

(event OnClicked(Day3Button)
  (CallFunction|SelectDay 3))

(event OnClicked(Day4Button)
  (CallFunction|SelectDay 4))

(event OnClicked(Day5Button)
  (CallFunction|SelectDay 5))

(event OnClicked(Day6Button)
  (CallFunction|SelectDay 6))

(event OnClicked(Day7Button)
  (CallFunction|SelectDay 7))

(event OnClicked(DaysBackButton)
  (CallFunction|ShowPage 0))

(event OnClicked(QualityLowButton)
  (bind _returnvalue (Settings|GetGameUserSettings))
  (Settings|SetOverallScalabilityLevel _returnvalue)
  (Settings|ApplySettings _returnvalue false)
  (Settings|SaveSettings _returnvalue)
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetSettingsStatus) "NSLOCTEXT(\"\", \"400AD07E4CCD9CFCCDA8288D49AC0D62\", \"Качество Low применено и сохранено.\")"))

(event OnClicked(QualityHighButton)
  (bind _returnvalue (Settings|GetGameUserSettings))
  (Settings|SetOverallScalabilityLevel _returnvalue 2)
  (Settings|ApplySettings _returnvalue false)
  (Settings|SaveSettings _returnvalue)
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetSettingsStatus) "NSLOCTEXT(\"\", \"1CF9650741BF41985CB0C699A60F5C52\", \"Качество High применено и сохранено.\")"))

(event OnClicked(QualityEpicButton)
  (bind _returnvalue (Settings|GetGameUserSettings))
  (Settings|SetOverallScalabilityLevel _returnvalue 3)
  (Settings|ApplySettings _returnvalue false)
  (Settings|SaveSettings _returnvalue)
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetSettingsStatus) "NSLOCTEXT(\"\", \"53A958CE491CA58D713EAE9D10A33D01\", \"Качество Epic применено и сохранено.\")"))

(event OnClicked(FullscreenButton)
  (bind _returnvalue (Settings|GetGameUserSettings))
  (if (|GetbBorderless)
    (Settings|SetFullscreenMode _returnvalue "Windowed")
    (|SetbBorderless false)
    (else
      (Settings|SetFullscreenMode _returnvalue "WindowedFullscreen")
      (|SetbBorderless true)))
  (Settings|ApplySettings _returnvalue false)
  (Settings|SaveSettings _returnvalue))

(event OnClicked(SettingsBackButton)
  (CallFunction|ShowPage 0))

(event OnClicked(CreditsBackButton)
  (CallFunction|ShowPage 0))

(event OnClicked(ExitCancelButton)
  (CallFunction|ShowPage 0))

(event OnClicked(ExitConfirmButton)
  (Game|QuitGame (Widget|GetOwningPlayer)))

(event OnClicked(LaunchBackButton)
  (CallFunction|Back))

(event OnClicked(Day1PlayButton)
  (CallFunction|OpenConnection))

(event OnClicked(Day1CollapseButton)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetHoverQuiet 0.5)
  (Widget|SetKeyboardFocus (Variables|WBP_MainMenu3D|GetDaysBackButton)))

(event OnClicked(Day2PlayButton)
  (CallFunction|OpenConnection))

(event OnClicked(Day2CollapseButton)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetHoverQuiet 0.5)
  (Widget|SetKeyboardFocus (Variables|WBP_MainMenu3D|GetDaysBackButton)))

(event OnClicked(Day3PlayButton)
  (CallFunction|OpenConnection))

(event OnClicked(Day3CollapseButton)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetHoverQuiet 0.5)
  (Widget|SetKeyboardFocus (Variables|WBP_MainMenu3D|GetDaysBackButton)))
