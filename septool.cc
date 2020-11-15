#include <string>
#include "navmon.hh"
#include <iostream>
#include <string.h>
#include <time.h>
#include "bits.hh"
#include <sys/time.h>
#include <arpa/inet.h>
#include "galileo.hh"
#include "nmmsender.hh"
#include "CLI/CLI.hpp"
#include "swrappers.hh"
#include "sclasses.hh"
#include "version.hh"

using namespace std;


static int sepsig2ubx(int sep)
{
  if(sep == 1) // GPS L1P
    return 0;
  else if(sep == 2) // GPS L2P
    return 4;
  else if(sep== 4) // GPS L5
    return 7; // ??
  else if(sep == 17)
    return 1; // Galileo E1
  else if(sep == 19)
    return 8; // Galileo E6
  else if(sep == 20)
    return 6; // Galileo E5a
  else if(sep==21)
    return 5; // Galileo E5b
  else if(sep==22)
    return 6; // Galileo "AltBoc"
  
  else if(sep==0)
    return 0; // GPS L1
  else if(sep==3)
    return 4; // GPS L2c
  else throw runtime_error("Asked to convert unknown Septentrio signal id "+to_string(sep));
  return -1;
}

struct SEPMessage
{
  SEPMessage(const std::basic_string<uint8_t>& str) : d_store(str) {}

  uint16_t getID() // includes revision
  {
    return d_store[4] + 256*d_store[5];
  }

  uint16_t getIDBare() // includes revision
  {
    return getID() & 0xFFF;
  }

  
  std::basic_string<uint8_t> getPayload()
  {
    return d_store.substr(8);
  }
  std::basic_string<uint8_t> d_store;
};

/* format:

   01  23   45   67    
   $@ CRC blk-id len [len-8 bytes] 
                  |
            multiple of four

   bits 0-12 of blk-id are the type
*/

std::pair<SEPMessage, struct timeval> getSEPMessage(int fd, double* timeout)
{
  uint8_t marker[2]={0};
  bool hadskip=false;
  for(;;) {
    marker[0] = marker[1];
    int res = readn2Timeout(fd, marker+1, 1, timeout);

    if(res < 0) {
      cerr<<"Readn2Timeout failed: "<<strerror(errno)<<endl;
      throw EofException();
    }
    if(marker[0]=='$' && marker[1]=='@') { // bingo
      if(hadskip) {
        cerr<<"\n";
        hadskip=false;
      }
      struct timeval tv;
      gettimeofday(&tv, 0);
      basic_string<uint8_t> msg;
      msg.append(marker, 2);  // 0,1
      uint8_t b[6];
      readn2Timeout(fd, b, 6, timeout);
      msg.append(b, 6); // crc id len

      // 0,1 = crc, 2-3 = marker, 4, 5
      //      uint16_t blkid = htons(b[2] + 256*b[3]);
      uint16_t len = b[4] + 256*b[5];
      //      cerr<<"Got message of type "<<getbitu((uint8_t*)&blkid, 0, 12)<<", revision "<<
      //        getbitu((uint8_t*)&blkid, 12, 4)<<" ("<<ntohs(blkid)<<"), len= "<<len<<endl;
      
      uint8_t buffer[len-8];
      res=readn2Timeout(fd, buffer, len-8, timeout);

      msg.append(buffer, len - 8); // checksum
      return make_pair(SEPMessage(msg), tv);
    }
    else if(marker[1] != '$') {
      hadskip=true;
      cerr<<".";
    }
         
  }                                                                       
}

static char program[]="septool";
uint16_t g_srcid{0};

int main(int argc, char** argv)
try
{
  time_t starttime=time(0);
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  vector<string> destinations;
  g_dtLS = 18;
  bool doVERSION{false}, doSTDOUT{false};
  CLI::App app(program);
  string sourceaddr;
  app.add_option("--source", sourceaddr, "Connect to this IP address:port to source SBF (otherwise stdin)");
  app.add_option("--destination,-d", destinations, "Send output to this IPv4/v6 address");
  app.add_option("--station", g_srcid, "Station id")->required();
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_flag("--stdout", doSTDOUT, "Emit output to stdout");
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
  NMMSender ns;
  ns.d_debug = true;
  for(const auto& s : destinations) {
    auto res=resolveName(s, true, true);
    if(res.empty()) {
      cerr<<"Unable to resolve '"<<s<<"' as destination for data, exiting"<<endl;
      exit(EXIT_FAILURE);
    }
    ns.addDestination(s); // ComboAddress(s, 29603));
  }
  if(doSTDOUT)
    ns.addDestination(1);

  int srcfd=0;
  if(!sourceaddr.empty()) {
    ComboAddress src(sourceaddr);
    
    srcfd = socket(src.sin4.sin_family, SOCK_STREAM, 0);
    if(srcfd < 0)
      unixDie("making socket for SBF connection");
    cerr<<"Connecting to "<< src.toStringWithPort()<<" to source data..";
    SConnectWithTimeout(srcfd, src, 5);
    cerr<<" done"<<endl;
  }
  ns.d_compress = true;
  ns.launch();
  cerr<<"Station "<<g_srcid<<endl;
  for(;;) {
    double to=1000;
    auto res = getSEPMessage(srcfd, &to);
    cerr<<res.first.getID()<<" - " <<res.first.getIDBare() << endl;
    if(res.first.getID() == 4023) { // I/NAV
      auto str = res.first.getPayload();
      struct SEPInav
      {
        uint32_t towMsec;
        uint16_t wn;
        uint8_t sv;
        uint8_t crcPassed;
        uint8_t viterbiCount;
        uint8_t src;
        uint8_t ign1;
        uint8_t rxChannel;
        uint8_t navBits[32];
      } __attribute__((packed));
      SEPInav si;
      memcpy(&si, str.c_str(), sizeof(si));
      //      cerr<<"tow "<<si.towMsec /1000<<" wn "<<si.wn <<" sv " << (int) si.sv - 70<<" ";
      
      if(!si.crcPassed) {
        cerr<<"I/NAV CRC error, skipping"<<endl;
        continue;
      }
      int sigid = si.src & 31;

      std::string inav((char*)si.navBits, 32);
      //      cerr<<makeHexDump(inav)<<endl;


      // byte order adjustment
      std::basic_string<uint8_t> payload;
      
      for(unsigned int i = 0 ; i < 8; ++i) {
        payload.append(1, si.navBits[4 * i + 3]);
        payload.append(1, si.navBits[4 * i + 2]);
        payload.append(1, si.navBits[4 * i + 1]);
        payload.append(1, si.navBits[4 * i + 0]);
      }

      
      basic_string<uint8_t> inav2;
      // copy in the even page
      for(int n = 0 ; n < 14; ++n)
        inav2.append(1, getbitu(payload.c_str(), 2 + n*8, 8));
      // odd page
      for(int n = 0 ; n < 2; ++n)
        inav2.append(1, getbitu(payload.c_str(), 116 + n*8, 8));
      //      cerr<<makeHexDump(inav2) << endl;


      NavMonMessage nmm;
      double t = utcFromGST(si.wn - 1024, si.towMsec / 1000.0);
      //      cerr<<t<< " " <<si.wn - 1024 <<" " <<si.towMsec /1000.0 <<" " << g_dtLS<<endl;
      nmm.set_sourceid(g_srcid);
      nmm.set_type(NavMonMessage::GalileoInavType);

      nmm.set_localutcseconds(t);
      nmm.set_localutcnanoseconds(0); // yeah XXX
            
      nmm.mutable_gi()->set_gnsswn(si.wn - 1024);
      
      nmm.mutable_gi()->set_gnsstow(si.towMsec/1000.0);
      nmm.mutable_gi()->set_gnssid(2);
      nmm.mutable_gi()->set_gnsssv(si.sv - 70);
      nmm.mutable_gi()->set_contents((const char*)&inav2[0], inav2.size());
      nmm.mutable_gi()->set_sigid(sepsig2ubx(sigid));
      ns.emitNMM( nmm);
      
    }
    else if(res.first.getID() == 5914) {
      // current time
    }

    else if(res.first.getID() == 4026) {
      // GLONASS
    }
    else if(res.first.getID() == 4017) { // GPS-CA

    }
    else if(res.first.getID() == 4018) { // GPS-L2C

    }
    else if(res.first.getID() == 4019) { // GPS raw L5

    }
    else if(res.first.getID() == 4093) { // navic raw

    }
    else if(res.first.getID() == 4022) {  // F/NAV
      auto str = res.first.getPayload();
      struct SEPFnav
      {
        uint32_t towMsec;
        uint16_t wn;
        uint8_t sv;
        uint8_t crcPassed;
        uint8_t viterbiCount;
        uint8_t src;
        uint8_t ign1;
        uint8_t rxChannel;
        uint8_t navBits[32];
      } __attribute__((packed));
      SEPFnav sf;
      memcpy(&sf, str.c_str(), sizeof(sf));
      int sigid = sf.src & 31;
      //      cerr<<"tow "<<sf.towMsec /1000<<" wn "<<sf.wn <<" sv " << (int) sf.sv - 70<<" sigid " << sigid <<" ";
      if(!sf.crcPassed) {
        cerr<<"F/NAV CRC error, skipping"<<endl;
        continue;
      }

      std::string fnav((char*)sf.navBits, 32);
      // byte order adjustment
      std::basic_string<uint8_t> payload;
      
      for(unsigned int i = 0 ; i < 8; ++i) {
        payload.append(1, sf.navBits[4 * i + 3]);
        payload.append(1, sf.navBits[4 * i + 2]);
        payload.append(1, sf.navBits[4 * i + 1]);
        payload.append(1, sf.navBits[4 * i + 0]);
      }

      NavMonMessage nmm;
      double t = utcFromGST(sf.wn - 1024, sf.towMsec / 1000.0);
      //      cerr<<t<< " " <<si.wn - 1024 <<" " <<si.towMsec /1000.0 <<" " << g_dtLS<<endl;
      nmm.set_sourceid(g_srcid);
      nmm.set_type(NavMonMessage::GalileoFnavType);

      nmm.set_localutcseconds(t);
      nmm.set_localutcnanoseconds(0); // yeah XXX
            
      nmm.mutable_gf()->set_gnsswn(sf.wn - 1024);
      
      nmm.mutable_gf()->set_gnsstow(sf.towMsec/1000.0);
      nmm.mutable_gf()->set_gnssid(2);
      nmm.mutable_gf()->set_gnsssv(sf.sv - 70);
      nmm.mutable_gf()->set_contents((const char*)&payload[0], payload.size());
      nmm.mutable_gf()->set_sigid(sepsig2ubx(sigid));
      ns.emitNMM( nmm);
    }
    else if(res.first.getIDBare() == 4047) {
      // BDSRaw
    }
    else if(res.first.getIDBare() == 4006) {
      auto str = res.first.getPayload();
      struct PVTCartesian
      {
        uint32_t towMsec;
        uint16_t wn;
        uint8_t mode, error;
        double x, y, z;
        float undulation;
        float vx, vy, vz;
      } __attribute__((packed));
      PVTCartesian pc;
      memcpy(&pc, str.c_str(), sizeof(pc));

      NavMonMessage nmm;
      nmm.set_type(NavMonMessage::ObserverPositionType);
      nmm.set_localutcseconds(utcFromGST(pc.wn - 1024, pc.towMsec / 1000.0));
      nmm.set_localutcnanoseconds(0);
      nmm.set_sourceid(g_srcid);
      
      nmm.mutable_op()->set_x(pc.x);
      nmm.mutable_op()->set_y(pc.y);
      nmm.mutable_op()->set_z(pc.z);
      nmm.mutable_op()->set_acc(3.14);
      
      ns.emitNMM( nmm);

      {
        NavMonMessage nmm;
        nmm.set_sourceid(g_srcid);
        nmm.set_localutcseconds(utcFromGST(pc.wn - 1024, pc.towMsec / 1000.0));
        nmm.set_localutcnanoseconds(0);
        
        nmm.set_type(NavMonMessage::ObserverDetailsType);
        nmm.mutable_od()->set_vendor("Septentrio");
        nmm.mutable_od()->set_hwversion("Mosaic");
        nmm.mutable_od()->set_swversion("");
        nmm.mutable_od()->set_serialno("3060601");
        nmm.mutable_od()->set_modules("");
        nmm.mutable_od()->set_clockoffsetns(0);
        nmm.mutable_od()->set_clockoffsetdriftns(0);
        nmm.mutable_od()->set_clockaccuracyns(0);
        nmm.mutable_od()->set_freqaccuracyps(0);

        nmm.mutable_od()->set_owner("Septentrio");
        nmm.mutable_od()->set_remark("");
        nmm.mutable_od()->set_recvgithash(g_gitHash);
        nmm.mutable_od()->set_uptime(time(0) - starttime);
        ns.emitNMM( nmm);
      }
      
    }
    else if(res.first.getIDBare() == 4024) { // GALRawCNAV
      struct SEPCnav
      {
        uint32_t towMsec;
        uint16_t wn;
        uint8_t sv;
        uint8_t crcPassed;
        uint8_t viterbiCount;
        uint8_t src;
        uint8_t ign1;
        uint8_t rxChannel;
        uint8_t navBits[48];
      } __attribute__((packed));

      SEPCnav sc;
      auto str = res.first.getPayload();
      memcpy(&sc, str.c_str(), sizeof(sc));
      int sigid = sc.src & 31;
      //      cerr<<"C/NAV tow "<<sc.towMsec /1000<<" wn "<<sc.wn <<" sv " << (int) sc.sv - 70<<" sigid " << sigid <<" ";
      if(!sc.crcPassed) {
        cerr<<"C/NAV CRC error, skipping"<<endl;
        continue;
      }

      std::string cnav((char*)sc.navBits, 48);
      // byte order adjustment
      std::basic_string<uint8_t> payload;
      
      for(unsigned int i = 0 ; i < 12; ++i) {
        payload.append(1, sc.navBits[4 * i + 3]);
        payload.append(1, sc.navBits[4 * i + 2]);
        payload.append(1, sc.navBits[4 * i + 1]);
        payload.append(1, sc.navBits[4 * i + 0]);
      }

      NavMonMessage nmm;
      double t = utcFromGST(sc.wn - 1024, sc.towMsec / 1000.0);
      //      cerr<<t<< " " <<si.wn - 1024 <<" " <<si.towMsec /1000.0 <<" " << g_dtLS<<endl;
      nmm.set_sourceid(g_srcid);
      nmm.set_type(NavMonMessage::GalileoCnavType);

      nmm.set_localutcseconds(t);
      nmm.set_localutcnanoseconds(0); // yeah XXX
            
      nmm.mutable_gc()->set_gnsswn(sc.wn - 1024);
      
      nmm.mutable_gc()->set_gnsstow(sc.towMsec/1000.0);
      nmm.mutable_gc()->set_gnssid(2);
      nmm.mutable_gc()->set_gnsssv(sc.sv - 70);
      nmm.mutable_gc()->set_contents((const char*)&payload[0], payload.size());
      nmm.mutable_gc()->set_sigid(sepsig2ubx(sigid));
      ns.emitNMM( nmm);
    }
    else if(res.first.getIDBare() == 4027) {
      auto str = res.first.getPayload();
      struct MeasEpoch
      {
        uint32_t towMsec;
        uint16_t wn;
        uint8_t n1;
        uint8_t sb1len;
        uint8_t sb2len;
        uint8_t commonFlags;
        uint8_t clkJumps;
        uint8_t res1;
      } __attribute__((packed));
      MeasEpoch me;
      memcpy(&me, str.c_str(), sizeof(me));
      //      cerr<<"Got "<<(int)me.n1<<" signal statuses, block1 "<<(int)me.sb1len<<", block2 "<<(int)me.sb2len<<endl;

      struct Block1
      {
        uint8_t rxchannel, type, sv, misc; // misc contains 4 bits of codeMSB
        uint32_t codeLSB;
        int32_t doppler;
        uint16_t carrierLSB;
        int8_t carrierMSB;
        uint8_t cn0;
        uint16_t lockTime;
        uint8_t obsinfo;
        uint8_t n2;
      } __attribute__((packed));
      struct Block2
      {
        uint8_t type, locktime, cn0, offsetMSB;
        int8_t carrierMSB;
        uint8_t obsinfo;
        uint16_t codeoffsetLSB;
        uint16_t carrierLSB;
        uint16_t dopplerOffsetLSB;
      } __attribute__((packed));

      int pos = sizeof(me);
      for(int n = 0 ; n < me.n1; ++n) {
        Block1 b1;
        memcpy(&b1, str.c_str() + pos, sizeof(b1));
        uint8_t sigid = b1.type & 31;
        //        cerr<<"sv "<<(int)b1.sv<<" sigid "<< (int)sigid <<" cn0 ";
        double db;
        if(sigid==1 || sigid ==2)
          db = b1.cn0 *0.25;
        else db = b1.cn0 * 0.25 + 10;
        //        cerr<<" "<<db;
        //        cerr<<" n2 "<< (int)b1.n2;
        //        cerr<<endl;
        pos += me.sb1len;

        if(b1.sv <= 36 || (b1.sv > 70 && b1.sv <= 106)) {
          NavMonMessage nmm;
          nmm.set_sourceid(g_srcid);
          nmm.set_localutcseconds(utcFromGST(me.wn - 1024, me.towMsec / 1000.0));
          nmm.set_localutcnanoseconds(0);
          
          nmm.set_type(NavMonMessage::ReceptionDataType);
          nmm.mutable_rd()->set_gnssid(b1.sv > 70 ? 2 : 0 );
          nmm.mutable_rd()->set_gnsssv(b1.sv <= 37 ? b1.sv : b1.sv - 70);
          nmm.mutable_rd()->set_db(db);
          nmm.mutable_rd()->set_el(0);
          nmm.mutable_rd()->set_azi(0);
          nmm.mutable_rd()->set_prres(-1);
          nmm.mutable_rd()->set_qi(7);
                      
          try {
            nmm.mutable_rd()->set_sigid(sepsig2ubx(sigid));
            ns.emitNMM(nmm);
          }
          catch(...){}

          /*
          LSB of the pseudorange. The pseudorange expressed in meters
            is computed as follows:
            PR type1 [m] = ( CodeMSB *4294967296+ CodeLSB )*0.001

            codeMSB hides in bits 0-3 of misc. 
          */
          
        }
        
        for(int m = 0 ; m < b1.n2; ++m) {
          Block2 b2;
          memcpy(&b2, str.c_str() + pos, sizeof(b2));
          pos += me.sb2len;
          sigid = b2.type & 31;
          //          cerr<<"\t sigid  "<<(int)sigid<<" cn0 ";
          if(sigid==1 || sigid ==2)
            db= b2.cn0 *0.25;
          else db = b2.cn0 * 0.25 + 10;
          //          cerr<<db;
          //          cerr<<endl;

          if(b1.sv <= 36 || (b1.sv > 70 && b1.sv <= 106)) {
            NavMonMessage nmm;
            nmm.set_sourceid(g_srcid);
            nmm.set_localutcseconds(utcFromGST(me.wn - 1024, me.towMsec / 1000.0));
            nmm.set_localutcnanoseconds(0);
            
            nmm.set_type(NavMonMessage::ReceptionDataType);
            nmm.mutable_rd()->set_gnssid(b1.sv > 70 ? 2 : 0 );
            nmm.mutable_rd()->set_gnsssv(b1.sv <= 37 ? b1.sv : b1.sv - 70);
            nmm.mutable_rd()->set_db(db);
            nmm.mutable_rd()->set_el(0);
            nmm.mutable_rd()->set_azi(0);
            nmm.mutable_rd()->set_prres(0);
            nmm.mutable_rd()->set_qi(7);
            try {
              nmm.mutable_rd()->set_sigid(sepsig2ubx(sigid));            
              ns.emitNMM(nmm);
            }catch(...){} // might be unknown signal type
          }
        }
      }
      
    }
    else {
      cerr<<"Unknown message "<<res.first.getID() << " / " <<res.first.getIDBare()<<"  ("<<res.first.d_store.size()<<" bytes)"<<endl;
    }
  }
}
catch(std::exception& e)
{
  cerr<<"Fatal: "<<e.what()<<endl;
}
catch(EofException& e)
{
  
}

      /*
        NAVBits contains the 234 bits of an I/NAV navigation page (in nominal
or alert mode). Note that the I/NAV page is transmitted as two sub-pages
(the so-called even and odd pages) of duration 1 second each (120 bits
each). 

In this block, the even and odd pages are concatenated, even page
first and odd page last. The 6 tails bits at the end of the even page are
removed (hence a total of 234 bits). If the even and odd pages have been
received from two different carriers (E5b and L1), bit 5 of the Source
field is set.
      */
         
