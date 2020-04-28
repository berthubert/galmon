#include "influxpush.hh"
#include "minicurl.hh"
using namespace std;


InfluxPusher::InfluxPusher(std::string_view dbname) : d_dbname(dbname)
{
  if(dbname=="null") {
    d_mute = true;
    cout<<"Not sending data to influxdb"<<endl;
  }
}

void InfluxPusher::queueValue(const std::string& line)
{
  d_buffer.insert(line);
  //  if(d_buffer.insert(line).second)
  //  cout<<line;
  checkSend();
}

void InfluxPusher::addValueObserver(int src, string_view name, const initializer_list<pair<const char*, double>>& values, double t, std::optional<SatID> satid)
{
  if(d_mute)
    return;
  
  if(t > 2200000000 || t < 0) {
    cerr<<"Unable to store item "<<name<<" for observer "<<src<<": time out of range "<<t<<endl;
    return;
  }
  for(const auto& p : values) {
    if(isnan(p.second))
      return;
  }
  string buffer;
  buffer+= string(name)+",src="+to_string(src);
  if(satid) {
    buffer+=",gnssid="+to_string(satid->gnss)+ +",sv=" +to_string(satid->sv)+",sigid="+to_string(satid->sigid);
  }
  
  buffer+= " ";
  bool lefirst=true;
  for(const auto& v : values) {
    if(!lefirst) {
      buffer +=",";
    }
    lefirst=false;
    buffer += string(v.first) + "="+to_string(v.second);
  }
  buffer += " " + to_string((uint64_t)(t* 1000000000))+"\n";

  queueValue(buffer);
}

  
void InfluxPusher::addValue(const SatID& id, string_view name, const initializer_list<pair<const char*, double>>& values, double t, std::optional<int> src, std::optional<string> tag)
{
  if(d_mute)
    return;

  if(t > 2200000000 || t < 0) {
    cerr<<"Unable to store item "<<name<<" for sv "<<id.gnss<<","<<id.sv<<": time out of range "<<t<<endl;
    return;
  }
  for(const auto& p : values) {
    if(isnan(p.second))
      return;
  }

  string buffer = string(name) +",gnssid="+to_string(id.gnss)+",sv=" +to_string(id.sv)+",sigid="+to_string(id.sigid);
  if(src)
    buffer += ","+*tag+"="+to_string(*src);
  
  buffer+= " ";
  bool lefirst=true;
  for(const auto& v : values) {
    if(!lefirst) {
      buffer +=",";
    }
    lefirst=false;
    buffer += string(v.first) + "="+to_string(v.second);
  }
  buffer += " " + to_string((uint64_t)(t*1000000000))+"\n";
  queueValue(buffer);
  
}


void InfluxPusher::checkSend()
{
  if(d_buffer.size() > 10000 || (time(0) - d_lastsent) > 10) {
    set<string> buffer;
    buffer.swap(d_buffer);
    //      thread t([buffer,this]() {
    if(!d_mute)
      doSend(buffer);
    //        });
    //      t.detach();
    d_lastsent=time(0);
  }
}

void InfluxPusher::doSend(const set<std::string>& buffer)
{
  MiniCurl mc;
  MiniCurl::MiniCurlHeaders mch;
  if(!buffer.empty()) {
    string newout;
    for(const auto& nl: buffer) 
      newout.append(nl);
    
    /*
      ofstream infl;
      infl.open ("infl.txt", std::ofstream::out | std::ofstream::app);
      infl << newout;
    */
    try {    
      mc.postURL("http://127.0.0.1:8086/write?db="+d_dbname, newout, mch);
    }
    catch(std::exception& e) {
      if(strstr(e.what(), "retention"))
        return;
      throw;        
    }
  }
}

InfluxPusher::~InfluxPusher()
{
  if(d_dbname != "null")
    doSend(d_buffer);
}
