#pragma once
#include <cstdint>
#include <zstd.h> // apt-get install libzstd-dev if you miss this. 
// can't easily be moved to zstdwrap.cc, trust me
#include <functional>
#include <thread>
#include <atomic>

// users submit (give()) data to this class
// the class has an internal buffer to which compressed data gets written
// If that buffer is full, we call emit() to empty it
// the emit() function must make sure that everything in the buffer gets sent!

class ZStdCompressor
{
public:
  explicit ZStdCompressor(const std::function<void(const char*, uint32_t)>& emit, int compressionLevel);
  ZStdCompressor(const ZStdCompressor& rhs) = delete;
  ~ZStdCompressor();
  void give(const char* data, uint32_t bytes);

  static int maxCompressionLevel();
  uint64_t d_inputBytes{0}, d_outputBytes{0};
  uint32_t outputBufferBytes(); // Number of bytes in output buffer
  uint32_t outputBufferCapacity(); // output buffer capacity
  void flush();

private:
  void flushToEmit();
  ZSTD_CCtx *d_z{nullptr};
  ZSTD_outBuffer d_out;
  uint32_t d_outcapacity;
  std::function<void(const char*, uint32_t)> d_emit;

};

/* this class is tremendously devious
   you pass it a filedescriptor from which it reads zstd compressed data
   You can then read the uncompressed data on the filedescriptor you 
   get from getFD()
*/

class ZStdReader 
{
public:
  ZStdReader(int fd); // we don't close this for you
  ZStdReader(const ZStdReader& rhs) = delete;
  ~ZStdReader();
  int getFD()
  {
    return d_readpipe;
  }
private:
  std::thread d_thread;
  int d_sourcefd; // this is where we read compressed data
  int d_writepipe; // which we then stuff into this pipe
  int d_readpipe;  // and it comes out here for the client

  void worker();
};
