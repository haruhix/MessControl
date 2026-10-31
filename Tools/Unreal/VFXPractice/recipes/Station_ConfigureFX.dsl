; Parameter contract matches MCSpongeSoapTrail. Only the emitter moves.
(fn ConfigureFX ()
  (bind fx @get:PracticeFX@)
  (if @get:bTrail@
    (Niagara|SetNiagaraVariable(Bool) :self fx :InVariableName "User.PreviewMotion" :InValue false)
    (Niagara|SetNiagaraVariable(Float) :self fx :InVariableName "User.Width" :InValue @get:TrailWidth@)
    (Niagara|SetNiagaraVariable(Float) :self fx :InVariableName "User.Lifetime" :InValue @get:TrailLifetime@)
    (Niagara|SetNiagaraVariable(Float) :self fx :InVariableName "User.Opacity" :InValue @get:TrailOpacity@)
    (Niagara|SetNiagaraVariable(Float) :self fx :InVariableName "User.SpawnSpacing" :InValue @get:TrailSpawnSpacing@)
    (Niagara|SetNiagaraVariable(Vector3) :self fx :InVariableName "User.SurfaceNormal" :InValue (Math|Vector|Normalize :A @get:SurfaceNormal@ :Tolerance 0.0001))
    (Niagara|SetNiagaraVariable(Vector3) :self fx :InVariableName "User.Direction" :InValue (Math|Vector|Normalize :A @get:TrailDirection@ :Tolerance 0.0001))
    (Niagara|SetNiagaraVariable(Vector3) :self fx :InVariableName "User.Velocity" :InValue (Math|Vector|MakeVector :X 0.0 :Y 0.0 :Z 0.0))))
