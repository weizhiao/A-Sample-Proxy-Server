#include<stdio.h>
#include<Windows.h>
#include <process.h>
#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65520 //发送数据报文的最大长度
#define CACHESIZE 20 //缓存大小为20
#define DATESIZE 100 //储存时间的空间大小为100
#define URLSIZE 1024 //储存url的空间大小为1024字节
#define MAXBLACKHOSTSIZE 20 //网站黑名单最大为20
#define MAXBLACKIP 20 //用户黑名单最大为20

struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

struct HttpRequestHeader {
	char method[10]; // POST 或者 GET
	char url[URLSIZE]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
};

struct HttpResponseHeader {
	char status[4];	//状态码
	char date[DATESIZE]; //日期
};

struct HttpCache {
	int count; //最近使用的次数
	char date[DATESIZE]; //日期
	char url[URLSIZE];	//url
	char text[MAXSIZE]; //内容
};

struct BlackHost {
	char host[1024];
};

struct BlackHostList {
	int num;
	struct BlackHost list[MAXBLACKHOSTSIZE];
};

struct BlackIP {
	char ip[30];
};

struct BlackIPList {
	int num;
	struct BlackIP list[MAXBLACKIP];
};


//程序所需的全局变量
const int ProxyPort = 8080;		//代理服务器监听端口
SOCKET ProxyServer;				//监听端口的套接字
struct sockaddr_in ProxyServerAddr;	//代理服务器的本机信息
struct HttpCache httpCache[CACHESIZE];	//代理服务的缓存
struct BlackHostList blackHostList;		//网站黑名单
struct BlackIPList blackIPList;			//用户黑名单
char RedirectAddr[URLSIZE];				//重定向地址

//初始化网站黑名单
void InitBlackHostList() {
	ZeroMemory(&blackHostList, sizeof(blackHostList));
}

//初始化重定向网址
void InitRedirectAddr() {
	ZeroMemory(RedirectAddr, sizeof(RedirectAddr));
}

//添加重定向网址
void AddRedirectAddr(char*url) {
	memcpy(RedirectAddr, url, strlen(url) + 1);
}

//判断是否进行重定向
int IfRedirect(char*url) {
	if (RedirectAddr[0] == '\0') {
		return FALSE;
	}
	else if (!strcmp(url, RedirectAddr)) {
		return FALSE;
	}
	return TRUE;
}

//构造钓鱼http响应头
void CreateHttpHeader(char *header,char*url) {
	char str[100] = "HTTP/1.1 302 Move temporarily";
	int current = 0;
	memcpy(header, str,strlen(str));
	current += strlen(str);
	memcpy(&header[current], "\r\n", 2);
	current += 2;
	char str1[100+URLSIZE] = "Location: ";
	strcat_s(str1, sizeof(str1), url);
	memcpy(&header[current],str1,strlen(str1));
	current += strlen(str1);
	memcpy(&header[current], "\r\n\r\n", 5);
}

//添加网站黑名单
void AddBlackHost(char* host) {
	if (blackHostList.num < MAXBLACKHOSTSIZE) {
		HOSTENT* hostent = gethostbyname(host);
		printf("标准主机名%s已添加到黑名单\n", hostent->h_name);
		memcpy(blackHostList.list[blackHostList.num++].host, hostent->h_name, strlen(host));
	}
	else {
		printf("标准主机名%s添加黑名单失败\n", host);
	}
}

//查找网站是否在黑名单里
int FindinBlackHost(char*host) {
	HOSTENT* hostent = gethostbyname(host);
	for (int i = 0; i < blackHostList.num; i++) {
		if (!strcmp(blackHostList.list[i].host, hostent->h_name)) {
			return TRUE;
		}
	}
	return FALSE;
}

//初始化用户黑名单
void InitBlackIPList() {
	ZeroMemory(&blackIPList, sizeof(blackIPList));
}

//添加用户黑名单
void AddBlackIP(char* ip) {
	if (blackIPList.num < MAXBLACKHOSTSIZE) {
		printf("用户%s已添加到黑名单\n", ip);
		memcpy(blackIPList.list[blackIPList.num++].ip, ip, strlen(ip));
	}
	else {
		printf("用户%s添加黑名单失败\n", ip);
	}
}

//查找用户是否在黑名单里
int FindinBlackIP(char* ip) {
	for (int i = 0; i < blackIPList.num; i++) {
		if (!strcmp(blackIPList.list[i].ip, ip)) {
			return TRUE;
		}
	}
	return FALSE;
}

//在缓存中寻找对应的URL
int FindURL(char*url) {
	for (int i = 0; i < CACHESIZE; i++) {
		if (!strcmp(url, httpCache[i].url)) {
			httpCache[i].count++;
			return i;
		}
	}
	return -1;
}

//添加缓存
void AddCache(const char*url,const char*buffer,const char*date) {
	int index = -1;
	int min_count = MAXINT;
	for (int i = 0; i < CACHESIZE; i++) {
		if (httpCache[i].count == 0) {
			index = i;
			break;
		}
		if (min_count > httpCache[i].count) {
			index = i;
			min_count = httpCache[i].count;
		}
	}
	httpCache[index].count=1;
	memcpy(httpCache[index].url, url, 1024);
	memcpy(httpCache[index].text, buffer, MAXSIZE);
	memcpy(httpCache[index].date, date, DATESIZE);
}

//更新缓存
void UpdateCache(int index, struct HttpResponseHeader* httpResponseHeader,const char*buffer) {
	memcpy(httpCache[index].text, buffer, MAXSIZE);
	memcpy(httpCache[index].date, httpResponseHeader->date, DATESIZE);
}

//对memcpy进行包装
void copy(void* dst,const void*src,size_t len) {
	memcpy(dst, src, len);
	((char*)dst)[len] = '\0';
}

//加载套接字库
int LoadDLL() {
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	return TRUE;
}

//初始化代理服务器的套接字端口
int InitSocket(){
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	//INADDR_ANY代表本机
	ProxyServerAddr.sin_addr.s_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//初始化缓存
int InitCache() {
	ZeroMemory(httpCache, sizeof(httpCache));
	return TRUE;
}

//解析请求HTTP头
void ParseRequestHttpHead(char* buffer, struct HttpRequestHeader* httpRequestHeader) {
	char* p;
	char* ptr=NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	if (p[0] == 'G') {//GET 方式
		printf("%s\n", p);
		copy(httpRequestHeader->method, "GET", 3);
		copy(httpRequestHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST 方式
		printf("%s\n", p);
		copy(httpRequestHeader->method, "POST", 4);
		copy(httpRequestHeader->url, &p[5], strlen(p) - 14);
	}
	else if (p[0] == 'C') {//CONNECT 方式
		copy(httpRequestHeader->method, "CONNECT", 7);
		copy(httpRequestHeader->url, &p[8], strlen(p) - 17);
	}
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			copy(httpRequestHeader->host, &p[6], strlen(p) - 6);
			for (int i = 0; i < strlen(httpRequestHeader->host); i++) {
				if (httpRequestHeader->host[i] == ':') {
					httpRequestHeader->host[i] = '\0';
				}
			}
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					copy(httpRequestHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//解析响应HTTP头
void ParseResponseHttpHead(char* buffer, struct HttpResponseHeader* httpResponseHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	copy(httpResponseHeader->status, &p[9], 3);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'D':
			copy(httpResponseHeader->date, &p[5], strlen(p) - 5);
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//修改请求报文
void ChangeRequest(char*old,char*new,int index) {
	int i;
	int current = 0;
	for (i = 0; old[i] != '\r'; i++);
	memcpy(new, old, i);
	current += i;
	memcpy(&new[current], &old[i], 2);
	i += 2;
	current += 2;
	char str[100] = "If-Modified-Since:";
	strcat_s(str, sizeof(str), httpCache[index].date);
	memcpy(&new[current], str, strlen(str));
	current += strlen(str);
	new[current++] = '\r';
	new[current++] = '\n';
	memcpy(&new[current], &old[i], strlen(old) - i);
}

//连接到服务器
int ConnectToServer(SOCKET* serverSocket, char* host) {
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(80);
	HOSTENT* hostent = gethostbyname(host);
	if (!hostent) {
		return FALSE;
	}
	struct in_addr Inaddr = *((struct in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		return FALSE;
	}
	return TRUE;
}

//处理线程
unsigned int __stdcall ProxyThread(struct ProxyParam* lpParameter) {
	char Buffer[MAXSIZE];
	ZeroMemory(Buffer, MAXSIZE);
	char* CacheBuffer;
	char* not_modified=NULL;
	char* modified=NULL;
	int recvSize;
	int ret;
	recvSize = recv(lpParameter->clientSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	//用于储存请求报文的状态
	struct HttpRequestHeader* httpRequestHeader = malloc(sizeof(struct HttpRequestHeader));
	//用于储存响应报文的状态
	struct HttpResponseHeader* httpResponseHeader = malloc(sizeof(struct HttpResponseHeader));
	CacheBuffer = malloc(sizeof(char) * (recvSize + 1));
	not_modified = malloc(sizeof(char) * (recvSize + 1));
	memcpy(CacheBuffer, Buffer, recvSize);
	memcpy(not_modified, Buffer, recvSize);
	ParseRequestHttpHead(CacheBuffer, httpRequestHeader);
	//忽略CONNECT报文
	if (!strcmp("CONNECT", httpRequestHeader->method)) {
		goto error;
	}
	//检测黑名单
	if (FindinBlackHost(httpRequestHeader->host)) {
		printf("禁止访问%s\n", httpRequestHeader->host);
		goto error;
	}
	//判断是否重定向
	if (IfRedirect(httpRequestHeader->url)) {
		printf("正在重定向到%s\n", RedirectAddr);
		CreateHttpHeader(Buffer, RedirectAddr);
		ret = send(((struct ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
		goto error;
	}
	printf("HOST:%s\n", httpRequestHeader->host);
	free(CacheBuffer);
	if (!ConnectToServer(&lpParameter->serverSocket, httpRequestHeader->host)) {
		goto error;
	}
	printf("代理连接主机 %s 成功\n", httpRequestHeader->host);
	//判断是否缓存
	int index=FindURL(httpRequestHeader->url);
	if (index == -1) {
		printf("URL:%s\n未缓存,正在请求源服务器...\n", httpRequestHeader->url);
		//将客户端发送的 HTTP 数据报文直接转发给目标服务器
		ret = send(((struct ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
		//等待目标服务器返回数据
		recvSize = recv(((struct ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
		CacheBuffer= malloc(sizeof(char) * (recvSize + 1));
		memcpy(CacheBuffer, Buffer, recvSize);
		if (recvSize <= 0) {
			goto error;
		}
		ParseResponseHttpHead(CacheBuffer, httpResponseHeader);
		free(CacheBuffer);
		AddCache(httpRequestHeader->url, Buffer,httpResponseHeader->date);
		//将目标服务器返回的数据直接转发给客户端
		ret = send(((struct ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	}
	else {
		printf("URL:%s\n已缓存,正在检查是否为最新版本...\n", httpRequestHeader->url);
		modified = malloc(sizeof(char) * (strlen(not_modified)+100));
		ChangeRequest(not_modified, modified, index);
		ret = send(((struct ProxyParam*)lpParameter)->serverSocket, modified, strlen(modified) + 1, 0);
		free(modified);
		recvSize = recv(((struct ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
		if (recvSize <= 0) {
			goto error;
		}
		ParseResponseHttpHead(Buffer, httpResponseHeader);
		if (!strcmp(httpResponseHeader->status, "304")) {
			printf("是最新版本，正在由代理服务器发送...\n");
			ret = send(((struct ProxyParam*)lpParameter)->clientSocket, httpCache[index].text, MAXSIZE, 0);
		}
		else {
			printf("不是最新版本，正在由目标服务器发送...\n");
			UpdateCache(index, httpResponseHeader, Buffer);
			ret = send(((struct ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
		}
	}
	//错误处理
error:
	Sleep(200);
	closesocket(((struct ProxyParam*)lpParameter)->clientSocket);
	closesocket(((struct ProxyParam*)lpParameter)->serverSocket);
	free(lpParameter);
	free(not_modified);
	_endthreadex(0);
	return 0;
}

int main() {
	printf("正在加载套接字库...\n");
	if (LoadDLL() == FALSE) {
		printf("加载套接字库失败\n");
		return -1;
	}
	printf("加载成功！\n");
	printf("正在初始化代理服务器...\n");
	if (InitSocket() == FALSE) {
		printf("初始化代理服务器的套接字端口失败\n");
		return -1;
	}
	printf("初始化完成！\n");
	printf("正在初始化缓存...\n");
	if (InitCache() == FALSE) {
		printf("初始化缓存失败\n");
		return -1;
	}
	printf("初始化完成！\n");
	InitBlackHostList();
	//AddBlackHost("today.hit.edu.cn");
	InitBlackIPList();
	//AddBlackIP("127.0.0.1");
	InitRedirectAddr();
	AddRedirectAddr("http://today.hit.edu.cn/");
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	HANDLE hThread;
	struct ProxyParam* CurrentProxy;
	struct sockaddr_in peerAddr;
	int peerLen=sizeof(peerAddr);
	while (1) {
		acceptSocket = accept(ProxyServer, NULL,NULL);
		getpeername(acceptSocket, (struct sockaddr*)&peerAddr, &peerLen);
		CurrentProxy = malloc(sizeof(struct ProxyParam));
		if (FindinBlackIP(inet_ntoa(peerAddr.sin_addr))) {
			printf("禁止%s访问外部网站\n", inet_ntoa(peerAddr.sin_addr));
			continue;
		}
		CurrentProxy->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0,&ProxyThread, (void*)CurrentProxy, 0, 0);
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}