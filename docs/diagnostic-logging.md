# Background diagnostic logging

`ReleaseLog.txt` and `GeneralsOnline.log` share a worker. Producers format text
and enqueue it; only the worker rotates, opens, writes and flushes these files.
Files stay open for the process lifetime. The worker flushes a batch once a
second, at 64 KiB of queued text, or at an explicit flush barrier. It never holds
the producer queue mutex during file I/O. Per-message debugger output was removed
from NetworkLog so attaching a debugger cannot add synchronous output to that path.
Other loggers and replay recording retain their existing behavior.

The queue allows 1 MiB of text and 2,048 entries. Verbose network entries cannot
consume the last 256 KiB or 256 entries. Important entries can evict queued verbose
entries; when only important entries remain and the queue is full, new entries
are dropped rather than waiting for storage. The next worker batch reports drop
counts per file and priority. One in-flight batch can coexist with the bounded
queue, so queued/in-flight text uses up to roughly 2 MiB, plus entry overhead and
small fixed rate-limit state. Individual messages are capped at 512 KiB with a
truncation marker, including large CRC history dumps.

Duplicate resend refusals are grouped per slot. Invalid transport packets are
grouped by error category (short/oversized header or payload, bad magic/CRC,
malformed channel). First occurrences are queued; repeats are summarized every
five seconds with a count and the latest message, retaining its peer/frame/size
context. A flush barrier also emits pending summaries if repetition has stopped.
The shared rate-key ranges are 1..16 for resend slots and 17..32 for transport.
No protocol, simulation timing, or packet-validation decisions change.

Uploads flush queued diagnostics before reading files. Orderly shutdown waits
up to one second; fatal hooks make a best-effort 250 ms flush. These barriers are
exceptions to the non-waiting producer path and can time out on a slow disk.
They indicate that a batch was processed, not that writes succeeded or reached
physical media. Files that cannot be opened are not retried every frame.
The writer has process lifetime rather than joining a possibly stuck disk thread
in static destruction. An abnormal exit can lose the buffered tail; if the disk
is blocked, that tail may be larger than one second. This trades immediate crash
coverage for removing routine storage stalls from gameplay.

Validation: `bash tests/diagnostic-log/run.sh`. Before shipping, build on Windows
and play with normal and verbose logging, confirm both files and previous-session
rotation, and confirm end-of-match uploads contain the latest NETSTAT entries.
