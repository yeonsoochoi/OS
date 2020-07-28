#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "syscall.h"

int
mystrcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

char*
mystrcat(char *username, const char *password)
{
	int i = strlen(username)-1;
	int j = 0;
	int fin = strlen(password)-1;

	username[i++]=' ';
	while(j<fin){
		username[i++] = password[j++];
		
	}
	return username;
}

char*
mystrcpy(char *dest, const char *src)
{
	char *tmp = dest;
	while((*dest++ = *src++ - 1)!= '\0')
		;
	return tmp;
}


char *argv[] = {"sh",0};


int
main()
{
	char username[15];
	char password[15];
	char userinfo[15];
	char userinfo_tmp[15];
	char *total;
	int fd,pid;
	int ret=-1;
	char tmp[10][31];
	
	while(1){
		fd = open("userlist", T_FILE);
		read(fd, &tmp,sizeof(tmp));
		ret = -1;
		for(int i =0 ; i<15; i++){
			username[i]='\0';
			password[i]='\0';
			userinfo[i]='\0';
			userinfo_tmp[i]='\0';
		}

		printf(1,"username: ");
		gets(username,sizeof(username));
		printf(1,"password: ");
		gets(password,sizeof(password));
		strcpy(userinfo_tmp,username);

		total = mystrcat(username,password);
		for(int i = 0 ; i <10; i++){
			if(mystrcmp(tmp[i],total)==0){
				ret = 1;
				break;
			}
		}
		if(ret==1){
			for(int i=0 ;i<sizeof(userinfo_tmp); i++){
				if(userinfo_tmp[i]=='\n')
					break;
				userinfo[i]=userinfo_tmp[i];
			}
			retusername(userinfo);
			
			pid = fork();
			if(pid <0){
				printf(1,"login: fork failed\n");
				exit();
			}
			if(pid == 0){
				exec("sh",argv);
				printf(1,"login: exec sh failed\n");
				exit();
			}
			else
				pid = wait();
		}
		else
			printf(1,"Wrong login information\n");

	}	
		
}

