// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <split.h>

using namespace std;

extent_server::extent_server() {
  pthread_mutex_init(&mutexFile, NULL);
  files[0x00000001] = "";
  extent_protocol::attr fileAttr;
  fileAttr.atime = fileAttr.mtime = fileAttr.ctime = time(NULL);
  fileAttr.size = 0;
  fileAttrs[0x00000001] = fileAttr;
}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  // You fill this in for Lab 2.
  pthread_mutex_lock(&mutexFile);
  if (files.find(id) == files.end()) {
    fileAttrs[id].atime = fileAttrs[id].ctime = fileAttrs[id].mtime = time(NULL);
  } else {
    fileAttrs[id].ctime = fileAttrs[id].mtime = time(NULL);
  }
  files[id] = buf;
  fileAttrs[id].size = (unsigned)buf.length();
  pthread_mutex_unlock(&mutexFile);
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  // You fill this in for Lab 2.
  pthread_mutex_lock(&mutexFile);
  if (files.find(id) != files.end()) {
    buf = files[id];
    fileAttrs[id].atime = time(NULL);
    pthread_mutex_unlock(&mutexFile);
    return extent_protocol::OK;
  } else {
    cout << "GET ERROR: file does not exist for inum " << id << endl;
    pthread_mutex_unlock(&mutexFile);
    return extent_protocol::NOENT;
  }
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You fill this in for Lab 2.
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
  pthread_mutex_lock(&mutexFile);
  if (fileAttrs.find(id) != fileAttrs.end()) {
    a.size = fileAttrs[id].size;
    a.atime = fileAttrs[id].atime;
    a.mtime = fileAttrs[id].mtime;
    a.ctime = fileAttrs[id].ctime;
    pthread_mutex_unlock(&mutexFile);
    return extent_protocol::OK;
  } else {
    //cout << "GETATTR ERROR: file attribute doesn't exisit for inum " << id << endl;
    pthread_mutex_unlock(&mutexFile);
    return extent_protocol::IOERR;
  }
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  // You fill this in for Lab 2.
  pthread_mutex_lock(&mutexFile);
  map<extent_protocol::extentid_t, string>::iterator fileIter = files.find(id);
  if (fileIter != files.end()) {
    files.erase(fileIter);
    map<extent_protocol::extentid_t, extent_protocol::attr>::iterator
        fileAttrIter = fileAttrs.find(id);
    if (fileAttrIter != fileAttrs.end()) {
      fileAttrs.erase(fileAttrIter);
      pthread_mutex_unlock(&mutexFile);
      return extent_protocol::OK;
    } else {
      cout << "REMOVE ERROR: attribute of inum " << id << " to be removed doesn't exist" << endl;
      pthread_mutex_unlock(&mutexFile);
      return extent_protocol::NOENT;
    }
  } else {
    cout << "REMOVE ERROR: file of inum " << id << " to be removed doesn't exist" << endl;
    pthread_mutex_unlock(&mutexFile);
    return extent_protocol::NOENT;
  }
}

