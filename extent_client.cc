// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  VERIFY(pthread_mutex_init(&mutexFile, NULL) == 0);
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  pthread_mutex_lock(&mutexFile);
  if (cacheFiles.find(eid) != cacheFiles.end()) {
    buf = cacheFiles[eid].fileExt;
    cacheAttrs[eid].atime = time(NULL);
  } else {
    pthread_mutex_unlock(&mutexFile);
    extent_protocol::attr attr;
    ret = cl->call(extent_protocol::get, eid, buf);
    ret = cl->call(extent_protocol::getattr, eid, attr);
    pthread_mutex_lock(&mutexFile);
    cacheFiles[eid].fileExt = buf;
    cacheFiles[eid].dirty = false;
    cacheAttrs[eid].atime = attr.atime;
    cacheAttrs[eid].ctime = attr.ctime;
    cacheAttrs[eid].mtime = attr.mtime;
    cacheAttrs[eid].size = attr.size;
  }
  pthread_mutex_unlock(&mutexFile);
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  pthread_mutex_lock(&mutexFile);
  if (cacheAttrs.find(eid) != cacheAttrs.end()) {
    attr.atime = cacheAttrs[eid].atime;
    attr.ctime = cacheAttrs[eid].ctime;
    attr.mtime = cacheAttrs[eid].mtime;
    attr.size = cacheAttrs[eid].size;
  } else {
    pthread_mutex_unlock(&mutexFile);
    ret = cl->call(extent_protocol::getattr, eid, attr);
    pthread_mutex_lock(&mutexFile);
    cacheAttrs[eid].atime = attr.atime;
    cacheAttrs[eid].ctime = attr.ctime;
    cacheAttrs[eid].mtime = attr.mtime;
    cacheAttrs[eid].size = attr.size;
  }
  pthread_mutex_unlock(&mutexFile);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  pthread_mutex_lock(&mutexFile);
  cacheFiles[eid].fileExt = buf;
  cacheFiles[eid].dirty = true;
  // Update file attributes. If the attributes are not cached,
  // fetch it from the server.
  if (cacheAttrs.find(eid) != cacheAttrs.end()) {
    cacheAttrs[eid].ctime = cacheAttrs[eid].mtime = time(NULL);
    cacheAttrs[eid].size = buf.length();
  } else {
    pthread_mutex_unlock(&mutexFile);
    extent_protocol::attr attr;
    extent_protocol::status getAttrRet;
    getAttrRet = cl->call(extent_protocol::getattr, eid, attr);
    pthread_mutex_lock(&mutexFile);
    if (getAttrRet == extent_protocol::OK) {
      cacheAttrs[eid].atime = attr.atime;
      cacheAttrs[eid].ctime = cacheAttrs[eid].mtime = time(NULL);
      cacheAttrs[eid].size = buf.length();
    } else {
      // If the control flow reaches here, it means the file doesn't exist in
      // the server either, so it's a newly created file.
      cacheAttrs[eid].atime = cacheAttrs[eid].ctime = cacheAttrs[eid].mtime =
          time(NULL);
      cacheAttrs[eid].size = buf.length();
    }
  }
  pthread_mutex_unlock(&mutexFile);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  cacheFiles.erase(eid);
  cacheAttrs.erase(eid);
  return ret;
}

void extent_client::flush(extent_protocol::extentid_t eid) {
  extent_protocol::status ret = extent_protocol::OK;
  pthread_mutex_lock(&mutexFile);
  if (cacheFiles.find(eid) != cacheFiles.end()) {
    if (cacheFiles[eid].dirty) {
      string buf = cacheFiles[eid].fileExt;
      cacheFiles.erase(eid);
      cacheAttrs.erase(eid);
      pthread_mutex_unlock(&mutexFile);
      int r;
      ret = cl->call(extent_protocol::put, eid, buf, r);
    } else {
      cacheFiles.erase(eid);
      cacheAttrs.erase(eid);
      pthread_mutex_unlock(&mutexFile);
    }
  } else {
    int r;
    if (cacheAttrs.find(eid) != cacheAttrs.end()) {
      cacheAttrs.erase(eid);
      pthread_mutex_unlock(&mutexFile);
    } else {
      pthread_mutex_unlock(&mutexFile);
      cl->call(extent_protocol::remove, eid, r);
    }
  }
}

