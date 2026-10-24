#pragma once

#include "OverlayRender.h"

namespace OverlaySettingsUi
{
// The settings window, drawn while open is true. Closing it with its X sets open to false.
void Render(const OverlayHost& host, bool& open);
} // namespace OverlaySettingsUi
