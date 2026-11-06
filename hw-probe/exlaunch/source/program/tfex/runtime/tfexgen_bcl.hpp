/* The BCL classes TF.EX's code uses that the game's code lacks, as the runtime's stand-ins (overrides.toml [service] rules with key
 * S-native map their members here; TF.EX's MatchReport is the first user): MemoryStream, ZipArchive (+ entries and their streams),
 * HttpClient (+ ByteArrayContent, HttpResponseMessage), File.Exists, Path.GetFileName, Uri.EscapeDataString, Encoding.UTF8.GetBytes.
 * Real work, the Switch's way: DEFLATE by miniz_oxide (ggrs-switch tfx_deflate_*), the SD card through nn::fs, HTTP through netws.
 * The objects are native (this file's fixed pools, a magic word each, never collected by the GC: Dispose gives them back); running out
 * of a pool or of the zip buffer is logged as an error. One report is built at a time (MatchReport's upload worker). */
#pragma once
#include "tfexgen_code_rt.hpp"

namespace tfex::gen::bcl {
/* System.IO.MemoryStream: new MemoryStream(), ToArray(), Dispose() (the zip's output: a 1 MB module buffer, one stream at a time) */
void* MemoryNew();
gen::Arr<uint8_t> MemoryToArray(void* memory);
void MemoryDispose(void* memory);
/* System.IO.Compression.ZipArchive (Create mode): new ZipArchive(stream, mode, leaveOpen), CreateEntry(name, level),
 * ZipFileExtensions.CreateEntryFromFile(archive, path, name, level), Dispose() (writes the central directory into the stream) */
void* ZipNew(void* stream, int32_t mode, bool leaveOpen);
void* ZipCreateEntry(void* zip, void* entryName, int32_t level);
void* ZipAddFile(void* zip, void* path, void* entryName, int32_t level);
void ZipDispose(void* zip);
/* ZipArchiveEntry.Open(): the entry's write stream; Stream.Write(buffer, offset, count) compresses into the zip; Stream.Dispose() ends
 * the entry (its local header's CRC-32 and sizes). Any other Stream is not the runtime's: logged, nothing done. */
void* ZipEntryOpen(void* entry);
void StreamWrite(void* stream, gen::Arr<uint8_t> buffer, int32_t offset, int32_t count);
void StreamDispose(void* stream);
/* System.Net.Http: new HttpClient { Timeout }, PostAsync(url, content) (synchronous here: S-async), new ByteArrayContent(bytes),
 * HttpResponseMessage.StatusCode, their Dispose(). The POST goes through netws::HttpPost (http / https, Connection: close). */
void* HttpNew();
void HttpSetTimeout(void* http, int64_t ticks);
void* HttpPost(void* http, void* url, void* content);
int32_t HttpStatus(void* response);
void HttpResponseDispose(void* response);
void* ContentNew(gen::Arr<uint8_t> bytes);
void ContentDispose(void* content);
/* File.Exists (sd: paths), Path.GetFileName, Uri.EscapeDataString (RFC 3986: unreserved kept, UTF-8 percent-encoded), UTF-8 bytes */
bool FileExists(void* path);
void* PathGetFileName(void* path);
void* EscapeDataString(void* s);
gen::Arr<uint8_t> Utf8Bytes(void* s);
/* Encoding.UTF8.GetString(bytes, index, count): a managed string */
void* Utf8String(gen::Arr<uint8_t> bytes, int32_t index, int32_t count);
/* System.Net.WebSockets.ClientWebSocket (TF.EX's MatchmakingService._webSocket, its server connection) over netws, one connection at a
 * time: new, State (WebSocketState), ConnectAsync (blocking, as TF.EX waits for it), ReceiveAsync (TF.EX's listen loop, on the game
 * thread once a frame: the next piece of a whole frame, or nothing yet (gen::SetWouldBlock: the loop's pass returns); first it sends what
 * SendAsync queued), SendAsync (queued: 8 slots of 12 KB,
 * two of 280 KB for a skin chunk), CloseStatus (none) / CloseStatusDescription (null). Connect / receive / send fail as .NET throws
 * (gen::Fault). WsAbortAll: TF.EX's cancellation (Close: cancellationTokenSource.Cancel): the open connection's receive fails. */
void* WsNew();
int32_t WsState(void* ws);
void WsConnect(void* ws, void* url);
void* WsReceive(void* ws, gen::Arr<uint8_t> segment);
void WsSend(void* ws, gen::Arr<uint8_t> segment, int32_t messageType, bool endOfMessage);
void WsAbortAll();
/* "wss://host[:port]/path?query" -> host, port, TLS, path (the socket's and the ping measurement's urls) */
bool ParseWsUrl(const char* url, char* host, int hostCap, int* port, bool* tls, char* path, int pathCap);
/* WebSocketReceiveResult's properties: Count, MessageType (Text 0, Binary 1, Close 2), EndOfMessage */
int32_t WsResultCount(void* result);
int32_t WsResultType(void* result);
bool WsResultEnd(void* result);
/* TFEX-DIFF S-native (glue Reports.CurrentLog for MatchReport.ReadCurrentLog): the session's log is the Switch's hw log
 * (sd:/xplay/hw-log.txt, hwlog.cpp: this boot's head and rolling tail), flushed, then read without its space padding */
gen::Arr<uint8_t> CurrentLog();
}
