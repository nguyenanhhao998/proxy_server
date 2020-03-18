// Proxy_Server.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "Proxy_Server.h"
#include "pch.h"
#include "stdafx.h"
#include "afxsock.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#include <fstream>
#include <string>
#include <sstream>
#include <vector>

#define PORT 8888
#define bufsize 2048
// The one and only application object

CWinApp theApp;

using namespace std;

//struct contains information of the query
struct Query
{
	string protocol;
	string method;
	string host;
	string page;
};

//declare function

//thread proxy_handling
DWORD WINAPI proxy_handling(LPVOID arg);

//Load blacklist
int Load_blacklist(vector<string>& blacklist);

//Receive query from client, return size of query received or -1 if has error
int ReceiveClient(CSocket &s_client, char*& query);

//Send the query to web server
// input: socket Proxy to Server connected to Server, server IP, query and query_size
// output: number of bytes sent, or -1 if failed
int SendServer(CSocket &s_pro_to_ser, char* ip, char* query, int query_size);

//Receive response from the web server
// input: socket Proxy to Server connected to Server
// output: response's size, -1 if failed
int ReceiveServer(CSocket &s_pro_to_ser, char*& Server_rep);

//Send server's response from client, return size of query sent or -1 if has error
int SendClient(CSocket &s_client, char* Server_rep, int SR_size);

//Function: check if the web server response is already in cache
//Input: struct Query, Cache_Res to get response if it's in cache, file_name to store name of file containing data
//Output: size of response, -1 if it hasn't been cached, -2 if Q_query is not a GET query
int isInCache(Query Q_query, char*& Cache_Res, string &file_name);

//Store web server's pack into cache
bool Caching(Query Q_query, char* Server_Rep, int SR_size);

//Add Condition to the GET query
void AddCondition(char* &Query, int & Query_size, char* Condition);
//Get If-Modified-Since from Cache pack
string get_If_since_modified(char* StrCache);
//Check Is Not Modified
bool IsNotModified(char* Res, int Res_size);
//Update Cache
int UpdateCache(string filename, Query Q_query, char* Res, int Res_size);
//Check Cache Condition before store the web server's response in cache
bool Check_CacheCondition(char* Res, int Res_size);

//move char* Query to struct Query
void toQuery(Query &Q, char* strQ);
//get content length from query. Return -1 if it dosen't have content-length header
int getContent_Length(char* query);
//get query header
char* get_query_header(char* query);
//convert string to char*
char* Stringtochar(string s);
//find index of substring in string
int index_strstr(char* str, const char* sub);
//get ip address from domain name
char *get_ip(const char *host);
//convert char* to wchar*
wchar_t *convertCharArrayToLPCWSTR(const char* charArray);
//append string that have null character
char* Append_nstring(char* &des, int des_size, char* src, int src_size);
//determine end of chunked encoding
bool isLastchunk(char* s, int size);
//Convert unsinged int to string
string Convert_int_to_string(int num);
//Legalize filename in windows
void Leglizefn(string &filename);
//Split query/response header
vector <vector<string>> Split(char* strQ);

vector<string> blacklist; //load from "blacklist.conf" file
//Response when donmain in blacklist
char* ResForbidden = "HTTP/1.0 403 Forbidden\r\n\Cache-Control: no-cache\r\n\Connection: close\r\n\r\n<html>403 Forbidden<html>";

int main()
{
	int nRetCode = 0;

	HMODULE hModule = ::GetModuleHandle(nullptr);

	if (hModule != nullptr)
	{
		// initialize MFC and print and error on failure
		if (!AfxWinInit(hModule, nullptr, ::GetCommandLine(), 0))
		{
			// TODO: code your application's behavior here.
			wprintf(L"Fatal Error: MFC initialization failed\n");
			nRetCode = 1;
		}
		else
		{
			//TODO: code your application's behavior here.
			//Init Windows socket library
			if (AfxSocketInit(NULL) == false)
			{
				cout << "Could not initialize socket library" << endl;
				exit(0);
			}
			//Declare
			CSocket s_proxy;
			//Init Proxy socket
			if (s_proxy.Create(PORT, SOCK_STREAM, NULL) == 0)
			{
				cout << "Proxy Socket Initialization failed" << endl;
				cout << s_proxy.GetLastError();
				exit(0);
			}
			//Load blacklist from "blacklist.conf"
			Load_blacklist(blacklist);
			CSocket s_client;
			DWORD proxy_handlingThreadID;
			HANDLE h_proxy_handlingThread;
			while (true)
			{
				//Listen to client
				if (s_proxy.Listen(100) == 0)
				{
					cout << "Cannot listen in this port" << endl;
					s_proxy.Close();
					exit(0);
				}
				else cout << "PROXY is LISTENING to clients" << endl;
				//Accepted client
				if (s_proxy.Accept(s_client))
				{
					cout << "ACCEPTED a client" << endl;
				}
				else
				{
					cout << "FAILED to ACCEPT a client" << endl;
					continue;
				}
				SOCKET* De_s_client= new SOCKET();
				*De_s_client = s_client.Detach();
				h_proxy_handlingThread = CreateThread(NULL, 0, proxy_handling, De_s_client, 0, &proxy_handlingThreadID);
				cout << "Done a client" << endl;

			}
		}	
	}
	else
	{
		// TODO: change error code to suit your needs
		wprintf(L"Fatal Error: GetModuleHandle failed\n");
		nRetCode = 1;
	}

	return nRetCode;

}

//Convert string to char*
char* Stringtochar(string s)
{
	const char* s_pattern = s.c_str();
	int len = strlen(s_pattern);
	char* char_s = new char[len + 1];
	strcpy_s(char_s, len+1, s_pattern);
	return char_s;
}
//move char* Query to struct Query
void toQuery(Query &Q, char* strQ)
{
	if (strQ==NULL || strlen(strQ) == 0) return;
	vector <vector<string>> list = Split(strQ);
	//Get protocol
	if (list[0].size()>2) Q.protocol = list[0][2];
	//Get method
	Q.method = list[0][0];
	if (Q.method != "GET" && Q.method != "POST") return;
	//Get host
	for (int i = 0; i < list.size(); i++)
	{
		if (list[i].size() > 0 && list[i][0] == "Host:")
		{
			Q.host = list[i][1];
			break;
		}
	}
	//Get page
	if (list[0].size() > 1)
	{
		int begin = list[0][1].find(Q.host.c_str());
		if (begin != string::npos) begin += Q.host.length();
		else begin = 0;
		Q.page = list[0][1].substr(begin);
	}
}
vector <vector<string>> Split(char* strQ)
{
	vector <vector<string>> list;
	stringstream sstrQ(strQ);
	string line, to;
	while (getline(sstrQ, line))
	{
		vector <string> list_token;
		stringstream streamline;
		int size=line.size();
		if (size > 0 && line[size - 1] == '\r') line.resize(size - 1);
		streamline << line;
		while (getline(streamline, to, ' '))
		{
			list_token.push_back(to);
		}
		list.push_back(list_token);
	}
	return list;
}
//get ip address from domain name
//Ref: https://cboard.cprogramming.com/c-programming/149613-ip-address-using-getaddrinfo.html
char *get_ip(const char *host)
{
	//struct hostent *hent;
	int iplen = 15; //XXX.XXX.XXX.XXX
	char *ip = (char *)malloc(iplen + 1);
	memset(ip, 0, iplen + 1);

	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // use AF_INET6 to force IPv6
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(host, "http", &hints, &servinfo)) != 0) {
		cout << "Can't get address info" << endl;
		return NULL;
	}

	// loop through all the results and connect to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if (p->ai_family == AF_INET)
		{
			in_addr  *addr;
			sockaddr_in *ipv = (sockaddr_in *)p->ai_addr;
			addr = (in_addr *)&(ipv->sin_addr);
			if (inet_ntop(AF_INET, addr, ip, iplen) == NULL)
			{
				continue;
			}
			break; // if we get here, we must have connected successfully
		}
		
	}

	if (p == NULL) {
		// looped off the end of the list with no connection
		cout << "failed to get ip" << endl;
		return NULL;
	}
	freeaddrinfo(servinfo);
	return ip;
}
//Ref: http://stackoverflow.com/questions/19715144/how-to-convert-char-to-lpcwstr
wchar_t *convertCharArrayToLPCWSTR(const char* charArray)
{
	wchar_t* wString = new wchar_t[4096];
	MultiByteToWideChar(CP_ACP, 0, charArray, -1, wString, 4096);
	return wString;
}

//append string that have null character
char* Append_nstring(char* &des, int des_size, char* src, int src_size)
{
	des = (char*)realloc(des, des_size + src_size);
	for (int i = des_size; i < des_size + src_size; i++)
	{
		des[i] = src[i - des_size];
	}
	return des;
}
//check the last chunked
bool isLastchunk(char* s, int size)
{
	if (size < 7) return true;
	if (s[size-7]=='\r' && s[size-6]=='\n' && s[size - 5] == '0' && s[size - 4] == '\r' && s[size - 3] == '\n' && s[size - 2] == '\r'&& s[size - 1] == '\n')
		return true;
	return false;
}
//find index of substring in string
int index_strstr(char* str, const char* sub)
{
	char* temp;
	if ((temp = strstr(str, sub)) != NULL)
		return temp - str;
	return -1;
}
//get content length from query. Return -1 if it dosen't have content-length header
int getContent_Length(char* query)
{
	int ifind = index_strstr(query, "Content-Length");
	if (ifind != -1)
	{
		ifind = ifind + 16;
		//determine the content's length
		int cont_len = 0;
		while (query[ifind] >= '0' && query[ifind] <= '9')
		{
			cont_len = cont_len * 10 + query[ifind] - 48;
			ifind++;
		}
		return cont_len;
	}
	else return -1;
}
//get query header
char* get_query_header(char* query)
{
	int end = index_strstr(query, "\r\n\r\n") - 1;
	if (end < 0) return NULL;
	char* head = new char[end + 2];
	for (int i = 0; i <= end; i++)
	{
		head[i] = query[i];
	}
	head[end + 1] = 0;
	return head;
}
DWORD WINAPI proxy_handling(LPVOID arg)
{
	//convert arg to csoket
	SOCKET* de_s_client = (SOCKET*)arg;
	CSocket s_client, s_pro_to_ser;
	s_client.Attach(*de_s_client);

	int tmpres;
	char buf[bufsize + 1];
	char* query = NULL;
	//Receive query from client
	int query_size = ReceiveClient(s_client, query);
	if (query_size <= 0) return 0;
	//Get query's header from query
	char* query_header = get_query_header(query);
	if (query_header == NULL) {
		s_client.Close();
		return 0;
	}
	//Move query string to struct Query
	Query Q_query;
	toQuery(Q_query, query_header);
	//Check domain in black list
	int bllsize = blacklist.size();
	for (int i = 0; i < bllsize; i++)
	{
		if (blacklist[i] == Q_query.host)
		{
			SendClient(s_client, ResForbidden, strlen(ResForbidden));
			s_client.Close();
			return 1;
		}
	}
	if (Q_query.method != "GET" && Q_query.method != "POST")
	{
		s_client.Close();
		return 0;
	}
	//Get host's ip from Q_query
	char* ip = get_ip(Q_query.host.c_str());
	if (ip == NULL)
	{
		s_client.Close();
		return 0;
	}
	//Connect to Server
	if (s_pro_to_ser.Create() == 0)
	{
		s_client.Close();
		return 0;
	}
	if (s_pro_to_ser.Connect(convertCharArrayToLPCWSTR(ip), 80) == 0)
	{
		s_client.Close();
		s_pro_to_ser.Close();
		return 0;
	}
	//Check if the web's response is already in Cache
	char* Cache_Res = NULL;
	string filename;
	int CR_size = isInCache(Q_query, Cache_Res, filename);
	if (CR_size == -1 || CR_size == -2) //If haven't cached or query's method == "POST"
	{
		//Send the query to server
		if (SendServer(s_pro_to_ser, ip, query, query_size) == -1)
		{
			s_client.Close();
			return 0;
		}
		//Receive response from Web server
		char* Server_rep = NULL;
		int SR_size = ReceiveServer(s_pro_to_ser, Server_rep);
		if (SR_size <= 0)
		{
			s_pro_to_ser.Close();
			s_client.Close();
			return 0;
		}
		//store the Web's Response in cache if it's a GET query's reponse
		if (CR_size == -1)
		{
			if (Check_CacheCondition(Server_rep, SR_size)) //check the cache condition before caching
				Caching(Q_query, Server_rep, SR_size);
		}
		//Send the response to client's browser
		if (SendClient(s_client, Server_rep, SR_size) == -1)
		{
			s_client.Close();
			s_pro_to_ser.Close();
			return 0;
		}
	}
	else
	{
		//Add condition to GET query
		string Condition = get_If_since_modified(Cache_Res);
		AddCondition(query, query_size, Stringtochar(Condition));
		//Send Conditional GET query to Server
		if (SendServer(s_pro_to_ser, ip, query, query_size) == -1)
		{
			s_client.Close();
			return 0;
		}
		//Receive response from Web server
		char* Server_rep = NULL;
		int SR_size = ReceiveServer(s_pro_to_ser, Server_rep);
		if (SR_size <= 0)
		{
			s_pro_to_ser.Close();
			s_client.Close();
			return 0;
		}
		//Check Is Not Modified
		if (IsNotModified(Server_rep, SR_size))
		{

			//Send the data in cache file to client's browser
			if (SendClient(s_client, Cache_Res, CR_size) == -1)
			{
				s_client.Close();
				s_pro_to_ser.Close();
				return 0;
			}
		}
		else
		{
			//Send the response to client's browser
			if (SendClient(s_client, Server_rep, SR_size) == -1)
			{
				s_client.Close();
				s_pro_to_ser.Close();
				return 0;
			}
			//Update Cache
			UpdateCache(filename, Q_query, Server_rep, SR_size);
		}
	}
	s_client.Close();
	s_pro_to_ser.Close();
	return 1;
}
//Receive query from client, return size of query received or -1 if has error
int ReceiveClient(CSocket &s_client, char*& query)
{
	char buf[bufsize + 1];
	int tmpres = 0;
	memset(buf, 0, bufsize); //set all buf's elements to null 
	query = NULL; //buffer to receive query
	int query_size = 0; // size of query
	int cont_len = -1; // content length (if it's a POST query)
	int current = 0; // current bytes of content received 
	while (true)
	{
		tmpres = s_client.Receive(buf, bufsize, 0); //Receive query, bufsize=1024 bytes each time
		if (tmpres <= 0) //Error receiving
		{
			s_client.Close();
			return -1;
		}
		Append_nstring(query, query_size, buf, tmpres); //Append buf to current query
		query_size += tmpres; //update query_size
		if (cont_len == -1) //if haven't found Content-Length
		{
			cont_len = getContent_Length(buf);
			if (cont_len != -1) //find Content-Length (POST)
			{
				int begin = index_strstr(buf, "\r\n\r\n") + 4; //determine begin of content
				//update current recieved
				current = tmpres - begin; //update current bytes of content
				if (current == cont_len) break;
			}
			else //no Content-Length (GET), break when find \r\n\r\n in buf
			{
				if (strstr(buf, "\r\n\r\n") != NULL) break;
			}
		}
		else //have already found Content-Length (POST)
		{
			current += tmpres; //update current bytes of content
			if (current == cont_len) break; //break when receive completely the content
		}
		memset(buf, 0, tmpres);
	}
	return query_size;
}
//Send the query to web server
// input: socket Proxy to Server perform the sending function, server IP, query and query_size
// output: number of bytes sent, or -1 if failed
int SendServer(CSocket &s_pro_to_ser, char* ip, char* query, int query_size)
{
	int sent = 0;
	int tmpres = 0;
	while (sent < query_size)
	{
		tmpres = s_pro_to_ser.Send(query + sent, query_size - sent, 0);
		if (tmpres <= 0) {
			s_pro_to_ser.Close();
			return -1;
		}
		sent += tmpres;
	}
	return sent;
}
//Function: Receive response from the web server
// input: socket Proxy to Server connected to Server
// output: response's size, -1 if failed
int ReceiveServer(CSocket &s_pro_to_ser, char*& Server_rep)
{
	char buf[bufsize + 1];
	Server_rep = NULL; //Server's response
	int SR_size = 0; //The number of bytes of response received
	int cont_len = 0; // the content's length (bytes)
	int current = 0; // current number of bytes of content which have been received
	int tmpres = 0, flag = 0;
	memset(buf, 0, sizeof(buf)); //reset the buffer
	while (true)
	{
		tmpres = s_pro_to_ser.Receive(buf, bufsize, 0);
		if (tmpres == 0)
		{
			break;
		}
		if (tmpres < 0)
		{
			return -1;
		}
		Append_nstring(Server_rep, SR_size, buf, tmpres);
		SR_size = SR_size + tmpres;
		if (flag == 0) //flag==0: first receive 
		{
			cont_len = getContent_Length(buf);
			if (cont_len != -1)
			{
				flag = 1;
				//determine the begin of content 
				int begin = index_strstr(buf, "\r\n\r\n") + 4;
				//update current recieved
				current = current + tmpres - begin;
				if (current == cont_len) break;
			}
			else
			{
				char* Ctl = strstr(buf, "Transfer-Encoding: chunked");
				if (Ctl != NULL)
				{
					flag = 2;
					if (isLastchunk(buf, tmpres) == 1) break;
				}
				else
				{
					flag = 3;
					if (tmpres < bufsize) break;
				}
			}
		}
		else if (flag == 1) //flag==1: header has Content-Length
		{
			current = current + tmpres;
			if (current == cont_len) break;
		}
		else if (flag == 2)//flag==2: chunked encoding
		{
			if (isLastchunk(buf, tmpres) == 1) break;
		}
		else if (flag == 3)
		{
			if (tmpres < bufsize) break;
		}
		memset(buf, 0, tmpres);
	}
	return SR_size;
}
//Function: Send server's response from client, return size of query sent or -1 if has error
int SendClient(CSocket &s_client, char* Server_rep, int SR_size)
{
	int sentc = 0, tmpres = 0;
	while (sentc < SR_size)
	{
		tmpres = s_client.Send(Server_rep + sentc, SR_size - sentc, 0);
		if (tmpres == -1) return -1;
		else sentc += tmpres;
	}
	return sentc;
}
//Function: check if the web server response is already in cache
//Input: struct Query, Cache_Res to get response if it's in cache, file_name to store name of file containing data
//Output: size of response, -1 if haven't cached, -2 if Q_query is not a GET
int isInCache(Query Q_query, char*& Cache_Res, string &file_name)
{
	Cache_Res = NULL;
	if (Q_query.method != "GET") return -2;
	fstream file;
	string filename = Q_query.method + Q_query.host + Q_query.page;
	Leglizefn(filename);
	filename = "Cache\\" + filename;
	if (filename.size() > 200) filename.resize(200);
	int filenamesize = filename.size();
	int i = 0;
	do
	{
		filename += Convert_int_to_string(i);
		file.open(filename.c_str(), ios_base::in|ios_base::binary);
		if (file.fail()) return -1;
		//Determine size of file
		file.seekg(0, ios_base::end);
		int len = file.tellg();
		file.seekg(0, ios_base::beg);
		//Read whole file into temp
		char* temp = new char[len];
		file.read(temp, len);
		//Check
		stringstream strstr;
		strstr << temp;
		string firstline;
		getline(strstr, firstline);
		if(firstline.size()>=1) firstline.resize(firstline.size() - 1);
		if (firstline==string(Q_query.method + Q_query.host + Q_query.page))
		{
			Cache_Res = temp + firstline.length() + 2;
			file.close();
			file_name = filename; //Store filename in file_name parameter
			return len - firstline.length() - 2;
		}
		//if not satisfy
		file.close();
		filename.resize(filenamesize);
		delete[] temp;
		i++;
	} while (true);
}
//Fucntion: store web server's pack into the cache
bool Caching(Query Q_query, char* Server_Rep, int SR_size)
{
	fstream file;
	string filename = Q_query.method + Q_query.host + Q_query.page;
	Leglizefn(filename);
	filename = "Cache\\" + filename;
	if (filename.size() > 200) filename.resize(200);
	int filenamesize = filename.size();
	int i = 0;
	do
	{
		filename += Convert_int_to_string(i);
		file.open(filename, ios_base::in | ios_base::binary);
		if (file.fail())
		{
			file.open(filename, ios_base::out | ios_base::binary);
			if (file.fail()) return false;
			file.write(Q_query.method.c_str(), Q_query.method.size());
			file.write(Q_query.host.c_str(), Q_query.host.size());
			file.write(Q_query.page.c_str(), Q_query.page.size());
			file.write("\r\n", 2);
			file.write(Server_Rep, SR_size);
			cout << "Cached a file" << endl;
			file.close();
			return true;
		}
		file.close();
		filename.resize(filenamesize);
		i++;
	} while (true);
}
//Convert unsinged int to string
string Convert_int_to_string(int num)
{
	string str;
	do
	{
		str = char(num % 10 + 48) + str;
		num /= 10;
	} while (num != 0);
	return str;
}
//Legalize filename in windows
void Leglizefn(string &filename)
{
	int len = filename.length();
	for (int i = 0; i < len; i++)
	{
		if (filename[i]=='\\' ||filename[i] == '/' || filename[i] == '|' || filename[i] == '?' || filename[i] == '*' || filename[i] == '\"' || filename[i] == '<' || filename[i] == '>')
		{
			filename[i] = '%';
		}
	}
}
string get_If_since_modified(char* StrCache)
{
	char* hStrCache = get_query_header(StrCache);
	if (hStrCache == NULL) return string("");
	string ret="";
	vector<vector<string>> list = Split(hStrCache);
	int size = list.size();
	int LastModif = -1, Date = -1, Expires = -1;
	for (int i = 0; i < size; i++){
		if (list[i][0] == "Last-Modified:") {
			LastModif = i;
			break;
		}
		else if (list[i][0] == "Expires:") Expires = i;
		else if (list[i][0] == "Date:") Date = i;
	}
	if (LastModif != -1) {
		for (int i = 1; i < list[LastModif].size(); i++)
			ret = ret + list[LastModif][i]+" ";
		ret.resize(ret.size() - 1);
		return ret;
	}
	if (Expires != -1) {
		for (int i = 1; i < list[Expires].size(); i++)
			ret = ret + list[Expires][i] + " ";
		ret.resize(ret.size() - 1);
		return ret;
	}
	if (Date != -1) {
		for (int i = 1; i < list[Date].size(); i++)
			ret = ret + list[Date][i] + " ";
		ret.resize(ret.size() - 1);
		return ret;
	}
	return ret;
}
//Add Condition to the GET query
void AddCondition(char* &Query, int & Query_size, char* Condition)
{
	int C_len = strlen(Condition);
	int new_Query_size = Query_size + C_len + 21;
	Query = (char*)realloc(Query, new_Query_size+1);
	Query[Query_size - 2] = 0;
	strcat_s(Query, new_Query_size + 1, "If-Modified-Since: ");
	strcat_s(Query, new_Query_size + 1, Condition);
	strcat_s(Query, new_Query_size + 1, "\r\n\r\n");
	Query_size = new_Query_size;
}
//Check Is Not Modified
bool IsNotModified(char* Res, int Res_size)
{
	string firstline = "";
	int i = 0;
	while (Res[i] != '\r' && i < Res_size)
	{
		firstline += Res[i];
		i++;
	}
	vector<string> list;
	stringstream stream_fl;
	stream_fl << firstline;
	string token;
	while (getline(stream_fl, token, ' '))
	{
		list.push_back(token);
	}
	if (list.size() >= 2 && list[1] == "304") return true;
	return false;
}
//Update Cache
int UpdateCache(string filename, Query Q_query, char* Res, int Res_size)
{
	fstream file;
	file.open(filename, ios_base::out | ios_base::binary);
	if (file.fail()){
		return 0;
		cout << "Falied to update a file in cache"<<endl;
	}

	file.write(Q_query.method.c_str(), Q_query.method.size());
	file.write(Q_query.host.c_str(), Q_query.host.size());
	file.write(Q_query.page.c_str(), Q_query.page.size());
	file.write("\r\n", 2);
	file.write(Res, Res_size);
	file.close();
	cout << "Updated a file in cache" << endl;
	return 1;
}
//Check Cache Condition before store the web server's response in cache
bool Check_CacheCondition(char* Res, int Res_size)
{
	char*  hRes = get_query_header(Res);
	if (hRes == NULL) return false;
	vector<vector<string>> list = Split(hRes);
	int size = list.size();
	int flag = 0;
	//if it is 206 Partial Content or 304 Not Modified then return false
	if (size>0 && list[0].size() >= 2)
		if (list[0][1] == "206" || list[0][1]=="304") return false;
	for (int i = 0; i < size; i++)
	{
		if (list[i][0] == "Cache-Control:") //if it cache-control no-store or private return false
		{
			for (int j = 0; j < list[i].size(); j++) {
				if (list[i][j].find("no-store") != string::npos || list[i][j].find("private") != string::npos)
					return false;
			}
		}
		if (list[i][0] == "Last-Modified:") flag = 1;
		else if (list[i][0] == "Expires:") flag = 1;
		else if (list[i][0] == "Date:") flag = 1;
	}
	if (flag == 0) return false; //if not exist at least 1 in {Last-Modfied, Exprires, Date} return false
	return true;
}
//Load blacklist
int Load_blacklist(vector<string>& blacklist)
{
	fstream file;
	file.open("blacklist.conf", ios::in);
	if (file.fail()) return 0;
	while (!file.eof()) {
		string temp;
		getline(file, temp);
		if (temp.size() == 0) continue;
		if (temp.back() == '\n') {
			temp.pop_back();
		}
		blacklist.push_back(temp);
	}
	return 1;
}
