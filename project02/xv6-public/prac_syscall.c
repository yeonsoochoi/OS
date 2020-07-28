#include "types.h"
#include "defs.h"

//simple system call
int myfunction(char *str)//get a string, then print it and  return 0xABCDABCD
{
	cprintf("%s\n",str);
	return 0xABCDABCD;
}
//after this, go to makefile and add prac_syscall.o

//wrapper for my_syscall
int sys_myfunction(void)
{
	char *str;
	//decode argument using argstr
	if (argstr(0, &str) < 0) //if fail
		return -1;
	return myfunction(str);
}
