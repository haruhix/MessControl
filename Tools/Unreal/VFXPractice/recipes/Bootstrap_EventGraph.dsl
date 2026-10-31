; Root assigns the task map's native MCVFXLab actor to Lab before saving.
(event EventBeginPlay
  @set:bReleased:false@
  @set:bFailed:false@
  @set:Status:"Preparing practice resources"@
  (bind lab @get:Lab@)
  (Utilities|IsValid :InputObject lab
    (:"Is Valid"
      (VFXLab|SetRunning :self lab :bEnabled false)
      (bind prep (WorldSubsystems|GetMCWorldPreloadSubsystem))
      (Utilities|IsValid :InputObject prep
        (:"Is Valid"
          (bind pc (Game|GetPlayerController :PlayerIndex 0))
          (Utilities|IsValid :InputObject pc
            (:"Is Valid"
              (Actor|Tick|SetActorTickEnabled :self pc :bEnabled false)
              (Loading|BeginPreparation :self prep :LocalController pc))
            (:"Is Not Valid" @set:Status:"Waiting for local spectator controller"@)))
        (:"Is Not Valid"
          @set:bFailed:true@
          @set:Status:"FAIL: preload subsystem unavailable"@
          (Development|PrintString :InString "VFX practice: preload subsystem unavailable; effects remain stopped." :Duration 120.0 :Key "VFXPracticeReadiness"))))
    (:"Is Not Valid"
      @set:bFailed:true@
      @set:Status:"FAIL: assign this map's Lab reference"@
      (Development|PrintString :InString "VFX practice: assign Bootstrap.Lab; effects remain stopped." :Duration 120.0 :Key "VFXPracticeReadiness"))))

(event AddEvent|EventTick (DeltaSeconds)
  (if (and (not @get:bReleased@) (not @get:bFailed@))
    (bind prep (WorldSubsystems|GetMCWorldPreloadSubsystem))
    (Utilities|IsValid :InputObject prep
      (:"Is Valid"
        (bind pc (Game|GetPlayerController :PlayerIndex 0))
        (Utilities|IsValid :InputObject pc
          (:"Is Valid"
            ; BeginPreparation is idempotent. Retry handles late controller creation.
            (Actor|Tick|SetActorTickEnabled :self pc :bEnabled false)
            (Loading|BeginPreparation :self prep :LocalController pc)
            (if (Loading|HasFailed :self prep)
              @set:bFailed:true@
              @set:Status:"FAIL: resource preparation failed; stop PIE to retry"@
              (VFXLab|SetRunning :self @get:Lab@ :bEnabled false)
              (Development|PrintString :InString "VFX practice: resource preparation failed; effects remain stopped. Stop PIE to retry; inspect MCWorldPreload log." :Duration 120.0 :Key "VFXPracticeReadiness")
              (elif (Loading|IsReady :self prep)
                @set:bReleased:true@
                @set:Status:"Ready: F3 stations / P pause / R reset"@
                (Actor|Tick|SetActorTickEnabled :self pc :bEnabled true)
                (VFXLab|SetRunning :self @get:Lab@ :bEnabled true)
                (Development|PrintString :InString "VFX practice ready. F3: stations. P: pause. R: reset." :Duration 6.0 :Key "VFXPracticeReadiness"))))
          (:"Is Not Valid" @set:Status:"Waiting for local spectator controller"@)))
      (:"Is Not Valid"
        @set:bFailed:true@
        @set:Status:"FAIL: preparation reference lost"@
        (VFXLab|SetRunning :self @get:Lab@ :bEnabled false)))))
