//
// Lock demo
//

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"
#include <arpa/inet.h>
#include <vector>
#include <stdlib.h>
#include <stdio.h>

std::string dst;
lock_client *lc;
lock_client *lc1;

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 2){
    fprintf(stderr, "Usage: %s [host:]port\n", argv[0]);
    exit(1);
  }

  dst = argv[1];
  lc = new lock_client(dst);
  lc1 = new lock_client(dst);
  r = lc->stat(1);
  printf ("stat returned %d\n", r);

  lc->acquire(1);
  lc->release(1);
  lc1->acquire(1);
  lc1->release(1);
  lc->acquire(1);
  
}
