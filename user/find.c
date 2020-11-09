#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* fmtname(char *path){
    static char buf[DIRSIZ+1];
    char *p;
    // Find first character after last slash/.
    for(p=path+strlen(path); p >= path && *p != '/'; p--);
    p++;  // 指向最后一个斜杠后的第一个字符,可能为空？比如 /xxx/ ->等价于 /xxx

    // Return blank-padded name.  返回"xxx      " 或 "xxxxxxxxxxxxxxxxxxxxxxxxxxx"
    if(strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf+strlen(p), 0, DIRSIZ-strlen(p));
    return buf;
}

void find(char *path,char *filename){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    // linux open(pathname,oflag)  -> pathname可以是绝对路径或相对路径，oflag -> O_RDONLY(0) 只读, O_WDONLY(1) 只写, O_RDWR(2)读写
    if((fd = open(path, 0)) < 0){  // 打开失败返回-1 否则一般返回3, 0是标准输入、1是标准输出、2是标准错误输出
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){  // 读取信息成功返回0，否则返回-1
        fprintf(2, ": cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){  // st中存放这个文件信息
        case T_FILE:
            // printf("get the filename:s\n",fmtname(path));
            if(strcmp(filename,fmtname(path)) == 0){// fmtname(path) 返回指针
                printf("%s\n",path);
            }
            break;

        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
                printf("find: path too long\n");
                break;
            }
            strcpy(buf, path);  // 现在buf获取到当前地址 -> ~~~/文件夹
            p = buf+strlen(buf);
            *p++ = '/';
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0){
                    continue;
                }
                if((strcmp(de.name,".") == 0) ||(strcmp(de.name,"..") == 0)){
                    continue;
                }
                memmove(p, de.name, DIRSIZ); // 注意p的位置没有移动
                p[DIRSIZ] = 0;
                if(stat(buf, &st) < 0){
                    printf("find: cannot stat %s\n", buf);
                    continue;
                }
                find(buf,filename);
            }
            break;
    }
    close(fd);
}

int main(int argc, char *argv[]){  // find path name
// path: 缺省或./ -> 当前目录; ../ -> 上一目录; / -> 根目录; 
    if(argc < 2){  // 只有find [path=.] name
        printf("Please input the file name\n");
    }
    if(argc == 2){  // 默认认为只提供了一个filename参数，并在当前路径下查找
        find(".",argv[1]);
    }
    if(argc == 3){
        printf("get it\n");
        find(argv[1],argv[2]);
    }
    exit();
}