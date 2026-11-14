#pragma once

#include "DragAndDrop/AssetDragDropOp.h"
#include "MCVFXActionSession.h"

/** Shared origin guard for copying an immutable library asset between Composer windows. */
class FMCComposerSourceDrag : public FAssetDragDropOp
{
public:
    DRAG_DROP_OPERATOR_TYPE(FMCComposerSourceDrag,FAssetDragDropOp)
    TWeakPtr<FMCVFXActionSession> SourceDocument;
    FGuid SourceToken;

    /** Data only: changing the source window's navigation does not invalidate its retained document. */
    bool IsSourceCurrent() const
    {
        const auto Source=SourceDocument.Pin();
        return Source&&Source->IsContextCurrent()&&Source->GetDraftToken()==SourceToken;
    }
};
