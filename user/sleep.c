#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc,char* argv[]){
    if(argc == 1){
        printf("Please input the sleep time!\n");
        exit();
    }
    if(argc > 2){
        printf("There are more than one sleep parameter,just taking the first parameter\n");
    }
    char *nString = argv[1];
    int n = 0;
    while(*nString>='0' && *nString <='9'){
        n = n*10+(*nString++ -'0');
    }
    printf("Sleep %d seconds\n",n);
    if(*nString!='\0'){
        printf("Please just input number!\n");
        return -1;
    }
    sleep(n*10);
    exit();
}
