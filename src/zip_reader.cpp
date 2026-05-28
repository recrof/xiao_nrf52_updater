#include "zip_reader.h"

#include "storage.h"

namespace zip_reader {

namespace {

constexpr uint32_t kLfhSig = 0x04034b50;   // "PK\x03\x04"
constexpr uint32_t kCdSig  = 0x02014b50;   // central directory entry — stop walking when we hit it

File     g_file;
uint32_t g_cursor = 0;   // position of the next LFH to parse

template <typename T>
bool read_le(uint16_t off, T* out) {
  uint8_t buf[sizeof(T)];
  if (g_file.read(buf, sizeof(T)) != (int)sizeof(T)) return false;
  T v = 0;
  for (size_t i = 0; i < sizeof(T); i++) v |= ((T)buf[i]) << (8 * i);
  *out = v;
  (void)off;
  return true;
}

}  // namespace

bool open(const char* path) {
  close();
  g_file = storage::fs().open(path, O_RDONLY);
  if (!g_file) return false;
  g_cursor = 0;
  return true;
}

void close() {
  if (g_file) g_file.close();
  g_cursor = 0;
}

bool next(Entry* out) {
  if (!g_file) return false;
  if (!g_file.seekSet(g_cursor)) return false;

  uint32_t sig;
  if (!read_le<uint32_t>(0, &sig)) return false;
  if (sig != kLfhSig) return false;        // central directory or EOF — done

  // Local File Header layout (after the signature):
  //   2 version, 2 flags, 2 method, 2 mtime, 2 mdate,
  //   4 crc32, 4 csize, 4 usize, 2 namelen, 2 extralen
  uint16_t method, namelen, extralen;
  uint32_t csize, usize;
  if (!g_file.seekSet(g_cursor + 8)) return false;
  if (!read_le<uint16_t>(0, &method)) return false;
  if (!g_file.seekSet(g_cursor + 18)) return false;
  if (!read_le<uint32_t>(0, &csize))  return false;
  if (!read_le<uint32_t>(0, &usize))  return false;
  if (!read_le<uint16_t>(0, &namelen)) return false;
  if (!read_le<uint16_t>(0, &extralen)) return false;

  if (method != 0) {                       // STORED only
    return false;
  }

  // Filename starts at g_cursor + 30.
  if (!g_file.seekSet(g_cursor + 30)) return false;
  uint16_t name_to_read = namelen;
  if (name_to_read >= sizeof(out->name)) name_to_read = sizeof(out->name) - 1;
  if (g_file.read(out->name, name_to_read) != name_to_read) return false;
  out->name[name_to_read] = '\0';

  out->data_offset = g_cursor + 30 + namelen + extralen;
  out->size        = usize;

  g_cursor = out->data_offset + csize;
  return true;
}

bool find(const char* name, Entry* out) {
  if (!g_file) return false;
  g_cursor = 0;
  Entry e;
  while (next(&e)) {
    if (strcmp(e.name, name) == 0) {
      *out = e;
      return true;
    }
  }
  return false;
}

int read(const Entry& e, uint32_t offset, void* buf, uint32_t len) {
  if (!g_file) return -1;
  if (offset >= e.size) return 0;
  if (offset + len > e.size) len = e.size - offset;
  if (!g_file.seekSet(e.data_offset + offset)) return -1;
  return g_file.read(buf, len);
}

}  // namespace zip_reader
