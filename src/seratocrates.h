#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

struct Track {
  std::string path;
};

struct DatabaseFile {
  std::string version;
  std::vector<std::shared_ptr<Track>> tracks;
};

struct Crate {
  std::string name;
  std::string version;
  std::vector<std::shared_ptr<Track>> tracks;
  std::vector<Crate> subcrates;
};

struct Library : DatabaseFile {
  std::vector<Crate> crates;
  Library(const DatabaseFile& database_file) : DatabaseFile(database_file) {}
};

class ReadException : public std::runtime_error {
public:
  using runtime_error::runtime_error;
};

// readLibrary takes the path to the directory containing the _Serato_ folder (not the path to the
// _Serato_ folder itself).
std::unique_ptr<Library> readLibrary(const std::string& path);
