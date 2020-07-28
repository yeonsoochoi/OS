#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
	char *buf = "Hello xv6!";
	int ret_val,a;
	ret_val = myfunction(buf);
	a = getpid();
	printf(1, "Return value : 0x%x\n", ret_val);
	printf(1,"pid : %d",a);
	exit();
}
