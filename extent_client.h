// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

using namespace std;

class extent_client {
 private:
  rpcc *cl;
  struct cacheFileEnt {
    string fileExt;
    bool dirty;
  };
  map<extent_protocol::extentid_t, cacheFileEnt> cacheFiles;
  map<extent_protocol::extentid_t, extent_protocol::attr> cacheAttrs;
  pthread_mutex_t mutexFile;

 public:
  extent_client(std::string dst);
  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  void flush(extent_protocol::extentid_t eid);
};

#endif 

