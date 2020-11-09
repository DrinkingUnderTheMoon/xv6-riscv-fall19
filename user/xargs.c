#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]){
// argv[1]~~~ 存着具体的命令
// argv[2]~~argv[argc-1] 命令部分参数
// 从标准输入读入每行剩余参数：行以\n作为区分
    char actual_argv[MAXARG][MAXARG]; // 可以存放32个字符串参数
    char *pass[MAXARG];  // exec必须要传字符串指针数组
    int n, i, str_pos = 0;  // 第i个字符串的第str_pos位置
    char c;
    for(i=0;i<MAXARG;i++){
        pass[i] = actual_argv[i];
    }
    for(i=1;i<argc;i++){
        strcpy(actual_argv[i-1],argv[i]);  // 0~argc-2
    }
    i--;  // 移动到实际需要填充的位置 -> argc-2
    while((n = read(0, &c, 1)) > 0) {  // 从标准输入中继续读入参数；读到 EOF 会结束读取
        if(c == ' '){  // 填充下一个字符串
            actual_argv[i++][str_pos] = '\0';
            str_pos = 0;
        }
        else if(c == '\n'|| c== '\r') {  // 输入完一行
            actual_argv[i++][str_pos] = '\0';
            pass[i] = 0;
            if(fork()!=0) {
                wait();
                pass[i] = actual_argv[i];
                i = argc - 1;  // 重新填充参数
                str_pos = 0;
            }
            else {
                exec(actual_argv[0],pass);
            }
        }
        else{
            if(str_pos<MAXARG){
                actual_argv[i][str_pos++] = c;
            }
            else {
                printf("Error:argv is too long\n");
                exit();
            }
        }
    }
    exit();
}
