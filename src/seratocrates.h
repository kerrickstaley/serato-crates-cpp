#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

struct Track {
  std::string path;
};

struct CrateFile {
  std::string name;
  std::string version;
  std::vector<Track> tracks;
};

struct DatabaseFile {
  std::string version;
  std::vector<Track> tracks;
};

struct Crate : CrateFile {
  std::vector<Crate> subcrates;
  Crate(const CrateFile& crate_file) : CrateFile(crate_file) {}
};

struct Library : DatabaseFile {
  std::vector<Crate> crates;
  Library(const DatabaseFile& database_file) : DatabaseFile(database_file) {}
};

class ReadException : public std::runtime_error {
public:
  using runtime_error::runtime_error;
};

std::unique_ptr<CrateFile> readCrate(const std::string& path);
// readLibrary takes the path to the directory containing the _Serato_ folder (not the path to the
// _Serato_ folder itself).
std::unique_ptr<Library> readLibrary(const std::string& path);
