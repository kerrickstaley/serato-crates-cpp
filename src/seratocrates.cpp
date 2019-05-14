#include <cstdio>
#include <filesystem>
#include <unordered_map>

#include "seratocrates.h"
#include "read_disk_files.h"


class CrateReader {
public:
  CrateReader(const std::vector<std::shared_ptr<Track>>& library_tracks) {
    for (const std::shared_ptr<Track>& track : library_tracks) {
      path_to_track_[track->path] = track;
    }
  }

  Crate read(const std::string& path) {
    CrateFile crate_file = *readFromPath<CrateFile>(path);
    Crate ret = crate_file;

    // Populate Crate::name. The crate's name is not actually stored in the .crate file itself;
    // it's only stored in the filename.
    ret.name = std::filesystem::path(path).stem();

    for (const CrateFileTrack& crate_file_track : crate_file.tracks) {
      auto itr = path_to_track_.find(crate_file_track.path);
      if (itr == path_to_track_.end()) {
        // Crate track was not in database, silently ignore it.
        continue;
      }
      ret.tracks.push_back(itr->second);
    }

    return ret;
  }

private:
  std::unordered_map<std::string, std::shared_ptr<Track>> path_to_track_;
};


// Helper used in nestCrates
std::vector<std::string> crateNamePieces(const Crate& crate) {
  std::vector<std::string> ret;
  size_t piece_start = 0;
  for (size_t i = 1; i < crate.name.size(); i++) {
    if (crate.name[i] == '%' && crate.name[i + 1] == '%') {
      ret.push_back(crate.name.substr(piece_start, i - piece_start));
      piece_start = i = i + 2;
    }
  }
  ret.push_back(crate.name.substr(piece_start));
  return ret;
}


// Take flat list of crates and create nested crate structure.
// In this function's inputs, subcrates are named like "GrandparentCrate%%ParentCrate%%Crate".
// In the output, there will be a crate called "GrandparentCrate" with a child called "ParentCrate"
// which itself has a child called "Crate".
// Destroys input.
std::vector<Crate> nestCrates(std::vector<Crate>&& crates) {
  // TODO This function copies a lot of data around; try to make it more efficient.
  std::map<std::vector<std::string>, Crate*> pieces_to_crate;

  for (auto& crate : crates) {
    pieces_to_crate[crateNamePieces(crate)] = &crate;
  }

  for (auto it = pieces_to_crate.rbegin(); it != pieces_to_crate.rend(); it++) {
    const std::vector<std::string>& pieces = it->first;
    Crate* crate = it->second;

    if (pieces.size() <= 1) {
      continue;
    }

    std::vector<std::string> parent_pieces(pieces.begin(), pieces.end() - 1);

    auto parent_it = pieces_to_crate.find(parent_pieces);
    if (parent_it == pieces_to_crate.end()) {
      // Crate's parent was not present. Silently ignore crate.
      continue;
    }

    crate->name = pieces.back();
    parent_it->second->subcrates.emplace_back(std::move(*crate));
  }

  std::vector<Crate> ret;
  for (auto& pair : pieces_to_crate) {
    const std::vector<std::string>& pieces = pair.first;
    Crate* crate = pair.second;

    if (pieces.size() > 1) {
      continue;
    }

    ret.emplace_back(std::move(*crate));
  }

  return ret;
}


std::unique_ptr<Library> readLibrary(const std::string& path) {
  std::filesystem::path root_path{path};
  std::filesystem::path serato_dir_path = root_path / "_Serato_";
  std::filesystem::path database_path = serato_dir_path / "database V2";
  std::filesystem::path crates_dir_path = serato_dir_path / "Subcrates";

  std::unique_ptr<DatabaseFile> database_file = readFromPath<DatabaseFile>(database_path.native());
  std::unique_ptr<Library> ret = std::make_unique<Library>(*database_file);

  CrateReader crate_reader(database_file->tracks);

  // TODO This does not correctly handle subcrates.
  for (std::filesystem::path crate_path : std::filesystem::directory_iterator(crates_dir_path)) {
    if (crate_path.extension() != ".crate") {
      // All files in this folder should be .crate files, but just in case skip file if it doesn't
      // have .crate extension.
      continue;
    }

    ret->crates.push_back(crate_reader.read(crate_path.native()));
  }

  ret->crates = nestCrates(std::move(ret->crates));

  return ret;
}
