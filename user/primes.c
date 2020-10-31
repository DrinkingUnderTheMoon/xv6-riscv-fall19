#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define N 35
void init_data(char*data) {
    for(int i=1;i<=N-1;i++) {
        data[i] = i+1;
    }
    data[0]=N-1;  // 剩余个数为34
}
void debug(char type,char left_num,char*data){
    if(type == 1) {
        printf("\n父进程：");
    }
    else {
        printf("\n子进程：");
    }
    for(int i=0;i<left_num;i++) {
        printf("%d ",data[i]);
    }
    printf("\n");
}

int main(){
    int parent_fd[2];
    int child_fd[2];
    char parent_buf[N];  // 父进程数据缓冲，其中 parent_buf[0] 为剩余需要判断的数字个数
    char child_buf[N];  // 子进程数据缓冲，其中 child_buf[0] 为剩余需要判断的数字个数
    init_data(parent_buf);
    int pid;
    while(1) {
        pipe(parent_fd);
        pipe(child_fd);
        pid = fork();
        if(pid != 0) { // 父进程
            debug(1,parent_buf[0]+1,parent_buf);
            write(parent_fd[1],parent_buf,parent_buf[0]+1);
            close(parent_fd[1]);
            wait();
            read(child_fd[0],parent_buf,1);//读子进程的child_buf[0]，即子进程处理后的剩余数字个数，并保存到parent_buf[0]
            printf("父进程：parent_buf[0]:%d",parent_buf[0]);
            if(parent_buf[0]==0){
                close(child_fd[0]);
                break;
            }
            while(read(child_fd[0],parent_buf+1,parent_buf[0])!=parent_buf[0]){
                // printf("stay");  最后会在这里死循环
            }
            close(child_fd[0]);
        
        }
        else {  // 子进程
            while(read(parent_fd[0],child_buf,1)!=1);
            while(read(parent_fd[0],child_buf+1,child_buf[0])!=child_buf[0]);
            debug(0,child_buf[0]+1,child_buf);
            char smallest_num = child_buf[1];
            printf("prime %d",smallest_num);
            int left_num = child_buf[0];
            int j = 0;
            for(int i=1;i<=left_num;i++) {
                if(child_buf[i]%smallest_num == 0) {
                    child_buf[0]--;
                }
                else {
                    ++j;
                    child_buf[j] = child_buf[i];
                    // printf("j:%d,child_buf[j]:%d",j,child_buf[j]);
                }
            }
            write(child_fd[1],child_buf,child_buf[0]+1);
            debug(0,child_buf[0]+1,child_buf);
            close(child_fd[1]);
            exit();
        }
    }
    return 0;
}