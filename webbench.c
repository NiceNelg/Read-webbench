/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */

//volatile 关键字表示每次读取该变量的时候都要从内存中获取最新的数据而不是从缓存或者寄存器中的备份中读取，确保每次读取的变量都是内存中最新的值
volatile int timerexpired=0;
int speed=0;
int failed=0;
int bytes=0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;
int clients=1;
int force=0;
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

//设置对shell执行本程序的时候对参数的解析规则
static const struct option long_options[] = {
    {"force",no_argument,&force,1},
    {"reload",no_argument,&force_reload,1},
    {"time",required_argument,NULL,'t'},
    {"help",no_argument,NULL,'?'},
    {"http09",no_argument,NULL,'9'},
    {"http10",no_argument,NULL,'1'},
    {"http11",no_argument,NULL,'2'},
    {"get",no_argument,&method,METHOD_GET},
    {"head",no_argument,&method,METHOD_HEAD},
    {"options",no_argument,&method,METHOD_OPTIONS},
    {"trace",no_argument,&method,METHOD_TRACE},
    {"version",no_argument,NULL,'V'},
    {"proxy",required_argument,NULL,'p'},
    {"clients",required_argument,NULL,'c'},
    {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
   timerexpired=1;
}	

static void usage(void)
{
    fprintf(stderr,
	    "webbench [option]... URL\n"
	    "  -f|--force               Don't wait for reply from server.\n"
	    "  -r|--reload              Send reload request - Pragma: no-cache.\n"
	    "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	    "  -p|--proxy <server:port> Use proxy server for request.\n"
	    "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	    "  -9|--http09              Use HTTP/0.9 style requests.\n"
	    "  -1|--http10              Use HTTP/1.0 protocol.\n"
	    "  -2|--http11              Use HTTP/1.1 protocol.\n"
	    "  --get                    Use GET request method.\n"
	    "  --head                   Use HEAD request method.\n"
	    "  --options                Use OPTIONS request method.\n"
	    "  --trace                  Use TRACE request method.\n"
	    "  -?|-h|--help             This information.\n"
	    "  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])
{
    int opt=0;
    int options_index=0;
    char *tmp=NULL;

    //当没有输入参数的时候输出程序帮助手册并退出
    if(argc==1) {
	    usage();
        return 2;
    } 

    while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF ) {
        switch(opt) {
            case  0 : 
                break;
            case 'f': force=1;
                break;
            case 'r': force_reload=1;
                break; 
            case '9': http10=0;
                break;
            case '1': http10=1;
                break;
            case '2': http10=2;
                break;
            //打印版本号
            case 'V': printf(PROGRAM_VERSION"\n");exit(0);
            //设置监听超时时间
            case 't': benchtime=atoi(optarg);
                break;	     
            case 'p': 
	            /* proxy server parsing server:port */
	            tmp=strrchr(optarg,':');
	            proxyhost=optarg;
                //若没有设置端口则采用默认端口
	            if(tmp==NULL) {
		            break;
	            }
                //若冒号的内存地址与输入参数的起始地址相同代表没有输入IP地址
	            if(tmp==optarg) {
		            fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		            return 2;
	            }
                //若输入参数的起始地址+参数的长度相加与冒号的内存地址位置相同代表没有输入端口号
	            if(tmp==optarg+strlen(optarg)-1) {
		            fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		            return 2;
	            }
                //将冒号置成结束字符\0保证字符串转换成数字正确
	            *tmp='\0';
                //记录端口号
	            proxyport=atoi(tmp+1);
                break;
            case ':':
            case 'h':
            //当参数标识为冒号、h、?时执行打印帮助命令
            case '?': usage();return 2;
                break;
            //记录需要创建客户的个数
            case 'c': clients=atoi(optarg);
                break;
        }
    }
 
    if(optind==argc) {
        fprintf(stderr,"webbench: Missing URL!\n");
		usage();
		return 2;
    }

    if(clients==0) {
        clients=1;
    }
    if(benchtime==0) { 
        benchtime=60;
    }
    /* Copyright */
    fprintf(stderr,
        "Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	    "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	);
    build_request(argv[optind]);
    /* print bench info */
    printf("\nBenchmarking: ");
    switch(method) {
        case METHOD_GET:
        default:printf("GET");
            break;
        case METHOD_OPTIONS:printf("OPTIONS");
            break;
        case METHOD_HEAD:printf("HEAD");
            break;
        case METHOD_TRACE:printf("TRACE");
            break;
    }
    printf(" %s",argv[optind]);
    switch(http10) {
	    case 0: printf(" (using HTTP/0.9)");
            break;
	    case 2: printf(" (using HTTP/1.1)");
            break;
    }
    printf("\n");
    if(clients==1) {
        printf("1 client");
    }
    else {
        printf("%d clients",clients);
    }

    printf(", running %d sec", benchtime);
    if(force) {
        printf(", early socket close");
    }
    if(proxyhost!=NULL) {
        printf(", via proxy server %s:%d",proxyhost,proxyport);
    }
    if(force_reload) {
        printf(", forcing reload");
    }
    printf(".\n");
    return bench();
}

void build_request(const char *url)
{
    char tmp[10];
    //记录url中域名的起始位置
    int i;
    //清空host变量
    bzero(host,MAXHOSTNAMELEN);
    //清空request变量
    bzero(request,REQUEST_SIZE);

    if(force_reload && proxyhost!=NULL && http10<1) {
        http10=1;
    }
    if(method==METHOD_HEAD && http10<1) {
        http10=1;
    }
    if(method==METHOD_OPTIONS && http10<2) {
        http10=2;
    }
    if(method==METHOD_TRACE && http10<2) {
        http10=2;
    }

    switch(method) {
	    default:
	    case METHOD_GET: 
            strcpy(request,"GET");
            break;
	    case METHOD_HEAD: 
            strcpy(request,"HEAD");
            break;
	    case METHOD_OPTIONS: 
            strcpy(request,"OPTIONS");
            break;
	    case METHOD_TRACE: 
            strcpy(request,"TRACE");
            break;
    }

    //字符串连接
    strcat(request," ");

    if(NULL==strstr(url,"://")) {
	    fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	    exit(2);
    }
    if(strlen(url)>1500) {
        fprintf(stderr,"URL is too long.\n");
	    exit(2);
    }
    if(proxyhost==NULL) {
        //当没有设置其他类型的网络协议时,检测是否为http协议,不是的话则退出进程
	    if(0!=strncasecmp("http://",url,7)) { 
            fprintf(stderr,
                "\nOnly HTTP protocol is directly supported, set --proxy for others.\n"
            );
            exit(2);
        }
    }
    /* protocol/host delimiter */
    i=strstr(url,"://")-url+3;
    /* printf("%d\n",i); */

    if(strchr(url+i,'/')==NULL) {
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    if(proxyhost==NULL) {
        /* get port from hostname */
        if(index(url+i,':')!=NULL && index(url+i,':')<index(url+i,'/')) {
            strncpy(host,url+i,strchr(url+i,':')-url-i);
	        bzero(tmp,10);
	        strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	        /* printf("tmp=%s\n",tmp); */
	        proxyport=atoi(tmp);
	        if(proxyport==0) {
                proxyport=80;
            }
        } else {
            strncpy(host,url+i,strcspn(url+i,"/"));
        }
        // printf("Host=%s\n",host);
        strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
    } else {
        // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
        strcat(request,url);
    }
    if(http10==1) {
	  strcat(request," HTTP/1.0");
    } else if (http10==2) {
        strcat(request," HTTP/1.1");
    }
    strcat(request,"\r\n");
    if(http10>0) {
	    strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
    }
    if(proxyhost==NULL && http10>0) {
	    strcat(request,"Host: ");
	    strcat(request,host);
	    strcat(request,"\r\n");
    }
    if(force_reload && proxyhost!=NULL) {
	    strcat(request,"Pragma: no-cache\r\n");
    }
    if(http10>1) {
	    strcat(request,"Connection: close\r\n");
    }
    /* add empty line at end */
    if(http10>0) {
        strcat(request,"\r\n"); 
    }
    // printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
    int i,j,k;	
    pid_t pid=0;
    FILE *f;

    /* check avaibility of target server */
    i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
    if(i<0) { 
	    fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);
    /* create pipe */
    if(pipe(mypipe)) {
	    perror("pipe failed.");
	    return 3;
    }

    /* not needed, since we have alarm() in childrens */
    /* wait 4 next system clock tick */
    /* 
       cas=time(NULL);
       while(time(NULL)==cas)
       sched_yield();
    */

    /* fork childs */
    for(i=0;i<clients;i++) {
        pid=fork();
	    if(pid <= (pid_t) 0) {
		    /* child process or error*/
	        sleep(1); /* make childs faster */
		    break;
	    }
    }

    if( pid< (pid_t) 0) {
        fprintf(stderr,"problems forking worker no. %d\n",i);
	    perror("fork failed.");
	    return 3;
    }

    if(pid== (pid_t) 0) {
        /* I am a child */
        if(proxyhost==NULL) {
            benchcore(host,proxyport,request);
        } else {
            benchcore(proxyhost,proxyport,request);
        }
        /* write results to pipe */
	    f=fdopen(mypipe[1],"w");
	    if(f==NULL) {
		    perror("open pipe for writing failed.");
		    return 3;
	    }
	    /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
	    fprintf(f,"%d %d %d\n",speed,failed,bytes);
	    fclose(f);
	    return 0;
    } else {
	    f=fdopen(mypipe[0],"r");
	    if(f==NULL) {
		    perror("open pipe for reading failed.");
		    return 3;
	    }
	    setvbuf(f,NULL,_IONBF,0);
	    speed=0;
        failed=0;
        bytes=0;

	    while(1) {
		    pid=fscanf(f,"%d %d %d",&i,&j,&k);
		    if(pid<2) {
                fprintf(stderr,"Some of our childrens died.\n");
                break;
            }
		    speed+=i;
		    failed+=j;
		    bytes+=k;
		    /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		    if(--clients==0) {
                break;
            }
	    }
	    fclose(f);

        printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
            (int)((speed+failed)/(benchtime/60.0f)),
		    (int)(bytes/(float)benchtime),
		    speed,
		    failed
        );
    }
    return i;
}

void benchcore(const char *host,const int port,const char *req)
{
    int rlen;
    char buf[1500];
    int s,i;
    //定义信号结构体
    struct sigaction sa;

    /* setup alarm signal handler */
    //设置接收到信号执行的回调函数
    sa.sa_handler=alarm_handler;
    sa.sa_flags=0;
    //监听SIGALRM信号,
    //若监听到此信号则执行alarm_handler回调函数,若返回值不为0则表示回调函数执行失败
    if(sigaction(SIGALRM,&sa,NULL)) {
        exit(3);
    }
    //设置单次定时器
    alarm(benchtime);
    rlen=strlen(req);
    nexttry:while(1) {
        //若超时时间到则结束
        if(timerexpired) {
            if(failed>0) {
                /* fprintf(stderr,"Correcting failed by signal\n"); */
                failed--;
            }
            return;
        }
        //连接http服务器
        s=Socket(host,port);
        //连接失败则失败数加1        
        if(s<0) { 
            failed++;
            continue;
        } 
        //往http服务器写入数据,写入数据长度少于req长度则失败数加1
        if(rlen!=write(s,req,rlen)) {
            failed++;
            close(s);
            continue;
        }
        //当http协议是0.9类型的时候
        if(http10==0) { 
            //若关闭http通讯失败则失败数加1
	        if(shutdown(s,1)) { 
                failed++;
                close(s);
                continue;
            }
            //若没有设置不等待回复则需要强制等待http服务器的回复
            if(force==0) {
                /* read all available data from socket */
	            while(1) {
                    //当等待超时的时候timerexpired会被设置为1
                    if(timerexpired) {
                        break; 
                    }
                    //阻塞读取http服务器返回的数据
	                i=read(s,buf,1500);
                    /* fprintf(stderr,"%d\n",i); */
                    //读取数据失败,失败数加1且返回到循环开头重新开始下一次测试
	                if(i<0) {
                        failed++;
                        close(s);
                        goto nexttry;
                    }
                }
            } else {
                if(i==0) {
                    break;
                }
		        else{
			       bytes+=i;
                }
	        }
        }
        if(close(s)) {
            failed++;
            continue;
        }
        speed++;
    }
}
