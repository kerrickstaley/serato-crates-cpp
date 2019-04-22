#include "seratocrates.h"
#include "memberpointer.h"
#include <cstdio>
#include <codecvt>
#include <locale>
#include <map>
#include <filesystem>

// Serato .crate files each encode exactly one root Crate object. Each Crate object contains
// several fields. Each field may be a primitive datatype or an object. Each field may be
// singular or repeated. We represent the primitive datatypes with std::string, sint32_t, uint32_t,
// and uint8_t. We represent repeated fields with std::vector. Strings are encoded as UTF-16
// on-disk but are converted into a std::string containing UTF-8 data.
//
// This file starts with some forward declarations. Then, we define templated read<T>() and
// read_repeated<T>() functions for reading objects from .crate files, along with specializations
// of read<T>() for primitive datatypes. Finally, we specify which fields each object type has and
// how they should be read from disk.
//
// For more information, including the specifics of the on-disk format, see
// https://www.mixxx.org/wiki/doku.php/serato_database_format

// First, some forward declarations.
typedef void (*ReadFunc)(FILE* file, size_t bytes, void* obj);

struct Field {
  MemberPointer member;
  ReadFunc readfunc;
};

template<typename T>
const std::map<std::string, Field> kFields;

// Next, the definition of read<T>() for composite objects.
const size_t kTagSize = 4;
const size_t kRecordSizeSize = 4;

// We take a void* rather than a T* so that all read<T> instantiations have the same signature,
// which allows them to be placed in the same container.
template<typename T>
void read(FILE* file, const size_t bytes, void* obj_void) {
  T* obj = static_cast<T*>(obj_void);
  size_t bytes_read = 0;
  while (bytes_read < bytes) {
    // Read tag.
    std::string tag(kTagSize, '\0');
    if (kTagSize != fread(tag.data(), 1, kTagSize, file)) {
      throw ReadCrateException("Crate file was truncated when reading tag.");
    }
    bytes_read += kTagSize;

    // Read size. It's stored as a big-endian 4-byte unsigned int.
    size_t record_size = 0;
    for (int i = 0; i < kRecordSizeSize; i++) {
      int byte = fgetc(file);
      if (byte == EOF) {
        throw ReadCrateException("Crate file was truncated when reading record size.");
      }
      record_size += byte;
    }
    bytes_read += kRecordSizeSize;
    bytes_read += record_size;

    if (!kFields<T>.count(tag)) {
      // Field is not supported, ignore it.
      fseek(file, record_size, SEEK_CUR);
      continue;
    }
    Field field = kFields<T>.at(tag);
    void* member = &(obj->*static_cast<char T::*>(field.member));
    field.readfunc(file, record_size, member);
  }
}

// Next, specializations of read<T>() for primitive datatypes.
template<>
void read<std::string>(FILE* file, const size_t bytes, void* str_void) {
  std::string* str = static_cast<std::string*>(str_void);
  std::u16string utf16_string;

  for (size_t i = 0; i < bytes / 2; i++) {
    char16_t c = 0;
    int b = fgetc(file);
    if (b == EOF) {
      throw ReadCrateException("Crate file was truncated when reading string");
    }
    c |= b << 8;

    b = fgetc(file);
    if (b == EOF) {
      throw ReadCrateException("Crate file was truncated when reading string");
    }
    c |= b;
    utf16_string += c;
  }

  *str = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(utf16_string);
}

// Next, definition of read_repeated<T>() (works for both objects and primitive datatypes).

// We take a void* rather than a std::vector<T>* so that all read_repeated<T> instantiations have
// the same signature, which allows them to be placed in the same container.
template<typename T>
void read_repeated(FILE* file, const size_t bytes, void* obj_vec_void) {
  std::vector<T>* obj_vec = static_cast<std::vector<T>*>(obj_vec_void);
  obj_vec->emplace_back();
  void* obj = &obj_vec->back();
  read<T>(file, bytes, obj);
}

// Next, definition of readCrate.
std::unique_ptr<Crate> readCrate(const std::string& path) {
  std::unique_ptr<Crate> ret = std::make_unique<Crate>();

  // The crate's name is not actually stored in the .crate file itself; it's only stored in the
  // filename.
  // Populate ret->name based on the filename, then call read<Crate>() to do the rest of the work.
  ret->name = std::filesystem::path(path).stem();

  FILE* fin = fopen(path.c_str(), "r");

  if (fin == nullptr) {
    throw ReadCrateException("Could not open .crate file at path " + path);
  }

  fseek(fin, 0, SEEK_END);
  size_t len = ftell(fin);
  fseek(fin, 0, SEEK_SET);

  // TODO need to clean up fin if this throws.
  read<Crate>(fin, len, ret.get());

  return ret;
}

// Finally, the rest of this file specifies the structure of .crate files, including how each
// object should be read from disk.

// Note: make sure that the readfunc you specify matches the type of the member! In particular,
// you must use read_repeated iff the member is a std::vector. Otherwise you'll get weird crashes
// at runtime.
template<>
const std::map<std::string, Field> kFields<Crate> = {
  {"vrsn", Field{.member = &Crate::version, .readfunc = read<std::string>}},
  {"otrk", Field{.member = &Crate::tracks, .readfunc = read_repeated<Track>}},
};

template<>
const std::map<std::string, Field> kFields<Track> = {
  {"ptrk", Field{.member = &Track::path, .readfunc = read<std::string>}},
};
