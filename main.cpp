#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <error.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
using std::cout;
using std::endl;


#define WAIT_FOR_PACKET_TIMEOUT 3
#define NUMBER_OF_FAILURES  7
#define MAX_MSG_LEN 512
#define HEADER_SIZE 4
#define OPCODE_WRQ 2
#define OPCODE_ACK 4
#define OPCODE_DATA 3
int session(int sockfd, struct sockaddr_in * clientSock, unsigned int clientSockSize, FILE * file);// int file_fd);
int send_ack(uint16_t block_num, struct sockaddr_in * clientSock, unsigned int clientSockSize, int sockfd);

int main(int argc, char** argv) {
    //get the port to listen
    // check & parse argument
    if (argc != 2) {
        cout << "Invalid number of arguments" << endl;
        return -1;
    }
    uint16_t port = atoi(argv[1]);

    //init socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); //setup a UDP socket
    if (sockfd < 0) {
        perror("TTFTP_ERROR");
        return -1;
    }
    //bind socket
    struct sockaddr_in myAddr = { 0 }, clientAddr = { 0 };
    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myAddr.sin_port = htons(port);

    if (0 > bind(sockfd, (struct sockaddr*) & myAddr, sizeof(myAddr))){
        perror("TTFTP_ERROR");
        close(sockfd);
        return -1;
    }

    // listen to port
    while (1) {
        struct sockaddr_in clientSock;
        unsigned int clientSockLen = sizeof(clientSock);
        char buf[MAX_MSG_LEN];

        // wait for messages
        if (0 > recvfrom(sockfd,&buf,MAX_MSG_LEN,MSG_WAITALL, (struct sockaddr*)&clientSock,&clientSockLen)){
            perror("TTFTP_ERROR");
            continue;
        };
        //parse incoming packet
        uint16_t* pNetOpcode = (uint16_t*)buf;
        uint16_t opcode = ntohs(*pNetOpcode);
        char fileName[MAX_MSG_LEN];
        strcpy(fileName, &buf[2]);
        char transMode[MAX_MSG_LEN];
        strcpy(transMode, &buf[2 + strlen(fileName) + 1]);

        // WRQ packet errors
        if (opcode != OPCODE_WRQ){
            cout << "TTFTP_ERROR: Recieved packet is not WRQ" <<endl;
            continue;
        }
        if (strcmp(transMode,"octet")){
            cout << "TTFTP_ERROR: Transmission mode is not of type octet" << endl;
            continue;
        }
        // WRQ OK
        cout << "IN:WRQ, " << fileName << "," << transMode <<" Where " << fileName << " and " << transMode << " are values of appropriate fields in the packet." << endl;

        //open file for data
        FILE* pFile = fopen(fileName,"w");
        if (NULL == pFile){
            cout << "TTFTP_ERROR: Cannot open file: "<< fileName << endl;
            continue;
        }

        //send ack
        if (send_ack(0,&clientSock,clientSockLen, sockfd) < 0) {
            // send ack failed
            fclose(pFile);
            continue;
        }

        //save the data
        int status = session(sockfd,&clientSock,clientSockLen,pFile);//fileno(pFile));
        fclose(pFile);
    }
}
/// session
/// manage communication process after reciving WRQ and sending ACK 0
/// \param sockfd - server socket to write to
/// \param clientSock - client socket
/// \param clientSockSize - client socket queue size
/// \param file - target file for recived DATA
/// \return 0 if success -1 if fails
int session(int sockfd, struct sockaddr_in * clientSock, unsigned int clientSockSize, FILE * file){//int file_fd) {
    //reciving packet timer
    struct timeval recive_timeout;
    recive_timeout.tv_sec = WAIT_FOR_PACKET_TIMEOUT;
    recive_timeout.tv_usec = 0;
    struct timeval tmp_timeout;
    int failures = 0;
    int package_count = 0;
    char buffer[MAX_MSG_LEN + HEADER_SIZE]; // recived data buffer
    size_t recived_size = 0;
    size_t written_size = 0;
    int status = 0;
    uint16_t netOpcode = 0;
    uint16_t opcode = 0;
    uint16_t netPackage = 0;
    uint16_t package = 0;
    fd_set tmp_fd;


    do {

        // Wait WAIT_FOR_PACKET_TIMEOUT to see if something appears
        // accept only DATA_OPCODE
        FD_SET(sockfd,&tmp_fd);
        tmp_timeout = recive_timeout;
        status = select(sockfd + 1, &tmp_fd, NULL, NULL, &tmp_timeout); //wait until recive packet / timeout
        if (status < 0) {
            perror("TTFTP_ERROR");
            return -1;
        }
        if (status > 0)
        {
            // if there was something at the socket and
            // we are here not because of a timeout
            // read packet from socket
            recived_size = recvfrom(sockfd, &buffer, MAX_MSG_LEN + HEADER_SIZE, MSG_WAITALL, NULL, NULL);
            if (recived_size < 0) {
                perror("TTFTP_ERROR");
                failures++;
                if (failures >= NUMBER_OF_FAILURES) {
                    cout << "FLOWERROR: max failures was reached" << endl;
                    return -1;
                }
            }
            //handle packet  - check opcode
            // Read the DATA packet from the socket (at least we hope this is a DATA packet)
            // parse packet
            netOpcode = *((uint16_t *)buffer);
            opcode = ntohs(netOpcode);
            netPackage = *((uint16_t *)&buffer[2]);
            package = ntohs(netPackage);
            if (opcode == OPCODE_DATA) {
                cout << "IN:DATA, " << package << "," << recived_size << endl;
                if (package == package_count + 1) {
                    // received data packet in order
                    written_size = fwrite(&buffer[HEADER_SIZE], 1, recived_size-HEADER_SIZE, file );
                    if (written_size != recived_size - HEADER_SIZE) {
                        perror("TTFTP_ERROR"); // error writing to file
                        return -1;
                    }
                    if (send_ack(package, clientSock, clientSockSize, sockfd) != 0) {
                        // sending ack failed
                        return -1;
                    }
                    if ((int)recived_size < MAX_MSG_LEN) {
                        // last data packet
                        break;
                    }
                    package_count++;
                }
                else {
                    // not in order
                    cout << "FLOWERROR: package not in order arrived" << endl;
                    break;
                }
            }
            else {
                // not data / wrong package
                break;
            }

        }
        if (status == 0)
        {
            //Time out expired while waiting for data to appear at the socket
            cout << "FLOWERROR: timeout" << endl;
            failures++;
            if (failures >= NUMBER_OF_FAILURES) {
                cout << "FLOWERROR: max failures was reached" << endl;
                return -1;
            }
            else {
                // send new ack
                if (send_ack(package_count, clientSock, clientSockSize, sockfd) != 0) {
                    return -1;
                }

            }
        }

    } while (1); // Continue while some socket was ready
                //but recvfrom somehow failed to read the data

    if (opcode != OPCODE_DATA || package != package_count + 1) {
        // wrong packet: not opcode Data, or not in order - terminate session
        cout << "RECVFAIL" << endl;
        return -1;
    }
    else {
        cout << "RECVOK" << endl;
        return 0;
    }
}

/// packet of ack message
struct ACK_package {
    uint16_t opcode;
    uint16_t block_num;
}__attribute__((packed));

/// send ack
/// creates an ACK packet and send to client (put in socket)
/// \param block_num - data block num to acknowledge
/// \param clientSock -
/// \param clientSockSize - socket max number of clients
/// \param sockfd - file descripor of server's socket
/// \return 0 if success, -1 if fails
int send_ack(uint16_t block_num, struct sockaddr_in * clientSock, unsigned int clientSockSize, int sockfd){//, uint16_t opcode) {
    struct ACK_package ack = { htons(OPCODE_ACK), htons(block_num) };
    int res = sendto(sockfd, (void *)&ack, sizeof(ack), 0, (struct sockaddr *)clientSock, clientSockSize);
    if (res != sizeof(ack)) {
        perror("TTFTP_ERROR");
        return -1;
    }
    else {
        cout << "OUT:ACK, " << block_num << endl;
        return 0;
    }

}

