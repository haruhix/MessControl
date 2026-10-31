; Native lab R resets only the selected task station and its emitter.
(fn Reset ()
  (bind resume @get:bRunning@)
  (bind fx @get:PracticeFX@)
  @set:Cycles:0@
  @set:MotionPhase:0.0@
  @set:CycleAge:0.0@
  (Components|Activation|Deactivate :self fx)
  (Transformation|SetRelativeLocation :self fx :NewLocation @get:EmitterOrigin@ :bSweep false :bTeleport true)
  @set:PreviousWorld:(Transformation|GetWorldLocation :self fx)@
  @call:ConfigureFX@
  (Niagara|ReinitializeSystem :self fx)
  (Components|Activation|Deactivate :self fx)
  (if resume @call:Play@ (else @set:Status:"Reset; paused"@)))
