(event EventBeginPlay
  (bind menu (UserInterface|CreateWidget :Class "/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C" :OwningPlayer self))
  (UserInterface|Viewport|AddtoViewport :self menu :ZOrder 50)
  (Input|SetInputModeUIOnly :PlayerController self :InWidgetToFocus menu :InMouseLockMode "DoNotLock" :bFlushInput true)
  (Variables|MouseInterface|SetShowMouseCursor :bShowMouseCursor true)
  (bind cameras (Actor|GetAllActorswithTag :Tag "Menu3D_Camera"))
  (for camera cameras (Game|Player|SetViewTargetwithBlend :self self :NewViewTarget camera :BlendTime 0.0))
)