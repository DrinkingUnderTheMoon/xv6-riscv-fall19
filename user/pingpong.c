#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char*argv[]){
    int parent_fd[2];
    int child_fd[2];
    char buf[5];
    int pid;
    pipe(parent_fd);
    pipe(child_fd);
    pid = fork();
    if (pid == 0) {
        // 子进程等父进程传输
        while(read(parent_fd[0],buf,4) != 4){
        }
        close(parent_fd[0]);
        if(strcmp(buf,"ping")==0) {
            printf("%d: received %s\n",getpid(),buf);  // 获取子进程pid
        }
        else {
            printf("error!");
        }
        write(child_fd[1],"pong",4);
        close(child_fd[1]);
        exit();
    }
    else {
         // 父进程 得到子进程pid
        write(parent_fd[1],"ping",4);
        close(parent_fd[1]);
        while(read(child_fd[0],buf,4) != 4){ // 父进程等子进程传输回来
        }
        if(strcmp(buf,"pong")==0) {
            printf("%d: received %s\n",getpid(),buf);  // 获取父进程pid
        }
        else {
            printf("error!");
        }
        close(child_fd[0]);
        exit();
    }
    exit();
}