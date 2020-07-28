#include <stdio.h>

int main(){
	int i;
	for(i=0; i< 10 ; i++){
#ifdef MULTILEVEL_SCHED
		printf("multi exec here\n");
#elif MLFQ_SCHED
		printf("MLFQ exec here\n");
		//variable here?
#else
		printf("error\n");
#endif
	}

}
