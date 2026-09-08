#ifndef MB_COMMON_SIDEBAR_POLICY_H_
#define MB_COMMON_SIDEBAR_POLICY_H_

#include "build/build_config.h"

namespace mb {

// Product capability and orientation policy, independent of browser ownership,
// preferences and feature trials. App/PWA windows retain native behavior.
constexpr bool RequiresVerticalSidebar(bool is_normal_window) {
  return BUILDFLAG(IS_LINUX) && is_normal_window;
}

}  // namespace mb

#endif  // MB_COMMON_SIDEBAR_POLICY_H_
