// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
//<<<<<<< HEAD
//=======
  VERIFY(pthread_mutex_init(&mutexLockList, NULL) == 0);
//>>>>>>> lab5
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
  lock_protocol::status ret = lock_protocol::OK;
//<<<<<<< HEAD
//=======
  pthread_mutex_lock(&mutexLockList);
  if (lockList.find(lid) == lockList.end()) {
    // If lock doesn't exist, just create lock and grant it to client.
    createLock(lid, id);
    pthread_mutex_unlock(&mutexLockList);
  } else {
    if (lockList[lid].state == LOCKED) {
      ret = lock_protocol::RETRY;
      if (lockList[lid].waitList.size() == 0) {
        // If the lock is locked and there isn't any other client waiting,
        // put the client into the waitlist and revoke the lock.
        // Unlock mutex before making RPC call.
        lockList[lid].waitList.push_back(id);
        pthread_mutex_unlock(&mutexLockList);
        rlock_protocol::status cliRpcRet = revokeLock(lid, lockList[lid].clientId);
        if (cliRpcRet != rlock_protocol::OK) {
          // Revoke failure. Return RPCERR.
          // Hope this doesn't happen for now...
          cout << "Revoke failure on lid:" << lid << " for client:" 
            << id << endl;
          ret = lock_protocol::RPCERR;
        }
      } else {
        // If the lock is locked and there are other clients waiting, just
        // put the client into the waitlist.
        lockList[lid].waitList.push_back(id);
        pthread_mutex_unlock(&mutexLockList);
      }
    } else {
      if (lockList[lid].waitList.size() > 0) {
        if (id.compare(lockList[lid].waitList.front()) == 0) {
          // If this client is the client at the top of the waitlist,
          // grant it the lock.
          lockList[lid].waitList.pop_front();
          updateLockState(lid, id, LOCKED);
          if (lockList[lid].waitList.size() > 0) {
            // If there are still other clients waiting for this lock,
            // send a revoke to this client before granting the lock,
            // so it will release the lock when it finishes using it.
            pthread_mutex_unlock(&mutexLockList);
            rlock_protocol::status cliRpcRet = revokeLock(lid, id);
            if (cliRpcRet != rlock_protocol::OK) {
              // Revoke failure. Return RPCERR.
              // Hope this doesn't happen for now since there isn't enough
              // information obtained to write a proper failure handler.
              cout << "Revoke failure on lid:" << lid << " for client:" 
                << id << endl;
              //ret = lock_protocol::RPCERR;
            }
          } else {
            pthread_mutex_unlock(&mutexLockList);
          }
        } else {
          // If the lock is free but the waitlist is not empty, and the
          // requesting client is not the one at the top of the waitlist,
          // just put it into the waitlist.
          lockList[lid].waitList.push_back(id);
          ret = lock_protocol::RETRY;
          pthread_mutex_unlock(&mutexLockList);
        }
      } else {
        // If the lock is free and the waitlist is empty, grant the lock
        // to the client.
        updateLockState(lid, id, LOCKED);
        pthread_mutex_unlock(&mutexLockList);
      }
    }  
  }
  //cout << "client " << id << " got returned " << ret << endl;
//>>>>>>> lab5
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
//<<<<<<< HEAD
//=======
  pthread_mutex_lock(&mutexLockList);
  updateLockState(lid, "", FREE);
  if (lockList[lid].waitList.size() > 0) {
    pthread_mutex_unlock(&mutexLockList);
    rlock_protocol::status retryRet =
        retryCli(lid, lockList[lid].waitList.front());
    if (retryRet != rlock_protocol::OK) {
      // Retry failure. Return RPCERR.
      // Hope this doesn't happen for now since there isn't enough
      // information obtained to write a proper failure handler.
      cout << "Retry failure on lid:" << lid << " for client:" 
        << lockList[lid].waitList.front() << endl;
      //ret = lock_protocol::RPCERR;
    }
  } else {
    pthread_mutex_unlock(&mutexLockList);
  }
//>>>>>>> lab5
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

//<<<<<<< HEAD
//=======
void lock_server_cache::createLock(
    lock_protocol::lockid_t lid, string clientId) {
  lockList[lid].lid = lid;
  lockList[lid].state = LOCKED;
  lockList[lid].clientId = clientId;
}

rlock_protocol::status lock_server_cache::revokeLock(
    lock_protocol::lockid_t lid, string clientId) {
  rlock_protocol::status ret = rlock_protocol::OK;
  int r;
  handle h(clientId);
  rpcc *cl = h.safebind();
  if (cl) {
    ret = cl->call(rlock_protocol::revoke, lid, r);
  }
  if (!cl || ret != rlock_protocol::OK) {
    ret = rlock_protocol::RPCERR;
  }
  return ret;
}

rlock_protocol::status lock_server_cache::retryCli(
    lock_protocol::lockid_t lid, string clientId) {
  rlock_protocol::status ret = rlock_protocol::OK;
  int r;
  handle h(clientId);
  rpcc *cl = h.safebind();
  if (cl) {
    ret = cl->call(rlock_protocol::retry, lid, r);
  }
  if (!cl || ret != rlock_protocol::OK) {
    ret = rlock_protocol::RPCERR;
  }
  return ret;
}

void lock_server_cache::updateLockState(
    lock_protocol::lockid_t lid, string clientId, lockState state) {
  lockList[lid].clientId = clientId;
  lockList[lid].state = state;
}

//>>>>>>> lab5
