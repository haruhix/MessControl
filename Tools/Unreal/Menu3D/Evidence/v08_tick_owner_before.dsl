(event UserInterface|EventPreConstruct (IsDesignTime))

(event UserInterface|EventConstruct
  (Variables|Default|SetPageIndex 0)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetOpen3 1.0)
  (Variables|Default|SetSelectedDay 3)
  (Variables|Default|SetWindowFirstDay 1)
  (Variables|Default|SetSnapRemaining 0.0)
  (Variables|Default|SetWheelAccumulator 0.0)
  (CallFunction|FindStageActors)
  (CallFunction|ShowPage 0))

(event UserInterface|EventTick (MyGeometry InDeltaTime)
  (CallFunction|UpdateStage))

(event OnClicked(PlayButton)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetSelectedDay 1)
  (Variables|Default|SetWindowFirstDay 1)
  (Variables|Default|SetOpen1 0.0)
  (Variables|Default|SetOpen2 0.0)
  (Variables|Default|SetOpen3 0.0)
  (Variables|Default|SetOpen4 0.0)
  (Variables|Default|SetOpen5 0.0)
  (Variables|Default|SetOpen6 0.0)
  (Variables|Default|SetOpen7 0.0)
  (Variables|Default|SetHoverQuiet 0.25)
  (Variables|Default|SetSnapRemaining 0.6)
  (Variables|Default|SetWheelAccumulator 0.0)
  (Variables|Default|SetStageScroll 0.0)
  (Variables|Default|SetStageScrollTarget 0.0)
  (Widget|SetScrollOffset (Variables|WBP_MainMenu3D|GetDayScroll))
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetDayCounter) "NSLOCTEXT(\"\", \"DCEC264C40E776AABB461C8A319E67CF\", \"01 / 07\")")
  (CallFunction|ShowPage 1)
  (CallFunction|ResetPointerIntent)
  (CallFunction|CenterSelectedDay))

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
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetSettingsStatus) "NSLOCTEXT(\"\", \"494FD6FC420C9E8C501EBD8A4E47E733\", \"Качество Low применено и сохранено.\")"))

(event OnClicked(QualityHighButton)
  (bind _returnvalue (Settings|GetGameUserSettings))
  (Settings|SetOverallScalabilityLevel _returnvalue 2)
  (Settings|ApplySettings _returnvalue false)
  (Settings|SaveSettings _returnvalue)
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetSettingsStatus) "NSLOCTEXT(\"\", \"A9D7D81944A6052427538EBC4592D78D\", \"Качество High применено и сохранено.\")"))

(event OnClicked(QualityEpicButton)
  (bind _returnvalue (Settings|GetGameUserSettings))
  (Settings|SetOverallScalabilityLevel _returnvalue 3)
  (Settings|ApplySettings _returnvalue false)
  (Settings|SaveSettings _returnvalue)
  (Widget|SetText(Text) (Variables|WBP_MainMenu3D|GetSettingsStatus) "NSLOCTEXT(\"\", \"6DA90F9E4249BBED8808B9858EBCCBD8\", \"Качество Epic применено и сохранено.\")"))

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
  (CallFunction|ResetPointerIntent)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetHoverQuiet 1.2)
  (Variables|Default|SetSnapRemaining 1.0)
  (Widget|SetKeyboardFocus (Variables|WBP_MainMenu3D|GetDaysBackButton)))

(event OnClicked(Day2PlayButton)
  (CallFunction|OpenConnection))

(event OnClicked(Day2CollapseButton)
  (CallFunction|ResetPointerIntent)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetHoverQuiet 1.2)
  (Variables|Default|SetSnapRemaining 1.0)
  (Widget|SetKeyboardFocus (Variables|WBP_MainMenu3D|GetDaysBackButton)))

(event OnClicked(Day3PlayButton)
  (CallFunction|OpenConnection))

(event OnClicked(Day3CollapseButton)
  (CallFunction|ResetPointerIntent)
  (Variables|Default|SetActiveDay 0)
  (Variables|Default|SetHoverQuiet 1.2)
  (Variables|Default|SetSnapRemaining 1.0)
  (Widget|SetKeyboardFocus (Variables|WBP_MainMenu3D|GetDaysBackButton)))
