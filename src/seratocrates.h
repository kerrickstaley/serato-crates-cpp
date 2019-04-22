#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

struct Track {
  std::string path;
};

struct Crate {
  std::string name;
  std::string version;
  std::vector<Track> tracks;
};

class ReadCrateException : public std::runtime_error {
public:
  using runtime_error::runtime_error;
};

std::unique_ptr<Crate> readCrate(const std::string& path);
