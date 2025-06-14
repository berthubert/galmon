/*
 * MIT License
 *
 * Copyright (c) 2018-2019 powerdns.com bv
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "minicurl.hh"
#include <curl/curl.h>
#include <stdexcept>
#include <vector>
#include "fmt/format.h"
#include "fmt/printf.h"
void MiniCurl::init()
{
  static std::atomic_flag s_init = ATOMIC_FLAG_INIT;

  if (s_init.test_and_set())
    return;

  CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
  if (code != 0) {
    throw std::runtime_error("Error initializing libcurl");
  }
}

MiniCurl::MiniCurl(const string& useragent)
{
  d_curl = curl_easy_init();
  if (d_curl == nullptr) {
    throw std::runtime_error("Error creating a MiniCurl session");
  }
  curl_easy_setopt(d_curl, CURLOPT_USERAGENT, useragent.c_str());
}

MiniCurl::~MiniCurl()
{
  if(d_host_list)
    curl_slist_free_all(d_host_list);
  curl_easy_cleanup(d_curl);
}

size_t MiniCurl::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  MiniCurl* us = (MiniCurl*)userdata;
  us->d_data.append(ptr, size*nmemb);
  return size*nmemb;
}

using namespace std;

string extractHostFromURL(const std::string& url)
{
  auto pos = url.find("://");
  if(pos == string::npos)
    throw std::runtime_error("Can't find host part of '"+url+"'");
  pos += 3;
  auto endpos = url.find('/', pos);
  if(endpos == string::npos)
    return url.substr(pos);

  return url.substr(pos, endpos-pos);
}

void MiniCurl::setupURL(const std::string& str, const ComboAddress* rem, const ComboAddress* src)
{
  if(rem) {
    if(d_host_list) {
      curl_slist_free_all(d_host_list);
      d_host_list = nullptr;
    }

    // url = http://hostname.enzo/url
    string host4=extractHostFromURL(str);
    // doest the host contain port indication
    std::size_t found = host4.find(':');
    vector<uint16_t> ports{80, 443};
    if (found != std::string::npos) {
      int port = std::stoi(host4.substr(found + 1));
      if (port <= 0 || port > 65535)
        throw std::overflow_error("Invalid port number");
      ports = {(uint16_t)port};
      host4 = host4.substr(0, found);
    }

    for (const auto& port : ports) {
      string hcode = fmt::sprintf("%s:%u:[%s]", host4 , port , rem->toString());
      //      fmt::print("hcode: {}\n", hcode);
      d_host_list = curl_slist_append(d_host_list, hcode.c_str());
    }

    curl_easy_setopt(d_curl, CURLOPT_RESOLVE, d_host_list);
  }
  // should be a setting
  curl_easy_setopt(d_curl, CURLOPT_FOLLOWLOCATION, 1L);
  /* only allow HTTP and HTTPS */
  curl_easy_setopt(d_curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(d_curl, CURLOPT_SSL_VERIFYPEER, true);
  curl_easy_setopt(d_curl, CURLOPT_SSL_VERIFYHOST, true);
  //  curl_easy_setopt(d_curl, CURLOPT_FAILONERROR, true);
  curl_easy_setopt(d_curl, CURLOPT_URL, str.c_str());

  curl_easy_setopt(d_curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(d_curl, CURLOPT_WRITEDATA, this);
  curl_easy_setopt(d_curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(d_curl, CURLOPT_CERTINFO, 1L);
  curl_easy_setopt(d_curl, CURLOPT_FILETIME, 1L);
  if(src) {
    curl_easy_setopt(d_curl, CURLOPT_INTERFACE, src->toString().c_str());
    // XXX report errors!!
    //    fmt::print("Setting interface to '{}', ret {}\n", src->toString().c_str(),
    //         ret);
  }

  clearHeaders();
  d_data.clear();
}

std::string MiniCurl::getURL(const std::string& str, const bool nobody, MiniCurl::certinfo_t* ciptr, const ComboAddress* rem, const ComboAddress* src)
{
  setupURL(str, rem, src);
  if (nobody)
    curl_easy_setopt(d_curl, CURLOPT_NOBODY, 1L);
  auto res = curl_easy_perform(d_curl);
  if(d_host_list) {
    curl_slist_free_all(d_host_list);
    d_host_list = nullptr;
  }
  if(res != CURLE_OK)  {
    throw std::runtime_error("Unable to retrieve URL "+str+ " - "+string(curl_easy_strerror(res)));
  }

  d_filetime=-1;
  curl_easy_getinfo(d_curl, CURLINFO_FILETIME, &d_filetime);
  
  if(ciptr) {
    struct curl_certinfo *ci;
    res = curl_easy_getinfo(d_curl, CURLINFO_CERTINFO, &ci);
    if(res) {
      throw std::runtime_error(fmt::format("URL: {}, Error: {}\n", str, curl_easy_strerror(res)));
    }

    int i;
    for(i = 0; i < ci->num_of_certs; i++) {
      struct curl_slist *slist;
      
      for(slist = ci->certinfo[i]; slist; slist = slist->next) {
        string data = slist->data;
        if(auto pos = data.find(':'); pos != string::npos)
          (*ciptr)[i][data.substr(0, pos)] = data.substr(pos+1);
      }
    }
  }
  d_http_code = 0;  
  curl_easy_getinfo(d_curl, CURLINFO_RESPONSE_CODE, &d_http_code);
  
  std::string ret=d_data;
  d_data.clear();
  return ret;
}

std::string MiniCurl::postURL(const std::string& str, const std::string& postdata, MiniCurlHeaders& headers)
{
  setupURL(str);
  setHeaders(headers);
  curl_easy_setopt(d_curl, CURLOPT_POSTFIELDSIZE, postdata.size());
  curl_easy_setopt(d_curl, CURLOPT_POSTFIELDS, postdata.c_str());

  auto res = curl_easy_perform(d_curl);

  long http_code = 0;
  curl_easy_getinfo(d_curl, CURLINFO_RESPONSE_CODE, &http_code);

  if(res != CURLE_OK || http_code >= 300 ) {
    cerr<<"Detailed error: "<<d_data<<endl;
    cerr<<postdata<<endl;
    throw std::runtime_error("Unable to post URL ("+std::to_string(http_code)+"): "+string(curl_easy_strerror(res))+", detail: "+d_data);
  }

  std::string ret=d_data;

  d_data.clear();
  return ret;
}

void MiniCurl::clearHeaders()
{
  if (d_curl) {
    curl_easy_setopt(d_curl, CURLOPT_HTTPHEADER, NULL);
    if (d_header_list != nullptr) {
      curl_slist_free_all(d_header_list);
      d_header_list = nullptr;
    }
  }
}

void MiniCurl::setHeaders(const MiniCurlHeaders& headers)
{
  if (d_curl) {
    for (auto& header : headers) {
      std::stringstream header_ss;
      header_ss << header.first << ": " << header.second;
      d_header_list = curl_slist_append(d_header_list, header_ss.str().c_str());
    }
    curl_easy_setopt(d_curl, CURLOPT_HTTPHEADER, d_header_list);
  }
}

string MiniCurl::urlEncode(string_view str)
{
  char *ptr= curl_easy_escape(d_curl , &str[0] , str.size() );
  string ret(ptr);
  curl_free(ptr);
  return ret;
}

