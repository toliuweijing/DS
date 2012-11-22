// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"
#include <map>
#include <pthread.h>

using namespace std;

class lock_server {

  protected:
    int nacquire;
    enum lStatus {
      FREE, LOCKED
    };

    struct lockStatus {
      lStatus status;
      string clientId;
      pthread_cond_t condVar;
      int waitingCount;
    };

    map<lock_protocol::lockid_t, lockStatus> locks;
    int waitingCount;

  public:
    pthread_mutex_t mutexLocks;
    lock_server();
    ~lock_server();
    lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
    lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
    lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);
    void changeLockState(lock_protocol::lockid_t lid, lStatus newStatus, string clientId);
    void createLock(lock_protocol::lockid_t lid, string clientId);
};

#endif 







