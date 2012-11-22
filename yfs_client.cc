// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "split.h"
#include "lock_client_cache.h"

using namespace std;

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lu = new lock_release_user_impl(ec);
  lc = new lock_client_cache(lock_dst, lu);
}

yfs_client::~yfs_client() {
  delete lc;
  delete lu;
  delete ec;
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock
  lc->acquire(inum);

  //printf("getfile %016llx\n", inum);
  cout << "getfile eid:" << inum << endl;
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    cout << "YFSCLIENT ERROR: getfile failed on inum " << inum << endl;
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  cout << "getfile eid:" << inum << " size:" << fin.size << endl;
  //printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:
  lc->release(inum);
  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock
  lc->acquire(inum);

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    cout << "YFSCLIENT ERROR: getdir failed on inum " << inum << endl;
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  lc->release(inum);
  return r;
}

// Function to look up a file or directory in a given directory.
yfs_client::status yfs_client::lookup(const char* name, inum parent,
    inum& child, bool& found, bool singleOp) {
  lc->acquire(parent);
  yfs_client::status r = OK;
  found = false;
  string parentDirCont;
  // Make RPC call to get the content of the parent directory first.
  if (ec->get(parent, parentDirCont) != extent_protocol::OK) {
    // If the parent directory doesn't exist, ec->get returns
    // extent_protocol::NOENT. So this function will return yfs_client::NOENT
    // correspondingly. 
    std::cout << "YFSCLIENT ERROR: lookup called on dir inum " << parent
      << " for file name " << name << " failed because directory doesn't exist"
      << std::endl;
    r = NOENT;
    lc->release(parent);
  } else {
    // RPC call to get returns extent_protocol::OK, which means that the
    // content of the parent directory has been successfully stored in
    // parentDirCont.
    vector<string> pairs = Split(parentDirCont, "|", true, true);
    if (pairs.size() > 0) {
      string nameToFind(name);
      vector<string>::iterator it;
      for (it = pairs.begin(); it != pairs.end(); ++it) {
        vector<string> aPair = Split((*it), ":", true, true);
        if (aPair.size() == 2 && (nameToFind.compare(aPair[0]) == 0)) {
          //cout << "file with name " << nameToFind << " already exist" << endl; 
          found = true;
          istringstream iss(aPair[1]);
          iss >> child;
          break;
        }
      }
    }
    if (singleOp) {
      lc->release(parent);
    }
  }
  return r;
}

yfs_client::status yfs_client::addDirent(const inum dirInum,
    inum fileInum, const char* name) {
  lc->acquire(fileInum);
  yfs_client::status ret = OK;
  string fileName(name);
  string oldExt;
  if (ec->get(dirInum, oldExt) == extent_protocol::OK) {
    ostringstream oss;
    oss << oldExt << fileName << ":" << fileInum << "|";
    string newExt = oss.str();
    if (ec->put(dirInum, newExt) == extent_protocol::OK) {
      if (ec->put(fileInum, "") != extent_protocol::OK)
        ret = RPCERR;
    } else {
      ret = RPCERR;
    }
  } else {
    ret = NOENT;
  }
  lc->release(fileInum);
  lc->release(dirInum);
  return ret;
}

yfs_client::status yfs_client::unlink(const inum dirInum,
    inum fileInum, const char* name) {
  lc->acquire(fileInum);
  yfs_client::status ret = OK;
  string fileName(name);
  string dirExtent;
  if (ec->get(dirInum, dirExtent) == extent_protocol::OK) {
    ostringstream oss;
    vector<string> pairs = Split(dirExtent, "|", true, true);
    if (pairs.size() > 0) {
      vector<string>::iterator it;
      // Delete the directory entry with the specified file/director name.
      for (it = pairs.begin(); it != pairs.end(); ++it) {
        vector<string> aPair = Split((*it), ":", true, true);
        if (aPair.size() == 2 && (fileName.compare(aPair[0]) != 0)) {
          //cout << "file with name " << nameToFind << " already exist" << endl; 
          oss << (*it) << "|";
        }
      }
    }
    // Update the director's extent after deleting the directory entry.
    if (ec->put(dirInum, oss.str()) == extent_protocol::OK) {
      // Delete the file.
      if (ec->remove(fileInum) != extent_protocol::OK)
        ret = IOERR;
    } else {
      ret = RPCERR;
    }
  } else {
    ret = NOENT;
  }
  lc->release(fileInum);
  lc->release(dirInum);
  return ret; 
}

yfs_client::status yfs_client::readDir(
    const inum dirInum, list<yfs_client::dirent>& dirents) {
  lc->acquire(dirInum);
  yfs_client::status ret = OK;
  string dirContent;
  if (ec->get(dirInum, dirContent) != extent_protocol::OK) {
    // If the parent directory doesn't exist, ec->get returns
    // extent_protocol::NOENT. So this function will return yfs_client::NOENT
    // correspondingly. 
    std::cout << "YFSCLIENT ERROR: readDir called on dir inum " << dirInum 
      << " failed because directory doesn't exist" << std::endl;
    ret = NOENT;
  } else {
    // RPC call to get returns extent_protocol::OK, which means that the
    // content of the parent directory has been successfully stored in
    // parentDirCont.
    vector<string> pairs = Split(dirContent, "|", true, true);
    if (pairs.size() > 0) {
      vector<string>::iterator it;
      for (it = pairs.begin(); it != pairs.end(); ++it) {
        vector<string> aPair = Split((*it), ":", true, true);
        if (aPair.size() == 2) {
          struct yfs_client::dirent aDirent;
          aDirent.name = aPair[0];
          istringstream iss(aPair[1]);
          iss >> aDirent.inum;
          dirents.push_back(aDirent);
        }
      }
    }
  }
  lc->release(dirInum);
  return ret;
}

yfs_client::status yfs_client::setSize(const inum ino,
    const unsigned int size) {
  lc->acquire(ino);
  yfs_client::status ret = OK;
  string fileExt;
  if (ec->get(ino, fileExt) == extent_protocol::OK) {
    if (size == 0) {
      if (ec->put(ino, "") != extent_protocol::OK)
        ret = RPCERR;
    } else if (size <= fileExt.length()) {
      if (ec->put(ino, fileExt.substr(0, size)) != extent_protocol::OK)
        ret = RPCERR;
    } else {
      unsigned int toAppend = size - fileExt.length();
      ostringstream oss;
      oss << fileExt;
      for (unsigned int i = 0; i < toAppend; ++i)
        oss << '\0';
      if (ec->put(ino, oss.str()) != extent_protocol::OK)
        ret = RPCERR;
    }
  } else {
    ret = NOENT;
  }
  lc->release(ino);
  return ret; 
}

yfs_client::status yfs_client::readFile(const inum ino,
    unsigned int off, unsigned int size, string& buf) {
  lc->acquire(ino);
  yfs_client::status ret = OK;
  string fileExt;
  if (ec->get(ino, fileExt) == extent_protocol::OK) {
    if (off >= fileExt.length())
      buf = "";
    else {
      if (size > fileExt.length() - off) {
        buf = fileExt.substr(off, fileExt.length() - off);
      } else {
        buf = fileExt.substr(off, size);
      }
    }
  } else {
    ret = NOENT;
  }
  lc->release(ino);
  return ret; 
}

yfs_client::status yfs_client::writeFile(const inum ino,
    const unsigned int off, const unsigned int size, const string buf) {
  lc->acquire(ino);
  yfs_client::status ret = OK;
  string fileExt;
  if (ec->get(ino, fileExt) == extent_protocol::OK) {
    ostringstream oss;
    if (off >= fileExt.length()) {
      unsigned int numOfNulls = off - fileExt.length();
      oss << fileExt;
      for (unsigned int i = 0; i < numOfNulls; ++i)
        oss << '\0';
      oss << buf;
    } else {
      oss << fileExt.substr(0, off);
      if ((off + size) < fileExt.length()) {
        oss << buf;
        oss << fileExt.substr((off + size), fileExt.length() - (off + size));
      } else {
        oss << buf;
      }
    }
    if (ec->put(ino, oss.str()) != extent_protocol::OK)
      ret = RPCERR;
  } else {
    ret = NOENT;
  }
  lc->release(ino);
  return ret; 
}

void yfs_client::releaseLock(const inum ino) {
  lc->release(ino);
}

