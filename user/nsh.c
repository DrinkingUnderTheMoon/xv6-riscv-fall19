#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define NEEDREDIRINPUT 1
#define NEEDREDIROUTPUT 2
#define MAXARGS 10
#define MAXFILENAME 20

typedef struct CMD {
    char *cmd_argv[MAXARGS];
    char *cmd_end_of_argv[MAXARGS];
    char cmd_argc;
    // 0 -> input; 1 -> output
    char type;
    char *file[2];
    char *end_of_file[2];
    int fd[2];
    int mode[2];
    // strcut CMD* left_child;
    // struct CMD* right_child;
}cmd;

cmd left_command;
cmd right_command;
char whitespace[] = " \t\r\n\v";  // 空格、tab、回车、换行、垂直跳格
char symbols[] = "<|>";

void panic(char *s) {
    fprintf(2, "%s\n", s);
    exit(-1);
}

int fork1() {
    int pid;
    pid = fork();
    if(pid == -1){
        panic("fork");
    }
    return pid;
}

void execRedir(cmd* command,int dir){
    close(command->fd[dir]);
    *command->end_of_file[dir]=0;
    if(open(command->file[dir], command->mode[dir]) < 0){
        fprintf(2, "open %s failed\n", command->file[dir]);
        exit(-1);
    }
}

void redircmd(cmd* command,char* file, char* end_of_file, int mode, int fd,int dir){
    command->type |= dir+1;
    command->mode[dir] = mode;
    command->fd[dir] = fd;
    command->file[dir] = file;
    command->end_of_file[dir] = end_of_file;
}

int getcmd(char *buf, int nbuf){
    fprintf(2, "@ ");
    memset(buf, 0, nbuf);
    gets(buf, nbuf);  // 获取命令行一行数据，区分标志为'\n'，一行长度最长只有nbuf
    if(buf[0] == 0) // EOF
        return -1;
    return 0;
}

// 若rest_commend首字符是重定向，则对command做出处理，完成重定向数据初始化；且将rest_commend进行移动；
// 返回当前ps指针所指向的结果的意义，是字符的int形式，注意>>是'+'
// 且会移动到下一个字符串的开头（字符串与字符串之间使用' '分割
int gettoken(char **ps, char *es, char **q, char **eq) {
    char *s;
    int ret;

    s = *ps;
    while(s < es && strchr(whitespace, *s))
        s++;
    if(q)
        *q = s;
    ret = *s;  //此时*s指向下一个切分点的第一个字符xxxx Y?xxx这里的Y
    switch(*s){
    case 0:
        break;
    case '|':
    case '<':  // 输入重定向
        s++;  // 移动到下一个字符串开头
        break;  
    case '>':  // 输出重定向 -> 指的是程序输出到文件，从头开始
        s++;
        if(*s == '>'){ // >> 追加内容到文件，从文章结尾出开始
            ret = '+';
            s++;
        }
        break;
    default:
        ret = 'a';
        while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s)){
            s++;  // 遇到 空格/换行 < > | 回车 垂直换格 才停下
        }
        break;
    }
    if(eq)
        *eq = s;  // s 指向特殊符号，字符串末尾
    while(s < es && strchr(whitespace, *s))
        s++;
    *ps = s;  // 当前cmd_string移动到下一个字符串开头
    return ret;
}

void parsecmd(cmd* left_cmd, cmd* right_cmd, char* cmd_string,int* pipe_flag){
    char* end_of_cmd;
    char* argv;
    char* end_of_argv;
    cmd* goal_cmd = left_cmd;
    end_of_cmd = cmd_string + strlen(cmd_string);
    int argc = 0;
    while(cmd_string<end_of_cmd){
        int token = gettoken(&cmd_string, end_of_cmd, &argv, &end_of_argv);  // end_of_argv 指向参数末尾字符的后一个
        switch(token){
            case '<':
                gettoken(&cmd_string, end_of_cmd, &argv, &end_of_argv);
                redircmd(goal_cmd,argv,end_of_argv,O_RDONLY,0,0);
                break;
            case '>':
                gettoken(&cmd_string, end_of_cmd, &argv, &end_of_argv);
                // printf("filename:");
                // for(char*temp = argv;temp<end_of_argv;temp++){
                //     printf("%c",*temp);
                // }
                // printf("\n");
                redircmd(goal_cmd,argv,end_of_argv,O_WRONLY|O_CREATE,1,1);
                break;
            case '+':
                gettoken(&cmd_string, end_of_cmd, &argv, &end_of_argv);
                redircmd(goal_cmd,argv,end_of_argv,O_WRONLY|O_CREATE,1,1);
                break;
            case '|':
                *pipe_flag = 1;
                goal_cmd->cmd_argv[argc] = 0;
                goal_cmd->cmd_argv[argc] = 0;
                goal_cmd->cmd_argc = argc;
                argc = 0;
                goal_cmd = right_cmd;
                break;
            default:
                goal_cmd->cmd_argv[argc] = argv;
                goal_cmd->cmd_end_of_argv[argc] = end_of_argv;
                argc++;
                break;
        }
    }
    goal_cmd->cmd_argc = argc;
}

void checkRedir(cmd* command){
    switch(command->type){
        case (NEEDREDIRINPUT|NEEDREDIROUTPUT):
            execRedir(command,1);
        case NEEDREDIRINPUT:
            execRedir(command,0);
            break;
        case NEEDREDIROUTPUT:
            execRedir(command,1);
            break;
    }
}

void runcmd(cmd* left_command, cmd* right_command,int* pipe_flag) {
    int fd[2];

    if(left_command == 0){
        exit(-1);
    }
    // printf("%d\n",left_command->cmd_argc);
    for(int i=0;i<left_command->cmd_argc;i++){
        *left_command->cmd_end_of_argv[i] = 0;
        // printf("%s\n",left_command->cmd_argv[i]);
    }
    if(*pipe_flag){
        if(pipe(fd)<0){
            panic("pipe create unsuccessfully!");
        }
        // printf("hello pipe\n");
        if(fork1()==0){
            close(1);  // 关掉标准输出口
            dup(fd[1]);  // 将fd[1]作为输出口（打印结果流向管道）
            close(fd[1]);
            close(fd[0]);
            checkRedir(left_command);
            exec(left_command->cmd_argv[0],left_command->cmd_argv);
            fprintf(2, "exec %s failed\n", left_command->cmd_argv[0]);
        }
        // 只有父进程会走到这里

        if(fork1()==0){  // 执行right_cmd
            close(0);  // 关掉 right_cmd的标准输入
            dup(fd[0]);  // 将fd[0]作为标准输入（输入数据来源于管道）
            close(fd[0]);
            close(fd[1]);
            for(int i=0;i<right_command->cmd_argc;i++){
                *right_command->cmd_end_of_argv[i] = 0;
                // printf("r:%s ",right_command->cmd_argv[i]);
            }
            checkRedir(right_command);
            exec(right_command->cmd_argv[0],right_command->cmd_argv);
            fprintf(2, "exec %s failed\n", right_command->cmd_argv[0]);
        }
        close(fd[0]);
        close(fd[1]);
        wait(0);
        wait(0);  
    }
    else{
        if(left_command->cmd_argv[0] == 0){  // 没有命令
            panic("null command\n");
        }
        checkRedir(left_command);
        exec(left_command->cmd_argv[0],left_command->cmd_argv);
        fprintf(2, "exec %s failed\n", left_command->cmd_argv[0]);
    }
    exit(0);
}

int main(void){
    static char buf[100];
    int fd;
    // 检查是否已经打开0，1，2文件描述符（标准输入、输出、错误输出）
    while((fd = open("console", O_RDWR)) >= 0){
        if(fd >= 3){
            close(fd);
            break;
        }
    }
    // 使用getcmd获取一行输入，并保存到buf中；同时通过parsecmd解析buf
    while(getcmd(buf, sizeof(buf)) >= 0){
        memset(&left_command,0,sizeof(cmd));
        memset(&right_command,0,sizeof(cmd));
        if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
            // Chdir must be called by the parent, not the child.
            buf[strlen(buf)-1] = 0;  // chop \n
            if(chdir(buf+3) < 0){  // chdir用于改变当前目录 -> buf+3 为 具体目录开始
                fprintf(2, "cannot cd %s\n", buf+3);
                continue;
            }
        }
        if(fork1() == 0){
                int flag = 0;
                parsecmd(&left_command, &right_command, buf, &flag);
                runcmd(&left_command, &right_command, &flag);
        }
        else{
            wait(0);
        }
    }
    exit(0);
}