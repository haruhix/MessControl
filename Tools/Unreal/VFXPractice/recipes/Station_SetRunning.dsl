; Exact function name/signature consumed by native MCVFXLab dispatcher.
(fn SetRunning (bEnabled)
  (if (!= bEnabled @get:bRunning@)
    (if bEnabled
      @set:bRunning:true@
      @call:Play@
      (else
        @set:bRunning:false@
        (bind fx @get:PracticeFX@)
        (if @get:bTrail@
          (Niagara|SetNiagaraVariable(Vector3) :self fx :InVariableName "User.Velocity" :InValue (Math|Vector|MakeVector :X 0.0 :Y 0.0 :Z 0.0)))
        (Components|Activation|Deactivate :self fx)
        @set:Status:"Paused; existing particles finish naturally"@))))
