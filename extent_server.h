// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"
#include <pthread.h>

using namespace std;

class extent_server {

  private:
    map<extent_protocol::extentid_t, string> files;
    map<extent_protocol::extentid_t, extent_protocol::attr> fileAttrs;
    pthread_mutex_t mutexFile;

  public:
    extent_server();
    int put(extent_protocol::extentid_t id, std::string, int &);
    int get(extent_protocol::extentid_t id, std::string &);
    int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
    int remove(extent_protocol::extentid_t id, int &);
};

#endif 

