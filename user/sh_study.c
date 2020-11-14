#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3

#define MAXARGS 10
char whitespace[] = " \t\r\n\v";  // 空格、tab、回车、换行、垂直跳格
char symbols[] = "<|>";
struct cmd {
    int type;
};

struct execcmd {
    int type;
    char *argv[MAXARGS];
    char *eargv[MAXARGS];
};

struct redircmd {
    int type;
    struct cmd *cmd;
    char *file;
    char *efile;
    int mode;
    int fd;
};

struct pipecmd {
    int type;
    struct cmd *left;
    struct cmd *right;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void runcmd(struct cmd *cmd) {
    int p[2];
    struct execcmd *ecmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if(cmd == 0)
    exit(-1);

    switch(cmd->type){
    default:
        panic("runcmd");

    case EXEC:
        ecmd = (struct execcmd*)cmd;
        if(ecmd->argv[0] == 0)
        exit(-1);
        exec(ecmd->argv[0], ecmd->argv);
        fprintf(2, "exec %s failed\n", ecmd->argv[0]);
        break;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        close(rcmd->fd);
        if(open(rcmd->file, rcmd->mode) < 0){
            fprintf(2, "open %s failed\n", rcmd->file);
            exit(-1);
        }
        runcmd(rcmd->cmd);
        break;

    case PIPE:
        pcmd = (struct pipecmd*)cmd;
        if(pipe(p) < 0)
        panic("pipe");
        if(fork1() == 0){
        close(1);
        dup(p[1]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->left);
        }
        if(fork1() == 0){
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);
        wait(0);
        wait(0);
        break;
    }
    exit(0);
}


struct cmd* execcmd(void) {
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd* redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd) {
    struct redircmd *cmd;

    cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDIR;
    cmd->cmd = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->mode = mode;
    cmd->fd = fd;
    return (struct cmd*)cmd;
}

struct cmd* pipecmd(struct cmd *left, struct cmd *right) {
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

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
    case '(':
    case ')':
    case ';':
    case '&':
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
    *ps = s;  // 移动到下一个字符串开头
    return ret;
}

struct cmd* parseredirs(struct cmd *cmd, char **ps, char *es) {
    int tok;
    char *q, *eq;

    while(peek(ps, es, "<>")){  // 检查是否有重定向相关的符号（当前的ps仍然是buf（头）指针的指针，同样es仍然是buf的尾指针（边界）
        tok = gettoken(ps, es, 0, 0);// 移到下一个字符串
        if(gettoken(ps, es, &q, &eq) != 'a'){   // 如果不是重定向地点：
            panic("missing file for redirection");
        }
        // q指向字符串开头,eq指向字符串末尾后的特殊符号
        switch(tok){
            case '<':
            cmd = redircmd(cmd, q, eq, O_RDONLY, 0);  // 重定向输入
            break;
            case '>':
            cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);  // 重定向输出，将命令结果输出到文件（之前的内容抛弃）
            break;
            case '+':  // >>
            cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);  // 重定向输出，输出结果附加到文件后面
            break;
        }
        // redircmd -> 申请一个空间，并且存放上面的参数，cmd->type为重定向REDIR
    }
    return cmd;
}

struct cmd* parseexec(char **ps, char *es) {
    char *q, *eq;
    int tok, argc;
    struct execcmd *cmd;
    struct cmd *ret;

    // if(peek(ps, es, "("))
    // return parseblock(ps, es);

    ret = execcmd(); // 申请一个cmd结构体内存空间并且初始化，并拿到这个指针
    cmd = (struct execcmd*)ret;  // 使用cmd指向ret这个结构体

    argc = 0;
    ret = parseredirs(ret, ps, es);  // 检查是否有重定向符号"<“或”>", 如果有就会处理出来文件名等相关数据
    while(!peek(ps, es, "|)&;")){
        if((tok=gettoken(ps, es, &q, &eq)) == 0)
        break;
        if(tok != 'a')
        panic("syntax");
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;
        if(argc >= MAXARGS)
        panic("too many args");
        ret = parseredirs(ret, ps, es);
    }
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;
    return ret;
}

struct cmd* parsepipe(char **ps, char *es)
{
    struct cmd *cmd;

    cmd = parseexec(ps, es);  // ps buf指针的指针；es buf末尾的指针
    if(peek(ps, es, "|")){  // 判断下一个分段点是不是'|' ，此时ps指向的内容为分段符号的下一字符
        gettoken(ps, es, 0, 0);  // A | B ，指针从|移动到B
        cmd = pipecmd(cmd, parsepipe(ps, es));
    }
    return cmd;
}

int getcmd(char *buf, int nbuf){
    fprintf(2, "@ ");
    memset(buf, 0, nbuf);
    gets(buf, nbuf);  // 获取命令行一行数据，区分标志为'\n'，一行长度最长只有nbuf
    if(buf[0] == 0) // EOF
        return -1;
    return 0;
}

int peek(char **ps, char *es, char *toks){
    char *s;  
    s = *ps;  // 拿到命令行的指针
    while(s < es && strchr(whitespace, *s)){
        s++;
    }
    // 找到下一个分段点 或字符串开头
    *ps = s;
    return *s && strchr(toks, *s);  // 判断当前分段点是否是输入参数'toks'
}


struct cmd* parseline(char **ps, char *es){ // buf指针的指针 buf末尾指针
    struct cmd *cmd;
    cmd = parsepipe(ps, es);
    // while(peek(ps, es, "&")){
    //     gettoken(ps, es, 0, 0);
    //     cmd = backcmd(cmd);
    // }
    // if(peek(ps, es, ";")){
    //     gettoken(ps, es, 0, 0);
    //     cmd = listcmd(cmd, parseline(ps, es));
    // }
    return cmd;
}

struct cmd* parsecmd(char *s){  // 命令行全部数据
    char *es;
    struct cmd *cmd;

    es = s + strlen(s);  // es 指向 buf 末尾的 0 -> gets处理的
    cmd = parseline(&s, es);  // ?
    peek(&s, es, "");  // 在检查某个东西
    if(s != es){
        fprintf(2, "leftovers: %s\n", s);
        panic("syntax");
    }
    nulterminate(cmd);
    return cmd;
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

    // Read and run input commands.
    while(getcmd(buf, sizeof(buf)) >= 0){
        if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
            // Chdir must be called by the parent, not the child.
            buf[strlen(buf)-1] = 0;  // chop \n
            if(chdir(buf+3) < 0)  // chdir用于改变当前目录 -> buf+3 为 具体目录开始
                fprintf(2, "cannot cd %s\n", buf+3);
                continue;
            }
            if(fork1() == 0){
                runcmd(parsecmd(buf));
            }
            else{
                wait(0);
            }
    }
    exit(0);
}