// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_BROWSER_BROWSER_CONFIG_GATE_INTERNAL_H_
#define MB_BROWSER_BROWSER_CONFIG_GATE_INTERNAL_H_

#include <string>
#include <string_view>

#include "base/component_export.h"

namespace base {
class FilePath;
}
namespace mb::config {
struct RuntimeConfig;
struct RuntimeConfigIssue;
}

namespace mb::internal {

// Nonmutating validation/formatting used by the startup gate and focused tests.
// These are internal integration APIs, not product UI configuration setters.
COMPONENT_EXPORT(MB_BROWSER_CONFIG)
bool IsValidStartupUrl(std::string_view raw);

COMPONENT_EXPORT(MB_BROWSER_CONFIG)
bool NativeRootMatches(const config::RuntimeConfig& snapshot,
                       const std::string& home,
                       const base::FilePath& supplied);

COMPONENT_EXPORT(MB_BROWSER_CONFIG)
std::string FormatRuntimeConfigIssue(const config::RuntimeConfigIssue& issue);

}  // namespace mb::internal

#endif  // MB_BROWSER_BROWSER_CONFIG_GATE_INTERNAL_H_
