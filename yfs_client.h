#ifndef yfs_client_h
#define yfs_client_h

#include <string>
#include "extent_client.h"
#include <vector>
#include <list>

#include "lock_protocol.h"
#include "lock_client_cache.h"

using namespace std;

class yfs_client {
  extent_client *ec;
  lock_client *lc;
  lock_release_user_impl *lu;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);

 public:
  yfs_client(std::string, std::string);
  ~yfs_client();

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

  // @Junyang.
  status lookup(const char* name, inum parent, inum& child, bool& found,
      bool singleOp);
  status createFile(const char* name, inum child, inum parent);
  status addDirent(const inum dirInum, const inum fileInum,
      const char* name);
  status unlink(const inum dirInum, const inum fileInum,
      const char* name);
  status readDir(const inum dirInum, std::list<dirent>& dirents);
  status setSize(const inum ino, const unsigned int size);
  status readFile(const inum ino, unsigned int off,
      unsigned int size, string& buf);
  status writeFile(const inum ino, const unsigned int off,
      const unsigned int size, string buf);
  void releaseLock(const inum ino);
};

#endif 
