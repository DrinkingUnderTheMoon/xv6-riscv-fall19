#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define N 35
void function(int grandparent_fd){
    int parent_fd[2];  // 父写子读通道
    int pid;
    char smallest_data;
    pipe(parent_fd);
    if(read(grandparent_fd,&smallest_data,sizeof(smallest_data))<=0){ // 从父读通道读
        close(grandparent_fd);  // 若写端关闭，则关闭读端
        close(parent_fd[1]);
        exit();
    }
    printf("primes:%d\n",smallest_data); 
    if((pid = fork()) != 0){ // 当前父进程
        close(parent_fd[0]);
        char data;
        while(1){
            if(read(grandparent_fd,&data,sizeof(data)) <= 0){ // 从父读通道读
                close(grandparent_fd);  // 若爷爷写通道关闭
                close(parent_fd[1]);  // 则关闭父写通道
                while (wait()!=-1);
                exit();
            }
            if(data % smallest_data == 0) {
                continue;
            }
            write(parent_fd[1],&data,sizeof(data));  // 写入父写通道
        }
    }
    else {  // 子进程
        close(parent_fd[1]);
        function(parent_fd[0]);
        exit();
    }   
}
// void function(int grandparent_fd){  // 父读通道
//     int parent_fd[2];  // 父写子读通道
//     int pid;
//     pipe(parent_fd);        
//     if((pid = fork()) != 0){ // 当前父进程
//         close(parent_fd[0]);
//         char smallest_data, data;
//         if(read(grandparent_fd,&smallest_data,sizeof(smallest_data))<=0){ // 从父读通道读
//             close(grandparent_fd);  // 若写端关闭，则关闭读端
//             close(parent_fd[1]);
//             exit();
//         }
//         printf("primes:%d\n",smallest_data); 
//         while(1){
//             if(read(grandparent_fd,&data,sizeof(data)) <= 0){ // 从父读通道读
//                 close(grandparent_fd);  // 若爷爷写通道关闭
//                 close(parent_fd[1]);  // 则关闭父写通道
//                 while (wait()!=-1);
//                 exit();
//             }
//             if(data % smallest_data == 0) {
//                 continue;
//             }
//             write(parent_fd[1],&data,sizeof(data));  // 写入父写通道
//         }
//     }
//     else {  // 子进程
//         close(parent_fd[1]);
//         function(parent_fd[0]);
//         exit();
//     }    
// }
int main(){
    
    int parent_fd[2];
    int pid;
    pipe(parent_fd);
    if((pid = fork())!=0) {  // 父进程
        close(parent_fd[0]);
        for(char i=2;i<=N;i++){
            write(parent_fd[1],&i,sizeof(i));
        }
        close(parent_fd[1]);
        while (wait()!=-1);
        exit();
    }
    else{  // 子进程
        close(parent_fd[1]);
        function(parent_fd[0]);
        exit();
    }
}
// 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 -> drop 4,6,~~,34 ->
// 3 5 7 9 11 13 15 17 19 21 23 25 27 29 31 33 35 -> drop 9 15 21 27 33 ->
// 5 7 11 13 17 19 23 25 29 31 35 -> drop 25 35 ->
// 7 11 13 17 19 23 29 31 -> drop nothing ->
// 11 13 17 19 23 29 31 -> drop nothing ->
// 13 17 19 23 29 31 -> drop nothing ->

