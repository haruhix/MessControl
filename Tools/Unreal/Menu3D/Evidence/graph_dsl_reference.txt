GRAMMAR OVERVIEW

    (event EventName
      stmt ...)
    (event EventName (Param1 Param2 ...)
      stmt ...)

    (fn FunctionName (Param1 Param2 ...)
      stmt ...)

STATEMENTS

    bind          (bind var expr)
                  (bind (a b) (NodeType|Id args...))
    exec          (NodeType|Id args...)
    return        (return)  (return expr)  (return e1 e2)
    if            (if cond
                    stmt ...
                    [(elif cond stmt ...) | (else stmt ...)])
    elif          Must be the LAST form inside an (if) or (elif) body — it IS
                  the else branch, not a statement after it.
    for           (for i (range stop) stmt ...)
                  (for i (range start stop) stmt ...)
                  (for elem array-expr stmt ...)
    while         (while cond stmt ...)
    switch        (switch TypeId value (:Case stmt...) ...)
                  Short aliases: int  string  name  (see SWITCH ALIASES below)
    break         (break)

MULTI-EXEC (latent/task nodes with named exec outputs)

    (NodeType|Id args...
      (:ExecOut1
        stmt ...)
      (:ExecOut2
        stmt ...))

    Named exec continuations are sub-lists whose head starts with ":".
    They must appear after all data args and keyword args.
    The call terminates the enclosing exec flow (like if/branch).
    Pin names with spaces use the quoted form: (:"Pin Name" stmts...).

    DATA OUTPUTS inside continuations — a multi-exec node's data output pins are
    automatically available inside its continuation bodies as underscore-prefixed
    variables derived from the pin name (spaces → underscores, lowercased):

        "As Static Mesh Actor" → _as_static_mesh_actor
        "Return Value"         → _return_value

    Use get_node_type_pins() to discover the exact output pin names (and therefore
    the variable name) for any multi-exec node.

    EXPLICIT NAMING — to avoid relying on the auto-generated underscore name, wrap
    the node call in (bind var ...).  The name you choose is then directly available
    inside every continuation body.  It is not accessible outside the bind form.

    Preferred form for casts:
      (bind meshActor (Utilities|Casting|CastToStaticMeshActor :Object spawnedActor)
        (:then
          (bind comp (Class|StaticMeshActor|GetStaticMeshComponent :self meshActor))
          (Components|StaticMesh|SetStaticMesh :self comp :NewMesh "/Engine/BasicShapes/Cube.Cube"))
        (:CastFailed))

    DISAMBIGUATION — two uses of the ":" prefix:
      :PinName val       keyword DATA argument  (bare symbol followed by a value)
      (:PinName stmts…)  exec CONTINUATION      (sub-list whose head starts with ":")

    Example — Gameplay Ability Task:
      (Ability|Tasks|WaitDelay 2.0
        (:OnWait
          (Development|PrintString "waiting"))
        (:OnComplete
          (Development|PrintString "done")))

    Example — IsValid (pin names contain spaces):
      (Utilities|IsValid obj
        (:"Is Valid"
          (Development|PrintString "ok"))
        (:"Is Not Valid"
          (Development|PrintString "nil")))

EXPRESSIONS

    literal       1  3.14  "hello"  true  false
    variable      MyVar

    Any unquoted word that is not a number or boolean is treated as a
    variable reference.  Values that look like identifiers but are NOT
    variables — class paths, enum names, asset references — MUST be
    quoted or you will get an "Undefined variable" error:

        class path   "/Script/Engine.StaticMeshActor"   ← needs quotes
        enum value   "AlwaysSpawn"                       ← needs quotes
        asset ref    "/Game/Meshes/SM_Cube.SM_Cube"      ← needs quotes
    arithmetic    (+ a b)  (- a b)  (* a b)  (/ a b)  (% a b)
    comparison    (== a b)  (!= a b)  (< a b)  (<= a b)  (> a b)  (>= a b)
    boolean       (and a b)  (or a b)  (xor a b)  (not expr)  (neg expr)
    ternary       (select cond a b)    ; if cond then a else b
    vector        (.x v)  (.y v)  (.z v)
    rotator       (.pitch r)  (.yaw r)  (.roll r)
    transform     (.location t)  (.rotation t)  (.scale t)
    node call     (NodeType|Id pos-args... :PinName val ...)

NODE CALLS

    Type IDs are used verbatim:
        (Math|Float|sin(degrees) angle)
        (Development|PrintString "hello")
        (Development|PrintString :InString "hello" :Duration 5.0)

    Variables (Blueprint member variables):
        (Variables|Default|GetMyVar)
        (Variables|Default|SetMyVar value)

OPERATORS
    arithmetic:   +  -  *  /  %
    comparison:   ==  !=  <  <=  >  >=
    boolean:      and  or  xor  not
    unary minus:  (- expr)          negate a value; (neg expr) is a deprecated alias

COMPONENT ACCESS maps to Break* nodes:
    .x .y .z             → Math|Vector|BreakVector
    .pitch .yaw .roll    → Math|Rotator|BreakRotator
    .location .rotation .scale → Math|Transform|BreakTransform

SWITCH ALIASES
    Short names expand to the full Blueprint node type ID:
        int    → Utilities|FlowControl|SwitchOnInt
        string → Utilities|FlowControl|SwitchOnString
        name   → Utilities|FlowControl|SwitchOnName

    Example:
        (switch int (Variables|Default|GetDamageType)
          (:0 (Development|PrintString "fire"))
          (:1 (Development|PrintString "ice"))
          (:Default (Development|PrintString "other")))

    For enum switches use the full type ID: Utilities|FlowControl|SwitchOn<EnumName>

SELF REFERENCE
    Inside any event or fn body, the variable `self` is automatically bound
    to a Self Reference node (the owning Blueprint object).  Use it anywhere
    a node expects an Object pin:

        (Utilities|Time|SetTimerbyFunctionName
          :Object self
          :FunctionName "SpawnCube"
          :Time 3.0
          :bLooping true)

    If the event already exposes a parameter named `self` (rare), no extra
    Self Reference node is created and that parameter is used instead.

COMMENTS
    ; everything after a semicolon on a line is ignored

IMPORTANT — REUSE VALUES WITH BIND, NEVER REPEAT CALLS

    Every (NodeType|Id ...) call creates a new node in the graph and runs
    it again.  Repeating the same call to use its output in two places
    creates two nodes — doubling execution, and producing different values
    for impure functions (random, physics queries, GetActorLocation, etc.).

    WRONG — creates two GetActorLocation nodes, each executing separately:
        (if (> (.z (Transformation|GetActorLocation)) 0)
          (Development|PrintString (Math|Float|ToString
            (.z (Transformation|GetActorLocation)))))

    RIGHT — one node bound once, reused freely:
        (bind loc (Transformation|GetActorLocation))
        (if (> (.z loc) 0)
          (Development|PrintString (Math|Float|ToString (.z loc))))

    Rule: if a node's output is needed in more than one place, always bind it
    first and reference the variable.  Inline calls are only safe when the
    result is consumed exactly once.

EXAMPLES

    (event EventBeginPlay
      (bind loc (Transformation|GetActorLocation))
      (if (> (.z loc) 0)
        (for _ (range 5)
          (Development|PrintString "tick"))))

    (event OnHit
      (switch int (Variables|Default|GetDamageType)
        (:0
          (Development|PrintString "fire"))
        (:1
          (Development|PrintString "ice"))
        (:Default
          (Development|PrintString "other"))))

    (fn Clamp (Value Min Max)
      (if (< Value Min)
        (return Min)
        (elif (> Value Max)
          (return Max)
          (else
            (return Value)))))

    (fn DoubleIt (Value)
      (return (* Value 2)))

    (fn ClampedScale (Value Multiplier)
      (return (* (select (< Value 0.0) 0.0
                   (select (> Value 100.0) 100.0 Value))
                 Multiplier)))

    (fn OffsetTransform (T Offset)
      (return (Math|Transform|MakeTransform
                (+ (.location T) Offset)
                (.rotation T)
                (.scale T))))

    ; Class paths and enum values must be quoted strings (unquoted words are
    ; treated as variable references and will raise "Undefined variable").
    (fn SpawnCubeAbove ()
      (bind loc (Transformation|GetActorLocation))
      (bind spawnLoc (+ loc (Math|Vector|MakeVector :X 0.0 :Y 0.0 :Z 200.0)))
      (Game|SpawnActorfromClass
        :Class "/Script/Engine.StaticMeshActor"
        :SpawnTransform (Math|Transform|MakeTransform :Location spawnLoc)
        :CollisionHandlingOverride "AlwaysSpawn"))

Before writing a graph:
 * Use find_node_types() to discover the relevant node type_ids.
 * Use get_node_type_pins() to discover the exact pin names for a node type_id.