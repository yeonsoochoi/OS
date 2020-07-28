// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

//#define BUFFSIZE 1024
char *argv[] = { "login", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr
  
  openfile("userlist");
  printf(1, "init: starting login\n");
  
  for(;;){
	
	
	pid = fork();
	if(pid<0){
		printf(1,"init login: fork failed\n");
		exit();
	}
	if(pid == 0){
		exec("login",argv);
		printf(1, "init: exec login failed\n");
		exit();
	}
	while((wpid=wait()) >= 0 && wpid != pid)
		printf(1,"zombie!\n");
				
  }
}

