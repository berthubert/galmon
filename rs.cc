#include "rs.hh"
#include <stdexcept>
#include <string.h>
#include <iostream>
extern "C" {
#include <fec.h>
}
using namespace std;

RSCodec::RSCodec(const std::vector<unsigned int>& roots, unsigned int fcr, unsigned int prim, unsigned int nroots, unsigned int pad, unsigned int bits)
  : d_N((1<< (bits)) - pad -1),
    d_K((1<< (bits)) - pad - 1 - nroots),
    d_nroots(nroots),
    d_bits(bits)
{
  if(d_bits > 8)
    throw std::runtime_error("This encoder supports 8 bits at most");
  
  for(const auto& r : roots)
    d_gfpoly |= (1<<r);

  d_rs = init_rs_char(d_bits, d_gfpoly, fcr, prim, nroots, pad);

  if(!d_rs)
    throw std::runtime_error("Unable to initialize RS codec");
}

void RSCodec::encode(std::string& msg)
{
  if(msg.size() > d_K)
    throw std::runtime_error("Can't encode message longer than "+std::to_string(d_K)+" bytes");
  msg.append(d_K - msg.size(), 0);

  //    void encode_rs_char(void *rs,unsigned char *data,
  //            unsigned char *parity);
  uint8_t parity[d_nroots];
  encode_rs_char(d_rs, (uint8_t*)msg.c_str(), parity);
  msg.append((char*)&parity[0], (char*)&parity[d_nroots]);
}

int RSCodec::decode(const std::string& in, std::string& out, vector<unsigned int>* corrs)
{
  // int decode_rs_char(void *rs,unsigned char *data,int *eras_pos,
  //        int no_eras);
  
  unsigned char data[in.length()];
  memcpy(data, in.c_str(), in.length());
  vector<int> eras_pos;
  int eras_no=0;
  if(corrs) {
    for(const auto& c : *corrs) {
      eras_pos.push_back(c);
      eras_no++;
    }
  }
  eras_pos.resize(d_nroots);
  int ret = decode_rs_char(d_rs, data, &eras_pos[0], eras_no);
  /*
    The decoder corrects the symbols "in place", returning the number of symbols in error. If the codeword is uncorrectable, -1 is returned and the data  block  is  unchanged.  If
    eras_pos  is non-null, it is used to return a list of corrected symbol positions, in no particular order.  This means that the array passed through this parameter must have at
    least nroots elements to prevent a possible buffer overflow.
  */
  if(ret < 0)
    throw std::runtime_error("Could not correct message");
  if(corrs)
    corrs->clear();
  if(ret && corrs) {
    for(int n=0; n < ret; ++n)
      corrs->push_back(eras_pos.at(n));
  }
  
  out.assign((char*) data, (char*)data + d_N);
  return ret;  
}
  
RSCodec::~RSCodec()
{
  if(d_rs)
    free_rs_char(d_rs);
}

////

RSCodecInt::RSCodecInt(const std::vector<unsigned int>& roots, unsigned int fcr, unsigned int prim, unsigned int nroots, unsigned int pad, unsigned int bits)
  : d_N((1<< (bits)) - pad -1),
    d_K((1<< (bits)) - pad - 1 - nroots),
    d_nroots(nroots),
    d_bits(bits)
{
  if(d_bits > 32)
    throw std::runtime_error("This encoder supports 32 bits at most");
  
  for(const auto& r : roots)
    d_gfpoly |= (1<<r);

  d_rs = init_rs_int(d_bits, d_gfpoly, fcr, prim, nroots, pad);

  if(!d_rs)
    throw std::runtime_error("Unable to initialize RS codec");
}

void RSCodecInt::encode(vector<unsigned int>& msg)
{
  if(msg.size() > d_K)
    throw std::runtime_error("Can't encode message longer than "+std::to_string(d_K)+" bytes");
  msg.resize(d_K);

  vector<unsigned int> parity(d_nroots);
  
  encode_rs_int(d_rs, (int*)&msg[0], (int*)&parity[0]);
  for(const auto& i : parity)
    msg.push_back(i);
}

int RSCodecInt::decode(const std::vector<unsigned int>& in, std::vector<unsigned int>& out, vector<unsigned int>* corrs)
{
  // int decode_rs_char(void *rs,unsigned char *data,int *eras_pos,
  //        int no_eras);
  
  vector<unsigned int> data = in;
  vector<unsigned int> eras_pos;
  int eras_no=0;
  if(corrs) {
    for(const auto& c : *corrs) {
      eras_pos.push_back(c);
      eras_no++;
    }
  }
  eras_pos.resize(d_nroots);
  int ret = decode_rs_int(d_rs, (int*)&data[0], (int*)&eras_pos[0], eras_no);
  /*
    The decoder corrects the symbols "in place", returning the number of symbols in error. If the codeword is uncorrectable, -1 is returned and the data  block  is  unchanged.  If
    eras_pos  is non-null, it is used to return a list of corrected symbol positions, in no particular order.  This means that the array passed through this parameter must have at
    least nroots elements to prevent a possible buffer overflow.
  */
  if(ret < 0)
    throw std::runtime_error("Could not correct message");
  if(corrs)
    corrs->clear();
  if(ret && corrs) {
    for(int n=0; n < ret; ++n)
      corrs->push_back(eras_pos[n]);
  }
  out = data;
  out.resize(d_N);
  return ret;  
}
  
RSCodecInt::~RSCodecInt()
{
  if(d_rs)
    free_rs_int(d_rs);
}
