#include <sstream>
#include <iostream>
#include <stdio.h>
#include "config.h"
#include "paxos.h"
#include "handle.h"
#include "tprintf.h"
#include "lang/verify.h"

// The config module maintains views. As a node joins or leaves a
// view, the next view will be the same as previous view, except with
// the new node added or removed. The first view contains only node
// 1. If node 2 joins after the first node (it will download the views
// from node 1), it will learn about view 1 with the first node as the
// only member.  It will then invoke Paxos to create the next view.
// It will tell Paxos to ask the nodes in view 1 to agree on the value
// {1, 2}.  If Paxos returns success, then it moves to view 2 with
// {1,2} as the members. When node 3 joins, the config module runs
// Paxos with the nodes in view 2 and the proposed value to be
// {1,2,3}. And so on.  When a node discovers that some node of the
// current view is not responding, it kicks off Paxos to propose a new
// value (the current view minus the node that isn't responding). The
// config module uses Paxos to create a total order of views, and it
// is ensured that the majority of the previous view agrees to the
// next view.  The Paxos log contains all the values (i.e., views)
// agreed on.
//
// The RSM module informs config to add nodes. The config module
// runs a heartbeater thread that checks in with nodes.  If a node
// doesn't respond, the config module will invoke Paxos's proposer to
// remove the node.  Higher layers will learn about this change when a
// Paxos acceptor accepts the new proposed value through
// paxos_commit().
//
// To be able to bring other nodes up to date to the latest formed
// view, each node will have a complete history of all view numbers
// and their values that it knows about. At any time a node can reboot
// and when it re-joins, it may be many views behind; by remembering
// all views, the other nodes can bring this re-joined node up to
// date.

/*
bool
operator> (const _t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m > b.m));
}

bool
operator>= (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m >= b.m));
}
*/
static void *
heartbeatthread(void *x)
{
  config *r = (config *) x;
  r->heartbeater();
  return 0;
}

config::config(std::string _first, std::string _me, config_view_change *_vc) 
  : myvid (0), first (_first), me (_me), vc (_vc)
{
  VERIFY (pthread_mutex_init(&cfg_mutex, NULL) == 0);
  VERIFY(pthread_cond_init(&config_cond, NULL) == 0);  

  std::ostringstream ost;
  ost << me;

  acc = new acceptor(this, me == _first, me, ost.str());
  pro = new proposer(this, acc, me);

  // XXX hack; maybe should have its own port number
  pxsrpc = acc->get_rpcs();
  pxsrpc->reg(paxos_protocol::heartbeat, this, &config::heartbeat);
  
  // Leader election
  registry[me].en.serialNum = 0;
  registry[me].en.id = me;
  registry[me].freshness = 0;
  epochViews[me].state = registry[me];
  epochViews[me].expired = true;
  isLeader = false;
  delta = theta = 3;
  refreshNum = seqNum = readNum = 0;
  statusMsgCount = ackMsgCount = epochCount = 0;
  lastReadStartTime = lastCompletedReadStartTime = epochStartTime = 0;
  VERIFY(pthread_cond_init(&roundTrip_cond, NULL) == 0);
  VERIFY(pthread_cond_init(&getEpoch_cond, NULL) == 0);

  {
      ScopedLock ml(&cfg_mutex);

      reconstruct();

      pthread_t th;
      VERIFY (pthread_create(&th, NULL, &heartbeatthread, (void *) this) == 0);
  }
}

void
config::restore(std::string s)
{
  ScopedLock ml(&cfg_mutex);
  acc->restore(s);
  reconstruct();
}

std::vector<std::string>
config::get_view(unsigned instance)
{
  ScopedLock ml(&cfg_mutex);
  return get_view_wo(instance);
}

// caller should hold cfg_mutex
std::vector<std::string>
config::get_view_wo(unsigned instance)
{
  std::string value = acc->value(instance);
  tprintf("get_view(%d): returns %s\n", instance, value.c_str());
  return members(value);
}

std::vector<std::string>
config::members(std::string value)
{
  std::istringstream ist(value);
  std::string m;
  std::vector<std::string> view;
  while (ist >> m) {
    view.push_back(m);
  }
  return view;
}

std::string
config::value(std::vector<std::string> m)
{
  std::ostringstream ost;
  for (unsigned i = 0; i < m.size(); i++)  {
    ost << m[i];
    ost << " ";
  }
  return ost.str();
}

// caller should hold cfg_mutex
void
config::reconstruct()
{
  if (acc->instance() > 0) {
    std::string m;
    myvid = acc->instance();
    mems = get_view_wo(myvid);
    tprintf("config::reconstruct: %d %s\n", myvid, print_members(mems).c_str());
  }
}

// Called by Paxos's acceptor.
void
config::paxos_commit(unsigned instance, std::string value)
{
  std::string m;
  std::vector<std::string> newmem;
  ScopedLock ml(&cfg_mutex);

  newmem = members(value);
  tprintf("config::paxos_commit: %d: %s\n", instance, 
	 print_members(newmem).c_str());

  for (unsigned i = 0; i < mems.size(); i++) {
    tprintf("config::paxos_commit: is %s still a member?\n", mems[i].c_str());
    if (!isamember(mems[i], newmem) && me != mems[i]) {
      tprintf("config::paxos_commit: delete %s\n", mems[i].c_str());
      mgr.delete_handle(mems[i]);
    }
  }

  mems = newmem;
  myvid = instance;
  if (vc) {
    unsigned vid = myvid;
    VERIFY(pthread_mutex_unlock(&cfg_mutex)==0);
    vc->commit_change(vid);
    VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
  }
}

bool
config::ismember(std::string m, unsigned vid)
{
  bool r;
  ScopedLock ml(&cfg_mutex);
  std::vector<std::string> v = get_view_wo(vid);
  r = isamember(m, v);
  return r;
}

bool
config::add(std::string new_m, unsigned vid)
{
  std::vector<std::string> m;
  std::vector<std::string> curm;
  ScopedLock ml(&cfg_mutex);
  if (vid != myvid)
    return false;
  tprintf("config::add %s\n", new_m.c_str());
  m = mems;
  m.push_back(new_m);
  curm = mems;
  std::string v = value(m);
  int nextvid = myvid + 1;
  VERIFY(pthread_mutex_unlock(&cfg_mutex)==0);
  bool r = pro->run(nextvid, curm, v);
  VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
  if (r) {
    tprintf("config::add: proposer returned success\n");
  } else {
    tprintf("config::add: proposer returned failure\n");
  }
  return r;
}

// caller should hold cfg_mutex
bool
config::remove_wo(std::string m)
{
  tprintf("config::remove: myvid %d remove? %s\n", myvid, m.c_str());
  std::vector<std::string> n;
  for (unsigned i = 0; i < mems.size(); i++) {
    if (mems[i] != m) n.push_back(mems[i]);
  }
  std::string v = value(n);
  std::vector<std::string> cmems = mems;
  int nextvid = myvid + 1;
  VERIFY(pthread_mutex_unlock(&cfg_mutex)==0);
  bool r = pro->run(nextvid, cmems, v);
  VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
  if (r) {
    tprintf("config::remove: proposer returned success\n");
  } else {
    tprintf("config::remove: proposer returned failure\n");
  }
  return r;
}

void
config::heartbeater()
{
  timeval now;
  timespec next_timeout;
  std::string m;
  heartbeat_t h;
  bool stable;
  unsigned vid;
  std::vector<std::string> cmems;
  ScopedLock ml(&cfg_mutex);
  
  while (1) {

    gettimeofday(&now, NULL);
    next_timeout.tv_sec = now.tv_sec + 3;
    next_timeout.tv_nsec = 0;
    tprintf("heartbeater: go to sleep\n");
    pthread_cond_timedwait(&config_cond, &cfg_mutex, &next_timeout);

    stable = true;
    vid = myvid;
    cmems = get_view_wo(vid);
    tprintf("heartbeater: current membership %s\n", print_members(cmems).c_str());

    if (!isamember(me, cmems)) {
      tprintf("heartbeater: not member yet; skip hearbeat\n");
      continue;
    }

    //find the node with the smallest id
    m = me;
    for (unsigned i = 0; i < cmems.size(); i++) {
      if (m > cmems[i])
	      m = cmems[i];
    }

    if (m == me) {
      //if i am the one with smallest id, ping the rest of the nodes
      for (unsigned i = 0; i < cmems.size(); i++) {
        if (cmems[i] != me) {
          if ((h = doheartbeat(cmems[i])) != OK) {
            stable = false;
            m = cmems[i];
            break;
          }
        }
      }
    } else {
      //the rest of the nodes ping the one with smallest id
      if ((h = doheartbeat(m)) != OK) 
        stable = false;
    }

    if (!stable && vid == myvid) {
      remove_wo(m);
    }
  }
}

paxos_protocol::status
config::heartbeat(std::string m, unsigned vid, int &r)
{
  ScopedLock ml(&cfg_mutex);
  int ret = paxos_protocol::ERR;
  r = (int) myvid;
  tprintf("heartbeat from %s(%d) myvid %d\n", m.c_str(), vid, myvid);
  if (vid == myvid) {
    ret = paxos_protocol::OK;
  } else if (pro->isrunning()) {
    VERIFY (vid == myvid + 1 || vid + 1 == myvid);
    ret = paxos_protocol::OK;
  } else {
    ret = paxos_protocol::ERR;
  }
  return ret;
}

config::heartbeat_t
config::doheartbeat(std::string m)
{
  int ret = rpc_const::timeout_failure;
  int r;
  unsigned vid = myvid;
  heartbeat_t res = OK;

  tprintf("doheartbeater to %s (%d)\n", m.c_str(), vid);
  handle h(m);
  VERIFY(pthread_mutex_unlock(&cfg_mutex)==0);
  rpcc *cl = h.safebind();
  if (cl) {
    ret = cl->call(paxos_protocol::heartbeat, me, vid, r, 
	      rpcc::to(1000));
  } 
  VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
  if (ret != paxos_protocol::OK) {
    if (ret == rpc_const::atmostonce_failure || 
        ret == rpc_const::oldsrv_failure) {
      mgr.delete_handle(m);
    } else {
      tprintf("doheartbeat: problem with %s (%d) my vid %d his vid %d\n", 
	     m.c_str(), ret, vid, r);
      if (ret < 0) res = FAILURE;
      else res = VIEWERR;
    }
  }
  tprintf("doheartbeat done %d\n", res);
  return res;
}

// Leader election

static void* sendRefreshThread (void* x) {
  threadStruct* r = (threadStruct*) x;
  r->cfg->sendRefresh(r->target);
  pthread_exit(NULL);
}

void config::refresh() {
  timeval now;
  timespec next_timeout;
  while(1) {
    // Upon refreshTimer timeout.
    VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
    ackMsgCount = 0;
    ++refreshNum;
    int rc;
    pthread_t threads[mems.size()];
    for (unsigned i = 0; i < mems.size(); ++i) {
      threadStruct ts;
      ts.cfg = this;
      ts.target = mems[i];
      rc = pthread_create(&threads[i], NULL, sendRefreshThread, (void*) &ts);
      if (rc) {
        printf("ERROR; return code from pthread_create() is %d\n", rc);
      }
    } 
    gettimeofday(&now, NULL);
    next_timeout.tv_sec = now.tv_sec + theta;
    next_timeout.tv_nsec = 0;
    tprintf("roundTripTimer turned on\n");
    int roundTripTimerRes =
        pthread_cond_timedwait(&roundTrip_cond, &cfg_mutex, &next_timeout);
    if (roundTripTimerRes == ETIMEDOUT) {
      roundTripTimeOut();
    }
    
    
  }
}

void config::sendRefresh(std::string target) {
  VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
  handle h(target);
  VERIFY(pthread_mutex_unlock(&cfg_mutex)==0);
  rpcc* cl = h.safebind();
  if (cl) {
    //cl->call(paxos_protocol::refreshReq, me, registry[me], refreshNum);
  }
}

paxos_protocol::status config::refreshReq(std::string src, epochState rg, unsigned rn) {
  pthread_mutex_lock(&cfg_mutex);
  if (registry[src].en.serialNum < rg.en.serialNum ||
      (registry[src].en.serialNum == rg.en.serialNum && registry[src].en.id < rg.en.id) ||
      (registry[src].en.serialNum == rg.en.serialNum && registry[src].en.id == rg.en.id && registry[src].freshness < rg.freshness)) {
    registry[src] = rg;
    handle h(src);
    pthread_mutex_unlock(&cfg_mutex);
    rpcc* cl = h.safebind();
    if (cl) {
      //cl->call(paxos_protocol::ackReq, me, rn);
    }
  }
  return paxos_protocol::OK;
} 

paxos_protocol::status config::ackReq(std::string src, unsigned rn) {
  pthread_mutex_lock(&cfg_mutex);
  if (++ackMsgCount >= ((mems.size() >> 1) + 1)) {
    // stop roundTripTimer
    ++(registry[me].freshness);
  }
  pthread_mutex_unlock(&cfg_mutex);
  return paxos_protocol::OK;
}

static void* sendGetEpochNumThread(void* x) {
  threadStruct* r = (threadStruct*) x;
  r->cfg->sendGetEpochNum(r->target);
  pthread_exit(NULL);
}

// Assume cfg_mutex is held when calling this function.
void config::roundTripTimeOut() {
  // stop refreshtimer.
  ++refreshNum;
  isLeader = false;
  epochViews[me].expired = true;
  epochCount = 0;
  globalMaxEn = registry[me].en;
  ++seqNum;
  getEpochWithTimer();
}

void config::sendGetEpochNum(std::string target) {
  VERIFY(pthread_mutex_lock(&cfg_mutex)==0);
  handle h(target);
  VERIFY(pthread_mutex_unlock(&cfg_mutex)==0);
  rpcc* cl = h.safebind();
  if (cl) {
    //cl->call(paxos_protocol::getEpochNumReq, me, registry[me], refreshNum);
  }
}

// Assume cfg_mutex is held when calling this function.
void config::getEpochWithTimer() {
  timeval now;
  timespec next_timeout;
  int rc;
  pthread_t threads[mems.size()];
  while (1) {
    for (unsigned i = 0; i < mems.size(); ++i) {
      threadStruct ts;
      ts.cfg = this;
      ts.target = mems[i];
      rc = pthread_create(&threads[i], NULL, sendGetEpochNumThread, (void*) &ts);
      if (rc) {
        tprintf("ERROR; return code from pthread_create() is %d\n", rc);
      }
    } 
    gettimeofday(&now, NULL);
    next_timeout.tv_sec = now.tv_sec + theta;
    next_timeout.tv_nsec = 0;
    tprintf("roundTripTimer turned on\n");
    int getEpochTimerRes =
        pthread_cond_timedwait(&roundTrip_cond, &cfg_mutex, &next_timeout);
    if (getEpochTimerRes == ETIMEDOUT) {
      // getEpochTimer timeout
      ++seqNum;
      epochCount = 0;
      globalMaxEn = registry[me].en;
      continue;
    } else if (getEpochTimerRes == 0) {
      break;
    }
  }
}

paxos_protocol::status config::getEpochNumReq(std::string src, unsigned sn) {
  pthread_mutex_lock(&cfg_mutex);
  std::map<std::string, epochState>::iterator it = registry.begin();
  localMaxEn = (*it).second.en;
  for (++it; it != registry.end(); ++it) {
    if ((*it).second.en.serialNum > localMaxEn.serialNum ||
        ((*it).second.en.serialNum == localMaxEn.serialNum && (*it).second.en.id > localMaxEn.id)) {
      localMaxEn = (*it).second.en;
  }
  handle h(src);
  pthread_mutex_unlock(&cfg_mutex);
  rpcc *cl = h.safebind();
    if (cl) {
    //cl->call(paxos_protocol::retEpochNumReq, me, sn, localMaxEn); 
    }
  }
  return paxos_protocol::OK;
}

paxos_protocol::status config::retEpochNumReq(std::string src, unsigned sn, epochNum en) {
  pthread_mutex_lock(&cfg_mutex);
  if (sn == seqNum) {
    if (en.serialNum > globalMaxEn.serialNum ||
        (en.serialNum == globalMaxEn.serialNum && en.id > globalMaxEn.id)) {
      globalMaxEn = en;
    }
    if (++epochCount >= ((mems.size() >> 1) + 1)) {
      registry[me].en.serialNum = globalMaxEn.serialNum + 1;
      epochStartTime = time(NULL);
      // start refreshTimer
    }
  }
  pthread_mutex_unlock(&cfg_mutex);
  return paxos_protocol::OK;
}

static void* sendCollectThread(void* x) {
  threadStruct* r = (threadStruct*) x;
  r->cfg->sendCollect(r->target);
  pthread_exit(NULL);
}

void config::readTimeOut() {
  pthread_mutex_lock(&cfg_mutex);
  lastReadStartTime = time(NULL);
  ++readNum;
  statusMsgCount = 0;
  oldEpochViews = epochViews;
  int rc;
  pthread_t threads[mems.size()];
  for (unsigned i = 0; i < mems.size(); ++i) {
    threadStruct ts;
    ts.cfg = this;
    ts.target = mems[i];
    rc = pthread_create(&threads[i], NULL, sendCollectThread, (void*) &ts);
    if (rc) {
      tprintf("ERROR; return code from pthread_create() is %d\n", rc);
    }
  } 
  pthread_mutex_unlock(&cfg_mutex);
}

void config::sendCollect(std::string target) {
  pthread_mutex_lock(&cfg_mutex);
  handle h(target);
  pthread_mutex_unlock(&cfg_mutex);
  rpcc* cl = h.safebind();
  if (cl) {
    //cl->(paxos_protocol::collectReq, me, readNum);
  }
}

paxos_protocol::status config::collectReq(std::string src, unsigned rn) {
  pthread_mutex_lock(&cfg_mutex);
  handle h(src);
  pthread_mutex_unlock(&cfg_mutex);
  rpcc* cl = h.safebind();
  if (cl) {
    //cl->call(paxos_protocol::statusReq, me, rn, registry);
  } 
  return paxos_protocol::OK;
}

paxos_protocol::status config::statusReq(std::string src, unsigned rn, std::map<std::string, epochState> srcReg) {
  pthread_mutex_lock(&cfg_mutex);
  std::map<std::string, epochView>::iterator it;
  for (it = epochViews.begin(); it != epochViews.end(); ++it) {
    (*it).second.state = maxState((*it).second.state, srcReg[(*it).first]);
  }
  if (++statusMsgCount >= ((mems.size() >> 1) + 1)) {
    lastCompletedReadStartTime = lastReadStartTime;
    for (it = epochViews.begin(); it != epochViews.end(); ++it) {
      if (stateLessOrEqual((*it).second.state, oldEpochViews[(*it).first].state)) {
        (*it).second.expired = true;
      }
      if (epochLess(oldEpochViews[(*it).first].state.en, (*it).second.state.en)) {
        (*it).second.expired = false;
      }
    }
    
    bool inited = false;
    epochNum minEpoch; 
    for (it = epochViews.begin(); it != epochViews.end(); ++it) {
      if ((!(*it).second.expired && !inited) ||
          (!(*it).second.expired && epochLess((*it).second.state.en, minEpoch))) {
        minEpoch = (*it).second.state.en;
      }
  }
    if (inited) {
      leaderEpoch = minEpoch;
    } else {
      leaderEpoch.serialNum = 0;
      (leaderEpoch.id) = "";
    }
    leader = leaderEpoch.id;
    // start readTimer with delta+theta time units;
  }
  pthread_mutex_unlock(&cfg_mutex);
  return paxos_protocol::OK;
}

config::epochState config::maxState(epochState &st1, epochState &st2) {
  if (stateLess(st1, st2)) {
    return st2;
  } else {
    return st1;
  }
}

bool config::epochLess(config::epochNum &en1, config::epochNum &en2) {
  if (en1.serialNum < en2.serialNum ||
      (en1.serialNum == en2.serialNum && en1.id < en2.id)) {
    return true;
  } else {
    return false;
  }
}

bool config::epochLessOrEqual(config::epochNum &en1, config::epochNum &en2) {
  if (en1.serialNum <= en2.serialNum ||
      (en1.serialNum == en2.serialNum && en1.id <= en2.id)) {
    return true;
  } else {
    return false;
  }
}
bool config::stateLess(epochState &st1, epochState &st2) {
  if (st1.en.serialNum < st2.en.serialNum ||
      (st1.en.serialNum == st2.en.serialNum && st1.en.id < st2.en.id) ||
      (st1.en.serialNum == st2.en.serialNum && st1.en.id == st2.en.id && st1.freshness < st2.freshness)) {
    return true;
  } else {
    return false;
  }
}

bool config::stateLessOrEqual(epochState &st1, epochState &st2) {
  if (st1.en.serialNum <= st2.en.serialNum ||
      (st1.en.serialNum == st2.en.serialNum && st1.en.id <= st2.en.id) ||
      (st1.en.serialNum == st2.en.serialNum && st1.en.id == st2.en.id && st1.freshness <= st2.freshness)) {
    return true;
  } else {
    return false;
  }
}

void config::becomeLeader() {
  // Upon change to lastCompletedReadStartTime.
  pthread_mutex_lock(&cfg_mutex);
  if ((leaderEpoch.serialNum == registry[me].en.serialNum &&
      leaderEpoch.id == registry[me].en.id) &&
      (lastCompletedReadStartTime - epochStartTime) >= 2 * delta + 3 * theta) {
    isLeader = true;
  }
  pthread_mutex_unlock(&cfg_mutex);
}

