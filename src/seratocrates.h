#pragma once

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
  std::vector<std::shared_ptr<Track>> tracks;
  std::vector<Crate> subcrates;
};

struct Library {
  std::string version;
  std::vector<std::shared_ptr<Track>> tracks;
  std::vector<Crate> crates;
};

class ReadException : public std::runtime_error {
public:
  using runtime_error::runtime_error;
};

// readLibrary takes the path to the directory containing the _Serato_ folder (not the path to the
// _Serato_ folder itself).
std::unique_ptr<Library> readLibrary(const std::string& path);
