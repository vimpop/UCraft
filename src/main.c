
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>

#include "UCraft.h"
static uint8_t cleanup_flag;

void cleanup(int signal_number)
{
  // clean up flag
  cleanup_flag = 1;
}
int main(int argc, char const *argv[])
{
  signal(SIGINT, cleanup);
  return UCraftStart(&cleanup_flag);
}