#ifndef MB_CONFIG_CONFIG_FILE_H_
#define MB_CONFIG_CONFIG_FILE_H_

#include <string>

#include "mb/config/config.h"

namespace mb::config {

struct ConfigFileResult {
  // Canonical filename; relative environment roots use its parent directory.
  std::string filename;
  ParseConfigResult parsed;
};

// Reads only a regular file, with a bounded allocation/read. Does not create
// files or directories. Symlinks to configuration files are allowed and resolved
// before reading; environment roots have a separate stricter symlink policy.
ConfigFileResult LoadConfigFile(const std::string& filename);

}  // namespace mb::config
#endif  // MB_CONFIG_CONFIG_FILE_H_
