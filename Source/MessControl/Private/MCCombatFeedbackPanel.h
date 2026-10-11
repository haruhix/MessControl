#pragma once

#include "CoreMinimal.h"

class AMCCombatFeedbackController;
class SWidget;

/** Local diagnostic menu. It holds weak PIE references and keeps the simulation running. */
TSharedRef<SWidget> MCBuildCombatFeedbackPanel(TWeakObjectPtr<AMCCombatFeedbackController> Controller);

/** Uses the same scenario identifiers and labels as the menu. */
FString MCCombatFeedbackScenarioTitle(int32 Index);
