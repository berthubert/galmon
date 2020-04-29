#include "comboaddress.hh"
#include "sclasses.hh"
#include <thread>
#include <signal.h>
#include "navmon.pb.h"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <mutex>
#include "storage.hh"
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "zstdwrap.hh"
#include "CLI/CLI.hpp"
#include "version.hh"
#include <netinet/tcp.h>

static char program[]="navrecv";

using namespace std;

extern const char* g_gitHash;

/* Goals in life:

   1) NEVER EVER GO DOWN
   2) Receive all frames without thinking (see 1)
   3) Put the frames in a scalable directory structure
*/

string g_storagedir="./storage/";

struct FatalException : public std::runtime_error
{
  FatalException(const std::string& str) : std::runtime_error(str){}
};



int getfd(const char* path, int mode, int permission)
{
  struct FileID {
    string path;
    int mode;
    int permission;

    bool operator<(const FileID& rhs) const
    {
      return std::tie(path, mode, permission) < std::tie(rhs.path, rhs.mode, rhs. permission);
    }
  };

  struct FDID {
    explicit FDID(int fd_)
    {
      fd = fd_;
      lastUsed = time(0);
    }
    FDID(const FDID& rhs) = delete;
    FDID& operator=(const FDID& rhs) = delete;
    FDID(FDID&& rhs)
    {
      fd = rhs.fd;
      lastUsed = rhs.lastUsed;
      rhs.fd = -1;
    }

    ~FDID()
    {
      if(fd >= 0) {
        cout<<"Closing fd "<<fd<<endl;
        close(fd);
      }
    }
         
    int fd;
    time_t lastUsed;
  };

  static std::mutex mut;
  std::lock_guard<std::mutex> lock(mut);

  static map<FileID, FDID> fds;

  // do some cleanup
  time_t now = time(0);
  for(auto iter = fds.begin(); iter != fds.end(); ) {
    if(now - iter->second.lastUsed > 60) {
      cout<<"Found stale entry for "<<iter->first.path<<endl;
      iter = fds.erase(iter);
    }
    else
      ++iter;
  }

  int toErase = fds.size() - 512;
  if(toErase > 0) {
    cout<<"Need to erase "<<toErase<<" entries"<<endl;
    auto end = fds.begin();
    std::advance(end, toErase);
    fds.erase(fds.begin(), end);
  }
  

  
  FileID fid({path, mode, permission});
  //  cout<<"Request for "<<path<<endl;
  auto iter = fds.find(fid);
  if(iter != fds.end()) {
    //    cout<<"Found a live FD!"<<endl;
    iter->second.lastUsed = time(0);
    return iter->second.fd;
  }

  //  cout<<"Did not find it, first doing some cleaning"<<endl;
  int fd = open(path, mode, permission);
  
  if(fd < 0) {
    throw FatalException("Unable to open file for storage: "+string(strerror(errno)));
  }
  cout<<"Opened fd "<<fd<<" for path "<<path<<endl;
  fds.emplace(fid, FDID(fd));
  return fd;
}

void writeToDisk(time_t s, uint64_t sourceid, std::string_view message)
{
  auto path = getPath(g_storagedir, s, sourceid, true);
  int fd = getfd(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  int res = write(fd, &message[0], message.size());
  if(res < 0) {
    close(fd);
    throw FatalException("Unable to open file for storage: "+string(strerror(errno)));
  }
  if((unsigned int)res != message.size()) {
    close(fd);
    throw FatalException("Partial write to storage");
  }
}

// note that this moves the socket
void recvSession2(Socket&& uns, ComboAddress client)
{
  string secret = SRead(uns, 8); // ignored for now
  cerr << "Entering compressed session for "<<client.toStringWithPort()<<endl;
  ZStdReader zsr(uns);
  int s = zsr.getFD();
  //  time_t start = time(0);
  for(;;) {
    // enable this to test ubxtool resilience & buffering
    //    if(time(0) - start > 30)
    //  sleep(10);
    string num=SRead(s, 4);
    if(num.empty()) {
      cerr<<"EOF from "<<client.toStringWithPort()<<endl;
      break;
    }
    string out="bert";
    
    string part = SRead(s, 2);
    out += part;
    
    uint16_t len;
    memcpy(&len, part.c_str(), 2);
    len = htons(len);
    
    part = SRead(s, len);
    out += part;
    
    NavMonMessage nmm;
    nmm.ParseFromString(part);
    uint32_t denum;
    
    memcpy(&denum, num.c_str(), 4);
    denum = htonl(denum);
    //    cerr<<"Received message "<<denum<< " "<<nmm.localutcseconds()<<" " << nmm.localutcnanoseconds()/1000000000.0<<endl;
    writeToDisk(nmm.localutcseconds(), nmm.sourceid(), out);
#ifdef __linux__
    SSetsockopt(uns, IPPROTO_TCP, TCP_CORK, 1 );
#endif

    SWrite(uns, num);
  }
}


void recvSession(int s, ComboAddress client)
{
  try {
    Socket sock(s); // this closes on destruction
    cerr<<"Receiving messages from "<<client.toStringWithPort()<<endl;
    for(;;) {
      string part=SRead(sock, 4);
      if(part.empty()) {
        cerr<<"EOF from "<<client.toStringWithPort()<<endl;
        break;
      }
      if(part != "bert") {
        if(part == "RNIE")
          return recvSession2(std::move(sock), client);  // protocol v2, socket is moved cuz cleanup is special
        cerr << "Wrong magic from "<<client.toStringWithPort()<<": "<<part<<endl;
        break;
      }
      string out=part;
      
      part = SRead(s, 2);
      out += part;
      
      uint16_t len;
      memcpy(&len, part.c_str(), 2);
      len = htons(len);
      
      part = SRead(s, len);
      out += part;
      
      NavMonMessage nmm;
      nmm.ParseFromString(part);
      
      writeToDisk(nmm.localutcseconds(), nmm.sourceid(), out);
    }
  }
  catch(std::exception& e) {
    cout<<"Error in receiving thread: "<<e.what()<<endl;
  }
  cout<<"Thread for "<<client.toStringWithPort()<< " exiting"<<endl;
}

void recvListener(Socket&& s, ComboAddress local)
{
  for(;;) {
    ComboAddress remote=local;
    int fd = SAccept(s, remote);
    std::thread t(recvSession, fd, remote);
    t.detach();
  }
}

int main(int argc, char** argv)
{
  bool doVERSION{false};

  CLI::App app(program);
  string localAddress("127.0.0.1");
  app.add_flag("--version", doVERSION, "show program version and copyright");
  
  app.add_option("--bind,-b", localAddress, "Address:port to bind to");
  app.add_option("--storage,-s", g_storagedir, "Location to store files");  
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  signal(SIGPIPE, SIG_IGN);
  
  ComboAddress recvaddr(localAddress, 29603);
  Socket receiver(recvaddr.sin4.sin_family, SOCK_STREAM, 0);
  SSetsockopt(receiver,SOL_SOCKET, SO_REUSEADDR, 1 );
  
  SBind(receiver, recvaddr);
  SListen(receiver, 128);

  thread recvThread(recvListener, std::move(receiver), recvaddr);
  recvThread.detach();
 
  for(;;) {
    sleep(1);
  }
}
