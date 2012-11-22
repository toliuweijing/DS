#ifndef config_h
#define config_h

#include <string>
#include <vector>
#include "paxos.h"

class config_view_change {
 public:
  virtual void commit_change(unsigned vid) = 0;
  virtual ~config_view_change() {};
};

class config : public paxos_change {
 private:
  acceptor *acc;
  proposer *pro;
  rpcs *pxsrpc;
  unsigned myvid;
  std::string first;
  std::string me;
  config_view_change *vc;
  std::vector<std::string> mems;
  pthread_mutex_t cfg_mutex;
  pthread_cond_t heartbeat_cond;
  pthread_cond_t config_cond;
  paxos_protocol::status heartbeat(std::string m, unsigned instance, int &r);
  std::string value(std::vector<std::string> mems);
  std::vector<std::string> members(std::string v);
  std::vector<std::string> get_view_wo(unsigned instance);
  bool remove_wo(std::string);
  void reconstruct();
  typedef enum {
    OK,	// response and same view #
    VIEWERR,	// response but different view #
    FAILURE,	// no response
  } heartbeat_t;
  heartbeat_t doheartbeat(std::string m);

  // Leader election
  unsigned refreshNum;
  unsigned seqNum;
  unsigned readNum;
  unsigned statusMsgCount;
  unsigned ackMsgCount;
  unsigned epochCount;
  unsigned int lastReadStartTime; 
  unsigned int lastCompletedReadStartTime;
  unsigned int epochStartTime;
  unsigned int delta;
  unsigned int theta;
  bool isLeader;
  struct epochNum {
    unsigned serialNum;
    std::string pid;
  };
  epochNum globalMaxEn;
  epochNum localMaxEn;
  epochNum leaderEpoch;
  struct epochState {
    epochNum en;
    unsigned freshness;
  };
  struct epochView {
    epochState state;
    bool expired;
  };
  std::map<std::string, epochState> registry;
  std::map<std::string, epochView> epochViews;
  std::map<std::string, epochView> oldEpochViews;
  pthread_cond_t roundTrip_cond;
  pthread_cond_t getEpoch_cond;

 public:
  config(std::string _first, std::string _me, config_view_change *_vc);
  unsigned vid() { return myvid; }
  std::string myaddr() { return me; };
  std::string dump() { return acc->dump(); };
  std::vector<std::string> get_view(unsigned instance);
  void restore(std::string s);
  bool add(std::string, unsigned vid);
  bool ismember(std::string m, unsigned vid);
  void heartbeater(void);
  void paxos_commit(unsigned instance, std::string v);
  rpcs *get_rpcs() { return acc->get_rpcs(); }
  void breakpoint(int b) { pro->breakpoint(b); }

  /* Leader election */
  // Refresh
  void refresh();
  paxos_protocol::status refreshReq(std::string src, epochState rg, unsigned rn);
  paxos_protocol::status ackReq(std::string src, unsigned rn);
  // Advance epoch
  void roundTripTimeOut();
  void getEpochTimeOut();
  paxos_protocol::status getEpochNumReq(std::string src, unsigned sn);
  paxos_protocol::status retEpochNumReq(std::string src, unsigned sn);
  // Collect
  void readTimeOut();
  paxos_protocol::status collectReq(std::string src, unsigned rn);
  paxos_protocol::status statusReq(std::string src, unsigned rn,
      std::map<std::string, epochState> srcReg);
  // Become leader
  void becomeLeader();
};

#endif
