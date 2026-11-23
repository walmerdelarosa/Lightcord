// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace lightcord {

// One-line summary of the process memory for the diagnostics log, for example
// "working set 61.2 MB (peak 143.0 MB), private 48.7 MB".
// On Windows "working set" is close to what Task Manager shows and "private" is
// the committed memory that belongs only to Lightcord.
QString memoryUsageSummary();

} // namespace lightcord
