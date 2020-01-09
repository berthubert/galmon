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
using namespace std;

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

void recvSession(int s, ComboAddress client)
{
  try {
    Socket sock(s);
    cerr<<"Receiving messages from "<<client.toStringWithPort()<<endl;
    for(;;) {
      string part=SRead(sock, 4);
      if(part.empty()) {
        cerr<<"EOF from "<<client.toStringWithPort()<<endl;
        break;
      }
      if(part != "bert") {
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
  signal(SIGPIPE, SIG_IGN);
  if(argc != 3) {
    cout<<"Syntax: navrecv listen-address storage"<<endl;
    return EXIT_FAILURE;
  }
  g_storagedir=argv[2];
  
  ComboAddress recvaddr(argv[1], 29603);
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
