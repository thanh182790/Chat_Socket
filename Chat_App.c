
/***********************************************************************************
								CHAT APPLICATION
	- main process is a passive socket ( side server )
	- After recived any connecttion main process will creat a thread to chat. This thread is a active socket ( side client )
************************************************************************************/

/***********************************************************************************
								Includes
************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

/***********************************************************************************
								Define
************************************************************************************/
#define MAX_CONNECTS 5
#define BACK_LOG MAX_CONNECTS
#define BUFF_SIZE 100
#define INET_ADDR_LEN  16
#define LEN_CMD 200
#define TERMINATE_CODE  "0x59"
/***********************************************************************************
								Global variables
************************************************************************************/
typedef struct
{
	int id_connect;
	int socket_fd;
	int port_number;
	struct sockaddr_in sockAddress;
    char IpAddressString[INET_ADDR_LEN];
}Connection_t;

pthread_t threadIdAccept;
pthread_t threadIdReciveData[MAX_CONNECTS];

Connection_t listConnection[MAX_CONNECTS];
Connection_t* this = NULL;
int numberConnection = 0;

pthread_mutex_t lockUpdateConnection = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lockSendMessage = PTHREAD_MUTEX_INITIALIZER;


/***********************************************************************************
								Prototypes
************************************************************************************/
void Menu(void);
static void* threadAcceptConnection(void* arg);
static void* threadReciverMessage(void* arg);
void showIP();
void showPort(Connection_t* currentConnect);
void showListConnection(Connection_t* list);
int connectToServer(char* ip, int* port ,int* socketFdServer);
int sendMessage(int* idConnect, char* msg);
int terminateConnet(int* idConnet);
void exitApp(void);
/***********************************************************************************
								Codes
************************************************************************************/

int main(int argc, char *argv[])
{
	int opt = 1;
    char userCmd[1024];
    char typeCmd[10];
    char ipCmd[16];
    int idCmd;
    int portCmd;
    int socketFdServer;

    this = (Connection_t *) malloc(sizeof(Connection_t));

	/* Check syntax run program */
	if (argc < 2) {
		printf("No port provided\ncommand: ./Chat_App <port number>\n");
		exit(EXIT_FAILURE);
	}
	else
	    this->port_number = atoi(argv[1]); /* convert a string to integer */
	
	/* Create socket fd */
	this->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(this->socket_fd == -1){
		printf("Socket creat fail !!!!! \n");
		exit(EXIT_FAILURE);
	}
	else if (setsockopt(this->socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
		/* Force socket attach with port_number . This will prevent error "address already in use" */
		printf("Setsockopt error !!! \n");
		exit(EXIT_FAILURE);
	}
	
	/* Initial address for socket */
    this->sockAddress.sin_family = AF_INET; /* for Ipv4 , AF_UNSPEC for Ipv4 or Ipv6*/
	this->sockAddress.sin_port = htons(this->port_number);  /* htons (host to net) to convert to network byte order */
	this->sockAddress.sin_addr.s_addr  = INADDR_ANY;   /* INADDR_ANY to help socket recvice any adrress when process run to other device that DHCP auto config new IP */
	inet_ntop(AF_INET, &(this->sockAddress.sin_addr), this->IpAddressString, INET_ADDR_LEN);
    printf("%s", this->IpAddressString);
	/* Binding address for socket */
	if (bind(this->socket_fd, (struct sockaddr*)&this->sockAddress, sizeof(this->sockAddress)) == -1){
		printf("Binding address error !!! \n");
		exit(EXIT_FAILURE);
	}
	
	/* Nghe tối đa 5 kết nối trong hàng đợi */
	if (listen(this->socket_fd, BACK_LOG) == -1){
		printf("Listen connecttion error !!! \n");
		exit(EXIT_FAILURE);
	}
	
	Menu();
	printf("Application is listenning at port %d \n", this->port_number);
	
	/* Creat a thread to accept connections */
	if(pthread_create(&threadIdAccept, NULL, &threadAcceptConnection, NULL)){
		printf(" Creat thread accept connections error !!! \n");
		exit(EXIT_FAILURE);
	}
	pthread_detach(threadIdAccept);

	/* hadle tasks in Menu */
	while(1)
	{
		printf("Enter your cmd: ");
        fgets(userCmd, 1024, stdin);
        sscanf(userCmd, "%s", typeCmd);

        if(!strcmp("help", typeCmd))
        {
            Menu();
        }
        else if(!strcmp("myip", typeCmd))
        {
            showIP();
        }
        else if(!strcmp("myport", typeCmd))
        {
            showPort(this);
        }
        else if(!strcmp("list", typeCmd))
        {
            showListConnection(listConnection);
        }
        else if(!strcmp("connect", typeCmd))
        {
            if(numberConnection == MAX_CONNECTS)
            {
                printf("Number Connection has reached the limit !! \n");
            }
            else
            {
                sscanf(userCmd, "%s %s %d", typeCmd, ipCmd, &portCmd);
                /* this case app is client want to connect to server */
                if(-1 == connectToServer(ipCmd, &portCmd, &socketFdServer))
                {
                    printf("Can not connect to server %s, at port %d \n", ipCmd, portCmd);
                }
                else
                {
                    printf("Connect to server %s, at port %d sucesss\n", ipCmd, portCmd);
                    pthread_mutex_lock(&lockUpdateConnection);
                    listConnection[numberConnection].id_connect = numberConnection + 1;
                    listConnection[numberConnection].port_number = portCmd;
                    listConnection[numberConnection].socket_fd = socketFdServer;
                    inet_pton(AF_INET, ipCmd, &listConnection[numberConnection].sockAddress.sin_addr);
                    numberConnection++;
                    pthread_mutex_unlock(&lockUpdateConnection);
                                /* Create a thread to receive data from this peer */
                    if (pthread_create(&threadIdReciveData[numberConnection-1], NULL, &threadReciverMessage, (void*)&listConnection[numberConnection-1]) != 0)
                    {
                        printf(" Creat thread recive message error !!! \n");
                        exit(EXIT_FAILURE);
                    }
                    pthread_detach(threadIdReciveData[numberConnection-1]);
                }
            }

        }
        else if(!strcmp("send", typeCmd))
        {
            /* Command: Send a message to a peer */
            int idConect;
            char idConnectString[10];
            char *msg;
            int ret_val;

            sscanf(userCmd, "%s %d", typeCmd, &idConect);
            sprintf(idConnectString, "%d", idConect);

            msg = userCmd + (strlen(typeCmd) + strlen(idConnectString) + 2); /* 2 is white space example: send 1 Xin chao cac ban */

            pthread_mutex_lock(&lockSendMessage);

            ret_val = sendMessage(&idConect, msg);

            pthread_mutex_unlock(&lockSendMessage);

            if (ret_val == 0)
            {      
                printf("\nSent message successfully.\n");
            }
            else if (ret_val == -1)
            {
                printf("Send msg error !!\n");
            }
        }
        else if(!strcmp("terminate", typeCmd))
        {
            /* Command: terminate 1  */
            int ret_val;
            int idConnect;
            sscanf(userCmd, "%s %d", typeCmd, &idConnect);

            pthread_mutex_lock(&lockUpdateConnection);

            ret_val = terminateConnet(&idConnect);

            if (ret_val == 0)
            { 
                printf("\nTerminate connect with ID %d successfully.\n", idConnect);
            }
            else if(ret_val == -1)
            {
                printf("\nNo connetion is available for terminating.\n");
            }

            numberConnection--;
            pthread_mutex_unlock(&lockUpdateConnection);
        }
        else if(!strcmp("exit", typeCmd))
        {
            exitApp();
        }
        else if(!strcmp("clear", typeCmd))
        {
            system("clear");
        }
        else
        {
            printf("Your command invalid. Please check again. \n");
            Menu();
        }
	}
	return 0;
}
/* Show Menu for user */
void Menu()
{
    printf("\n******************************** Chat Application ********************************\n");
    printf("\nUse the commands below:\n");
    printf("1. help                             : display user interface options\n");
    printf("2. myip                             : display IP address of this app\n");
    printf("3. myport                           : display listening port of this app\n");
    printf("4. connect <destination> <port no>  : connect to the app of another computer\n");
    printf("5. list                             : list all the connections of this app\n");
    printf("6. terminate <connection id>        : terminate a connection\n");
    printf("7. send <connection id> <message>   : send a message to a connection\n");
    printf("8. clear                            : clear screen\n");
    printf("9. exit                             : close all connections & terminate this app\n");  
    printf("\n**********************************************************************************\n");
}

static void* threadAcceptConnection(void* arg)
{
	int newSocketClientFd;
    struct sockaddr_in clientAddress;
    socklen_t lenAddress = sizeof(clientAddress);
    int portClient = 0;
    char addr_in_str[INET_ADDR_LEN];

	while(1)
	{
		newSocketClientFd = accept(this->socket_fd, (struct sockaddr*)&clientAddress, (socklen_t *)&lenAddress);
        if(newSocketClientFd == -1)
        {
            printf(" Create new socket fd client for handle error !!! \n");
            exit(EXIT_FAILURE);
        }

        pthread_mutex_lock(&lockUpdateConnection);
        /* Updtae list connecttion in here */
        if(numberConnection < MAX_CONNECTS)
        {
            inet_ntop(AF_INET, &clientAddress.sin_addr, addr_in_str, INET_ADDR_LEN); /* conver IP from binary to text */
            portClient = ntohs(clientAddress.sin_port);
            listConnection[numberConnection].id_connect = numberConnection + 1;
            listConnection[numberConnection].port_number = portClient;
            listConnection[numberConnection].socket_fd = newSocketClientFd;
            listConnection[numberConnection].sockAddress = (struct sockaddr_in)clientAddress;
            numberConnection++;
            /* Create a thread to receive data from this peer */
            if (pthread_create(&threadIdReciveData[numberConnection-1], NULL, &threadReciverMessage, (void*)&listConnection[numberConnection-1]) != 0)
            {
                printf(" Creat thread recive message error !!! \n");
                exit(EXIT_FAILURE);
            }
            pthread_detach(threadIdReciveData[numberConnection-1]);
            printf("Accept connection from IP %s, at port %d \n", addr_in_str, portClient);
        }
        else
        {
            printf(" Cannot establish new connection !!! \n");
        }
        pthread_mutex_unlock(&lockUpdateConnection);
	}
}

static void* threadReciverMessage(void* arg)
{
    char receiveBuff[BUFF_SIZE];
    Connection_t* clientConnection = (Connection_t*)arg;
    int readBytes = 0;
    while(1)
    {  
        readBytes = read(clientConnection->socket_fd, receiveBuff, BUFF_SIZE);
        if(readBytes < 0)
        {
            printf(" Recived msg error !! \n");
            exit(EXIT_FAILURE);
        }
        else if (!strcmp(receiveBuff, TERMINATE_CODE))
        {
            close(clientConnection->socket_fd);
            printf("\nThe Connection at port %d has disconnected.\n", clientConnection->port_number);
            pthread_mutex_lock(&lockUpdateConnection);
            numberConnection --;
            pthread_mutex_unlock(&lockUpdateConnection);
            break;
        }
        else
        {
            printf("\n********************************************\n");
            printf("* Message received from: %s\n", inet_ntoa(clientConnection->sockAddress.sin_addr));
            printf("* Sender's port: %d\n", clientConnection->port_number);
            printf("* Message: %s", receiveBuff);
            printf("********************************************\n");
            printf("\nEnter your command: ");
            memset(receiveBuff, 0, sizeof(receiveBuff));
        }
    }
    pthread_exit(NULL);
}

void showIP(void)
{
    char ip_address[16];

    FILE *fp = popen("hostname -I", "r");

    fscanf(fp, "%s", ip_address);

    printf("Current IP address of this app is : %s \n", ip_address);
}

void showPort(Connection_t* currentConnect)
{
    printf("Current Port of this app is : %d \n", currentConnect->port_number);
}

void showListConnection(Connection_t* list)
{
    printf("\n********************************************\n");
    printf("ID |        IP Address         | Port No.\n");
    for (int i = 0; i < numberConnection; i++)
    {
        printf("%d  |     %s       | %d\n", list[i].id_connect, inet_ntoa(list[i].sockAddress.sin_addr), list[i].port_number);
    }
    printf("********************************************\n");
}

int connectToServer(char* ip, int* port ,int* socketFdServer)
{
    int server_fd;
    struct sockaddr_in serv_addr;

    memset(&serv_addr, '0',sizeof(serv_addr));
    /* Create socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1 )
        return -1;

    /* intial add server */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(*port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) == -1) 
        return -1;
    /* connect to server*/
    if (connect(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        return -1;
    socketFdServer = &server_fd;
	return 0;
}

int sendMessage(int* idConnect, char* msg)
{
    int numbWrite;
    numbWrite = write(listConnection[*idConnect].socket_fd, msg, strlen(msg));
    if(numbWrite <= 0)
        return -1;
    return numbWrite;
}

int terminateConnet(int* idConnet)
{
    return sendMessage(idConnet, TERMINATE_CODE);
}

void exitApp(void)
{
    for (int i = 0; i < numberConnection; i++)
    {
        terminateConnet(&i);
    }
    close(this->socket_fd);
    printf("Exit App success !!! \n");
    exit(EXIT_SUCCESS);
}