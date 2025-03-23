#include "engine.hh"

#include "utils.cc"
#include "buffer.hh"


// Arquivo de testes temporario para testar funcionamento da engine

Engine engine = Engine();

void handleSignal(int signal) {
    if (signal == SIGIO) {
        std::cout << "SIGIO recebido: dados disponíveis no socket\n";
    }
    std::cout << "Dados recebidos:\n";

    Buffer * buf = new Buffer();
    buf->length = 1522;

    struct sockaddr saddr;

    buf->length = engine.receive(buf, saddr);

    printEth(buf->data, buf->length);
    fflush(stdout);

    delete buf;
}

// Funcões para montar o quadro ethernet --------------------------------------

void get_mac(Buffer * buf, ifreq ifreq_c, int sock_raw)
{
	memset(&ifreq_c,0,sizeof(ifreq_c));
	strncpy(ifreq_c.ifr_name,"lo",IFNAMSIZ-1);

	if((ioctl(sock_raw,SIOCGIFHWADDR,&ifreq_c))<0)
		printf("error in SIOCGIFHWADDR ioctl reading");
	
	struct ethhdr *eth = (struct ethhdr *)(buf->data);
	memcpy(eth->h_source, ifreq_c.ifr_hwaddr.sa_data, ETH_ALEN);
	memset(eth->h_dest, 0xFF, ETH_ALEN);

	printf("Source mac= %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",(unsigned char)(eth->h_source[0]),(unsigned char)(eth->h_source[1]),(unsigned char)(eth->h_source[2]),(unsigned char)(eth->h_source[3]),(unsigned char)(eth->h_source[4]),(unsigned char)(eth->h_source[5]));
	printf("Dest mac= %.2X-%.2X-%.2X-%.2X-%.2X-%.2X\n",(unsigned char)(eth->h_dest[0]),(unsigned char)(eth->h_dest[1]),(unsigned char)(eth->h_dest[2]),(unsigned char)(eth->h_dest[3]),(unsigned char)(eth->h_dest[4]),(unsigned char)(eth->h_dest[5]));

   	eth->h_proto = htons(ETH_P_802_EX1);   //0x88B5

	buf->length+=sizeof(struct ethhdr);
}

void get_data(Buffer * buf)
{
	buf->data[buf->length++]	=	0xAA;
	buf->data[buf->length++]	=	0xBB;
	buf->data[buf->length++]	=	0xCC;
	buf->data[buf->length++]	=	0xDD;
	buf->data[buf->length++]	=	0xEE;
}

void get_eth_index(ifreq & ifreq_i, int sock_raw)
{
	memset(&ifreq_i,0,sizeof(ifreq_i));
	strncpy(ifreq_i.ifr_name,"lo",IFNAMSIZ-1);

	if((ioctl(sock_raw,SIOCGIFINDEX,&ifreq_i))<0)
		printf("error in index ioctl reading");

	printf("index=%d\n",ifreq_i.ifr_ifindex);
}

// ----------------------------------------------------------------------------

int main (int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: send? 1 para sim, 0 para nao";
        return 1;
    }
    const int send = atoi(argv[1]);

    engine.setupSignalHandler(handleSignal);


    if (send) {
        
        // Setta interface
        struct ifreq ifreq_c, ifreq_i;

        Buffer * buf = new Buffer();
	    get_eth_index(ifreq_i, engine.socket_raw);  // interface number
        get_mac(buf, ifreq_c, engine.socket_raw);   // Setta macs do quadro de ethernet
        get_data(buf);                              // Setta dados do quadro
                                     
        // Setta endereço alvo
        struct sockaddr_ll sadr_ll;
        sadr_ll.sll_ifindex = ifreq_i.ifr_ifindex;
        sadr_ll.sll_halen = ETH_ALEN;  // Numero de octetos do endereco MAC
        unsigned char broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(sadr_ll.sll_addr, broadcast_mac, ETH_ALEN);  // Endereco broadcast MAC
        
        engine.send(buf, (sockaddr *)&sadr_ll);
        delete buf;
    } else {
        while(1) {sleep(10);}
    }
}

