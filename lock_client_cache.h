// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "extent_client.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"
#include <map>

using namespace std;


// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_release_user_impl: public lock_release_user {  
  private:
    extent_client* ec;
  public:
    lock_release_user_impl(extent_client* _ec);
    ~lock_release_user_impl();
    void dorelease(lock_protocol::lockid_t);
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;

  enum lockState {NONE, FREE, LOCKED, ACQUIRING, RELEASING};
  struct lockInfo {
    lock_protocol::lockid_t lid;
    pthread_t threadId;
    pthread_cond_t retryCondVar;
    pthread_cond_t acquireCondVar;
    pthread_cond_t lockCondVar;
    // At anytime there will be at most one thread waiting to revoke, retry
    // or acquire the lock from server, so use bool variables instead of
    // waitlist to indicate if there is any thread waiting.
    bool waitingToRetry;
    bool waitingToAcq;
    bool toRetry;
    bool toRevoke;
    lockState state;
    map<pthread_t, bool> waitList;
  };
  map<lock_protocol::lockid_t, lockInfo> lockList;
  pthread_mutex_t mutexLockList;
  
  void createLock(lock_protocol::lockid_t lid, pthread_t threadId);
  void updateLockState(
      lock_protocol::lockid_t lid, pthread_t threadId, lockState state);
  void acquireAndMaybeRetry(lock_protocol::lockid_t lid);
 public:
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);
};


#endif
