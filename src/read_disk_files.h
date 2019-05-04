// This file contains code to read and parse the raw "database V2" and *.crate files from disk.
#pragma once

#include <codecvt>
#include <cstdio>
#include <locale>
#include <map>

#include "seratocrates.h"
#include "memberpointer.h"

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

// First, some structs to represent the on-disk objects. The structs in seratocrates.h don't
// exactly match the on-disk format.

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

struct DatabaseFile {
  std::string version;
  std::vector<std::shared_ptr<Track>> tracks;

  // Note: This cast operator doesn't populate Library::crates!
  operator Library() const {
    Library ret{};
    ret.version = version;
    ret.tracks = tracks;
    return ret;
  }
};

// Next, other structs and declarations we need.

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

// Next, definition of read_from_path which reads a whole file (DatabaseFile or CrateFile) and
// returns it as a unique_ptr.
template<typename T>
std::unique_ptr<T> readFromPath(const std::string& path) {
  std::unique_ptr<T> ret = std::make_unique<T>();

  ReadContext ctx{};
  ctx.file = fopen(path.c_str(), "r");
  if (ctx.file == nullptr) {
    throw ReadException("Could not open file at path " + path);
  }

  fseek(ctx.file, 0, SEEK_END);
  size_t len = ftell(ctx.file);
  fseek(ctx.file, 0, SEEK_SET);

  // TODO need to clean up fin if this throws.
  read<T>(&ctx, len, ret.get());

  fclose(ctx.file);

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
