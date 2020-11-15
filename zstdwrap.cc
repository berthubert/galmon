#include "zstdwrap.hh"
#include <zstd.h>
#include <iostream>
#include <string.h>
#include <vector>
#include "navmon.hh"

using std::cout;
using std::endl;
using std::cerr;


ZStdCompressor::ZStdCompressor(const std::function<void(const char*, uint32_t)>& emit, int compressionLevel): d_emit(emit)
{
  d_z=ZSTD_createCStream();
  ZSTD_initCStream(d_z, compressionLevel);
  d_outcapacity=ZSTD_CStreamOutSize();
  d_out.dst = malloc(d_outcapacity); // ????
  d_out.pos=0;
  d_out.size=d_outcapacity;
}

int ZStdCompressor::maxCompressionLevel()
{
  return ZSTD_maxCLevel();
}

ZStdCompressor::~ZStdCompressor()
{
  for(;;) {
    auto res = ZSTD_endStream(d_z, &d_out);
    d_outputBytes += d_out.pos;
    try {
      d_emit((const char*)d_out.dst, d_out.pos);
    }
    catch(...){}
    //    cout<<"res: "<<res<<endl;
    if(!res)
      break;
    d_out.pos = 0;
    if(ZSTD_isError(res)) {
      cerr<<"Error in ZSTD_endStream"<<endl;
      break;
    }
  }

  ZSTD_freeCStream(d_z);

  free(d_out.dst);  // ????
}

uint32_t ZStdCompressor::outputBufferBytes()
{
  return d_out.pos;
}

uint32_t ZStdCompressor::outputBufferCapacity()
{
  return d_out.size;
}

void ZStdCompressor::flushToEmit()
{
  d_outputBytes += d_out.pos;
  d_emit((char*)d_out.dst, d_out.pos);
  d_out.pos=0;
}

void ZStdCompressor::flush()
{
  ZSTD_flushStream(d_z, &d_out);
  flushToEmit();
}



void ZStdCompressor::give(const char* data, uint32_t bytes)
{
  d_inputBytes += bytes;
  ZSTD_inBuffer in;
  in.src=data;
  in.pos=0;
  in.size=bytes;

  for(;;) {
    //    cout<<"before out: "<<d_out.pos<<endl;
    ZSTD_compressStream(d_z, &d_out, &in);
    //    cout<<"after out: "<<d_out.pos<<", in.pos="<<in.pos<<", in.size="<<in.size<<endl;
    if(in.pos == in.size)
      break;
    // if we are here, zstd did not consume everything, so we must make room
    flushToEmit();
  }
}

ZStdReader::ZStdReader(int fd)
{
  d_sourcefd = fd;

  int pfd[2];
  if(pipe(pfd) < 0)
    unixDie("Creating pipe for zstd reader");

  d_readpipe=pfd[0]; // for the customer
  d_writepipe=pfd[1]; // where we stuff data
  d_thread = std::thread(std::bind(&ZStdReader::worker, this));
}

static size_t writen(int fd, const void *buf, size_t count)
{
  const char *ptr = (char*)buf;
  const char *eptr = ptr + count;

  while(ptr != eptr) {
    ssize_t res = ::write(fd, ptr, eptr - ptr);
    if(res < 0) {
      if (errno == EAGAIN)
        throw std::runtime_error("used writen2 on non-blocking socket, got EAGAIN");
      else if (errno == EPIPE) {
        // other end closed, we are pleased to exit
        return 0;
      }
      else
        unixDie("failed in writen2");
    }
    else if (res == 0)
      throw std::runtime_error("could not write all bytes, got eof in writen2");

    ptr += (size_t) res;
  }

  return count;
}

void ZStdReader::worker()
{
  auto z = ZSTD_createDStream(); // think about automatic cleanup somehow
  ZSTD_initDStream(z);
  ZSTD_outBuffer output;
  ZSTD_inBuffer input;

  auto inputcapacity=128;
  std::vector<char> src(inputcapacity);
  input.src = &src[0];

  auto outputcapacity = ZSTD_DStreamOutSize();
  std::vector<char> dst(outputcapacity);
  output.dst = &dst[0]; 

  for(;;) {
    input.pos=0;
    int ret = read(d_sourcefd, (char*)input.src, inputcapacity);
    if(ret <= 0) {
      cerr<<"Got EOF on input fd "<<d_sourcefd<<", terminating thread"<<endl;
      break;
    }
    input.size = ret; // this is unsigned, so we need 'ret' to see the error
    while(input.pos != input.size) {
      output.pos=0;
      output.size=outputcapacity;
      ZSTD_decompressStream(z, &output, &input);

      int res;
      res = writen(d_writepipe, output.dst, output.pos);
      if(!res) // we are history
        break;
      if(res < 0) {
        cerr<<"Error in zstd thread: "<<strerror(errno)<<endl;
        break;
      }
    }
  }
  close(d_writepipe);
  ZSTD_freeDStream(z);
}

ZStdReader::~ZStdReader()
{
  cerr<<"ZStdReader destructor called"<<endl;
  int rc = close(d_readpipe);
  cerr<<"Close rc = "<<rc<<endl;
  cerr<<"Waiting on join"<<endl;
  d_thread.join();
  cerr<<"Done waiting on join"<<endl;
}
