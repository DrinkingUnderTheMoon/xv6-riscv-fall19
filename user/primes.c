#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char*argv[]){
    int fd1[2];
    int fd2[2];
    char buf[5];
    int pid;
    pipe(fd1);
    pipe(fd2);
    pid = fork();
    if (pid == 0) {
        // 子进程等父进程传输
        while(read(fd1[0],buf,4) != 4){
        }
        close(fd1[0]);
        if(strcmp(buf,"ping")==0) {
            printf("id:%d received %s\n",getpid(),buf);
        }
        else {
            printf("error!");
        }
        write(fd2[1],"pong",4);
        close(fd2[1]);
        exit();
    }
    else {
         // 父进程 得到子进程pid
        write(fd1[1],"ping",4);
        close(fd1[1]);
        while(read(fd2[0],buf,4) != 4){ // 父进程等子进程传输回来
        }
        if(strcmp(buf,"pong")==0) {
            printf("id:%d received %s\n",getpid(),buf);
        }
        else {
            printf("error!");
        }
        close(fd2[0]);
        exit();
    }
    exit();
}