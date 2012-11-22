// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

//<<<<<<< HEAD
//=======
using namespace std;
//>>>>>>> lab5

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();
//<<<<<<< HEAD
//=======

  VERIFY(pthread_mutex_init(&mutexLockList, NULL) == 0);
//>>>>>>> lab5
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
//<<<<<<< HEAD
  //return lock_protocol::OK;
//=======
  pthread_mutex_lock(&mutexLockList);
  if (lockList.find(lid) != lockList.end()) {
    // If the lock entry already exisits.
    switch (lockList[lid].state) {
      case NONE:
        if (lockList[lid].waitingToAcq) {
          // If there is some thread waiting to acquire the lock from server,
          // put the current thread into waitlist.
          (lockList[lid].waitList)[pthread_self()] = true;
          while (lockList[lid].state != FREE) {
            //cout << "Waiting for acquiring lock lid:" << lid << endl;
            pthread_cond_wait(&lockList[lid].lockCondVar, &mutexLockList);
          }
          // Thread gets the lock, and erases itself from the waitlist.
          updateLockState(lid, pthread_self(), LOCKED);
          lockList[lid].waitList.erase(pthread_self());
          pthread_mutex_unlock(&mutexLockList);
        } else {
          // If there is no thread waiting to acquire the lock from server,
          // the current thread acquires it from server.
          updateLockState(lid, pthread_self(), ACQUIRING);
          pthread_mutex_unlock(&mutexLockList);
          acquireAndMaybeRetry(lid);
        }
        break;
      case FREE:
        updateLockState(lid, pthread_self(), LOCKED);
        pthread_mutex_unlock(&mutexLockList);
        break;
      case ACQUIRING:
      case LOCKED:
        (lockList[lid].waitList)[pthread_self()] = true;
        while (lockList[lid].state != FREE) {
          pthread_cond_wait(&lockList[lid].lockCondVar, &mutexLockList);
        }
        // Thread gets the lock, and erases itself from the waitlist.
        updateLockState(lid, pthread_self(), LOCKED);
        lockList[lid].waitList.erase(pthread_self());
        pthread_mutex_unlock(&mutexLockList);
        break;
      case RELEASING:
        if (lockList[lid].waitingToAcq) {
          // If the lock is being released, and there is already a thread
          // waiting to acquire the lock from the server after it is released,
          // just put the current thread into waitlist.
          (lockList[lid].waitList)[pthread_self()] = true;
          while (lockList[lid].state != FREE) {
            //cout << "Waiting for acquiring lock lid:" << lid << endl;
            pthread_cond_wait(&lockList[lid].lockCondVar, &mutexLockList);
          }
          // Thread gets the lock, and erases itself from the waitlist.
          updateLockState(lid, pthread_self(), LOCKED);
          lockList[lid].waitList.erase(pthread_self());
          pthread_mutex_unlock(&mutexLockList);
        } else {
          // If the lock is being released, and there is no other thread
          // waiting to acquire it from the server after it is released,
          // the current thread will wait to acquire it from the server.
          lockList[lid].waitingToAcq = true;
          while (lockList[lid].state != NONE) {
            pthread_cond_wait(&lockList[lid].acquireCondVar, &mutexLockList);
          }
          lockList[lid].waitingToAcq = false;
          updateLockState(lid, pthread_self(), ACQUIRING);
          pthread_mutex_unlock(&mutexLockList);
          acquireAndMaybeRetry(lid);
        }
        break;
      default: break;
    }
  } else {
    // If the lock entry doesn't exist, create it and acquire the lock.
    createLock(lid, pthread_self());
    // Unlock mutex before making RPC call.
    pthread_mutex_unlock(&mutexLockList);
    acquireAndMaybeRetry(lid);
  }
  return ret;
//>>>>>>> lab5
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
//<<<<<<< HEAD
  //return lock_protocol::OK;

//=======
  pthread_mutex_lock(&mutexLockList);
  updateLockState(lid, pthread_self(), FREE);
  if (lockList[lid].waitList.size() == 0) {
    if (lockList[lid].toRevoke) {
      // If there is no other thread waiting for this lock, and this lock
      // has been required to be revoked by the server, release it back to
      // the server.
      updateLockState(lid, pthread_self(), RELEASING);
      lockList[lid].toRevoke = false;
      pthread_mutex_unlock(&mutexLockList);
      // Flush cache before releasing lock.
      // Write back dirty cache if there is any to server.
      lu->dorelease(lid);
      int r;
      lock_protocol::status ret;
      // Release lock.
      ret = cl->call(lock_protocol::release, lid, id, r);
      if (ret == lock_protocol::OK) {
        // Successfully released the lock back to the server, so update
        // the lock state to NONE.
        pthread_mutex_lock(&mutexLockList);
        updateLockState(lid, pthread_self(), NONE);
        if (lockList[lid].waitingToAcq)
          pthread_cond_signal(&lockList[lid].acquireCondVar);
        pthread_mutex_unlock(&mutexLockList);
      }
    } else {
      pthread_mutex_unlock(&mutexLockList);
    }
  } else {
    // If there are other threads waiting for this lock, wake them up.
    pthread_cond_broadcast(&lockList[lid].lockCondVar);
    pthread_mutex_unlock(&mutexLockList);
  }
  //cout << "client " << id << " thread " << pthread_self() << endl;
  return lock_protocol::OK;
//>>>>>>> lab5
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;
//<<<<<<< HEAD
//=======
  pthread_mutex_lock(&mutexLockList);
  lockList[lid].toRevoke = true;
  if (lockList[lid].state == FREE && lockList[lid].waitList.size() == 0) {
    // If no thead is holding or waiting for this lock right now, release it
    // back to the server.
    updateLockState(lid, pthread_self(), RELEASING);
    lockList[lid].toRevoke = false;
    pthread_mutex_unlock(&mutexLockList);
    // Flush cache before releasing lock.
    // Write back dirty cache if there is any to server.
    lu->dorelease(lid);
    int r;
    lock_protocol::status releaseRet;
    // Release lock.
    releaseRet = cl->call(lock_protocol::release, lid, id, r);
    if (releaseRet == lock_protocol::OK) {
       // Successfully released the lock back to the server, so update the
       // the lock state to NONE.
      pthread_mutex_lock(&mutexLockList);
      updateLockState(lid, pthread_self(), NONE);
      if (lockList[lid].waitingToAcq)
        pthread_cond_signal(&lockList[lid].acquireCondVar);
      pthread_mutex_unlock(&mutexLockList);
    }
  } else {
    pthread_mutex_unlock(&mutexLockList);
  }
//>>>>>>> lab5
  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
/*<<<<<<< HEAD
  return ret;
}


=======*/
  pthread_mutex_lock(&mutexLockList);
  lockList[lid].toRetry = true;
  if (lockList[lid].waitingToRetry) {
    pthread_cond_signal(&lockList[lid].retryCondVar);
  }
  pthread_mutex_unlock(&mutexLockList);
  return ret;
}

void lock_client_cache::createLock(
    lock_protocol::lockid_t lid, pthread_t threadId) {
  lockList[lid].lid = lid;
  lockList[lid].threadId = threadId;
  VERIFY(pthread_cond_init(&(lockList[lid].retryCondVar), NULL) == 0);
  VERIFY(pthread_cond_init(&(lockList[lid].acquireCondVar), NULL) == 0);
  VERIFY(pthread_cond_init(&(lockList[lid].lockCondVar), NULL) == 0);
  lockList[lid].waitingToAcq = false;
  lockList[lid].waitingToRetry = false;
  lockList[lid].toRetry = false;
  lockList[lid].toRevoke = false;
  lockList[lid].state = ACQUIRING;
}

void lock_client_cache::updateLockState(
    lock_protocol::lockid_t lid, pthread_t threadId, lockState state) {
  lockList[lid].threadId = threadId;
  lockList[lid].state = state;
}

void lock_client_cache::acquireAndMaybeRetry(lock_protocol::lockid_t lid) {
  int r;
  lock_protocol::status ret = cl->call(lock_protocol::acquire, lid, id, r);
  if (ret == lock_protocol::RETRY) {
    pthread_mutex_lock(&mutexLockList);
    if (lockList[lid].toRetry) {
      // If the lock is ready to be re-acquired, re-acquire it.
      // Unlock mutex before making RPC call.
      lockList[lid].toRetry = false;
      pthread_mutex_unlock(&mutexLockList);
      int r;
      ret = cl->call(lock_protocol::acquire, lid, id, r);
      if (ret != lock_protocol::OK) {
        cout << "Failure on re-acquiring lock lid:" << lid << " ret:"
            << ret << endl;
      } else {
        pthread_mutex_lock(&mutexLockList);
        updateLockState(lid, pthread_self(), LOCKED);
        pthread_mutex_unlock(&mutexLockList);
      }
    } else {
      while (!lockList[lid].toRetry) {
        lockList[lid].waitingToRetry = true;
        pthread_cond_wait(&lockList[lid].retryCondVar, &mutexLockList);
      }
      lockList[lid].toRetry = false;
      lockList[lid].waitingToRetry = false;
      pthread_mutex_unlock(&mutexLockList);
      int r;
      ret = cl->call(lock_protocol::acquire, lid, id, r);
      if (ret != lock_protocol::OK) {
        cout << "Failure on re-acquiring lock lid:" << lid << " ret:"
            << ret << endl;
      } else {
      
        pthread_mutex_lock(&mutexLockList);
        updateLockState(lid, pthread_self(), LOCKED);
        pthread_mutex_unlock(&mutexLockList);
      }
    }
  } else if (ret == lock_protocol::OK) {
    pthread_mutex_lock(&mutexLockList);
    updateLockState(lid, pthread_self(), LOCKED);
    pthread_mutex_unlock(&mutexLockList);
  } else {
    cout << "Failure on re-acquiring lock lid:" << lid << " ret:"
        << ret << endl;
  }
}

lock_release_user_impl::lock_release_user_impl(extent_client* _ec)
  : ec(_ec) {}

lock_release_user_impl::~lock_release_user_impl() {}

void lock_release_user_impl::dorelease(lock_protocol::lockid_t lid) {
  ec->flush(lid);
}
//>>>>>>> lab5

