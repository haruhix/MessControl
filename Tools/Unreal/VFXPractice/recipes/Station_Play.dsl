; Explicit play/repeat reinitializes this component, never another station.
(fn Play ()
  (bind prep (WorldSubsystems|GetMCWorldPreloadSubsystem))
  (Utilities|IsValid :InputObject prep
    (:"Is Valid"
      (if (Loading|IsReady :self prep)
        (bind fx @get:PracticeFX@)
        (bind system (Niagara|GetNiagaraSystemAsset :self fx))
        (Utilities|IsValid :InputObject system
          (:"Is Valid"
            @call:ConfigureFX@
            @set:CycleAge:0.0@
            (Niagara|ReinitializeSystem :self fx)
            (Components|Activation|Activate :self fx :bReset true)
            @set:Cycles:(+ @get:Cycles@ 1)@
            @set:Status:"RUN: isolated visual practice"@)
          (:"Is Not Valid" @set:bRunning:false@ @set:Status:"FAIL: assign PracticeFX Niagara System"@))
        (else @set:bRunning:false@ @set:Status:"Preparing resources"@)))
    (:"Is Not Valid" @set:bRunning:false@ @set:Status:"FAIL: preload subsystem unavailable"@)))
