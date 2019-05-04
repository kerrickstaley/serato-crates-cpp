#include "seratocrates.h"
#include "memberpointer.h"
#include <cstdio>
#include <codecvt>
#include <locale>
#include <map>
#include <filesystem>
#include <unordered_map>

// Serato .crate files each encode exactly one root Crate object. Each Crate object contains
// several fields. Each field may be a primitive datatype or an object. Each field may be
// singular or repeated. We represent the primitive datatypes with std::string, sint32_t, uint32_t,
// and uint8_t. We represent repeated fields with std::vector. Strings are encoded as UTF-16
// on-disk but are converted into a std::string containing UTF-8 data.
//
// This file starts with some forward declarations. Then, we define templated read<T>() and
// read_repeated<T>() functions for reading objects from .crate files, along with specializations
// of read<T>() for primitive datatypes. Finally, we specify kFields for each object type. kFields
// describes the object's fields and how to read them.
//
// For more information, including the specifics of the on-disk format, see
// https://www.mixxx.org/wiki/doku.php/serato_database_format

// First, some forward declarations.

struct ReadContext {
  FILE *file;
};

typedef void (*ReadFunc)(ReadContext* ctx, size_t bytes, void* obj);

struct Field {
  MemberPointer member;
  ReadFunc readfunc;
};

template<typename T>
const std::map<std::string, Field> kFields;

struct CrateFileTrack {
  std::string path;
};

struct CrateFile {
  std::string name;
  std::string version;
  std::vector<CrateFileTrack> tracks;

  // Note: This cast operator doesn't populate Crate::tracks!
  operator Crate() const {
    Crate ret{};
    ret.name = name;
    ret.version = version;
    return ret;
  }
};

// Next, the definition of read<T>() for objects. Objects aren't primitive datatypes but instead
// contain several fields, each of which may be a primitive datatype or another object.
const size_t kTagSize = 4;
const size_t kRecordSizeSize = 4;

// We take a void* rather than a T* so that all read<T> instantiations have the same signature,
// which allows them to be placed in the same container.
template<typename T>
void read(ReadContext* ctx, const size_t bytes, void* obj_void) {
  T* obj = static_cast<T*>(obj_void);
  size_t bytes_read = 0;
  while (bytes_read < bytes) {
    // Read tag.
    std::string tag(kTagSize, '\0');
    if (kTagSize != fread(tag.data(), 1, kTagSize, ctx->file)) {
      throw ReadException(
          "File was truncated when reading tag (at offset "
          + std::to_string(ftell(ctx->file)) + ")!");
    }
    bytes_read += kTagSize;

    // Read size. It's stored as a big-endian 4-byte unsigned int.
    size_t record_size = 0;
    for (size_t i = 0; i < kRecordSizeSize; i++) {
      int byte = fgetc(ctx->file);
      if (byte == EOF) {
        throw ReadException(
            "File was truncated when reading field size (at offset "
            + std::to_string(ftell(ctx->file)) + ")!");
      }
      record_size <<= 8;
      record_size += byte;
    }
    bytes_read += kRecordSizeSize;
    bytes_read += record_size;

    if (!kFields<T>.count(tag)) {
      // Field is not supported, silently ignore it.
      fseek(ctx->file, record_size, SEEK_CUR);
      continue;
    }
    Field field = kFields<T>.at(tag);
    void* member = &(obj->*static_cast<char T::*>(field.member));
    field.readfunc(ctx, record_size, member);
  }
}

// Next, specializations of read<T>() for primitive datatypes.
template<>
void read<std::string>(ReadContext* ctx, const size_t bytes, void* str_void) {
  std::string* str = static_cast<std::string*>(str_void);
  std::u16string utf16_string;

  for (size_t i = 0; i < bytes / 2; i++) {
    char16_t c = 0;
    int b = fgetc(ctx->file);
    if (b == EOF) {
      throw ReadException("Crate file was truncated when reading string");
    }
    c |= b << 8;

    b = fgetc(ctx->file);
    if (b == EOF) {
      throw ReadException("Crate file was truncated when reading string");
    }
    c |= b;
    utf16_string += c;
  }

  *str = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(utf16_string);
}

// Next, specialization of read<T> for shared_ptr<Track>
// TODO It would be nice if I could write this generically for any shared_ptr<T>, but I think C++
// makes that hard.
template<>
void read<std::shared_ptr<Track>>(ReadContext* ctx, const size_t bytes, void* shared_ptr_void) {
  std::shared_ptr<Track>& sp = *static_cast<std::shared_ptr<Track>*>(shared_ptr_void);
  sp = std::make_shared<Track>();
  read<Track>(ctx, bytes, sp.get());
}

// Next, definition of read_repeated<T>() (works for both objects and primitive datatypes).

// We take a void* rather than a std::vector<T>* so that all read_repeated<T> instantiations have
// the same signature, which allows them to be placed in the same container.
template<typename T>
void read_repeated(ReadContext* ctx, const size_t bytes, void* obj_vec_void) {
  std::vector<T>* obj_vec = static_cast<std::vector<T>*>(obj_vec_void);
  obj_vec->emplace_back();
  void* obj = &obj_vec->back();
  read<T>(ctx, bytes, obj);
}

// Next, definition of readCrate.
std::unique_ptr<CrateFile> readCrate(const std::string& path) {
  std::unique_ptr<CrateFile> ret = std::make_unique<CrateFile>();

  // The crate's name is not actually stored in the .crate file itself; it's only stored in the
  // filename.
  // Populate ret->name based on the filename, then call read<CrateFile>() to do the rest of the
  // work.
  ret->name = std::filesystem::path(path).stem();

  ReadContext ctx{};
  ctx.file = fopen(path.c_str(), "r");
  if (ctx.file == nullptr) {
    throw ReadException("Could not open .crate file at path " + path);
  }

  fseek(ctx.file, 0, SEEK_END);
  size_t len = ftell(ctx.file);
  fseek(ctx.file, 0, SEEK_SET);

  // TODO need to clean up fin if this throws.
  read<CrateFile>(&ctx, len, ret.get());

  fclose(ctx.file);

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

  ReadContext ctx{};
  ctx.file = fopen(database_path.c_str(), "r");
  if (ctx.file == nullptr) {
    throw ReadException("Could not open database file at path \"" + database_path.native() + "\"!");
  }

  fseek(ctx.file, 0, SEEK_END);
  size_t len = ftell(ctx.file);
  fseek(ctx.file, 0, SEEK_SET);

  DatabaseFile database_file;
  read<DatabaseFile>(&ctx, len, &database_file);

  fclose(ctx.file);

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

  std::unique_ptr<Library> ret = std::make_unique<Library>(database_file);

  ret->crates = getCratesFromCrateFiles(crate_files, database_file.tracks);

  return ret;
}

// Finally, the rest of this file gives kFields for each object type. kFields specifies what
// fields the type has and how they should be read from disk.

// Note: make sure that the readfunc you specify matches the type of the member! In particular,
// you must use read_repeated iff the member is a std::vector. Otherwise you'll get weird crashes
// at runtime.
template<>
const std::map<std::string, Field> kFields<Track> = {
  {"pfil", Field{.member = &Track::path, .readfunc = read<std::string>}},
};


template<>
const std::map<std::string, Field> kFields<CrateFileTrack> = {
  {"ptrk", Field{.member = &Track::path, .readfunc = read<std::string>}},
};

template<>
const std::map<std::string, Field> kFields<CrateFile> = {
  {"vrsn", Field{.member = &CrateFile::version, .readfunc = read<std::string>}},
  {"otrk", Field{.member = &CrateFile::tracks, .readfunc = read_repeated<CrateFileTrack>}},
};

template<>
const std::map<std::string, Field> kFields<DatabaseFile> = {
  {"vrsn", Field{.member = &DatabaseFile::version, .readfunc = read<std::string>}},
  {"otrk", Field{.member = &DatabaseFile::tracks, .readfunc = read_repeated<std::shared_ptr<Track>>}},
};
