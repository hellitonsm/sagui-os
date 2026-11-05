/* http.c - pilha mínima experimental para HTTP sem TLS sobre QEMU user-net.
 * Rede estática padrão QEMU: 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
 * Não é uma implementação geral de TCP/IP: sem DHCP, fragmentação, HTTPS ou
 * retransmissões sofisticadas. Foi mantida pequena para a primeira ferramenta. */
#include "http.h"
#include "rede.h"
#include "texto.h"
#include "sistema.h"
#include "relogio.h"

#define IP_LOCAL 0x0A00020Fu
#define IP_DNS   0x0A000203u
#define IP_GW    0x0A000202u
#define ETH_IP   0x0800
#define ETH_ARP  0x0806
#define ETH_LEN 14
#define IP_LEN 20
#define TCP_LEN 20
#define MAX_FRAME 1514
#define TCP_ACK 0x10
#define TCP_SYN 0x02
#define TCP_FIN 0x01
#define TCP_RST 0x04
#define TCP_PSH 0x08

static uint8_t frame[MAX_FRAME];
/* MAC convencional do gateway slirp 10.0.2.2 no QEMU user-net. */
static uint8_t mac_gw[6]={0x52,0x55,0x0a,0x00,0x02,0x02};
static uint16_t ipid=0x4251;
static uint16_t port_local=49152;
static bool dns_tcp_query(uint32_t server,const uint8_t *query,uint16_t qlen,uint16_t id,uint32_t *answer);
static uint32_t seq_local=0x53414755;
static uint32_t seq_remoto;
static uint32_t soma(const uint8_t *p, uint32_t n, uint32_t s)
{
    while(n>1){s+=((uint16_t)p[0]<<8)|p[1];p+=2;n-=2;}
    if(n)s+=(uint16_t)p[0]<<8;
    while(s>>16)s=(s&0xffff)+(s>>16);
    return s;
}
static uint16_t checksum(const uint8_t *p,uint32_t n,uint32_t s){return (uint16_t)~soma(p,n,s);}
static void put16(uint8_t*p,uint16_t x){p[0]=x>>8;p[1]=x;}
static void put32(uint8_t*p,uint32_t x){p[0]=x>>24;p[1]=x>>16;p[2]=x>>8;p[3]=x;}
static uint16_t get16(const uint8_t*p){return ((uint16_t)p[0]<<8)|p[1];}
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static bool eth_send(uint16_t type,const uint8_t *dst,const uint8_t *payload,uint16_t n)
{
    const uint8_t *m=e1000_mac(); for(int i=0;i<6;i++){frame[i]=dst[i];frame[6+i]=m[i];}
    put16(frame+12,type);for(uint16_t i=0;i<n;i++)frame[14+i]=payload[i];
    return e1000_enviar(frame,(uint16_t)(14+n));
}
static bool arp_gateway(void)
{
    uint8_t p[28]={0},bc[6]={255,255,255,255,255,255};
    put16(p,1);put16(p+2,ETH_IP);p[4]=6;p[5]=4;put16(p+6,1);
    const uint8_t*m=e1000_mac();for(int i=0;i<6;i++)p[8+i]=m[i];put32(p+14,IP_LOCAL);put32(p+24,IP_GW);
    if(!eth_send(ETH_ARP,bc,p,sizeof p))return false;
    uint32_t fim=relogio_tiques()+200;
    while((int32_t)(relogio_tiques()-fim)<0){
        uint16_t n=e1000_receber(frame,sizeof frame);
        if(n>=42&&get16(frame+12)==ETH_ARP&&get16(frame+20)==2&&get32(frame+28)==IP_GW){for(int i=0;i<6;i++)mac_gw[i]=frame[22+i];return true;}
        __asm__ volatile("pause");
    }
    return false;
}
static uint16_t ip_packet(uint8_t *p,uint32_t dst,uint8_t proto,const uint8_t *data,uint16_t n)
{
    p[0]=0x45;p[1]=0;put16(p+2,IP_LEN+n);put16(p+4,ipid++);put16(p+6,0x4000);p[8]=64;p[9]=proto;put16(p+10,0);put32(p+12,IP_LOCAL);put32(p+16,dst);put16(p+10,checksum(p,IP_LEN,0));
    for(uint16_t i=0;i<n;i++) p[IP_LEN+i]=data[i];
    return IP_LEN+n;
}
static bool send_ip(uint32_t dst,uint8_t proto,const uint8_t*data,uint16_t n)
{
    uint8_t p[1500];uint32_t next=(dst&0xffffff00u)==(IP_LOCAL&0xffffff00u)?dst:IP_GW;
    if(next==IP_GW&&(mac_gw[0]|mac_gw[1]|mac_gw[2]|mac_gw[3]|mac_gw[4]|mac_gw[5])==0&&!arp_gateway())return false;
    uint16_t z=ip_packet(p,dst,proto,data,n);return eth_send(ETH_IP,next==IP_GW?mac_gw:mac_gw,p,z);
}
/* QEMU user-mode gateway proxies ARP; use its MAC for all outbound IP frames. */
static bool recv_ip(uint8_t proto,uint32_t peer,uint8_t*out,uint16_t cap,uint16_t*len,uint32_t loops)
{
    uint32_t fim=relogio_tiques()+loops/10+1;
    while((int32_t)(relogio_tiques()-fim)<0){uint16_t n=e1000_receber(frame,sizeof frame);if(n>=34&&get16(frame+12)==ETH_IP&&frame[23]==proto&&get32(frame+30)==IP_LOCAL&&(!peer||get32(frame+26)==peer)){uint16_t h=(frame[14]&15)*4,z=get16(frame+16);if(z>=h+IP_LEN&&z<=n-14){uint16_t l=z-h;if(l>cap)return false;for(uint16_t i=0;i<l;i++)out[i]=frame[14+h+i];*len=l;return true;}}__asm__ volatile("pause");}return false;
}
static bool dns_lookup(const char*host,uint32_t*ip)
{
    uint8_t u[512]={0},q[256]={0};uint16_t id=0x5347;put16(u,port_local++);put16(u+2,53);put16(u+4,0);put16(u+6,0);put16(u+8,0);put16(u+10,0);
    put16(q,id);put16(q+2,0x0100);put16(q+4,1);put16(q+6,0);put16(q+8,0);put16(q+10,0);uint16_t k=12;
    const char*s=host;while(*s){const char*e=s;while(*e&&*e!='.')e++;uint32_t l=(uint32_t)(e-s);if(!l||l>63||k+l+6>=sizeof q)return false;q[k++]=(uint8_t)l;while(s<e)q[k++]=(uint8_t)*s++;if(*s=='.')s++;}
    q[k++]=0;put16(q+k,1);k+=2;put16(q+k,1);k+=2;put16(u+4,k+8);put16(u+8,0x0100);put16(u+10,0);for(uint16_t i=0;i<k;i++)u[8+i]=q[i];
    uint16_t c=checksum(u+8,k,0); /* UDP checksum is optional for IPv4; leave zero. */(void)c;
    if(!send_ip(IP_DNS,17,u,k+8))return false;
    uint8_t r[512];uint16_t n;if(!recv_ip(17,IP_DNS,r,sizeof r,&n,1500)||n<20)return dns_tcp_query(0x08080808u,q,k,id,ip);
    uint16_t ul=get16(r+4);if(ul<20||ul>n||get16(r+8)!=id||(get16(r+10)&0x000f))return false;
    uint16_t off=20,qd=get16(r+12),an=get16(r+14);for(uint16_t i=0;i<qd&&off<ul;i++){while(off<ul&&r[off]){if((r[off]&0xc0)==0xc0){off+=2;break;}off+=1+r[off];}if(off<ul&&r[off]==0)off++;off+=4;}
    for(uint16_t i=0;i<an&&off+12<=ul;i++){if((r[off]&0xc0)==0xc0)off+=2;else{while(off<ul&&r[off])off+=1+r[off];off++;}if(off+10>ul)return false;uint16_t typ=get16(r+off),cl=get16(r+off+2),rd=get16(r+off+8);off+=10;if(off+rd>ul)return false;if(typ==1&&cl==1&&rd==4){*ip=get32(r+off);return true;}off+=rd;}
    return false;
}
static uint16_t tcp_sum(uint32_t dst,const uint8_t*t,uint16_t n)
{
    uint8_t pseudo[12];put32(pseudo,IP_LOCAL);put32(pseudo+4,dst);pseudo[8]=0;pseudo[9]=6;put16(pseudo+10,n);return checksum(t,n,soma(pseudo,12,0));
}
static bool tcp_send(uint32_t dst,uint16_t dport,uint8_t flags,const uint8_t*data,uint16_t n)
{
    uint8_t t[1200]={0};put16(t,port_local);put16(t+2,dport);put32(t+4,seq_local);put32(t+8,seq_remoto);t[12]=0x50;t[13]=flags;put16(t+14,4096);put16(t+16,0);put16(t+18,0);for(uint16_t i=0;i<n;i++)t[20+i]=data[i];put16(t+16,tcp_sum(dst,t,20+n));return send_ip(dst,6,t,20+n);
}
static bool wait_tcp(uint32_t peer,uint16_t port,uint8_t*out,uint16_t cap,uint16_t*len,uint8_t*flags,uint32_t loops)
{
    uint32_t fim=relogio_tiques()+loops/10+1;
    while((int32_t)(relogio_tiques()-fim)<0){uint16_t n=e1000_receber(frame,sizeof frame);if(n>=54&&get16(frame+12)==ETH_IP&&frame[23]==6&&get32(frame+26)==peer){uint8_t*t=frame+34;uint16_t h=(t[12]>>4)*4;if(get16(t+2)==port&&h>=20&&n>=34+h){*flags=t[13];seq_remoto=get32(t+4)+(!!(t[13]&TCP_SYN));uint16_t l=get16(frame+16)-20-h;if(l>cap)return false;for(uint16_t i=0;i<l;i++)out[i]=t[h+i];*len=l;return true;}}__asm__ volatile("pause");}return false;
}
static bool dns_tcp_query(uint32_t server,const uint8_t *query,uint16_t qlen,uint16_t id,uint32_t *answer)
{
    uint16_t remote_port=53; port_local++; uint32_t initial=seq_local;
    if(!tcp_send(server,remote_port,TCP_SYN,NULL,0))return false;
    uint8_t data[1500],flags;uint16_t n;
    if(!wait_tcp(server,port_local,data,sizeof data,&n,&flags,5000)||!(flags&TCP_SYN)||!(flags&TCP_ACK))return false;
    seq_local=initial+1; tcp_send(server,remote_port,TCP_ACK,NULL,0);
    uint8_t payload[260];put16(payload,qlen);for(uint16_t i=0;i<qlen;i++)payload[2+i]=query[i];
    if(!tcp_send(server,remote_port,TCP_ACK|TCP_PSH,payload,qlen+2))return false;
    seq_local+=qlen+2;
    uint8_t response[512];uint16_t used=0;
    for(int tries=0;tries<3&&used<sizeof response;tries++){
        if(!wait_tcp(server,port_local,data,sizeof data,&n,&flags,5000))return false;
        if(n){uint16_t take=n;if(take>sizeof(response)-used)take=sizeof(response)-used;for(uint16_t j=0;j<take;j++)response[used++]=data[j];seq_remoto+=n;tcp_send(server,remote_port,TCP_ACK,NULL,0);if(used>=2&&used>=get16(response)+2)break;}
    }
    if(used<14||get16(response+2)!=id)return false;
    uint16_t len=get16(response),off=14,qd=get16(response+6),an=get16(response+8);
    for(uint16_t i=0;i<qd&&off<len+2;i++){while(off<len+2&&response[off]){if((response[off]&0xc0)==0xc0){off+=2;break;}off+=1+response[off];}if(off<len+2&&response[off]==0)off++;off+=4;}
    for(uint16_t i=0;i<an&&off+12<=len+2;i++){if((response[off]&0xc0)==0xc0)off+=2;else{while(off<len+2&&response[off])off+=1+response[off];off++;}if(off+10>len+2)return false;uint16_t typ=get16(response+off),cl=get16(response+off+2),rd=get16(response+off+8);off+=10;if(off+rd>len+2)return false;if(typ==1&&cl==1&&rd==4){*answer=get32(response+off);return true;}off+=rd;}
    return false;
}

int32_t http_obter(const char *url,void*destino,uint32_t capacidade)
{
    if(!e1000_disponivel()||!url||!destino||capacidade<128)return -1;
    if(url[0]!='h'||url[1]!='t'||url[2]!='t'||url[3]!='p'||url[4]!=':'||url[5]!='/'||url[6]!='/')return -1;
    const char*host=url+7;char h[128];uint32_t i=0;while(host[i]&&host[i]!='/'&&host[i]!=':'&&i<sizeof h-1){h[i]=host[i];i++;}h[i]=0;if(!i)return -1;
    uint16_t port=80;if(host[i]==':'){port=0;i++;while(host[i]>='0'&&host[i]<='9'){port=(uint16_t)(port*10+host[i++]-'0');if(port>6553)return -1;}if(!port)return -1;}
    const char*path=host+i;if(!*path)path="/";if(*path!='/')return -1;
    uint32_t ip=0;bool numerico=true;uint32_t oct=0;int partes=0;
    for(uint32_t j=0;h[j];j++){if(h[j]>='0'&&h[j]<='9'){oct=oct*10u+(uint32_t)(h[j]-'0');if(oct>255)return -1;}else if(h[j]=='.'&&partes<3){ip=(ip<<8)|oct;oct=0;partes++;}else{numerico=false;break;}}
    if(numerico&&partes==3)ip=(ip<<8)|oct;else if(!dns_lookup(h,&ip))return -20;
    /* Resolve next-hop MAC on the gateway (QEMU answers proxy ARP). */
    if(!arp_gateway())return -21;
    uint32_t start=seq_local;
    if(!tcp_send(ip,port,TCP_SYN,NULL,0))return -22;
    uint8_t data[1500],flags;uint16_t n;
    if(!wait_tcp(ip,port_local,data,sizeof data,&n,&flags,5000)||!(flags&TCP_SYN)||!(flags&TCP_ACK))return -23;
    seq_local=start+1;seq_remoto+=0; if(!tcp_send(ip,port,TCP_ACK,NULL,0))return -1;
    char req[512];int q=0;const char*pre="GET ";while(*pre)req[q++]=*pre++;while(*path&&q<400)req[q++]=*path++;
    pre=" HTTP/1.0\r\nHost: ";while(*pre)req[q++]=*pre++;for(i=0;h[i]&&q<460;i++)req[q++]=h[i];pre="\r\nConnection: close\r\nUser-Agent: SaguiOS-wget/0.1\r\n\r\n";while(*pre&&q<510)req[q++]=*pre++;
    if(!tcp_send(ip,port,TCP_ACK|TCP_PSH,(uint8_t*)req,(uint16_t)q)) return -1;
    seq_local+=(uint32_t)q;
    uint8_t*dst=(uint8_t*)destino;uint32_t used=0;
    while(used<capacidade){if(!wait_tcp(ip,port_local,data,sizeof data,&n,&flags,10000))break;if(n){if(n>capacidade-used)n=(uint16_t)(capacidade-used);for(uint16_t j=0;j<n;j++)dst[used++]=data[j];seq_remoto+=n;tcp_send(ip,port,TCP_ACK,NULL,0);}if(flags&TCP_FIN){seq_remoto++;tcp_send(ip,port,TCP_ACK,NULL,0);break;}if(flags&TCP_RST)return used?(int32_t)used:-1;}
    return (int32_t)used;
}

int32_t sis_http_obter(uint32_t urlp,uint32_t dst,uint32_t cap,uint32_t d,uint32_t e)
{
    (void)d;(void)e;
    if(cap>65536u||cap<128u||!usuario_ok((void*)dst,cap,true))return ERRO_ENDERECO;
    char url[256];int n=usuario_texto((const char*)urlp,url,sizeof url);
    if(n<1)return n<0?n:ERRO_INVALIDO;
    /* int 0x80 usa interrupt gate e entra com IF=0. Habilitamos IRQs para
     * que o relógio avance durante o polling; serializamos a pilha por ora. */
    __asm__ volatile("sti" ::: "memory");
    int32_t r=http_obter(url,(void*)dst,cap);
    __asm__ volatile("cli" ::: "memory");
    return r;
}
