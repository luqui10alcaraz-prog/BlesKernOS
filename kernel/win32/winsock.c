#include "win32.h"
#include "process.h"
#include "../include/network.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../string.h"

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INVALID_SOCKET 0xFFFFFFFFU
#define SOCKET_ERROR (-1)
#define WIN_SOCKET_BASE 0x7A000000U
#define WSAEINTR 10004
#define WSAEINVAL 10022
#define WSAEWOULDBLOCK 10035
#define WSAEAFNOSUPPORT 10047
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP 10045
#define WSAENOTSOCK 10038
#define WSAECONNREFUSED 10061
#define WSANOTINITIALISED 10093
#define WSAVERNOTSUPPORTED 10092
#define WSAHOST_NOT_FOUND 11001
#define FD_READ 0x0001
#define FD_WRITE 0x0002
#define FD_OOB 0x0004
#define FD_ACCEPT 0x0008
#define FD_CONNECT 0x0010
#define FD_CLOSE 0x0020

typedef struct PACKED { uint16_t family, port; uint8_t address[4], zero[8]; } sockaddr_in32_t;
typedef struct PACKED { char *name; char **aliases; int16_t address_type, address_length; char **address_list; } hostent32_t;
typedef struct {
    bool used, connected, nonblocking, listening;
    uint8_t type;
    uint32_t owner;
    int native;
    uint8_t remote[4];
    uint16_t port, local_port;
    void *async_hwnd;
    uint32_t async_message;
    uint32_t async_events;
    uint32_t async_pending;
} win_socket_t;
typedef struct { uint32_t owner, references; } wsa_process_t;
static win_socket_t sockets[NET_SOCKET_MAX];
static wsa_process_t processes[TASK_MAX];
static char host_name[256]; static char *host_aliases[1]; static uint8_t host_address[4]; static char *host_addresses[2]; static hostent32_t host_entry;
static uint32_t async_request_id = 1U;


static void async_post(win_socket_t *socket, uint32_t handle,
                       uint32_t event, int error) {
    if (!socket || !(socket->async_events & event) ||
        (socket->async_pending & event) || !socket->async_message) return;
    if (win32_user_post_message(socket->async_hwnd, socket->async_message,
            handle, (int32_t)((event & 0xFFFFU) | ((uint32_t)(uint16_t)error << 16))))
        socket->async_pending |= event;
}

static uint32_t async_host_copy(const hostent32_t *host, char *buffer,
                                uint32_t capacity) {
    uint32_t name_length, required;
    hostent32_t *out;
    char **aliases, **addresses;
    char *name;
    uint8_t *address;
    if (!host || !buffer) return 0U;
    name_length = (uint32_t)kstrlen(host->name) + 1U;
    required = sizeof(hostent32_t) + 3U * sizeof(char *) + name_length + 4U;
    if (capacity < required) return required;
    out = (hostent32_t *)buffer;
    aliases = (char **)(buffer + sizeof(*out));
    addresses = aliases + 1U;
    name = (char *)(addresses + 2U);
    address = (uint8_t *)(name + name_length);
    kmemcpy(name, host->name, name_length);
    kmemcpy(address, host->address_list[0], 4U);
    aliases[0] = NULL;
    addresses[0] = (char *)address;
    addresses[1] = NULL;
    out->name = name;
    out->aliases = aliases;
    out->address_type = AF_INET;
    out->address_length = 4;
    out->address_list = addresses;
    return required;
}

static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static uint16_t swap16(uint16_t value){return(uint16_t)((value<<8)|(value>>8));}
static uint32_t swap32(uint32_t value){return(value<<24)|((value<<8)&0x00FF0000U)|((value>>8)&0x0000FF00U)|(value>>24);}
static void set_error(int error){win32_process_set_last_error((uint32_t)error);}
static bool initialized(void){uint32_t owner=task_current_process_id();for(uint32_t i=0;i<TASK_MAX;i++)if(processes[i].owner==owner&&processes[i].references)return true;return false;}
static win_socket_t *socket_from(uint32_t handle){uint32_t slot;if(handle<WIN_SOCKET_BASE||handle>=WIN_SOCKET_BASE+NET_SOCKET_MAX)return NULL;slot=handle-WIN_SOCKET_BASE;return sockets[slot].used&&sockets[slot].owner==task_current_process_id()?&sockets[slot]:NULL;}
static uint32_t WIN32_API ws_WSAStartup(uint16_t version,void*raw){uint8_t*data=(uint8_t*)raw;uint8_t major=(uint8_t)(version&0xFFU),minor=(uint8_t)(version>>8);wsa_process_t*free_slot=NULL;if(!raw)return WSAEINVAL;if(major<1U||major>2U)return WSAVERNOTSUPPORTED;for(uint32_t i=0;i<TASK_MAX;i++){if(processes[i].owner==task_current_process_id()){processes[i].references++;free_slot=&processes[i];break;}if(!processes[i].owner&&!free_slot)free_slot=&processes[i];}if(!free_slot)return WSAEINVAL;if(!free_slot->owner){free_slot->owner=task_current_process_id();free_slot->references=1U;}kmemset(data,0,400U);*(uint16_t*)(data+0)=version;*(uint16_t*)(data+2)=(uint16_t)((2U<<8)|2U);kstrcpy((char*)data+4,"BlesKernOS TCP/IP Winsock");kstrcpy((char*)data+261,"Running");*(uint16_t*)(data+390)=NET_SOCKET_MAX;*(uint16_t*)(data+392)=0U;(void)minor;set_error(0);return 0;}
static int WIN32_API ws_WSACleanup(void){uint32_t owner=task_current_process_id();for(uint32_t i=0;i<TASK_MAX;i++)if(processes[i].owner==owner&&processes[i].references){if(--processes[i].references==0U)processes[i].owner=0;set_error(0);return 0;}set_error(WSANOTINITIALISED);return SOCKET_ERROR;}
static int WIN32_API ws_WSAGetLastError(void){return (int)win32_process_get_last_error();}
static void WIN32_API ws_WSASetLastError(int error){set_error(error);}
static uint32_t WIN32_API ws_socket(int af,int type,int protocol){int native;if(!initialized()){set_error(WSANOTINITIALISED);return INVALID_SOCKET;}if(af!=AF_INET){set_error(WSAEAFNOSUPPORT);return INVALID_SOCKET;}if((type!=SOCK_STREAM&&type!=SOCK_DGRAM)||(protocol!=0&&protocol!=(type==SOCK_STREAM?IPPROTO_TCP:IPPROTO_UDP))){set_error(WSAESOCKTNOSUPPORT);return INVALID_SOCKET;}native=network_socket_open(type==SOCK_STREAM?NET_SOCKET_TCP:NET_SOCKET_UDP);if(native<0){set_error(WSAEWOULDBLOCK);return INVALID_SOCKET;}for(uint32_t i=0;i<NET_SOCKET_MAX;i++)if(!sockets[i].used){kmemset(&sockets[i],0,sizeof(sockets[i]));sockets[i].used=true;sockets[i].type=(uint8_t)type;sockets[i].owner=task_current_process_id();sockets[i].native=native;set_error(0);return WIN_SOCKET_BASE+i;}network_socket_close(native);set_error(WSAEWOULDBLOCK);return INVALID_SOCKET;}
static int WIN32_API ws_connect(uint32_t handle,const sockaddr_in32_t*address,int length){win_socket_t*s=socket_from(handle);if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(!address||length<16||address->family!=AF_INET){set_error(WSAEINVAL);return SOCKET_ERROR;}if(!network_socket_connect(s->native,address->address,swap16(address->port),s->nonblocking?1U:10000U)){set_error(s->nonblocking?WSAEWOULDBLOCK:WSAECONNREFUSED);if(s->async_message)async_post(s,handle,FD_CONNECT,WSAECONNREFUSED);return SOCKET_ERROR;}kmemcpy(s->remote,address->address,4);s->port=swap16(address->port);s->connected=true;s->async_pending&=~FD_CONNECT;async_post(s,handle,FD_CONNECT,0);async_post(s,handle,FD_WRITE,0);set_error(0);return 0;}
static int WIN32_API ws_send(uint32_t handle,const char*buffer,int length,int flags UNUSED){win_socket_t*s=socket_from(handle);int32_t sent;if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(!buffer||length<0){set_error(WSAEINVAL);return SOCKET_ERROR;}s->async_pending&=~FD_WRITE;sent=network_socket_send(s->native,buffer,(uint32_t)length,s->nonblocking?1U:10000U);if(sent<0){set_error(s->nonblocking?WSAEWOULDBLOCK:WSAECONNREFUSED);return SOCKET_ERROR;}async_post(s,handle,FD_WRITE,0);set_error(0);return(int)sent;}
static int WIN32_API ws_recv(uint32_t handle,char*buffer,int length,int flags UNUSED){win_socket_t*s=socket_from(handle);int32_t received;if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(!buffer||length<=0){set_error(WSAEINVAL);return SOCKET_ERROR;}s->async_pending&=~FD_READ;received=network_socket_receive(s->native,buffer,(uint32_t)length,s->nonblocking?1U:10000U);if(received<0){set_error(s->nonblocking?WSAEWOULDBLOCK:WSAECONNREFUSED);return SOCKET_ERROR;}if(received==0){s->connected=false;s->async_pending&=~FD_CLOSE;async_post(s,handle,FD_CLOSE,0);}else if(network_socket_readable(s->native))async_post(s,handle,FD_READ,0);set_error(0);return(int)received;}
static int WIN32_API ws_closesocket(uint32_t handle){win_socket_t*s=socket_from(handle);if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}network_socket_close(s->native);kmemset(s,0,sizeof(*s));set_error(0);return 0;}
static int WIN32_API ws_shutdown(uint32_t handle,int how UNUSED){win_socket_t*s=socket_from(handle);if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}s->connected=false;return 0;}
static int WIN32_API ws_ioctlsocket(uint32_t handle,uint32_t command,uint32_t*argument){win_socket_t*s=socket_from(handle);if(!s||!argument){set_error(s?WSAEINVAL:WSAENOTSOCK);return SOCKET_ERROR;}if(command==0x8004667EU){s->nonblocking=*argument!=0U;return 0;}set_error(WSAEOPNOTSUPP);return SOCKET_ERROR;}
static uint16_t WIN32_API ws_htons(uint16_t value){return swap16(value);} static uint16_t WIN32_API ws_ntohs(uint16_t value){return swap16(value);}
static uint32_t WIN32_API ws_htonl(uint32_t value){return swap32(value);} static uint32_t WIN32_API ws_ntohl(uint32_t value){return swap32(value);}
static uint32_t WIN32_API ws_inet_addr(const char*text){uint32_t result=0,part=0,count=0;if(!text)return 0xFFFFFFFFU;while(1){if(*text>='0'&&*text<='9'){part=part*10U+(uint32_t)(*text-'0');if(part>255U)return 0xFFFFFFFFU;}else if(*text=='.'||*text==0){if(count>=4U)return 0xFFFFFFFFU;result|=part<<(count*8U);count++;part=0;if(!*text)break;}else return 0xFFFFFFFFU;text++;}return count==4U?result:0xFFFFFFFFU;}
static char*WIN32_API ws_inet_ntoa(uint32_t address){static char out[16];uint32_t pos=0;for(uint32_t i=0;i<4;i++){uint32_t value=(address>>(i*8U))&255U;char rev[3];uint32_t n=0;do{rev[n++]=(char)('0'+value%10U);value/=10U;}while(value);while(n)out[pos++]=rev[--n];if(i!=3)out[pos++]='.';}out[pos]=0;return out;}
static hostent32_t *make_host(const char*name,const uint8_t address[4]){kstrncpy(host_name,name?name:"",sizeof(host_name)-1U);host_name[sizeof(host_name)-1U]=0;kmemcpy(host_address,address,4);host_aliases[0]=NULL;host_addresses[0]=(char*)host_address;host_addresses[1]=NULL;host_entry.name=host_name;host_entry.aliases=host_aliases;host_entry.address_type=AF_INET;host_entry.address_length=4;host_entry.address_list=host_addresses;return&host_entry;}
static hostent32_t*WIN32_API ws_gethostbyname(const char*name){uint8_t address[4];uint32_t numeric;if(!name){set_error(WSAEINVAL);return NULL;}numeric=ws_inet_addr(name);if(numeric!=0xFFFFFFFFU)kmemcpy(address,&numeric,4);else if(!network_resolve(name,address,10000U)){set_error(11001);return NULL;}set_error(0);return make_host(name,address);}
static hostent32_t*WIN32_API ws_gethostbyaddr(const uint8_t*address,int length,int type){if(!address||length!=4||type!=AF_INET){set_error(WSAEINVAL);return NULL;}return make_host(ws_inet_ntoa(*(const uint32_t*)address),address);}
static int WIN32_API ws_gethostname(char*out,int length){const char*name="bleskernos";if(!out||length<11){set_error(WSAEINVAL);return SOCKET_ERROR;}kstrcpy(out,name);return 0;}
static int fill_sockaddr(win_socket_t*s,sockaddr_in32_t*address,int*length){if(!s||!address||!length||*length<16)return SOCKET_ERROR;kmemset(address,0,16);address->family=AF_INET;address->port=swap16(s->port);kmemcpy(address->address,s->remote,4);*length=16;return 0;}
static int WIN32_API ws_getpeername(uint32_t handle,sockaddr_in32_t*address,int*length){win_socket_t*s=socket_from(handle);if(!s||!s->connected){set_error(WSAENOTSOCK);return SOCKET_ERROR;}return fill_sockaddr(s,address,length);}
static int WIN32_API ws_getsockname(uint32_t handle,sockaddr_in32_t*address,int*length){win_socket_t*s=socket_from(handle);net_info_t info;if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}network_get_info(&info);if(!address||!length||*length<16)return SOCKET_ERROR;kmemset(address,0,16);address->family=AF_INET;address->port=swap16(s->local_port);kmemcpy(address->address,info.address,4);*length=16;return 0;}
static int WIN32_API ws_setsockopt(uint32_t handle,int level UNUSED,int option UNUSED,const char*value UNUSED,int length UNUSED){if(!socket_from(handle)){set_error(WSAENOTSOCK);return SOCKET_ERROR;}return 0;}
static int WIN32_API ws_getsockopt(uint32_t handle,int level UNUSED,int option UNUSED,char*value,int*length){if(!socket_from(handle)){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(value&&length&&*length>=(int)sizeof(int)){*(int*)value=0;*length=sizeof(int);return 0;}set_error(WSAEINVAL);return SOCKET_ERROR;}
static int WIN32_API ws_bind(uint32_t handle,const void*raw,int length){win_socket_t*s=socket_from(handle);const sockaddr_in32_t*address=(const sockaddr_in32_t*)raw;uint16_t port;if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(!address||length<16||address->family!=AF_INET){set_error(WSAEINVAL);return SOCKET_ERROR;}port=swap16(address->port);if(!network_socket_bind(s->native,address->address,port)){set_error(WSAEINVAL);return SOCKET_ERROR;}s->local_port=port;set_error(0);return 0;}
static int WIN32_API ws_listen(uint32_t handle,int backlog){win_socket_t*s=socket_from(handle);if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(s->type!=SOCK_STREAM||!network_socket_listen(s->native,(uint8_t)(backlog<1?1:backlog))){set_error(WSAEOPNOTSUPP);return SOCKET_ERROR;}s->listening=true;set_error(0);return 0;}
static uint32_t WIN32_API ws_accept(uint32_t handle,void*raw,int*length){win_socket_t*s=socket_from(handle);uint8_t remote[4];uint16_t port;int native;if(!s||!s->listening){set_error(WSAENOTSOCK);return INVALID_SOCKET;}s->async_pending&=~FD_ACCEPT;native=network_socket_accept(s->native,remote,&port,s->nonblocking?1U:10000U);if(native<0){set_error(WSAEWOULDBLOCK);return INVALID_SOCKET;}for(uint32_t i=0;i<NET_SOCKET_MAX;i++)if(!sockets[i].used){kmemset(&sockets[i],0,sizeof(sockets[i]));sockets[i].used=true;sockets[i].connected=true;sockets[i].type=SOCK_STREAM;sockets[i].owner=task_current_process_id();sockets[i].native=native;sockets[i].port=port;sockets[i].local_port=s->local_port;kmemcpy(sockets[i].remote,remote,4);if(raw&&length)fill_sockaddr(&sockets[i],(sockaddr_in32_t*)raw,length);if(network_socket_readable(s->native))async_post(s,handle,FD_ACCEPT,0);set_error(0);return WIN_SOCKET_BASE+i;}network_socket_close(native);set_error(WSAEWOULDBLOCK);return INVALID_SOCKET;}
static int WIN32_API ws_sendto(uint32_t handle,const char*buffer,int length,int flags UNUSED,const void*raw,int tolen){win_socket_t*s=socket_from(handle);const sockaddr_in32_t*to=(const sockaddr_in32_t*)raw;int32_t sent;if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(s->type!=SOCK_DGRAM||!buffer||length<0||!to||tolen<16||to->family!=AF_INET){set_error(WSAEINVAL);return SOCKET_ERROR;}sent=network_socket_sendto(s->native,to->address,swap16(to->port),buffer,(uint32_t)length,10000U);if(sent<0){set_error(WSAECONNREFUSED);return SOCKET_ERROR;}set_error(0);return(int)sent;}
static int WIN32_API ws_recvfrom(uint32_t handle,char*buffer,int length,int flags UNUSED,void*raw,int*fromlen){win_socket_t*s=socket_from(handle);uint8_t remote[4];uint16_t port;int32_t received;if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}if(s->type!=SOCK_DGRAM||!buffer||length<=0){set_error(WSAEINVAL);return SOCKET_ERROR;}received=network_socket_receivefrom(s->native,remote,&port,buffer,(uint32_t)length,s->nonblocking?1U:10000U);if(received<0){set_error(WSAECONNREFUSED);return SOCKET_ERROR;}if(received==0&&s->nonblocking){set_error(WSAEWOULDBLOCK);return SOCKET_ERROR;}if(raw&&fromlen&&*fromlen>=16){sockaddr_in32_t*from=(sockaddr_in32_t*)raw;kmemset(from,0,16);from->family=AF_INET;from->port=swap16(port);kmemcpy(from->address,remote,4);*fromlen=16;}set_error(0);return(int)received;}
typedef struct PACKED{uint32_t count;uint32_t sockets[64];}fd_set32_t;
static int filter_set(fd_set32_t*set,bool writable){uint32_t out=0;if(!set)return 0;for(uint32_t i=0;i<set->count&&i<64U;i++){win_socket_t*s=socket_from(set->sockets[i]);if(s&&(writable?(s->connected||s->type==SOCK_DGRAM):network_socket_readable(s->native)))set->sockets[out++]=set->sockets[i];}set->count=out;return(int)out;}
static int WIN32_API ws_select(int ignored UNUSED,fd_set32_t*read,fd_set32_t*write,fd_set32_t*except,void*timeout UNUSED){int count=0;if(read)count+=filter_set(read,false);if(write)count+=filter_set(write,true);if(except)except->count=0;return count;}
static int WIN32_API ws___WSAFDIsSet(uint32_t socket,fd_set32_t*set){if(!set)return 0;for(uint32_t i=0;i<set->count&&i<64U;i++)if(set->sockets[i]==socket)return 1;return 0;}
static int WIN32_API ws_WSAAsyncSelect(uint32_t handle,void*hwnd,uint32_t message,int events){win_socket_t*s=socket_from(handle);if(!s){set_error(WSAENOTSOCK);return SOCKET_ERROR;}s->async_hwnd=hwnd;s->async_message=message;s->async_events=(uint32_t)events;s->async_pending=0U;s->nonblocking=events!=0;if(events&FD_WRITE)async_post(s,handle,FD_WRITE,0);if(s->connected&&(events&FD_CONNECT))async_post(s,handle,FD_CONNECT,0);if(network_socket_readable(s->native))async_post(s,handle,s->listening?FD_ACCEPT:FD_READ,0);set_error(0);return 0;}
static uint32_t WIN32_API ws_WSAAsyncGetHostByName(void*hwnd,uint32_t message,const char*name,char*buffer,int capacity){hostent32_t*host;uint32_t request=async_request_id++,required;int error=0;if(!request)request=async_request_id++;host=ws_gethostbyname(name);if(!host){error=ws_WSAGetLastError();required=0U;}else required=async_host_copy(host,buffer,capacity>0?(uint32_t)capacity:0U);if(host&&required>(uint32_t)capacity)error=WSAEINVAL;(void)win32_user_post_message(hwnd,message,request,(int32_t)((required&0xFFFFU)|((uint32_t)(uint16_t)error<<16)));return request;}
static uint32_t WIN32_API ws_WSAAsyncGetHostByAddr(void*hwnd,uint32_t message,const char*address,int length,int type,char*buffer,int capacity){hostent32_t*host;uint32_t request=async_request_id++,required;int error=0;if(!request)request=async_request_id++;host=ws_gethostbyaddr((const uint8_t*)address,length,type);if(!host){error=ws_WSAGetLastError();required=0U;}else required=async_host_copy(host,buffer,capacity>0?(uint32_t)capacity:0U);if(host&&required>(uint32_t)capacity)error=WSAEINVAL;(void)win32_user_post_message(hwnd,message,request,(int32_t)((required&0xFFFFU)|((uint32_t)(uint16_t)error<<16)));return request;}
static int WIN32_API ws_WSACancelAsyncRequest(uint32_t request UNUSED){return 0;}
static int WIN32_API ws_WSAIsBlocking(void){return 0;} static int WIN32_API ws_WSACancelBlockingCall(void){return 0;} static void*WIN32_API ws_WSASetBlockingHook(void*hook){return hook;} static int WIN32_API ws_WSAUnhookBlockingHook(void){return 0;}

void win32_winsock_poll(void){uint32_t owner=task_current_process_id();for(uint32_t i=0;i<NET_SOCKET_MAX;i++){win_socket_t*s=&sockets[i];uint32_t handle=WIN_SOCKET_BASE+i;if(!s->used||s->owner!=owner||!s->async_message)continue;if(network_socket_readable(s->native))async_post(s,handle,s->listening?FD_ACCEPT:FD_READ,0);if((s->connected||s->type==SOCK_DGRAM)&&(s->async_events&FD_WRITE))async_post(s,handle,FD_WRITE,0);}}

void win32_winsock_cleanup_process(uint32_t owner){for(uint32_t i=0;i<NET_SOCKET_MAX;i++)if(sockets[i].used&&sockets[i].owner==owner){network_socket_close(sockets[i].native);kmemset(&sockets[i],0,sizeof(sockets[i]));}for(uint32_t i=0;i<TASK_MAX;i++)if(processes[i].owner==owner)kmemset(&processes[i],0,sizeof(processes[i]));}
uint32_t win32_winsock_resolve(const char*name){
#define W(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&ws_##api
 W(accept);W(bind);W(closesocket);W(connect);W(getpeername);W(getsockname);W(getsockopt);W(htonl);W(htons);W(inet_addr);W(inet_ntoa);W(ioctlsocket);W(listen);W(ntohl);W(ntohs);W(recv);W(recvfrom);W(select);W(send);W(sendto);W(setsockopt);W(shutdown);W(socket);W(gethostbyaddr);W(gethostbyname);W(gethostname);W(WSAAsyncSelect);W(WSAAsyncGetHostByName);W(WSAAsyncGetHostByAddr);W(WSACancelAsyncRequest);W(WSAGetLastError);W(WSASetLastError);W(WSASetBlockingHook);W(WSAUnhookBlockingHook);W(WSACancelBlockingCall);W(WSAIsBlocking);W(WSAStartup);W(WSACleanup);W(__WSAFDIsSet);
#undef W
 return 0;
}
uint32_t win32_winsock_resolve_ordinal(uint16_t ordinal){static const uint16_t ordinals[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,51,52,57,101,109,110,111,112,113,114,115,116,151};static void*functions[]={ws_accept,ws_bind,ws_closesocket,ws_connect,ws_getpeername,ws_getsockname,ws_getsockopt,ws_htonl,ws_htons,ws_inet_addr,ws_inet_ntoa,ws_ioctlsocket,ws_listen,ws_ntohl,ws_ntohs,ws_recv,ws_recvfrom,ws_select,ws_send,ws_sendto,ws_setsockopt,ws_shutdown,ws_socket,ws_gethostbyaddr,ws_gethostbyname,ws_gethostname,ws_WSAAsyncSelect,ws_WSASetBlockingHook,ws_WSAUnhookBlockingHook,ws_WSAGetLastError,ws_WSASetLastError,ws_WSACancelBlockingCall,ws_WSAIsBlocking,ws_WSAStartup,ws_WSACleanup,ws___WSAFDIsSet};for(uint32_t i=0;i<sizeof(ordinals)/sizeof(ordinals[0]);i++)if(ordinals[i]==ordinal)return(uint32_t)(uintptr_t)functions[i];return 0;}
