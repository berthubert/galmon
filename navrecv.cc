#include "comboaddress.hh"
#include "sclasses.hh"
#include <thread>
#include <signal.h>
#include "navmon.pb.h"
#include "fmt/format.h"
#include "fmt/printf.h"
#include <mutex>
#include "storage.hh"

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

void writeToDisk(time_t s, uint64_t sourceid, std::string_view message)
{
  auto path = getPath(g_storagedir, s, sourceid, true);
  int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  if(fd < 0) {
    throw FatalException("Unable to open file for storage: "+string(strerror(errno)));
  }
  int res = write(fd, &message[0], message.size());
  if(res < 0) {
    close(fd);
    throw FatalException("Unable to open file for storage: "+string(strerror(errno)));
  }
  if((unsigned int)res != message.size()) {
    close(fd);
    throw FatalException("Partial write to storage");
  }
  close(fd);
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
        cerr << "Wrong magic from "<<client.toStringWithPort()<<endl;
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
