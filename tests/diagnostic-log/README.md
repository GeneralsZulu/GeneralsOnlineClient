Run `bash tests/diagnostic-log/run.sh` with GCC or Clang and pthread support.

The queue tests deliberately block the sink while producers saturate the queue:
producers must finish, a flush barrier must time out, important messages must
survive a verbose flood, and dropped messages must be counted. They also cover
concurrent producers, both channels, repeated errors with changing frame details,
timed flushing, final draining, and message-size limits.

The file integration test compiles the actual ReleaseLog.cpp with minimal engine
stubs. It exercises shared release/network output, rotation once per session,
ordering, flush-before-read, CRC history, and cached paths after engine teardown.
The stubs replace engine types only; they do not replace the queue or file writer.
These host tests do not replace a Windows client build and an online smoke test.
