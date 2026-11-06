/* Shared log for the hardware probe: everything that matters goes to the Ryujinx log ("[hw] ...") and
 * to sdmc:/xplay/hw-log.txt. Both the game thread and the network thread append; the network thread
 * writes the file (Flush), so the game thread never waits for the SD card. */
#pragma once
#include <cstddef>
#include <cstdint>

namespace hw {
/* Mounts the SD card as "sd:" once; true if it is available. Safe from any thread. */
bool MountSd();
/* Formats a line, sends it to the guest debug log and appends it to the file buffer. */
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
/* Appends an already formatted line to the file buffer only (the caller logged it itself). */
void Append(const char* line);
/* Writes the log file if anything was appended since the last call. */
void Flush();
/* Flush, also when the rolling tail was rewritten less than 2 s ago (before an upload). */
void FlushNow();
/* hw.txt "hwloghead N": the file's head ends at N bytes instead of 1 MB (tests of the rolling tail; 4096 .. 1 MB) */
void SetHeadLimit(size_t n);
/* Writes `n` bytes to `path`, padded with spaces to `fixedSize` (files can't grow, and an old longer
 * file would leave stale bytes). False on failure. */
bool WriteFixedFile(const char* path, const char* data, size_t n, size_t fixedSize);
bool WriteFixedRange(const char* path, const char* data, size_t from, size_t n, size_t fixedSize);   /* data[from..n) only (from > 0) */
/* WriteFixedFile on a writer thread (data must stay valid and only grow); appends after the first write write only the new part. */
void WriteFixedAsync(const char* path, const char* data, size_t n, size_t fixedSize);
/* The previous session's hw-log.txt is read at the first Flush, before this session overwrites it (PreviousFileLen). */
bool PreviousLogRead();
const char* PreviousStatus();   /* what the boot's copy of the previous session's log did (opened, sizes, written) */
/* The previous session's whole log, copied to sd:/xplay/hw-log-prev.txt at the first Flush (0: none this boot); devupdate uploads it. */
size_t PreviousFileLen();
/* Bytes appended so far (for "the log grew"). Live: in a lobby or a match (xplay), devupdate uploads the log periodically while set. */
uint64_t Appended();
void SetLive(bool on);
bool Live();
}
