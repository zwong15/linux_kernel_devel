#include "userapp.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define LENGTH 1000

void register_pid(unsigned int pid){
  char command[LENGTH];
  memset(command, '\0', LENGTH);
  sprintf(command, "echo %u > /proc/mp1/status", pid);
  system(command);
}

void factorial_function(int fact_value){
  int init_val = 1;
  while(fact_value-- != 0){
    init_val *= fact_value;
  }
}

int main(int argc, char* argv[])
{
 
    int expire = atoi(argv[1]);
    time_t start_time = time(NULL);

    //int value_factorial = atoi(argv[2]);
    
    register_pid(getpid());
    
    while(1){
      if((int)(time(NULL) - start_time) > expire){
        break;
      }
    }
            
    return 0;
}
