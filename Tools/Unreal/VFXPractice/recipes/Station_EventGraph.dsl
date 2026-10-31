(event EventBeginPlay
  (bind fx @get:PracticeFX@)
  (Components|Activation|SetAutoActivate :self fx :bNewAutoActivate false)
  (Components|Activation|Deactivate :self fx)
  @set:bRunning:false@
  @set:EmitterOrigin:(Class|SceneComponent|GetRelativeLocation :self fx)@
  @set:PreviousWorld:(Transformation|GetWorldLocation :self fx)@
  @set:Status:"Preparing resources"@
  @call:ConfigureFX@)

(event AddEvent|EventTick (DeltaSeconds)
  (if @get:bRunning@
    (bind dt (select (> DeltaSeconds 0.0001) DeltaSeconds 0.0001))
    (bind fx @get:PracticeFX@)
    @set:CycleAge:(+ @get:CycleAge@ DeltaSeconds)@
    (if @get:bTrail@
      (bind amplitude (select (> @get:TrailAmplitude@ 1.0) @get:TrailAmplitude@ 1.0))
      @set:MotionPhase:(+ @get:MotionPhase@ (* DeltaSeconds (/ @get:TrailSpeed@ amplitude)))@
      (bind axis (Math|Vector|Normalize :A @get:TrailDirection@ :Tolerance 0.0001))
      (bind offset (* axis (* amplitude (Math|Trig|Sin(Radians) :A @get:MotionPhase@))))
      (Transformation|SetRelativeLocation :self fx :NewLocation (+ @get:EmitterOrigin@ offset) :bSweep false :bTeleport false)
      (bind position (Transformation|GetWorldLocation :self fx))
      (bind velocity (/ (- position @get:PreviousWorld@) dt))
      (bind direction (Math|Vector|Normalize :A velocity :Tolerance 0.0001))
      (Niagara|SetNiagaraVariable(Vector3) :self fx :InVariableName "User.Velocity" :InValue velocity)
      (Niagara|SetNiagaraVariable(Vector3) :self fx :InVariableName "User.Direction" :InValue direction)
      @set:PreviousWorld:position@)
    (if (and @get:bAutoRepeat@ (>= @get:CycleAge@ (select (> @get:RepeatSeconds@ 0.1) @get:RepeatSeconds@ 0.1)))
      @call:Play@)))
