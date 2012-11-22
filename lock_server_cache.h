#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>

#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


using namespace std;

class lock_server_cache {
 private:
  int nacquire;

  enum lockState {FREE, LOCKED};
  struct lockInfo {
    lock_protocol::lockid_t lid;
    lockState state;
    string clientId;
    list<string> waitList;
  };
  map<lock_protocol::lockid_t, lockInfo> lockList;
  pthread_mutex_t mutexLockList;

  rlock_protocol::status revokeLock(
      lock_protocol::lockid_t lid, string clientId);
  rlock_protocol::status retryCli(
      lock_protocol::lockid_t lid, string clientId);
  void createLock(lock_protocol::lockid_t lid, std::string clientId);
  void updateLockState(
      lock_protocol::lockid_t lid, string clientId, lockState state);

 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
