#pragma once

#include "InfGUIContext.h"

#include <core/log/InfLog.h>

namespace infengine
{
class InfGUIRenderable
{
  public:
    virtual ~InfGUIRenderable() = default;
    virtual void OnRender(InfGUIContext *ctx)
    {
        INFLOG_FATAL("InfGUIRenderable::OnRender not implemented");
    }
};
} // namespace infengine