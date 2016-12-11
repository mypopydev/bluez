#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <setjmp.h>
#include <signal.h>
#include <errno.h>

#define BUFSIZE BUFSIZ
#define ERR_BUF BUFSIZE/12
char err[ERR_BUF] = {0};

void err_set(const char* str)
{
        memset(err, 0, sizeof(err));
        sprintf(err, "%s(%s)", "网络异常，请重试", str);
}

static void sig_alrm(int signo)
{
        err_set("Recv timeout");
}

#define TIMEOUT 20
extern int h_errno;

void geturl(char *url, char *resp)
{
        int cfd;
        struct sockaddr_in cadd;
        struct hostent *pURL = NULL;
        char myurl[BUFSIZE];
        char *pHost = 0;
        char host[BUFSIZE],GET[BUFSIZE];
        char request[BUFSIZE];
        static char text[BUFSIZE];

        /* 分离主机中的主机地址和相对路径 */
        memset(myurl, 0, BUFSIZE);
        memset(host, 0, BUFSIZE);
        memset(GET, 0, BUFSIZE);
        if (strstr(url, "https://") != NULL) {
                strcpy(myurl, url + strlen("https://"));
        } else {
                strcpy(myurl,url);
        }
        for (pHost = myurl;*pHost != '/' && *pHost != '\0';++pHost);


        // 获取相对路径保存到GET中
        if ((int)(pHost-myurl) == strlen(myurl)) {
                strcpy(GET,"/");//即url中没有给出相对路径，需要自己手动的在url尾
        } else {
                strcpy(GET,pHost);//地址段pHost到strlen(myurl)保存的是相对路径
        }

        //将主机信息保存到host中
        //此处将它置零，即它所指向的内容里面已经分离出了相对路径，剩下的为host信
        //息(从myurl到pHost地址段存放的是HOST)
        *pHost = '\0';
        strcpy(host,myurl);
        //设置socket参数
        if (-1 == (cfd = socket(AF_INET,SOCK_STREAM,0))) {
                printf("create socket failed of client!\n");
                return ;
        }

        pURL = gethostbyname(host);// 将上面获得的主机信息通过域名解析函数获得域>名信息
        if (pURL == NULL) {
                printf("%s\n", hstrerror(h_errno));
                err_set(hstrerror(h_errno));
                return;
        }

        // 设置IP地址结构
        bzero(&cadd,sizeof(struct sockaddr_in));
        cadd.sin_family = AF_INET;
        cadd.sin_addr.s_addr = *((unsigned long*)pURL->h_addr_list[0]);
        /*cadd.sin_addr.s_addr = inet_addr("121.40.185.206");*/
        cadd.sin_port = htons(80);
        //向WEB服务器发送URL信息
        memset(request,0,BUFSIZE);
        strcat(request,"GET ");
        strcat(request,GET);
        strcat(request," HTTP/1.1\r\n");//至此为http请求行的信息
        strcat(request,"HOST: ");
        strcat(request,host);
        strcat(request,"\r\n");
        strcat(request,"Cache-Control: no-cache\r\n\r\n");

        // 连接服务器
        int cc;
        if(-1 == (cc = connect(cfd,(struct sockaddr*)&cadd,(socklen_t)sizeof(cadd)))) {
                printf("connect failed of client!\n");
                err_set(strerror(errno));
                return;
        }
        printf("request:%s\n", request);

        //向服务器发送url请求的request
        int cs;
        if (-1 == (cs = send(cfd,request,strlen(request),0))) {
                printf("向服务器发送请求的request失败!\n");
                err_set(strerror(errno));
                return;
        }
        // 客户端接收服务器的返回信息
        memset(text, 0, BUFSIZE);
        int cr;
        if(-1 == (cr = recv(cfd, text, BUFSIZE, 0))) {
                printf("recieve failure!\n");
                if (errno != EINTR) {
                        //err_set("Recv failure");
                        err_set(strerror(errno));
                        //alarm(0);
                }
                return;
        }
        //alarm(0);
        char *tmp = NULL;
        if ((tmp = strstr(text, "T:")) != NULL) {
                strcat(resp, tmp);
        } else if ((tmp = strstr(text, "F:")) != NULL) {
                strcat(resp, tmp);
        }
        printf("%s\n", text);
        close(cfd);
}

