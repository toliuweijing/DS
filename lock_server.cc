// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
  pthread_mutex_init(&mutexLocks, NULL);
}

lock_server::~lock_server() {
  pthread_mutex_destroy(&mutexLocks);
  map<lock_protocol::lockid_t, lockStatus>::iterator it;
  for(it = locks.begin(); it != locks.end(); ++it) {
    pthread_cond_destroy(&((it->second).condVar));
  }
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status lock_server::acquire(
    int clt, lock_protocol::lockid_t lid, int &r) {
  // lock the locks map and check if the lock is free to aquire
  ostringstream convert;
  convert << clt;
  string clientId = convert.str();  
  pthread_mutex_lock(&mutexLocks);
  cout << "acquire called for clt:" << clt << " lid:" << lid << endl; 
  if (locks.find(lid) == locks.end()) {
    createLock(lid, clientId);
    cout << "lock lid " << lid << " created" << endl;
  } else {
    // check if the lock is free
    if (locks[lid].status == FREE) {
      locks[lid].status = LOCKED;
      locks[lid].clientId = clientId;
      cout << "granted lock for clt:" << clt << " lid:" << lid << endl; 
    } else {
      //if (locks[lid].clientId.compare(clientId) != 0) {
        while (locks[lid].status == LOCKED) {
          cout << "waiting for lock clt:" << clt << " lid:" << lid << endl;
          cout << "lid:" << lid << " is currently held by " << locks[lid].clientId << endl;
          ++locks[lid].waitingCount;
          pthread_cond_wait(&locks[lid].condVar, &mutexLocks);
          --locks[lid].waitingCount;
        }
        locks[lid].status = LOCKED;
        locks[lid].clientId = clientId;
        cout << "granted lock after waiting for clt:" << clt << " lid:" << lid << endl;
      //}
    } 
  }
  pthread_mutex_unlock(&mutexLocks);

  r = 0;
  return lock_protocol::OK;
}

lock_protocol::status lock_server::release(
    int clt, lock_protocol::lockid_t lid, int &r) {
  // lock the locks map and release the lock, then notify any client that is
  // waiting for this lock
  pthread_mutex_lock(&mutexLocks);
  changeLockState(lid, FREE, "");
  
  if (locks[lid].waitingCount > 0) {
    pthread_cond_broadcast(&locks[lid].condVar);
  }
  pthread_mutex_unlock(&mutexLocks);

  r = 0;
  return lock_protocol::OK;
}

void lock_server::changeLockState(lock_protocol::lockid_t lid,
    lStatus newStatus, string clientId) {
  locks[lid].status = newStatus;
  locks[lid].clientId = clientId;
}

void lock_server::createLock(lock_protocol::lockid_t lid, string clientId) {
  locks[lid].status = LOCKED;
  locks[lid].clientId = clientId;
  pthread_cond_init(&(locks[lid].condVar), NULL);
  locks[lid].waitingCount = 0;
}

