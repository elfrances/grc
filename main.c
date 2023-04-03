#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_MAX_BUF_LEN 10000

//static char tcpSndBuf[TCP_MAX_BUF_LEN];

int main(int argc, char *argv[])
{
    const char *locAddr;
    const char *remAddr;
    uint16_t locPort;
    uint16_t remPort;
    const char *name;
    const char *gender;
    const char *age;
    const char *ride;
    struct in_addr addr4;
    struct in6_addr addr6;
    struct sockaddr_storage locSock = {0};
    struct sockaddr_storage remSock = {0};
    int af, sd;
    socklen_t sockLen;
    int enabled = 1;
    int sndBufSize = 512 * 1024;
    char msgBuf[1024];
    int bibNum;
    time_t startTime;

    if (argc != 9) {
        printf("SYNTAX: grc <locAddr> <locPort> <remAddr> <remPort> <name> <gender> <age> <ride>\n");
        return -1;
    }

    locAddr = argv[1];
    locPort = atoi(argv[2]);
    remAddr = argv[3];
    remPort = atoi(argv[4]);
    name = argv[5];
    gender = argv[6];
    age = argv[7];
    ride = argv[8];

    if ((remPort < 49152) || (remPort > 65535)) {
        printf("ERROR: illegal local port %u. Valid range is 49152-65535\n", locPort);
        return -1;
    }

    if (inet_pton(AF_INET, locAddr, &addr4) == 1) {
        struct sockaddr_in *pSock = (struct sockaddr_in *) &locSock;
        af = AF_INET;
        pSock->sin_family = AF_INET;
        pSock->sin_port = htons(locPort);
        memcpy(&pSock->sin_addr, &addr4, sizeof (addr4));
        sockLen = sizeof (struct sockaddr_in);
    } else if (inet_pton(AF_INET6, locAddr, &addr6) == 1) {
        struct sockaddr_in6 *pSock = (struct sockaddr_in6 *) &locSock;
        af = AF_INET6;
        pSock->sin6_family = AF_INET6;
        pSock->sin6_port = htons(locPort);
        memcpy(&pSock->sin6_addr, &addr6, sizeof (addr6));
        sockLen = sizeof (struct sockaddr_in6);
    } else {
        printf("ERROR: illegal local address %s\n", locAddr);
        return -1;
    }

    if (inet_pton(AF_INET, remAddr, &addr4) == 1) {
        struct sockaddr_in *pSock = (struct sockaddr_in *) &remSock;
        pSock->sin_family = AF_INET;
        pSock->sin_port = htons(remPort);
        memcpy(&pSock->sin_addr, &addr4, sizeof (addr4));
    } else if (inet_pton(AF_INET6, remAddr, &addr6) == 1) {
        struct sockaddr_in6 *pSock = (struct sockaddr_in6 *) &remSock;
        pSock->sin6_family = AF_INET6;
        pSock->sin6_port = htons(remPort);
        memcpy(&pSock->sin6_addr, &addr6, sizeof (addr6));
    } else {
        printf("ERROR: illegal local address %s\n", remAddr);
        return -1;
    }

    if (locSock.ss_family != remSock.ss_family) {
        printf("ERROR: incompatible local/remote IP addresses\n");
        return -1;
    }

    printf("grc: local=%s:%u remote=%s:%u name=%s gender=%s age=%s ride=%s\n",
            locAddr, locPort, remAddr, remPort, name, gender, age, ride);

    if ((sd = socket(af, SOCK_STREAM, 0)) < 0) {
        printf("ERROR: failed to alloc socket: %s\n", strerror(errno));
        return -1;
    }

    if (setsockopt(sd, SOL_SOCKET, SO_SNDBUF,  (char *) &sndBufSize, sizeof (sndBufSize)) < 0) {
        printf("ERROR: failed to set SO_SNDBUF option: %s\n", strerror(errno));
        return -1;
    }

    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR,  (char *) &enabled, sizeof (enabled)) < 0) {
        printf("ERROR: failed to set SO_REUSEADDR option: %s\n", strerror(errno));
        close(sd);
        return -1;
    }

    if (bind(sd, (struct sockaddr *) &locSock, sockLen) < 0) {
        printf("ERROR: bind failed: %s\n", strerror(errno));
        return -1;
    }

    if (connect(sd, (struct sockaddr *) &remSock, sockLen) < 0) {
        printf("ERROR: failed to connect socket: %s\n", strerror(errno));
        close(sd);
        return -1;
    }

    printf("Connected to GRS server!\n");

    // Send the "regReq" message
    {
        size_t msgLen;

        snprintf(msgBuf, sizeof (msgBuf), "{\"msgType\": \"regReq\", \"name\": \"%s\", \"gender\": \"%s\", \"age\": \"%s\", \"ride\": \"%s\"}", name, gender, age, ride);
        msgLen = strlen(msgBuf) + 1;

        if (send(sd, msgBuf, msgLen, 0) != msgLen) {
            printf("ERROR: send(regReq) failed: %s\n", strerror(errno));
            close(sd);
            return -1;
        }
    }

    // Wait for the "regResp" message
    {
        size_t msgLen;
        const char *p;

        if ((msgLen = recv(sd, msgBuf, sizeof (msgBuf), 0)) < 0) {
            printf("ERROR: recv(regResp) failed: %s\n", strerror(errno));
            close(sd);
            return -1;
        }
        printf("Received: %s\n", msgBuf);
        if (strstr(msgBuf, "\"msgType\": \"regResp\"") == NULL) {
            printf("ERROR: unexpected message type!\n");
            close(sd);
            return -1;
        }
        if (strstr(msgBuf, "\"status\": \"success\"") == NULL) {
            printf("ERROR: regReq failed!\n");
            close(sd);
            return -1;
        }
        if ((p = strstr(msgBuf, "\"bibNum\"")) == NULL) {
            printf("ERROR: no bib number! msgBuf=%s\n", msgBuf);
            close(sd);
            return -1;
        } else if (sscanf(p, "\"bibNum\": \"%d\"", &bibNum) != 1) {
            printf("ERROR: invalid bib number!\n");
            close(sd);
            return -1;
        }
        if ((p = strstr(msgBuf, "\"startTime\"")) == NULL) {
            printf("ERROR: no startTime! msgBuf=%s\n", msgBuf);
            close(sd);
            return -1;
        } else if (sscanf(p, " \"startTime\": \"%ld\"", &startTime) != 1) {
            printf("ERROR: invalid startTime!\n");
            close(sd);
            return -1;
        }
        printf("regResp: binNum=%d startTime=%ld\n", bibNum, startTime);
    }

    // Wait for the "go" message
    {
        size_t msgLen;

        if ((msgLen = recv(sd, msgBuf, sizeof (msgBuf), 0)) < 0) {
            printf("ERROR: recv(go) failed: %s\n", strerror(errno));
            close(sd);
            return -1;
        }
        printf("Received: %s\n", msgBuf);
        if (strstr(msgBuf, "\"msgType\": \"go\"") == NULL) {
            printf("ERROR: unexpected message type!\n");
            close(sd);
            return -1;
        }
    }

    // Send the "progUpd" messages
    {
        size_t msgLen;

        for (int n = 0; n < 10; n++) {
            time_t now = time(NULL);
            snprintf(msgBuf, sizeof (msgBuf), "{\"msgType\": \"progUpd\", \"time\": \"%ld\", \"distance\": \"%d\", \"speed\": \"%d\",  \"power\": \"%d\"}", now, n, n*2, n*2);
            msgLen = strlen(msgBuf) + 1;

            if (send(sd, msgBuf, msgLen, 0) != msgLen) {
                printf("ERROR: send(progRep) failed: %s\n", strerror(errno));
                close(sd);
                return -1;
            }

            sleep(1);
        }
    }

    close(sd);

    return 0;
}




