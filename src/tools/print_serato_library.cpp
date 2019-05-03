#include "seratocrates.h"
#include <filesystem>
#include <iostream>

// Usage:
//   print_serato_library [path]
// Reads the Serato library at path (defaults to current directory) and prints its contents.

int main(int argc, char** argv) {
  std::filesystem::path in_path;
  if (argc <= 1) {
    in_path = ".";
  } else {
    in_path = argv[1];
  }

  std::unique_ptr<Library> library = readLibrary(in_path.native());

  std::cout << "Library contains " << library->tracks.size() << " tracks:\n";
  for (auto& track : library->tracks) {
    std::cout << '\n';
    std::cout << "  Path: " << track->path << '\n';
  }

  std::cout << '\n';
  std::cout << "Library contains " << library->crates.size() << " crates:\n";
  for (auto& crate : library->crates) {
    std::cout << '\n';
    std::cout << "  Name: " << crate.name << '\n';
    std::cout << "  Crate contains " << crate.tracks.size() << " tracks:\n";

    for (auto& track: crate.tracks) {
      std::cout << '\n';
      std::cout << "    Path: " << track->path << '\n';
    }
  }
}
