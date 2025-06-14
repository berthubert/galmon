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

#pragma once
#include <string>
#include <curl/curl.h>
#include "comboaddress.hh"
#include <map>
#include <atomic>
#include <stdexcept>
using std::string;
// turns out 'CURL' is currently typedef for void which means we can't easily forward declare it

class MiniCurl
{
public:
  using MiniCurlHeaders = std::map<std::string, std::string>;

  static void init();

  MiniCurl(const string& useragent="MiniCurl/0.0");
  ~MiniCurl();
  MiniCurl& operator=(const MiniCurl&) = delete;
  typedef std::map<int, std::map<std::string, std::string>> certinfo_t;
  std::string getURL(const std::string& str, const bool nobody=0, certinfo_t* ciptr=0, const ComboAddress* rem=0, const ComboAddress* src=0);
  std::string postURL(const std::string& str, const std::string& postdata, MiniCurlHeaders& headers);

  std::string urlEncode(std::string_view str);
  CURL *d_curl;
  time_t d_filetime=-1;
  long d_http_code=-1;
private:
  std::string d_data;
  static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

  struct curl_slist* d_header_list = nullptr;
  struct curl_slist *d_host_list = nullptr;
  void setupURL(const std::string& str, const ComboAddress* rem=0, const ComboAddress* src=0);
  void setHeaders(const MiniCurlHeaders& headers);
  void clearHeaders();
};

std::string extractHostFromURL(const std::string& url);
