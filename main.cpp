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

#define NUM_OF_ALLOWED_CONNECTION_REQ 5 //this is a random number I chose.

#define WAIT_FOR_PACKET_TIMEOUT 3
#define NUMBER_OF_FAILURES  7
#define MAX_MSG_LEN 512
#define OPCODE_WRQ 2
#define OPCODE_ACK 4
#define OPCODE_DATA 3
int session(int sockfd, int file_fd);
int send_ack(uint16_t block_num, int sockfd);

int main(int argc, char** argv) {

    //get the port to listen
    if (argc != 2) {
        cout << "Invalid number of arguments" << endl;
        return -1;
    }

    uint32_t port = atoi(argv[1]);

    //init socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); //setup a UDP socket
    if (sockfd < 0) {
        perror("TTFTP_ERROR:");
        return -1;
    }
    //bind socket
    struct sockaddr_in myAddr = { 0 }, clientAddr = { 0 };

    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = INADDR_ANY;
    myAddr.sin_port = htonl(port);

    if (0 > bind(sockfd, (struct sockaddr*) & myAddr, sizeof(myAddr))){
        perror("TTFTP_ERROR:");
        return -1;
    }

    //listen
    listen(sockfd, NUM_OF_ALLOWED_CONNECTION_REQ);

    while (1) {
        char buf[MAX_MSG_LEN];


        if (0 > recvfrom(sockfd,&buf,MAX_MSG_LEN,0, NULL,NULL)){ //TODO (Raveh): make sure this is blocking
            perror("TTFTP_ERROR");
            continue;
        };
        cout << "recived" << endl;
        //parse incoming packet
        uint16_t netOpcode = (uint16_t)buf[0]; // TODO (raveh): remove, this is for debug to see this casting is ok.
        uint16_t opcode = ntohs((uint16_t)buf[0]);

        char fileName[MAX_MSG_LEN];
        strcpy(fileName, &buf[2]);

        char transMode[MAX_MSG_LEN];
        strcpy(transMode, &buf[2 + strlen(fileName) + 1]);// TODO (raveh): make sure this index arithmetic

        if (opcode != 2){
            cout << "TTFTP_ERROR: Recieved packet is not WRQ" <<endl;
            continue;
        }
        if (strcmp(transMode,"octet")){
            cout << "TTFTP_ERROR: Transmission mode is not of type octet" << endl;
            continue;
        }

        cout << "IN:WRQ," << fileName << "," << transMode <<"Where " << fileName << " and " << transMode << "are values of appropriate fields in the packet." << endl;

        //open file for data
        FILE* pFile = fopen(fileName,"w");
        if (NULL == pFile){
            cout << "TTFTP_ERROR: Cannot open file: "<< fileName << endl;
            continue;
        }

        //send ack
        if (send_ack(0, sockfd) < 0) {
            fclose(pFile);
            continue;
        }

        //save the data
        int status = session(sockfd,fileno(pFile));
        fclose(pFile);
    }
}

int session(int sockfd,  int file_fd) {
    //reciving packet timer
    struct timeval recive_timeout = { WAIT_FOR_PACKET_TIMEOUT };
    //recive_timeout.tv_sec = WAIT_FOR_PACKET_TIMEOUT;
    //recive_timeout.tv_sec = 3;
    //count failures
    int failures = 0;
    int package_count = 0;
    int recived_package = 0;
    char buffer[MAX_MSG_LEN];
    size_t recived_size = 0;
    size_t written_size = 0;
    int status = 0;
    uint16_t netOpcode = 0;
    uint16_t opcode = 0;
    uint16_t netPackage = 0;
    uint16_t package = 0;
    do {

        // TODO: Wait WAIT_FOR_PACKET_TIMEOUT to see if something appears
        // for us at the socket (we are waiting for DATA)
        status = select(sockfd + 1, (fd_set*)&sockfd, NULL, NULL, &recive_timeout); //wait

        if (status < 0) {
            perror("TTFTP ERROR:");
            return -1; // TODO: check if need to return here or only failures++
        }
        if (status > 0) // TODO: if there was something at the socket and
            // we are here not because of a timeout
        {
            //read();
            recived_size = recvfrom(sockfd, &buffer, MAX_MSG_LEN, MSG_WAITALL, NULL, NULL); //TODO (Raveh): make sure this is blocking
            if (recived_size < 0) {
                perror("TTFTP ERROR:");
            }
            //handle();
            // TODO: Read the DATA packet from the socket (at
            // least we hope this is a DATA packet)
            netOpcode = (uint16_t)buffer[0]; // TODO (raveh): remove, this is for debug to see this casting is ok.
            opcode = ntohs(netOpcode);
            netPackage = (uint16_t)buffer[3]; // TODO (raveh): remove, this is for debug to see this casting is ok.
            package = ntohs(netPackage);
            if (opcode == OPCODE_DATA) {
                cout << "IN:DATA, " << package << "," << recived_size << endl;
                if (package == package_count + 1) {
                    // if data ok
                    package_count++;
                    written_size = write(file_fd, &buffer[5], recived_size);
                    if (written_size != recived_size) {
                        perror("TTFTP ERROR:"); // error writing to file
                    }
                    if (send_ack(package, sockfd) != 0) {
                        return -1;
                    }
                    if ((int)recived_size < MAX_MSG_LEN) {
                        break;
                    }
                }
                else {
                    // not in order
                    cout << "FLOW ERROR: package not in order arrived" << endl;
                    break;
                }
            }
            else {
                // not data / wrong package
                break;
            }

        }
        if (status == 0) // TODO: Time out expired while waiting for data
            // to appear at the socket
        {
            cout << "FLOWERROR: timeout" << endl;
            if (++failures > NUMBER_OF_FAILURES) {
                cout << "FLOWERROR: max failures was reached" << endl;
                return -1;
            }
            else {
                // send new ack
                if (send_ack(package_count, sockfd) != 0) {
                    return -1;
                }
                continue;
            }
        }

    } while (1); // TODO: Continue while some socket was ready
    // but recvfrom somehow failed to read the data

    if (opcode != OPCODE_DATA || package != package_count + 1) {
        // TODO: We got something else but DATA
        // TODO: The incoming block number is not what we have
        // expected, i.e. this is a DATA pkt but the block number
        // in DATA was wrong (not last ACK’s block number + 1)

        cout << "RECVFAIL" << endl;
        return -1;
        // FATAL ERROR BAIL OUT
    }
    else {
        cout << "RECVOK" << endl;
        return 0;
    }
}

struct ACK_package {
    uint16_t block_num;
    uint16_t opcode;
}__attribute__((packed));

int send_ack(uint16_t block_num, int sockfd) {

    struct ACK_package ack = { block_num, OPCODE_ACK };
    int res = write(sockfd, (void*)&ack, sizeof(ack));
    if (res < 0) {
        perror("TTFTP ERROR:");
        return -1;
    }
    else {
        cout << "OUT:ACK, " << block_num << endl;
        return 0;
    }

}

