// Standalone Linux environment-root policy; no Chromium integration yet.
#ifndef MB_BROWSER_ENVIRONMENT_PATHS_H_
#define MB_BROWSER_ENVIRONMENT_PATHS_H_

#include <map>
#include <string>

namespace mb {

using EnvironmentDirectories = std::map<std::string, std::string>;

enum class EnvironmentPathErrorCode {
  kNone,
  kInvalidPath,
  kOverlappingRoots,
  kUnknownEnvironment,
  kUnsafeFilesystem,
  kSystemError,
};

struct EnvironmentPathError {
  EnvironmentPathErrorCode code = EnvironmentPathErrorCode::kNone;
  std::string key;
  std::string message;
  int system_errno = 0;
};

// Pure validation: no filesystem access or mutation. Outputs are cleared on
// failure. config_file and home must be absolute. Rejects '..' components,
// components exceeding NAME_MAX bytes, normalized paths >= PATH_MAX bytes,
// symlink-independent lexical aliases, and nested roots across every entry.
bool ResolveEnvironmentPaths(const std::string& config_file,
                             const std::string& home,
                             const EnvironmentDirectories& directories,
                             EnvironmentDirectories* resolved,
                             EnvironmentPathError* error);

// Nonmutating filesystem audit after pure resolution. Missing roots/parents
// are allowed. Existing components must satisfy the same policy as prepare.
// Inspects all entries and rejects filesystem aliases; clears output on error.
bool AuditEnvironmentPaths(const std::string& config_file,
                           const std::string& home,
                           const EnvironmentDirectories& directories,
                           EnvironmentDirectories* resolved,
                           EnvironmentPathError* error);

// Holds the open directory verified by PrepareEnvironmentRoot. This descriptor
// does not substitute for Chromium's singleton or bind future path lookups.
class PreparedEnvironmentRoot {
 public:
  PreparedEnvironmentRoot() = default;
  ~PreparedEnvironmentRoot();
  PreparedEnvironmentRoot(const PreparedEnvironmentRoot&) = delete;
  PreparedEnvironmentRoot& operator=(const PreparedEnvironmentRoot&) = delete;
  PreparedEnvironmentRoot(PreparedEnvironmentRoot&& other) noexcept;
  PreparedEnvironmentRoot& operator=(PreparedEnvironmentRoot&& other) noexcept;

  const std::string& path() const { return path_; }
  int directory_fd() const { return fd_; }

 private:
  friend bool PrepareEnvironmentRoot(const std::string&, const std::string&,
                                    const EnvironmentDirectories&,
                                    const std::string&,
                                    PreparedEnvironmentRoot*,
                                    EnvironmentPathError*);
  std::string path_;
  int fd_ = -1;
};

// Resolve every entry, then inspect all existing path components before any
// mutation. Creates only missing components of the selected root (mode 0700).
// Never follows symlinks, chmods existing paths, or removes anything. A failure
// during creation can leave newly created private parent directories behind.
// Both output pointers must be non-null. No implicit environment/getenv access.
bool PrepareEnvironmentRoot(const std::string& config_file,
                            const std::string& home,
                            const EnvironmentDirectories& directories,
                            const std::string& selected,
                            PreparedEnvironmentRoot* prepared,
                            EnvironmentPathError* error);

}  // namespace mb
#endif  // MB_BROWSER_ENVIRONMENT_PATHS_H_
