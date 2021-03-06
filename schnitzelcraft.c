#include <stdio.h>
#include <tchar.h>
#include <winsock2.h>
#include <assert.h>
#include <math.h>
//#include "include/stdint.h"
#include "include/zlib.h"

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned long uint32_t;
typedef signed long int32_t;

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif
//#define CHUNK 16384
#define CHUNK 1024

// Uncomment to enable physics
//#define PHYSICS

#define flippy 0 // Cos nopflip
#define numzombies 16 // Cos the more the deadlier etc
//#define numzombies 0
#define maxclients 64 // Player ID cannot be >127 (< 0 signed) as this means "teleport"
#define maxsnow 64

#define blockAt(x, y, z) ( ((x)>=0 && (y)>=0 && (z)>=0 && (x)<mapx && (y)<mapy && (z)<mapz) ? (block + (y)*mapx*mapz + (z)*mapz + (x)) : (block) )

struct BLOCKCHANGE {
        char player;
        short x;
        short y;
        short z;
        char newvalue;
} BLOCKCHANGE;
struct MOB {
        char used;
        char respawn;
        char direction;
        char hp;
        char name[65];
        // Position
        short x;
        short y;
        short z;
        char heading;
        char pitch;
} MOB;
struct CLIENT {
        char used;
        char op;
        SOCKET socket;
        char stage; // 0 - Connected
                       // 1 - Authenticated
                       // 2 - Welcome Message Sent
                       // 3 - Map sent
                       // 4 - Other players spawned, ready
        char name[65];
        char protocol;
        // Position
        short x;
        short y;
        short z;
        char heading;
        char pitch;
} CLIENT;
struct SNOW {
    short x;
    short y;
    short z;
    char xi; // Increment (speed/direction)
    char yi; // Increment (speed/direction)
    char zi;
} SNOW;

char *block;
int16_t mapx=256, mapy=128, mapz=256;
int32_t mapsize = 0;
struct CLIENT client[maxclients];
int clients;
struct MOB mob[maxclients];
struct SNOW snow[maxsnow];
int snowenabled = 0;

int def(FILE *source, FILE *dest, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    char in[CHUNK];
    char out[CHUNK];

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    //ret = deflateInit(&strm, level);
    ret = deflateInit2(&strm, level, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
        return ret;

    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)deflateEnd(&strm);
            return Z_ERRNO;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    return Z_OK;
}
double findnoise2(double x,double y)
{
 int n, nn;
 n=(int)x+(int)y*57;
 n=(n<<13)^n;
 nn=(n*(n*n*60493+19990303)+1376312589)&0x7fffffff;
 return 1.0-((double)nn/1073741824.0);
}
double interpolate1(double a,double b,double x)
{
 double ft=x * 3.1415927;
 double f=(1.0-cos(ft))* 0.5;
 return a*(1.0-f)+b*f;
}
double noise(double x,double y)
{
 double int1, int2, floorx, floory;
 double s,t,u,v;//Integer declaration
 floorx=(double)((int)x);//This is kinda a cheap way to floor a double integer.
 floory=(double)((int)y);
 s=findnoise2(floorx,floory);
 t=findnoise2(floorx+1,floory);
 u=findnoise2(floorx,floory+1);//Get the surrounding pixels to calculate the transition.
 v=findnoise2(floorx+1,floory+1);
 int1=interpolate1(s,t,x-floorx);//Interpolate between the values.
 int2=interpolate1(u,v,x-floorx);//Here we use x-floorx, to get 1st dimension. Don't mind the x-floorx thingie, it's part of the cosine formula.
 return interpolate1(int1,int2,y-floory);//Here we use y-floory, to get the 2nd dimension.
}

char* paddedToCString(const char *padded, char *out) {
    int i, len;
    len = -1;
    for (i = 63; i >= 0; i--) {
        if (padded[i] != ' ') {
            len = i + 1;
            break;
        }
    }
    if (len == -1) {
        out[0] = '\0';
    }else{
        memcpy(out, padded, len);
        out[len] = '\0';
    }
    return out;
}

char* cToPaddedString(const char *cstring, char *out) {
    size_t len = strlen(cstring);
    memset(out, ' ', 64);
    if (len > 64)
        memcpy(out, cstring, 64);
    else
        memcpy(out, cstring, len);
    return out;
}

size_t sendByte(SOCKET socket, char value) {
    return send(socket, (char*)&value, sizeof(char), 0);
}
size_t sendByteArray(SOCKET socket, char *array, int len) {
    return send(socket, (char*)array, sizeof(char)*len, 0);
}
size_t sendInt16(SOCKET socket, int16_t value) {
    value = htons(value);
    return send(socket, (char*)&value, sizeof(int16_t), 0);
}
size_t sendInt32(SOCKET socket, int32_t value) {
    value = htonl(value);
    return send(socket, (char*)&value, sizeof(int32_t), 0);
}

char recvByte(SOCKET socket) {
    char value;
    recv(socket, (char*)&value, sizeof(char), 0);
    return value;
}
char* recvByteArray(SOCKET socket, char *array, int len) {
    recv(socket, (char*)array, sizeof(char)*len, 0);
    return array;
}
int16_t recvInt16(SOCKET socket) {
    int16_t value;
    recv(socket, (char*)&value, sizeof(int16_t), 0);
    return ntohs(value);
}
int32_t recvInt32(SOCKET socket) {
    int32_t value;
    recv(socket, (char*)&value, sizeof(int32_t), 0);
    return ntohl(value);
}

void sendPacket_welcome(SOCKET socket, char version, char *name, char *motd, char op){
    char namebuf[64];
    char motdbuf[64];
    sendByte(socket, 0x00); // Welcome
    sendByte(socket, version); // Protocol Version
    cToPaddedString(name, namebuf);
    cToPaddedString(motd, motdbuf);
    sendByteArray(socket, namebuf, 64); // Server Name
    sendByteArray(socket, motdbuf, 64); // MOTD
    sendByte(socket, op); // OP status
}

void sendPacket_levelInitialize(SOCKET socket){
    sendByte(socket, 0x02);
}

void sendPacket_levelChunk(SOCKET socket, short size, char *chunk, char percent){
    sendByte(socket, 0x03); // Level Chunk
    sendInt16(socket, size); // Chunk Size
    sendByteArray(socket, chunk, 1024); // Chunk
    sendByte(socket, percent);
}

void sendPacket_levelFinalize(SOCKET socket, short x, short y, short z){
    sendByte(socket, 0x04); // Finalise
    sendInt16(socket, x); // X
    sendInt16(socket, y); // Y
    sendInt16(socket, z); // Z
}

void sendPacket_setBlock(SOCKET socket, short x, short y, short z, char type){
    sendByte(socket, 0x06); // Set Block
    sendInt16(socket, x); // X
    sendInt16(socket, y); // Y
    sendInt16(socket, z); // Z
    sendByte(socket, type); // Block Type
}

void sendPacket_spawnPlayer(SOCKET socket, char id, char *name, short x, short y, short z, char heading, char pitch){
    char namebuf[64];
    sendByte(socket, 0x07); // Spawn Player
    sendByte(socket, id); // Player ID
    cToPaddedString(name, namebuf);
    sendByteArray(socket, namebuf, 64); // Send Name
    sendInt16(socket, x); // Send X
    sendInt16(socket, y); // Send Y
    sendInt16(socket, z); // Send Z
    sendByte(socket, heading); // Send Heading
    sendByte(socket, pitch); // Send Pitch
}

void sendPacket_positionAndOrientation(SOCKET socket, char id, short x, short y, short z, char heading, char pitch){
    sendByte(socket, 0x08); // Position and Orientation Update
    sendByte(socket, id); // Player ID
    sendInt16(socket, x); // Send X
    sendInt16(socket, y); // Send Y
    sendInt16(socket, z); // Send Z
    sendByte(socket, heading); // Send Heading
    sendByte(socket, pitch); // Send Pitch
}

void sendPacket_despawn(SOCKET socket, char id){
    sendByte(socket, 0x0c); // Despawn
    sendByte(socket, id); // Player ID
}

void sendPacket_chatMessage(SOCKET socket, char id, char *message){
    char messagebuf[64];
    sendByte(socket, 0x0d); // Chat Message
    sendByte(socket, id); // Player ID
    cToPaddedString(message, messagebuf);
    sendByteArray(socket, messagebuf, 64); // Message Body
}

void sendPacket_kick(SOCKET socket, char *message){
    char messagebuf[64];
    sendByte(socket, 0x0e);
    cToPaddedString(message, messagebuf);
    sendByteArray(socket, messagebuf, 64);
}

char* setBlock(short x, short y, short z, char type){
    char* p;
    p=blockAt(x,y,z);
    *p = type;
    return p;
}

char* setBlock_synced(short x, short y, short z, char type){
    char *p = setBlock(x, y, z, type);
    int i;
    
    for (i=0;i<maxclients;i++){
        if (client[i].used==1&&client[i].stage==4){
            sendPacket_setBlock(client[i].socket, x, y, z, type);
        }
    }
    
    return p;
}

char getBlock(short x, short y, short z){
    char* p;
    p=blockAt(x,y,z);
    return *p;
}
char touching(short x, short y, short z, char type){
    char num = 0;
                                       if (getBlock(x-1,y,z)==type) num++;
    if (getBlock(x,y,z-1)==type) num++;                                      if (getBlock(x,y,z+1)==type) num++;
                                       if (getBlock(x+1,y,z)==type) num++;
    return num;
}
char touchingdg(short x, short y, short z, char type){
    char num = 0;
    if (getBlock(x-1,y,z-1)==type) num++;if (getBlock(x-1,y,z)==type) num++;if (getBlock(x-1,y,z+1)==type) num++;
    if (getBlock(x,y,z-1)==type) num++;                                        if (getBlock(x,y,z+1)==type) num++;
    if (getBlock(x+1,y,z-1)==type) num++;if (getBlock(x+1,y,z)==type) num++;if (getBlock(x+1,y,z+1)==type) num++;
    return num;
}
char touchinglr(short x, short y, short z, char type){
    char num = 0;
    num = num + touchingdg(x+3,y,z-3,type);num = num + touchingdg(x+3,y,z,type);num = num + touchingdg(x+3,y,z+3,type);
    num = num + touchingdg(x  ,y,z-3,type);num = num + touchingdg(x  ,y,z,type);num = num + touchingdg(x  ,y,z+3,type);
    num = num + touchingdg(x-3,y,z-3,type);num = num + touchingdg(x-3,y,z,type);num = num + touchingdg(x-3,y,z+3,type);
    return num;
}

void resetSnowBlock(int i) {
    snow[i].x=rand()%mapx;
    snow[i].y=mapy-1;
    snow[i].z=rand()%mapz;
    //snow[i].xi = rand()%5-2;
    snow[i].xi = -1;
    //snow[i].yi = -rand()%3;
    snow[i].yi = -2;
    //snow[i].zi = rand()%5-2;
    snow[i].zi = 0;
    setBlock_synced(snow[i].x, snow[i].y, snow[i].z, 0x24); // Place new snow block (white cloth)
}

void backupmap(){
    int32_t header;
    FILE* fp;
    char fname[255];
    int backupinc = 0;

    puts("Beginning map backup...");
    
    fp = fopen("backups/backupinc.sys", "rb");
    if (fp!=NULL){
        fread(&backupinc, sizeof(backupinc), 1, fp);
        fclose(fp);
    }

    // Randomly rename previus backup
    sprintf(fname, "backups/backup_%d.dat", backupinc);
    rename("backups/backup.dat", fname);
    backupinc++;

    printf("Saving map backup...");
    fp = fopen("backups/backup.dat", "wb");
    header = htonl(mapsize);
    fwrite(&header,sizeof(int32_t),1,fp);
    fwrite(block,sizeof(char)*mapsize,1,fp);
    fclose(fp);
    printf("done.\n");

    fp = fopen("backups/backupinc.sys", "wb");
    if (fp!=NULL){
        fwrite(&backupinc, sizeof(backupinc), 1, fp);
        fclose(fp);
    }
}

void generateMap(int type) {
    int i, j, k;
    
    switch (type) {
        case 0: // Half flatgrass, half sand
            for (i=0;i<mapx;i++){
                for (j=0;j<mapz;j++){
                    setBlock(i,0,j,0x0C); // Sand
                    if (j!=0&&i!=0){
                        setBlock(i,mapy/2-3,j,0x07); // Bedrock (indestructible)
                    }else{
                        setBlock(i,mapy/2-3,j,0x09); // Stationary Water (indestructible)
                    }
                    setBlock(i,mapy/2-2,j,0x08); // Liquid Water
                    setBlock(i,mapy/2-1,j,0x08); // Liquid Water
                    setBlock(i,mapy/2,j,0x03); // Dirt
                    setBlock(i,mapy/2+1,j,0x03); // Dirt
                    setBlock(i,mapy/2+2,j,0x02); // Grass
                }
            }
            break;
        case 1: // Noise
            for (i=0;i<mapx;i++){
                for (j=0;j<mapz;j++){
                    int height;
                    height = (int)((noise(i*0.025f,j*0.025f)+1.0f)*(mapy/8))+mapy/4;
                    for (k = 0;k<height;k++){
                        if (height>(mapy/4)+(mapy/6)){
                            setBlock(i,k,j,0x01); // Stone
                        }else{
                            if (k!=height-1){
                                setBlock(i,k,j,0x03); // Dirt
                            }else{
                                setBlock(i,k,j,0x02); // Grass
                            }
                        }
                    }
                }
            }
            break;
    }
    for (i=1;i<(mapy/2)+4;i++){ // Builds route to surface
        setBlock(0,i,0,0x07); // Bedrock (indestructible)
        setBlock(1,i,0,0x07); // Bedrock (indestructible)
        setBlock(0,i,1,0x07); // Bedrock (indestructible)
        setBlock(1,i,1,0x09); // Stationary Water (indestructible)
        setBlock(2,i,2,0x07); // Bedrock (indestructible)
        if (i>3){
            setBlock(2,i,1,0x07); // Bedrock (indestructible)
            setBlock(1,i,2,0x07); // Bedrock (indestructible)
        }else{
            setBlock(2,i,1,0x09); // Stationary Water (indestructible)
            setBlock(1,i,2,0x09); // Stationary Water (indestructible)
        }
        setBlock(2,i,0,0x07); // Bedrock (indestructible)
        setBlock(0,i,2,0x07); // Bedrock (indestructible)
    }
}

int main(int argc, char* argv[])
{
    // Vars
    const int DEFAULT_PORT = 25565;
    int j = 0, i = 0, k = 0, l = 0, m = 0, physx = 0, physy = 0, physz = 0, phys = 0, sal = 0, iMode = 1;
    struct sockaddr_in sa;
    struct fd_set readable, writeable;
    struct timeval timeout;
    WSADATA wsaData;
    SOCKET server, tempclient;
    FILE *fp;

    mapsize = mapx*mapy*mapz;
    block = (char*)malloc(mapsize*sizeof(char)); // Allocate
    assert(block!=NULL);
    memset(block,0x00,mapsize*sizeof(char));

    CreateDirectoryA("backups", 0); // Create backups folder (A as W is default)

    fp = fopen("backups/backup.dat", "rb");
    if (fp!=NULL){ // If map exists load it
        int32_t header;
        printf("Loading map...");
        fread(&header, sizeof(int32_t), 1, fp); // Skip header
        fread(block, sizeof(int8_t)*mapsize, 1, fp); // Read map from backup
        fclose(fp);
    }else{ // Else create anew
        printf("Preparing map...");
        generateMap(0);
    }
    printf("/\n");

    sa.sin_family = AF_INET;
    sa.sin_port = htons(DEFAULT_PORT);
    sa.sin_addr.s_addr = INADDR_ANY;
    sal = sizeof sa;

    timeout.tv_sec = 0;
    timeout.tv_usec = 100; // 20 milliseconds

    for(i = 0;i < maxclients;i++){
        client[i].used = 0;
        mob[i].used = 0;
    }
    for(i = 0; i < numzombies; i++){
        mob[i].used = 1;
        mob[i].x=(rand()%mapx)*32+16;
        mob[i].y=(mapy/2+10)*32;
        mob[i].z=(rand()%mapz)*32+16;
        mob[i].hp = 1; // Insta-kill
        mob[i].direction = 0;
        mob[i].heading = 0;
        mob[i].pitch = 0;
        mob[i].respawn = 0;
        strcpy(mob[i].name, "&2Zombie&7Mob");
    }
    if (snowenabled)
    for(i = 0; i < maxsnow; i++){
        resetSnowBlock(i);
    }

    // init
    WSAStartup(MAKEWORD(2,0), &wsaData);
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    //ioctlsocket(server,FIONBIO,&iMode);

    if (bind(server, (SOCKADDR*)&sa, sizeof sa)!=SOCKET_ERROR){
        printf("SchnitzelCraft V0.3 (C) Andrew Faulds 2010-2011 \n");
        printf("This application uses zlib (C) 1995-2010 Jean-loup Gailly and Mark Adler\n");
        printf("Map size: %d", mapx);
        printf("x%dx", mapy);
        printf("%d: ", mapz);
        printf("%dKB\n", (int)(mapsize/1024));
        printf("Connect to port %d\n", DEFAULT_PORT);
        while (1){
            Sleep(50);
            phys = 0;
            // *** CONNECT BEGIN ***
            if (listen(server, 64)!=SOCKET_ERROR){
                FD_ZERO(&readable);
                FD_SET(server, &readable);
                if (select((int)NULL, &readable, NULL, NULL, &timeout) != SOCKET_ERROR){ // Query for waiting connections
                    if (FD_ISSET(server, &readable)){ // If there are waiting connections...
                        tempclient = accept(server, (SOCKADDR*)&sa, &sal);
                        if (tempclient!=SOCKET_ERROR){
                            int thisclient;
                            for(i = 0;i < maxclients;i++){
                                if (client[i].used != 1&&mob[i].used != 1){
                                    thisclient = i;
                                    break;
                                }
                            }
                            printf("Client %d connected\n", thisclient);
                            memset(&client[thisclient], 0, sizeof(CLIENT)); // Reset
                            client[thisclient].used = 1;
                            client[thisclient].stage = 0;
                            client[thisclient].socket = tempclient;
                            clients++;
                        }else{
                            printf("error: %d", WSAGetLastError());
                            system("pause");
                        }
                    }
                }else{
                    printf("error: %d", WSAGetLastError());
                    system("pause");
                }
            }else{
                printf("error: %d", WSAGetLastError());
                system("pause");
            }
            // *** CONNECT END ***
            for(i = 0;i < maxclients;i++){
                if (client[i].used==1){
                    FD_ZERO(&readable);
                    FD_SET(client[i].socket, &readable);
                    select((int)NULL, &readable, NULL, NULL, &timeout);
                    // *** RECV BEGIN ***
                    while(FD_ISSET(client[i].socket, &readable)){
                        char packettype;
                        packettype = recvByte(client[i].socket);
                        switch (packettype){
                            case 0x00:
                                if (client[i].stage==0){
                                    client[i].protocol = recvByte(client[i].socket);
                                    if (client[i].protocol == 0x07){ // 7
                                        char namebuf[64];
                                        char verbuf[64];
                                        recvByteArray(client[i].socket, namebuf, 64); // Get Name
                                        paddedToCString(namebuf, client[i].name);
                                        printf("Client %d identified: %s\n", i, client[i].name);
                                        recvByteArray(client[i].socket, verbuf, 64); // Skip Verification Key
                                        recvByte(client[i].socket); // Skip Unused byte
                                        client[i].stage = 1;
                                        for (j=0;j<maxclients;j++){ // Yay EVEN MOAR dirty hax
                                            if (client[j].used==1&&j!=i&&client[j].stage==4){
                                                char joinbuf[64];
                                                sendPacket_spawnPlayer(client[j].socket, i, client[i].name, client[i].x, client[i].y, client[i].z, client[i].heading, client[i].pitch);
                                                sprintf(joinbuf, "&e%s joined", client[i].name);
                                                sendPacket_chatMessage(client[j].socket, i, joinbuf);
                                            }
                                        }
                                    }else{
                                        char kickbuf[64];
                                        cToPaddedString(kickbuf, "Incompatible Protocol Version");
                                        sendPacket_kick(client[i].socket, kickbuf);
                                        printf("Client %d kicked: Incompatible Protocol Version ", i);
                                        printf("(%d)\n", client[i].protocol);
                                        closesocket(client[i].socket);
                                        client[i].used=0;
                                        goto exitloop;
                                    }
                                }else{
                                    printf("Client %d left\n", i);
                                    closesocket(client[i].socket);
                                    client[i].used=0;
                                    for (j=0;j<maxclients;j++){ // ZOMG MOAR HAX
                                        if (client[j].used==1&&client[j].stage==4){
                                            char partbuf[64];
                                            sendPacket_despawn(client[j].socket, i);
                                            if (flippy==1){
                                                sendPacket_despawn(client[j].socket, 64+i); // Despawn Flippy
                                            }
                                            sprintf(partbuf, "&c%s left", client[i].name);
                                            sendPacket_chatMessage(client[j].socket, i, partbuf);
                                        }
                                    }
                                    backupmap(); // Save map backup
                                    goto exitloop;
                                }
                            break;
                            case 0x01: // Ping
                            break;
                            case 0x05: // Set Block
                                {
                                    struct BLOCKCHANGE bc;
                                    char mode;
                                    
                                    bc.player = i;
                                    bc.x = recvInt16(client[i].socket); // X
                                    bc.y = recvInt16(client[i].socket); // Y
                                    bc.z = recvInt16(client[i].socket); // Z
                                    mode = recvByte(client[i].socket); // Get Mode
                                    bc.newvalue = recvByte(client[i].socket); // Block type
                                    if (mode==0x00){ // If deleted
                                        bc.newvalue=0x00; // Air (deleted)
                                    }
                                    if (bc.newvalue==0x27){ // If Brown Mushroom
                                        bc.newvalue=0x08; // Liquid Water
                                    } // Disabled due to water physics
                                    if (bc.newvalue==0x2C){ // Step
                                        if (getBlock(bc.x,bc.y-1,bc.z)==0x2C){
                                            sendPacket_setBlock(client[i].socket, bc.x, bc.y, bc.z, 0x00); // Air
                                            bc.newvalue=0x2B; // Double Step
                                            bc.y -= 1;
                                        }
                                    }
                                    if (bc.newvalue==0x03&&touching(bc.x,bc.y,bc.z,0x02)>0){ // If dirt touching grass
                                        bc.newvalue=0x02; // Grass
                                    }
                                    if (getBlock(bc.x,bc.y,bc.z)==0x07||getBlock(bc.x,bc.y,bc.z)==0x09){ // If indestructible
                                        bc.newvalue=getBlock(bc.x,bc.y,bc.z);
                                    }
                                    setBlock_synced(bc.x,bc.y,bc.z,bc.newvalue);
                                    phys=1;
                                    physx=bc.x;
                                    physy=bc.y;
                                    physz=bc.z;
                                }
                            break;
                            case 0x08: // Position/Orientation
                                recvByte(client[i].socket); // Skip Player ID
                                client[i].x = recvInt16(client[i].socket); // Get new Player X
                                client[i].y = recvInt16(client[i].socket); // Get new Player Y
                                client[i].z = recvInt16(client[i].socket); // Get new Player Z
                                client[i].heading = recvByte(client[i].socket); // Get new Player Heading
                                client[i].pitch = recvByte(client[i].socket); // Get new Player Pitch
                            break;
                            case 0x0d: // Chat Message
                                {
                                    char messagebuf[64];
                                    char csmessagebuf[65];
                                    recvByte(client[i].socket); // Skip Player ID
                                    recvByteArray(client[i].socket, messagebuf, 64); // Recieve message
                                    paddedToCString(messagebuf, csmessagebuf);
                                    for (j=0;j<maxclients;j++){ // Yay dirty hax
                                        if (client[j].used==1&&client[j].stage==4){
                                            char msgbuf[256];
                                            sprintf(msgbuf, "&e<%s> &f%s", client[i].name, csmessagebuf);
                                            sendPacket_chatMessage(client[j].socket, i, msgbuf);
                                        }
                                    }
                                }
                            break;
                            default:
                                {
                                    char kickbuf[64];
                                    printf("Error: Unknown packet type: %x\n", packettype);
                                    cToPaddedString("Incompatible Protocol Version", kickbuf);
                                    sendPacket_kick(client[i].socket, kickbuf);
                                    printf("Client %d kicked: Incompatible Protocol Version\n", i);
                                    closesocket(client[i].socket);
                                    memset(&client[i].socket,0,sizeof(client[i].socket));
                                    client[i].used=0;
                                    goto exitloop;
                                }
                            break;
                        }
                        FD_ZERO(&readable);
                        FD_SET(client[i].socket, &readable);
                        select((int)NULL, &readable, NULL, NULL, &timeout);
                    }
exitloop:
                    // *** RECV END ***
                    #ifdef PHYSICS
                    if (phys)
                    for (j=physx-8;j<physx+8;j++){
                        for (k=physz-8;k<physz+8;k++){
                            if (getBlock(j,physy,k)==0x08&&touchinglr(j, physy, k, 0x13)>0){ // If Liquid Water and Touching Sponge (at long distance)
                                setBlock_synced(j,physy,k,0x00); // Set to Air
                            }else if (getBlock(j,physy,k)==0x00&&(touching(j, physy, k, 0x08)>0||getBlock(j, physy+1, k)==0x08)){ // If Air Touching Water/Water Above
                                if (touchinglr(j, physy, k, 0x13)==0){ // Not Touching Sponge (at long distance)
                                    setBlock_synced(j,physy,k,0x08); // Set to Liquid Water
                                }
                            }else if (getBlock(j,physy,k)==0x02&&getBlock(j,physy+1,k)!=0x00){ // If Grass and Vertical is not free
                                setBlock_synced(j,physy,k,0x03); // Set to Dirt
                            }else if (getBlock(j,physy,k)==0x03&&touching(j,physy, k, 0x02)>0){ // Dirt and Touching Grass
                                if (getBlock(j,physy+1,k)==0x00){ // Vertical is free
                                    setBlock_synced(j,physy,k,0x02); // Set to Grass
                                }
                            }
                        }
                    }
                    #endif
                    FD_ZERO(&writeable);
                    FD_SET(client[i].socket, &writeable);
                    select((int)NULL, NULL, &writeable, NULL, &timeout);
                    // *** SEND BEGIN ***
                    if (FD_ISSET(client[i].socket, &writeable)){
                        switch (client[i].stage){
                            case 1: // Welcome message
                                sendPacket_welcome(client[i].socket, 0x07, "SchnitzelCraft0 Server", "MOTD would go here but I haven't set one", client[i].op);
                                printf("Client %d sent welcome message\n", i);
                                client[i].stage=2;
                            break;
                            case 2:// Sending the map, one chunk at a time
                                {
                                FILE *fpin, *fpout;
                                backupmap();

                                printf("Compressing map data...");
                                fpin = fopen("backups/backup.dat", "rb");
                                fpout = fopen("map.gz", "wb");
                                if (fpin != NULL && fpout != NULL){
                                    k = def(fpin,fpout,9);
                                    if (k!=Z_OK){
                                        printf("Error compressing data: ");
                                        if (k==Z_MEM_ERROR){
                                            printf("Z_MEM_ERROR\n");
                                        }else if(k==Z_STREAM_ERROR){
                                            printf("Z_STREAM_ERROR\n");
                                        }else if(k==Z_VERSION_ERROR){
                                            printf("Z_VERSION_ERROR\n");
                                        }else if(k==Z_ERRNO){
                                            printf("Z_ERRNO:\n");
                                        }else{
                                            printf("%d\n", k);
                                        }
                                    }else{
                                        printf("done.\n");
                                    }
                                    fclose(fpin);
                                    fclose(fpout);
                                }else{
                                    printf("Error compressing data: Failed to open file.\n");
                                }

                                printf("Sending compressed map data...\n");
                                fpin = fopen("map.gz", "rb");
                                if (fpin != NULL){
                                    int filesize;
                                    fseek(fpin, 0L, SEEK_END);
                                    filesize = ftell(fpin); // Get file size
                                    fseek(fpin, 0L, SEEK_SET);
                                    sendPacket_levelInitialize(client[i].socket);
                                    k = 0;
                                    j = 0;
                                    while(1){
                                        char chunkin[1024];
                                        char chunk[1024];
                                        j=fread(chunkin,1,1024,fpin);
                                        memset(chunk,0,1024);
                                        memcpy(chunk,chunkin,j);
                                        k = k + j;
                                        sendPacket_levelChunk(client[i].socket, j, chunk, (char)floor((double)k*(100/(double)filesize)));
                                        if (feof(fpin)!=0){
                                            sendPacket_levelFinalize(client[i].socket, mapx, mapy, mapz);
                                            printf("Finalized map data - total %d bytes sent.\n", k);
                                            break;
                                        }
                                    }
                                    fclose(fpin);
                                }else{
                                    printf("Error. Map data could not be sent.");
                                    exit(1);
                                }
                                client[i].stage=3;
                                }
                            break;
                            case 3: // Player data
                                for(j = 0;j < maxclients;j++){
                                    if (client[j].used==1){
                                        if (j!=i){
                                            sendPacket_spawnPlayer(client[i].socket, j, client[j].name, client[j].x, client[j].y, client[j].z, client[j].heading, client[j].pitch);
                                        }
                                        if (flippy==1){
                                            char namebuf[64];
                                            // Flippy
                                            strcpy(namebuf, client[j].name);
                                            namebuf[0] = 'f';
                                            sendPacket_spawnPlayer(client[i].socket, 64+j, namebuf, (mapx*32)-client[j].x, client[j].y, (mapz*32)-client[j].z, client[j].heading+127, client[j].pitch);
                                        }
                                    }
                                    if (mob[j].used==1&&mob[j].respawn==0){
                                        sendPacket_spawnPlayer(client[i].socket, j, mob[j].name, mob[j].x, mob[j].y, mob[j].z, mob[j].heading, mob[j].pitch);
                                    }
                                }
                                // Spawn
                                client[i].x=6*32;
                                client[i].y=(mapy/2+10)*32;
                                client[i].z=6*32;
                                sendPacket_positionAndOrientation(client[i].socket, 0xFF /* Teleport */, client[i].x, client[i].y, client[i].z, client[i].heading, client[i].pitch);
                                printf("done.\n");
                                client[i].stage=4;
                            break;
                            case 4: // Normal
                                for(j = 0;j < maxclients;j++){ // Send Player Positions
                                    if (client[j].used==1){
                                        if (j!=i){
                                            sendPacket_positionAndOrientation(client[i].socket, j, client[j].x, client[j].y, client[j].z, client[j].heading, client[j].pitch);
                                        }
                                        if (flippy==1){
                                            // Flippy
                                            sendPacket_positionAndOrientation(client[i].socket, 64+j, (mapx*32)-client[j].x, client[j].y, (mapz*32)-client[j].z, client[j].heading+127, client[j].pitch);
                                        }
                                    }
                                    if (mob[j].used==1&&mob[j].respawn==0){
                                        sendPacket_positionAndOrientation(client[i].socket, j, mob[j].x, mob[j].y, mob[j].z, mob[j].heading, mob[j].pitch);
                                    }
                                }
                            break;
                        }
                    }
                    // *** SEND END ***
                    // *** MOBS BEGIN ***
                    for (j=0;j<maxclients;j++){
                        if (mob[j].used==1){
                            if (mob[j].respawn==0){ // If timer is 0
                                char upper, lower;
                                if (getBlock(mob[j].x/32, mob[j].y/32-2, mob[j].z/32)==0x00){ // Lame Gravity
                                    mob[j].y = mob[j].y - 8;
                                }
                                if (mob[j].y>((mob[j].y/32)*32+16)){ // Y position correction
                                    mob[j].y = mob[j].y - 8;
                                }
                                switch (mob[j].direction){
                                    case 0: // Forward
                                        mob[j].x = mob[j].x + 8;
                                        upper = getBlock(mob[j].x/32+1, mob[j].y/32, mob[j].z/32);
                                        lower = getBlock(mob[j].x/32+1, mob[j].y/32-1, mob[j].z/32);
                                    break;
                                    case 1: // Right
                                        mob[j].z = mob[j].z + 8;
                                        upper = getBlock(mob[j].x/32, mob[j].y/32, mob[j].z/32+1);
                                        lower = getBlock(mob[j].x/32, mob[j].y/32-1, mob[j].z/32+1);
                                    break;
                                    case 2: // Back
                                        mob[j].x = mob[j].x - 8;
                                        upper = getBlock(mob[j].x/32-1, mob[j].y/32, mob[j].z/32);
                                        lower = getBlock(mob[j].x/32-1, mob[j].y/32-1, mob[j].z/32);
                                    break;
                                    case 3: // Left
                                        mob[j].z = mob[j].z - 8;
                                        upper = getBlock(mob[j].x/32, mob[j].y/32, mob[j].z/32-1);
                                        lower = getBlock(mob[j].x/32, mob[j].y/32-1, mob[j].z/32-1);
                                    break;
                                }
                                // Lame Collision avoidance
                                if (upper!=0x00 // Not air
                                    &&upper!=0x08 // Not water
                                    &&upper!=0x28){ // Not Red Mushroom (for epick trappawge)
                                    mob[j].direction++;
                                    mob[j].x = (mob[j].x/32)*32+16;
                                    mob[j].y = (mob[j].y/32)*32+16;
                                    mob[j].z = (mob[j].z/32)*32+16;
                                // JUMP
                                }else if (lower!=0x00 // Not air
                                    &&lower!=0x08 // Not water
                                    &&lower!=0x28){ // Not Red Mushroom (for epick trappawge)
                                    mob[j].y=mob[j].y+32; // Jump
                                switch (mob[j].direction){
                                    case 0: // Forward
                                        mob[j].x = mob[j].x + 32;
                                    break;
                                    case 1: // Right
                                        mob[j].z = mob[j].z + 32;
                                    break;
                                    case 2: // Back
                                        mob[j].x = mob[j].x - 32;
                                    break;
                                    case 3: // Left
                                        mob[j].z = mob[j].z - 32;
                                    break;
                                }
                                }
                                if (mob[j].x>mapx*32||mob[j].z>mapz*32){
                                    mob[j].direction++;
                                    mob[j].x = (mob[j].x/32)*32+16;
                                    mob[j].y = (mob[j].y/32)*32+16;
                                    mob[j].z = (mob[j].z/32)*32+16;
                                }
                                if (mob[j].direction>3){
                                    mob[j].direction = 0;
                                }
                                mob[j].heading = (mob[j].direction+1)*64; // Adjust visible direction

                                if (getBlock(mob[j].x/32, mob[j].y/32-1, mob[j].z/32)==0x28){ // Touching Red Mushroom
                                    mob[j].hp = mob[j].hp - 1;
                                    setBlock_synced(mob[j].x/32, mob[j].y/32-1, mob[j].z/32, 0x00);
                                    for (k=0;k<maxclients;k++){ // Yay moar dirty hax
                                        if (client[k].used==1&&client[k].stage==4){
                                            char hitbuf[64];
                                            sprintf(hitbuf, "&7Zombie %d &cHIT", j);
                                            sendPacket_chatMessage(client[k].socket, 0, hitbuf);
                                        }
                                    }
                                }
                                if (mob[j].hp<=0){ // Kill
                                    mob[j].x=(rand()%mapx)*32+16;
                                    mob[j].y=(mapy/2+10)*32;
                                    mob[j].z=(rand()%mapz)*32+16;
                                    mob[j].hp = 4;
                                    mob[j].heading = 0;
                                    mob[j].pitch = 0;
                                    mob[j].respawn = 1; // Set timer to 1, respawn
                                    for (k=0;k<maxclients;k++){ // Yay moar dirty hax
                                        if (client[k].used==1&&client[k].stage==4){
                                            char diedbuf[64];
                                            sprintf(diedbuf, "&7Zombie %d &4DIED", j);
                                            sendPacket_chatMessage(client[k].socket, 0, diedbuf);
                                            sendPacket_despawn(client[k].socket, j); // Despawn
                                        }
                                    }
                                }
                            }else{
                                mob[j].respawn++; // Increment timer
                                if (mob[j].respawn==0){
                                    for (k=0;k<maxclients;k++){ // Yay moar dirty hax
                                        if (client[k].used==1&&client[k].stage==4){
                                            char respawnbuf[64];
                                            sprintf(respawnbuf, "&7Zombie %d &aRESPAWNED", j);
                                            sendPacket_chatMessage(client[k].socket, 0, respawnbuf);
                                            sendPacket_spawnPlayer(client[k].socket, j, mob[j].name, mob[j].x, mob[j].y, mob[j].z, mob[j].heading, mob[j].pitch);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // *** MOBS END ***
                    // *** SNOW BEGIN ***
                    if (snowenabled)
                    for (j=0; j < maxsnow; j++) {
                        short x, y, z;
                        short x2, y2, z2;
                        char next;
                        x = snow[j].x;
                        y = snow[j].y;
                        z = snow[j].z;
                        x2 = (snow[j].x+snow[j].xi)%mapx;
                        y2 = snow[j].y+snow[j].yi;
                        z2 = (snow[j].z+snow[j].zi)%mapz;
                        next = getBlock(x2, y2, z2);
                        if (getBlock(x, y, z) == 0x24) { // Snow block in this location
                            if (next == 0x24 || next == 0x00) { // Next position is snow/air
                                setBlock_synced(x, y, z, 0x00); // Clear current snow
                                setBlock_synced(x2, y2, z2, 0x24); // Place new snow block (white cloth)
                                snow[j].x = x2;
                                snow[j].y = y2;
                                snow[j].z = z2;
                            }else if (next == 0x02) { // Next position is grass
                                setBlock_synced(x, y, z, 0x00); // Clear current snow
                                setBlock_synced(x2, y2, z2, 0x24); // Place new snow block (white cloth)
                                resetSnowBlock(i);
                            }else if (snow[j].yi < -1) { // Reduce if moving at increment >1 downward
                                snow[j].yi = -1;
                            }else{
                                setBlock_synced(x, y, z, 0x00);
                                resetSnowBlock(j);
                            }
                        }else{ // Create new snow block
                            resetSnowBlock(j);
                        }
                    }
                }
            }
        }
    }else{
        printf("Bind/listen failure: %d", WSAGetLastError());
        system("pause");
    }

    // Cleanup
    WSACleanup();
    closesocket(server);
    return 0;
}
