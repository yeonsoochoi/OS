#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
	int pid;
	int i=0;

	pid = fork();

	if (pid ==0){
		for(i = 0; i< 10; i++){
			yield();
			printf(1,"Child \n");
		}	
	}else if(pid > 0){
		for(i = 0;i< 10; i++){
			yield();
			printf(1, "parent\n");
		}	
		wait();	
	}

	exit();
}
