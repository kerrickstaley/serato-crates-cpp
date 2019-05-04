#include <cstdio>
#include <filesystem>
#include <unordered_map>

#include "seratocrates.h"
#include "read_disk_files.h"

// Next, definition of readCrate.
std::unique_ptr<CrateFile> readCrate(const std::string& path) {
  std::unique_ptr<CrateFile> ret = readFromPath<CrateFile>(path);

  // The crate's name is not actually stored in the .crate file itself; it's only stored in the
  // filename.
  // Populate ret->name based on the filename, then call read<CrateFile>() to do the rest of the
  // work.
  ret->name = std::filesystem::path(path).stem();

  return ret;
}

std::vector<Crate> getCratesFromCrateFiles(const std::vector<CrateFile>& crate_files,
                                           const std::vector<std::shared_ptr<Track>>& tracks) {
  std::unordered_map<std::string, const std::shared_ptr<Track>*> path_to_track;
  for (const std::shared_ptr<Track>& track : tracks) {
    path_to_track[track->path] = &track;
  }

  std::vector<Crate> ret;

  for (const CrateFile& crate_file : crate_files) {
    ret.emplace_back(crate_file);
    for (const CrateFileTrack& crate_file_track : crate_file.tracks) {
      auto itr = path_to_track.find(crate_file_track.path);
      if (itr == path_to_track.end()) {
        // Crate track was not in database, silently ignore it.
        continue;
      }
      ret.back().tracks.push_back(*itr->second);
    }
  }
  return ret;
}

// And the definition of readLibrary.
std::unique_ptr<Library> readLibrary(const std::string& path) {
  std::filesystem::path root_path{path};
  std::filesystem::path serato_dir_path = root_path / "_Serato_";
  std::filesystem::path database_path = serato_dir_path / "database V2";
  std::filesystem::path crates_dir_path = serato_dir_path / "Subcrates";

  std::unique_ptr<DatabaseFile> database_file = readFromPath<DatabaseFile>(database_path.native());

  // TODO This does not correctly handle subcrates.
  std::vector<CrateFile> crate_files;
  for (std::filesystem::path crate_path : std::filesystem::directory_iterator(crates_dir_path)) {
    if (crate_path.extension() != ".crate") {
      // All files in this folder should be .crate files, but just in case skip file if it doesn't
      // have .crate extension.
      continue;
    }

    // This could be more efficient (less copying). Oh well.
    std::unique_ptr<CrateFile> crate_file = readCrate(crate_path.native());

    crate_files.emplace_back(*crate_file);
  }

  std::unique_ptr<Library> ret = std::make_unique<Library>(*database_file);

  ret->crates = getCratesFromCrateFiles(crate_files, database_file->tracks);

  return ret;
}
