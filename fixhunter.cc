#include "fixhunter.hh"
#include <iostream>
#include "bits.hh"
#include "rs.hh"
#include "ephemeris.hh"
#include "galileo.hh"
using namespace std;

void FixHunter::reportInav(const basic_string<uint8_t>& inav, int32_t gst)
{
  int wtype = getbitu(&inav[0], 0, 6);

  if(wtype >= 1 && wtype <=4) {
    d_latestiod = getbitu(&inav[0], 6, 10);
    if(wtype == 1) inav1 = inav;
    else if(wtype == 2) inav2 = inav;
    else if(wtype == 3) inav3 = inav;
    else if(wtype == 4) inav4 = inav;
  }
  else if(wtype == 16) inav16 = inav;
  else if(wtype == 17) inav17 = inav;
  else if(wtype == 18) inav18 = inav;
  else if(wtype == 19) inav19 = inav;
  else if(wtype == 20) inav20 = inav;
  else return;

  if(wtype == 16) {
    GalileoMessage gm;
    gm.parse(inav);
    int32_t t0r = 1 + gst - (gst%30);
    d_inav16t0r = t0r;
    cout<<" redced af0red "<< 1000000000.0*ldexp(gm.af0red, -26)<<" ns, "<<3600.0*(1000000000.0/(1<<20))*ldexp(gm.af1red, -15)<<" ns/hour ("<<gm.af1red<<") t0r "<<t0r<<" ";


    //(30*((nmm.gi().gnsstow()-2)/30)+1) % 604800; // page 56 of the OSS ICD 2.0
    REDCEDAdaptor rca(gm, t0r);
    
    Point pointRed;
    cout<<"eyred "<<gm.eyred<<" exred "<<gm.exred<<"\nlambda0red in rad "<< ldexp(M_PI*gm.lambda0red, -22)<<" atan2 " <<atan2(1.0*gm.eyred, 1.0*gm.exred)<<" deltaAred "<<gm.deltaAred<<endl;
    getCoordinates(gst, rca, &pointRed, false);
    cout<<"Reduced coordinates: "<<pointRed<<endl;
  }

  tryFix(gst);
}

void FixHunter::tryFix(int32_t gst)
{
  RSCodec rsc({0, 2, 3, 4, 8}, 1, 1,  60,              137);
  string in;
  vector<unsigned int> corr;

  int cnt=0;
  for(const auto& theinav : {inav4, inav3, inav2, inav1, inav20, inav19, inav18, inav17}) 
    if(!theinav.empty()) ++cnt;
  if(cnt < 4)
    return;
  
  auto blankOut = [&in, &corr](int amount) {
    for(int p=0; p < amount ; ++p) {
      corr.push_back(in.size());
      in.append(1, (char)0); // we just don't have the information
    }
  };
    
  int wtype=4;
  int navcount=0;
  for(const auto& theinav : {inav4, inav3, inav2}) {
    if(!theinav.empty()) {
      ++navcount;
      cout<<"Have wtype "<<wtype<<endl;
      unsigned char tmp[14];
      for(int i=0; i < 14; i++)
        setbitu(tmp, i*8, 8, getbitu(&theinav[0], 16+i*8, 8));
      std::reverse(begin(tmp), end(tmp));
      in.append((char*)tmp, 14);
    }
    else {
      cout<<"Blanking wtype "<<wtype<<endl;
      blankOut(14);
    }
    wtype--;
  }

  if(!inav1.empty()) {
    cout<<"Have wtype "<<wtype<<endl;
    ++navcount;
    unsigned char tmp[16];
    setbitu(tmp, 0, 6, 1); // wtype 1 somehow
    setbitu(tmp, 6, 2, getbitu(&inav1[0], 14, 2)); // last 2 bits of iodnav
    setbitu(tmp, 8, 8, getbitu(&inav1[0], 6, 8)); // first 8 bits of iodnav
    for(int i=0; i < 14; ++i)
      setbitu(tmp, 16+i*8, 8, getbitu(&inav1[0], 16+i*8, 8));
    std::reverse(begin(tmp), end(tmp));
    in.append((char*)tmp, 16);
  }
  else {
    cout<<"Blanking wtype "<< wtype<<", which is special"<<endl;
    blankOut(14); // 14 symbols we don't have
    unsigned char tmp[2];
    // and two that we can fake:
    setbitu(tmp, 0, 6, 1); // wtype 1 somehow
    setbitu(tmp, 6, 2, d_latestiod & 3); // last 2 bits of iodnav
    setbitu(tmp, 8, 8, d_latestiod >> 2);
    std::reverse(begin(tmp), end(tmp));
    in.append((char*)tmp, 2);
  }
  
  // now the parity bits

  wtype=20;
  for(const auto& theinav : {inav20, inav19, inav18, inav17}) {
    if(!theinav.empty()) {
      cout<<"Have wtype "<<wtype<<endl;
      GalileoMessage gm;
      gm.parse(theinav);
      
      std::reverse(gm.rsparity.begin(), gm.rsparity.end());
      in += gm.rsparity;
    }
    else {
      cout<<"Blanking wtype "<<wtype<<endl;
      
      blankOut(15);
    }
    --wtype;
  }

  if(corr.size() >= 60 && navcount != 4) {
    cout<<"Too many erasures ("<<corr.size()<<"), can't correct"<<endl;
    return;
  }
  
  for(auto& c : corr)
    c = 137 + c;
  
  
  string out;
  cout<<"Input size "<<in.size()<<", corrections/erasure size "<<corr.size()<<", decode status: "<<endl;
  try {
    if(corr.size() < 60) {
      cout<<"Got a reconstructed ephemeris! With "<<rsc.decode(in, out, &corr)<<" corrections"<<endl;
      for(const auto& c : corr) {
        cout<<c-137<<" "; // padding!
      }
      cout<<endl;
    }
    else {
      cout<<"Had all 4 nav words but no parity, not doing corrections"<<endl;
      out=in;
    }
    struct GalileoMessage gm=fillGMFromRS(out);
    Point point;
    getCoordinates(gst, gm, &point, false);
    cout<<"full coordinates: "<<point<<endl;

    if(!inav16.empty()) {
      GalileoMessage gm16;
      gm16.parse(inav16);
      REDCEDAdaptor rca(gm16, d_inav16t0r);
      
      Point pointRed;
      cout<<"eyred "<<gm.eyred<<" exred "<<gm.exred<<"\nlambda0red in rad "<< ldexp(M_PI*gm.lambda0red, -22)<<" atan2 " <<atan2(1.0*gm.eyred, 1.0*gm.exred)<<" deltaAred "<<gm.deltaAred<<endl;
      getCoordinates(gst, rca, &pointRed);
      cout<<"Reduced coordinates: "<<pointRed<<", distance: ";
      Vector dist(pointRed, point);
          
      cout<<"Distance: "<<dist<<", length "<<dist.length()<<", clockdiff "<<
        (rca.getAtomicOffset(gst).first - gm.getAtomicOffset(gst).first)/3<<"m"<<endl;

    }
    
  }
  catch(...)
    {
      cout << "failed"<<endl;
    }
}


struct GalileoMessage FixHunter::fillGMFromRS(const std::string& out)
{
  // we need to reconstruct words 1, 2, 3 and 4 and feed them to the parser
  basic_string<uint8_t> inav[5];
  string inavraw[5];
  inavraw[4] = out.substr(0, 14); 
  inavraw[3] = out.substr(14, 14);
  inavraw[2] = out.substr(28, 14);
  inavraw[1] = out.substr(42, 16);
  for(int n=1; n<=4; ++n)
    reverse(inavraw[n].begin(), inavraw[n].end());

  
  uint8_t tmp[16];
  setbitu(tmp, 0, 6, 1); // wtype 1
  setbitu(tmp, 6, 2, getbitu((unsigned char*) inavraw[1].c_str(), 14, 2));
  setbitu(tmp, 8, 8, getbitu((unsigned char*) inavraw[1].c_str(), 6, 8));
  for(int n=0; n < 14; ++n)
    setbitu(tmp, 16 + n*8, 8, getbitu((unsigned char*) inavraw[1].c_str(), 16 + n*8, 8));

  inav[1].assign(tmp, 16);

  struct GalileoMessage gm={};
  gm.parse(inav[1]);
  cout<<"wtype: "<<(int)gm.wtype<<", iod "<<gm.iodnav<<endl;
  cout<<"T0e "<<gm.getT0e()<<" e "<<gm.getE()<<" sqrtA " << gm.getSqrtA() << endl;
  
  for(int i = 2; i <= 4; ++i) {
    setbitu(tmp, 0, 6, i);
    setbitu(tmp, 6, 10, getbitu((unsigned char*) inav[1].c_str(), 6, 10)); // fake in IOD from inav1
    for(int n=0; n < 14; ++n)
      setbitu(tmp, 16 + n*8, 8, getbitu((unsigned char*) inavraw[i].c_str(), n*8, 8));
    inav[i].assign(tmp, 16);
    gm.parse(inav[i]);
    cout<<"wtype: "<<(int)gm.wtype<<", iod "<<gm.iodnav<<endl;
  }
  return gm;
}
