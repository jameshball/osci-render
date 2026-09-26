#pragma once

#include <osci_file_import/osci_file_import.h>
#include <osci_scripting/osci_scripting.h>

// A synth source supplies either prepared geometry or a procedural point stream.
// Source loading, project ownership and editor services belong to the product.
class PointSource : public FrameSource {
public:
    virtual osci::Point nextSample(LuaState& state, LuaVariables& variables) = 0;
};
