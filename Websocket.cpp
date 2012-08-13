#include "Websocket.h"
#include <string>

#define MAX_TRY_WRITE 5
#define MAX_TRY_READ 3

//#define DEBUG

Websocket::Websocket(char * url) {
    fillFields(url);
    socket.set_blocking(false, 2000);
}

void Websocket::fillFields(char * url) {
    char *res = NULL;
    char *res1 = NULL;

    char buf[50];
    strcpy(buf, url);

    res = strtok(buf, ":");
    if (strcmp(res, "ws")) {
#ifdef DEBUG
        printf("\r\nFormat printfor: please use: \"ws://ip-or-domain[:port]/path\"\r\n\r\n");
#endif
    } else {
        //ip_domain and port
        res = strtok(NULL, "/");

        //path
        res1 = strtok(NULL, " ");
        if (res1 != NULL) {
            path = res1;
        }

        //ip_domain
        res = strtok(res, ":");

        //port
        res1 = strtok(NULL, " ");
        if (res1 != NULL) {
            port = res1;
        } else {
            port = "80";
        }

        if (res != NULL) {
            ip_domain = res;
        }
    }
}


bool Websocket::connect() {
    char cmd[200];
    
    while (socket.connect(ip_domain.c_str(), atoi(port.c_str())) < 0) {
        printf("Unable to connect to (%s) on port (%d)\r\n", ip_domain.c_str(), atoi(port.c_str()));
        wait(0.2);
    }

    // sent http header to upgrade to the ws protocol
    sprintf(cmd, "GET /%s HTTP/1.1\r\n", path.c_str());
    write(cmd, strlen(cmd));

    sprintf(cmd, "Host: %s:%s\r\n", ip_domain.c_str(), port.c_str());
    write(cmd, strlen(cmd));

    sprintf(cmd, "Upgrade: WebSocket\r\n");
    write(cmd, strlen(cmd));

    sprintf(cmd, "Connection: Upgrade\r\n");
    write(cmd, strlen(cmd));

    sprintf(cmd, "Sec-WebSocket-Key: L159VM0TWUzyDxwJEIEzjw==\r\n");
    write(cmd, strlen(cmd));

    sprintf(cmd, "Sec-WebSocket-Version: 13\r\n\r\n");
    int ret = write(cmd, strlen(cmd));
    if(ret != strlen(cmd))
    {
      close();
      printf("Could not send request");
      return false;
    }

    ret = read(cmd, 200);
    if(ret < 0)
    {
      close();
      printf("Could not receive answer\r\n");
      return false;
    }
    
    cmd[ret] = '\0';
#ifdef DEBUG
    printf("recv: %s\r\n", cmd);
#endif

    if( strstr(cmd, "DdLWT/1JcX+nQFHebYP+rqEx5xI=") == NULL )
    {
      printf("Wrong answer from server, got \"%s\" instead\r\n", cmd);
      do{
        ret = read(cmd, 200);
        if(ret < 0)
        {
          printf("Could not receive answer\r\n");
          return false;
        }
        cmd[ret] = '\0';
        printf("%s",cmd);
      } while(ret > 0);
      close();
      return false;
    }
    
    printf("\r\nip_domain: %s\r\npath: /%s\r\nport: %s\r\n\r\n", ip_domain.c_str(), path.c_str(), port.c_str());
    return true;
}

int Websocket::sendLength(uint32_t len) {

    if (len < 126) {
        sendChar(len | (1<<7));
        return 1;
    } else if (len < 65535) {
        sendChar(126 | (1<<7));
        sendChar((len >> 8) & 0xff);
        sendChar(len & 0xff);
        return 3;
    } else {
        sendChar(127 | (1<<7));
        for (int i = 0; i < 8; i++) {
            sendChar((len >> i*8) & 0xff);
        }
        return 9;
    }
}

int Websocket::sendChar(char c) {
    return write(&c, 1);
}

int Websocket::readChar(char * pC, bool block)
{
    return read(pC, 1, block);
}

int Websocket::sendOpcode(uint8_t opcode) {
    return sendChar(0x80 | (opcode & 0x0f));
}

int Websocket::sendMask() {
    for (int i = 0; i < 4; i++) {
        sendChar(0);
    }
    return 4;
}

int Websocket::send(char * str) {
    sendOpcode(0x01);
    sendLength(strlen(str));
    sendMask();
    int res = write(str, strlen(str));
    return res;
}


bool Websocket::read(char * message) {
    int i = 0;
    uint32_t len_msg;
    char opcode = 0;
    char c;
    char mask[4] = {0, 0, 0, 0};
    bool is_masked = false;
    Timer tmr;
    
    // read the opcode
    tmr.start();
    while (true) {
        if (tmr.read() > 3) {
            printf("timeout ws\r\n");
            return false;
        }
        
        socket.set_blocking(false, 1);
        if (socket.receive(&opcode, 1) != 1) {
            socket.set_blocking(false, 2000);
            return false;
        }
        
        socket.set_blocking(false, 2000);

        if (opcode == 0x81)
            break;
    }
#ifdef DEBUG
    printf("opcode: 0x%X\r\n", opcode);
#endif

    readChar(&c);
    len_msg = c & 0x7f;
    is_masked = c & 0x80;
    if (len_msg == 126) {
        readChar(&c);
        len_msg = c << 8;
        readChar(&c);
        len_msg += c;
    } else if (len_msg == 127) {
        len_msg = 0;
        for (int i = 0; i < 8; i++) {
            readChar(&c);
            len_msg += (c << (7-i)*8);
        }
    }
    
    if(len_msg == 0) {
        return false;
    }
#ifdef DEBUG
    printf("length: %d\r\n", len_msg);
#endif
    if (is_masked) {
        for (i = 0; i < 4; i++)
            readChar(&c);
            mask[i] = c;
    }
    
    int nb = read(message, len_msg, false);
    if (nb != len_msg)
        return false;

    for (i = 0; i < len_msg; i++) {
        message[i] = message[i] ^ mask[i % 4];
    }
    
    message[len_msg] = '\0';
    
    return true;
}

bool Websocket::close() {
    if(!is_connected())
      return false;
      
    int ret = socket.close();
    if (ret < 0)
    {
      printf("Could not disconnect");
      return false;
    }
    return true;
}

bool Websocket::is_connected() {
    return socket.is_connected();
}

std::string Websocket::getPath() {
    return path;
}

int Websocket::write(char * str, int len, uint32_t timeout) {
    int res = 0, idx = 0;
    for (int j = 0; j < MAX_TRY_WRITE; j++) {
        if (idx == len)
            return len;
        for(int i = 0; i < MAX_TRY_WRITE; i++) {
            if ((res = socket.send_all(str + idx, len - idx)) != -1)
                break;
            
            if (i == MAX_TRY_WRITE - 1)
                return -1;
        }
        idx += res;
    }
    return idx;
}

int Websocket::read(char * str, int len, bool block) {
    int res = 0, idx = 0;
    for (int j = 0; j < MAX_TRY_READ; j++) {
    
        if (idx == len)
            return len;
            
        for(int i = 0; i < MAX_TRY_READ; i++) {
        
            if (block) {
                if ((res = socket.receive_all(str + idx, len - idx)) != -1)
                    break;
            } else {
                if ((res = socket.receive(str + idx, len - idx)) != -1)
                    break;
            }
            if (i == MAX_TRY_READ - 1 || !block)
                return (idx == 0) ? -1 : idx;
        }
        
        idx += res;
    }
    return idx;
}
