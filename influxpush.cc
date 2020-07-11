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
  if(!d_buffer.insert(line).second)
    d_numdedupmsmts++;
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
    if(!v.first) // trick to null out certain fields
      continue;
    d_numvalues++;
    if(!lefirst) {
      buffer +=",";
    }
    lefirst=false;
    buffer += string(v.first) + "="+to_string(v.second);
  }
  buffer += " " + to_string((uint64_t)(t* 1000000000))+"\n";
  d_nummsmts++;
  d_msmtmap[(string)name]++;
  queueValue(buffer);
}


void InfluxPusher::addValue(const SatID& id, string_view name, const initializer_list<pair<const char*, var_t>>& values, double t, std::optional<int> src, std::optional<string> tag)
{

  vector<pair<string,var_t>> tags{{"sv", id.sv}, {"gnssid", id.gnss}, {"sigid", id.sigid}};

  if(src) {
    tags.push_back({*tag, *src});
  }
  addValue(tags, name, values, t);
}

void InfluxPusher::addValue(const vector<pair<string,var_t>>& tags, string_view name, const initializer_list<pair<const char*, var_t>>& values, double t)
{
  if(d_mute)
    return;

  if(t > 2200000000 || t < 0) {
    cerr<<"Unable to store item "<<name<<" for ";
    //    for(const auto& t: tags)
    //      cerr<<t.first<<"="<<t.second<<" ";
    cerr<<": time out of range "<<t<<endl;
    return;
  }
  for(const auto& p : values) {
    if(auto ptr = std::get_if<double>(&p.second))
      if(isnan(*ptr))
        return;
  }

  string buffer = string(name);
  for(const auto& t : tags) {
    buffer += ","+t.first + "=";
    std::visit([&buffer](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, string>) {
                      // string tags really don't need a "
                       buffer += arg;
          }
        else {
          // resist the urge to do integer tags, it sucks
          buffer += to_string(arg);
        }
      }, t.second);
  }
  
  buffer+= " ";
  bool lefirst=true;
  for(const auto& v : values) {
    if(!v.first) // trick to null out certain fields
      continue;

    d_numvalues++;
    if(!lefirst) {
      buffer +=",";
    }
    lefirst=false;
    buffer += string(v.first) + "=";

    std::visit([&buffer](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, string>)
                       buffer += "\""+arg+"\"";
        else {
          buffer += to_string(arg);
          if constexpr (!std::is_same_v<T, double>)
                         buffer+="i";
        }
      }, v.second);
  }
  buffer += " " + to_string((uint64_t)(t*1000000000))+"\n";
  d_nummsmts++;
  d_msmtmap[(string)name]++;
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
