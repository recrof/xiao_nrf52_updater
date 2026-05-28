#pragma once

#include <Arduino.h>
#include <SdFat.h>

// Minimal walker for STORED (uncompressed) ZIP files, which is what nrfutil
// always produces (verified by the user). Walks Local File Headers from the
// start of the archive; ignores the central directory.

namespace zip_reader {

struct Entry {
  char     name[64];
  uint32_t data_offset;   // absolute byte offset in the archive
  uint32_t size;          // uncompressed size in bytes
};

// Opens `path` for read. Use the same Entry-based API to read its contents.
// Only one archive can be open at a time. close() releases the handle.
bool open(const char* path);
void close();

// Iterate all entries. Call repeatedly; returns false when there are no more
// entries (or on error).
bool next(Entry* out);

// Find an entry by exact name match (case-sensitive). Resets the iterator.
bool find(const char* name, Entry* out);

// Read up to `len` bytes from the entry starting at `offset` within it.
// Returns the number of bytes actually read, or -1 on error.
int read(const Entry& e, uint32_t offset, void* buf, uint32_t len);

}  // namespace zip_reader
