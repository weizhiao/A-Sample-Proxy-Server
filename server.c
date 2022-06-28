#include<stdio.h>
#include<Windows.h>
#include <process.h>
#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65520 //�������ݱ��ĵ���󳤶�
#define CACHESIZE 20 //�����СΪ20
#define DATESIZE 100 //����ʱ��Ŀռ��СΪ100
#define URLSIZE 1024 //����url�Ŀռ��СΪ1024�ֽ�
#define MAXBLACKHOSTSIZE 20 //��վ���������Ϊ20
#define MAXBLACKIP 20 //�û����������Ϊ20

struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

struct HttpRequestHeader {
	char method[10]; // POST ���� GET
	char url[URLSIZE]; // ����� url
	char host[1024]; // Ŀ������
	char cookie[1024 * 10]; //cookie
};

struct HttpResponseHeader {
	char status[4];	//״̬��
	char date[DATESIZE]; //����
};

struct HttpCache {
	int count; //���ʹ�õĴ���
	char date[DATESIZE]; //����
	char url[URLSIZE];	//url
	char text[MAXSIZE]; //����
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


//���������ȫ�ֱ���
const int ProxyPort = 8080;		//��������������˿�
SOCKET ProxyServer;				//�����˿ڵ��׽���
struct sockaddr_in ProxyServerAddr;	//����������ı�����Ϣ
struct HttpCache httpCache[CACHESIZE];	//�������Ļ���
struct BlackHostList blackHostList;		//��վ������
struct BlackIPList blackIPList;			//�û�������
char RedirectAddr[URLSIZE];				//�ض����ַ

//��ʼ����վ������
void InitBlackHostList() {
	ZeroMemory(&blackHostList, sizeof(blackHostList));
}

//��ʼ���ض�����ַ
void InitRedirectAddr() {
	ZeroMemory(RedirectAddr, sizeof(RedirectAddr));
}

//����ض�����ַ
void AddRedirectAddr(char*url) {
	memcpy(RedirectAddr, url, strlen(url) + 1);
}

//�ж��Ƿ�����ض���
int IfRedirect(char*url) {
	if (RedirectAddr[0] == '\0') {
		return FALSE;
	}
	else if (!strcmp(url, RedirectAddr)) {
		return FALSE;
	}
	return TRUE;
}

//�������http��Ӧͷ
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

//�����վ������
void AddBlackHost(char* host) {
	if (blackHostList.num < MAXBLACKHOSTSIZE) {
		HOSTENT* hostent = gethostbyname(host);
		printf("��׼������%s����ӵ�������\n", hostent->h_name);
		memcpy(blackHostList.list[blackHostList.num++].host, hostent->h_name, strlen(host));
	}
	else {
		printf("��׼������%s��Ӻ�����ʧ��\n", host);
	}
}

//������վ�Ƿ��ں�������
int FindinBlackHost(char*host) {
	HOSTENT* hostent = gethostbyname(host);
	for (int i = 0; i < blackHostList.num; i++) {
		if (!strcmp(blackHostList.list[i].host, hostent->h_name)) {
			return TRUE;
		}
	}
	return FALSE;
}

//��ʼ���û�������
void InitBlackIPList() {
	ZeroMemory(&blackIPList, sizeof(blackIPList));
}

//����û�������
void AddBlackIP(char* ip) {
	if (blackIPList.num < MAXBLACKHOSTSIZE) {
		printf("�û�%s����ӵ�������\n", ip);
		memcpy(blackIPList.list[blackIPList.num++].ip, ip, strlen(ip));
	}
	else {
		printf("�û�%s��Ӻ�����ʧ��\n", ip);
	}
}

//�����û��Ƿ��ں�������
int FindinBlackIP(char* ip) {
	for (int i = 0; i < blackIPList.num; i++) {
		if (!strcmp(blackIPList.list[i].ip, ip)) {
			return TRUE;
		}
	}
	return FALSE;
}

//�ڻ�����Ѱ�Ҷ�Ӧ��URL
int FindURL(char*url) {
	for (int i = 0; i < CACHESIZE; i++) {
		if (!strcmp(url, httpCache[i].url)) {
			httpCache[i].count++;
			return i;
		}
	}
	return -1;
}

//��ӻ���
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

//���»���
void UpdateCache(int index, struct HttpResponseHeader* httpResponseHeader,const char*buffer) {
	memcpy(httpCache[index].text, buffer, MAXSIZE);
	memcpy(httpCache[index].date, httpResponseHeader->date, DATESIZE);
}

//��memcpy���а�װ
void copy(void* dst,const void*src,size_t len) {
	memcpy(dst, src, len);
	((char*)dst)[len] = '\0';
}

//�����׽��ֿ�
int LoadDLL() {
	WORD wVersionRequested;
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
		printf("���� winsock ʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("�����ҵ���ȷ�� winsock �汾\n");
		WSACleanup();
		return FALSE;
	}
	return TRUE;
}

//��ʼ��������������׽��ֶ˿�
int InitSocket(){
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("�����׽���ʧ�ܣ��������Ϊ��%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	//INADDR_ANY������
	ProxyServerAddr.sin_addr.s_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("���׽���ʧ��\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("�����˿�%d ʧ��", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//��ʼ������
int InitCache() {
	ZeroMemory(httpCache, sizeof(httpCache));
	return TRUE;
}

//��������HTTPͷ
void ParseRequestHttpHead(char* buffer, struct HttpRequestHeader* httpRequestHeader) {
	char* p;
	char* ptr=NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//��ȡ��һ��
	if (p[0] == 'G') {//GET ��ʽ
		printf("%s\n", p);
		copy(httpRequestHeader->method, "GET", 3);
		copy(httpRequestHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST ��ʽ
		printf("%s\n", p);
		copy(httpRequestHeader->method, "POST", 4);
		copy(httpRequestHeader->url, &p[5], strlen(p) - 14);
	}
	else if (p[0] == 'C') {//CONNECT ��ʽ
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

//������ӦHTTPͷ
void ParseResponseHttpHead(char* buffer, struct HttpResponseHeader* httpResponseHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//��ȡ��һ��
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

//�޸�������
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

//���ӵ�������
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

//�����߳�
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
	//���ڴ��������ĵ�״̬
	struct HttpRequestHeader* httpRequestHeader = malloc(sizeof(struct HttpRequestHeader));
	//���ڴ�����Ӧ���ĵ�״̬
	struct HttpResponseHeader* httpResponseHeader = malloc(sizeof(struct HttpResponseHeader));
	CacheBuffer = malloc(sizeof(char) * (recvSize + 1));
	not_modified = malloc(sizeof(char) * (recvSize + 1));
	memcpy(CacheBuffer, Buffer, recvSize);
	memcpy(not_modified, Buffer, recvSize);
	ParseRequestHttpHead(CacheBuffer, httpRequestHeader);
	//����CONNECT����
	if (!strcmp("CONNECT", httpRequestHeader->method)) {
		goto error;
	}
	//��������
	if (FindinBlackHost(httpRequestHeader->host)) {
		printf("��ֹ����%s\n", httpRequestHeader->host);
		goto error;
	}
	//�ж��Ƿ��ض���
	if (IfRedirect(httpRequestHeader->url)) {
		printf("�����ض���%s\n", RedirectAddr);
		CreateHttpHeader(Buffer, RedirectAddr);
		ret = send(((struct ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
		goto error;
	}
	printf("HOST:%s\n", httpRequestHeader->host);
	free(CacheBuffer);
	if (!ConnectToServer(&lpParameter->serverSocket, httpRequestHeader->host)) {
		goto error;
	}
	printf("������������ %s �ɹ�\n", httpRequestHeader->host);
	//�ж��Ƿ񻺴�
	int index=FindURL(httpRequestHeader->url);
	if (index == -1) {
		printf("URL:%s\nδ����,��������Դ������...\n", httpRequestHeader->url);
		//���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
		ret = send(((struct ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
		//�ȴ�Ŀ���������������
		recvSize = recv(((struct ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
		CacheBuffer= malloc(sizeof(char) * (recvSize + 1));
		memcpy(CacheBuffer, Buffer, recvSize);
		if (recvSize <= 0) {
			goto error;
		}
		ParseResponseHttpHead(CacheBuffer, httpResponseHeader);
		free(CacheBuffer);
		AddCache(httpRequestHeader->url, Buffer,httpResponseHeader->date);
		//��Ŀ����������ص�����ֱ��ת�����ͻ���
		ret = send(((struct ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	}
	else {
		printf("URL:%s\n�ѻ���,���ڼ���Ƿ�Ϊ���°汾...\n", httpRequestHeader->url);
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
			printf("�����°汾�������ɴ������������...\n");
			ret = send(((struct ProxyParam*)lpParameter)->clientSocket, httpCache[index].text, MAXSIZE, 0);
		}
		else {
			printf("�������°汾��������Ŀ�����������...\n");
			UpdateCache(index, httpResponseHeader, Buffer);
			ret = send(((struct ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
		}
	}
	//������
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
	printf("���ڼ����׽��ֿ�...\n");
	if (LoadDLL() == FALSE) {
		printf("�����׽��ֿ�ʧ��\n");
		return -1;
	}
	printf("���سɹ���\n");
	printf("���ڳ�ʼ�����������...\n");
	if (InitSocket() == FALSE) {
		printf("��ʼ��������������׽��ֶ˿�ʧ��\n");
		return -1;
	}
	printf("��ʼ����ɣ�\n");
	printf("���ڳ�ʼ������...\n");
	if (InitCache() == FALSE) {
		printf("��ʼ������ʧ��\n");
		return -1;
	}
	printf("��ʼ����ɣ�\n");
	InitBlackHostList();
	//AddBlackHost("today.hit.edu.cn");
	InitBlackIPList();
	//AddBlackIP("127.0.0.1");
	InitRedirectAddr();
	AddRedirectAddr("http://today.hit.edu.cn/");
	printf("����������������У������˿� %d\n", ProxyPort);
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
			printf("��ֹ%s�����ⲿ��վ\n", inet_ntoa(peerAddr.sin_addr));
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