#pragma once
#include <string>
#include <vector>

class RSCodec
{
public:
  RSCodec(const std::vector<unsigned int>& roots, unsigned int fcr, unsigned int prim, unsigned int nroots, unsigned int pad=0, unsigned int bits=8);
  void encode(std::string& msg);
  
  int decode(const std::string& in, std::string& out, std::vector<unsigned int>* corrections=0);
  int getPoly() // the representation as a number
  {
    return d_gfpoly;
  }
  ~RSCodec();
private:
  void* d_rs{0};
  unsigned int d_gfpoly{0};
public:
  const unsigned int d_N, d_K, d_nroots, d_bits;
  
};


class RSCodecInt
{
public:
  RSCodecInt(const std::vector<unsigned int>& roots, unsigned int fcr, unsigned int prim, unsigned int nroots, unsigned int pad=0, unsigned int bits=8);
  void encode(std::vector<unsigned int>& msg);
  
  int decode(const std::vector<unsigned int>& in, std::vector<unsigned int>& out, std::vector<unsigned int>* corrections=0);
  int getPoly() // the representation as a number
  {
    return d_gfpoly;
  }
  ~RSCodecInt();
private:
  void* d_rs{0};
  unsigned int d_gfpoly{0};
public:
  const unsigned int d_N, d_K, d_nroots, d_bits;
  
};
