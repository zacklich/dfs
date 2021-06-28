// Zachariah Lichtenberg
// A very simple distributed file client
//


#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>

#include "md5.h"
#include "dfs.h"

int debug = 0;

#define CHUNKSIZE 1024

// The "server" structure keeps track of remote servers that are listed
// in the config file.  When a connection is open, this structure
// also holds the socket descriptors.

typedef struct server_s {
    // Stuff from config file
    char *name;
    char *host;
    int port;
    int index;

    // Stuff for when we are connected
    int is_connected;
    int readfd;
    int writefd;
    FILE *readfile;
    FILE *writefile;
} server_t;

#define MAX_PIECES 4
#define MAX_SERVERS 4
server_t servers[MAX_SERVERS];

// The username and password that were in the config file
char *username = "";
char *password = "";


//
// The remote file structure keeps track of the list of files stored
// on a remote server.  we need to collect which servers have which pieces of the file
// we are looking for.
//
typedef struct remotefile_s {
    struct remotefile_s *next;
    char *filename;

    // this is an array of which servers each piece is stored on.
    // We only need one server per piece.  If all 4 entries in this
    // array are filled in, the file is complete.
    server_t *pieces[MAX_PIECES];
} remotefile_t;

remotefile_t *filelist = NULL;



int min(int a,int b)
{
    if (a < b) {
        return a;
    } else {
        return b;
    }
}


// Pieces and servers are numbered from 0 to make math easier.
// This routine does what "table 1" in the assignment does, and
// tells you for a given hash value and server which pieces
// go where.
void whichpieces(int xvalue, int server, int *piece1, int *piece2)
{
    *piece1 = (server + MAX_SERVERS - xvalue) % 4;
    *piece2 = (*piece1 + 1) % 4;
}

// addserver - process a "server" line in the config file.  Add the
// server to the servers[] array.
int addserver(char *config)
{
    char *name;
    char *hostport;
    int servernum;
    char *host;
    int port;

    name = strsep(&config," ");
    hostport = strsep(&config," ");

    if ((name == NULL) || (hostport == NULL)) {
        printf("Bad 'Server' line in config file\n");
        return -1;
    }

    if ((strncmp(name,"DFS",3) != 0) ||
        (name[3] < '1') || (name[3] > '4')) {
        printf("Server name must be DFS1,DFS2,DFS3, or DFS4\n");
        return -1;
    }

    servernum = name[3] - '1';          // servernum will go from 0..3

    host = strsep(&hostport,":");
    port = atoi(hostport);
    
    servers[servernum].name = strdup(name);
    servers[servernum].host = strdup(host);
    servers[servernum].port = port;
    servers[servernum].index = servernum;

    if (debug > 1) {
        printf("Added server '%s' at index %d on host %s:%d\n",
           servers[servernum].name,
           servernum,
           servers[servernum].host,
           servers[servernum].port);
    }

    return 0;
}

//
// readconfig - read a configuration file
//
int readconfig(char *filename)
{
    FILE *file;
    char *x;
    char line[1000];
    char *uname = NULL;
    char *pword = NULL;
    char *keyword;
    int i;
    int errors;

    file = fopen(filename,"r");
    if (!file) {
        return -1;
    }

    while (!feof(file)) {
        if (!fgets(line,sizeof(line),file)) {
            break;
        }

        // remove the newline
        x = strchr(line,'\n');
        if (x) *x = '\0';

        // check for blank lines or comments
        if ((line[0] == '\0') || (line[0] == '#')) {
            continue;
        }

        // Get the keyword to decide what to do
        x = line;
        keyword = strsep(&x," ");

        if ((strcasecmp(keyword,"Server") == 0) || (strcasecmp(keyword,"Server:") == 0)) {
            addserver(x);
        } else if ((strcasecmp(keyword,"Username") == 0) || (strcasecmp(keyword,"Username:") == 0)) {
            uname = strsep(&x," ");
            if (uname != NULL) {
                uname = strdup(uname);
            }
        } else if ((strcasecmp(keyword,"Password") == 0) || (strcasecmp(keyword,"Password:") == 0)) {
            pword = strsep(&x," ");
            if (pword != NULL) {
                pword = strdup(pword);
            }
        }
    }


    fclose(file);
    

    // Make sure user name and password are defined
    if ((uname == NULL) || (pword == NULL)) {
        printf("Username or password are missing from config file\n");
        return -1;
    }

    // make sure all 4 DFS servers are defined.
    errors = 0;
    for (i = 0; i < MAX_SERVERS; i++) {
        if (servers[i].host == NULL) {
            printf("Server info for DFS%d is not in the config file\n",i+1);
            errors++;
        }
    }

    if (errors > 0) {
        return -1;
    }

    username = uname;
    password = pword;

    if (debug > 1) {
        printf("User name is '%s',  password is '%s'\n",username,password);
    }

    return 0;
}

//
// Use the MD5 library routine to compute the sum of an entire file.
// Read a chunk of the file at a time, and pass it through MD5 till done.
//
// Returns -1 if the file could not be opened, or 0 if the sum
// was computed.

int md5_file(char *filename, uint8_t *md5sum)
{
    int fd;
    uint8_t chunk[CHUNKSIZE];
    int res;
    MD5_CTX ctx;

    MD5_Init(&ctx);

    fd = open(filename,O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    for (;;) {
        res = read(fd,chunk,sizeof(chunk));
        if (res == 0) {
            break;
        }
        MD5_Update(&ctx,chunk,res);
    }

    close(fd);

    MD5_Final(md5sum,&ctx);

    return 0;
}

//
// findfile - locate a file in the remote file list and return
// the structure for the matching file.  return NULL if not found

remotefile_t *findfile(char *filename)
{
    remotefile_t *fl;

    fl = filelist;

    while (fl != NULL) {

        if (strcmp(filename,fl->filename) == 0) {
            return fl;
        }

        fl = fl->next;
    }
    return NULL;
}


// clearfilelist - Clear out the file list, we use this before we query the 
// servers.
void clearfilelist(void)
{
    remotefile_t *fl;
    remotefile_t *flnext;

    fl = filelist;

    while (fl != NULL) {
        flnext = fl->next;
        free(fl->filename);
        free(fl);
        fl = flnext;
    }

    filelist = NULL;
}

// Add a file to the file list.  As each file is returned from the LIST
// command we add it to the remote file list and remember which pieces
// were on that server.  
int addfile(server_t *server, char *filename)
{
    char *piece;
    int piecenum;
    remotefile_t *remfile;

    // First, strip off the "."
    if (*filename != '.') {
        return -1;
    }

    filename++;

    // Get the piece number. Find the last dot in the filename
    piece = strrchr(filename,'.');
    if (!piece) {
        return -1;
    }

    // Chop off the .0 .1 .2 .3 extension
    *piece = '\0';
    piece++;

    // validate the piece number
    if ((*piece < '0') || (*piece > '3')) {
        return -1;
    }

    piecenum = *piece - '0';    // make a piece number from 0 to 3

    remfile = findfile(filename);

    // If the file was not found, add it to the list.
    if (!remfile) {
        remfile = (remotefile_t *) calloc(1,sizeof(remotefile_t));
        remfile->filename = strdup(filename);
        remfile->next = filelist;
        filelist = remfile;
    }

    // Remember the server that has this piece.  If we already have
    // a server, that's great, don't overwrite what we had before.
    if (remfile->pieces[piecenum] == NULL) {
        remfile->pieces[piecenum] = server;
    }

    return 0;

}

// This routine computes the MD5 Sum of the file and returns the 
// last digit of the sum modulo 4.  It will be used to determine
// which pieces of the file go on which servers.
int pair_x_value(char *filename)
{
    uint8_t md5sum[MD5_DIGEST_LENGTH];

    // compute the checksum, return -1 if error.
    if (md5_file(filename,md5sum) < 0) {
        return -1;
    }

    // Grab last byte of sum and return it mod 4.
    return md5sum[MD5_DIGEST_LENGTH-1] % 4;
}


// close_connection - close the sockets for a server.
void close_connection(server_t *server)
{
    if (server->is_connected) {
        fclose(server->readfile);
        fclose(server->writefile);
    }

    server->is_connected = 0;
}

// close_all_conections - force all server connections to be closed.
void close_all_connections(void)
{
    int i;

    for (i = 0; i < MAX_SERVERS; i++) {
        close_connection(&servers[i]);
    }
}

//
// Connect to a server.  The connecton method needs to have a 1-second timeout.
// For timeout stuff see https://stackoverflow.com/questions/2597608/c-socket-connection-timeout
int connect_to_server(server_t *server)
{
    struct hostent *serverent;
    struct sockaddr_in serveraddr;
    long arg;
    fd_set writeset;
    struct timeval timeout;
    int res;

    /* Clean up */
    server->is_connected = 0;           // NOT connected.

    /* gethostbyname: get the server's DNS entry */
    serverent = gethostbyname(server->host);
    if (serverent == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", server->host);
        return -1;
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)serverent->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, serverent->h_length);
    serveraddr.sin_port = htons(server->port);

    server->readfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

    if (server->readfd < 0) {
        return -1;
    }

    // Set the socket to non-blocking mode so we can wait for a 1-second
    // timeout on connection.
    arg = fcntl(server->readfd,F_GETFL,NULL);
    arg |= O_NONBLOCK;
    fcntl(server->readfd,F_SETFL,arg);

    res = connect(server->readfd,(struct sockaddr *) &serveraddr,sizeof(serveraddr));

    if ((res < 0) && (errno == EINPROGRESS)) {
        // use select() to wait for the socket to be connected, then wait up to 1 second.
        FD_ZERO(&writeset);
        FD_SET(server->readfd,&writeset);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        res = select(server->readfd+1,NULL,&writeset,NULL,&timeout);

        // the stackoverflow article says to get the "SO_ERROR" socket option
        // to determine if we are really connected.

        if (res > 0) {
            int valopt = 0;
            socklen_t lon = sizeof(valopt);

            getsockopt(server->readfd,SOL_SOCKET,SO_ERROR,(void *) &valopt,&lon);

            // 'valopt' will be what "errno" would have been if we were blocking.
            if (valopt != 0) {
                res = -1;
            }
        }
    }

    // If there was an error on 'select' close the socket and bail.

    if (res <= 0) {
        close(server->readfd);
        return -1;
    }

    // Set the socket back to blocking mode.
    arg &= ~O_NONBLOCK;
    fcntl(server->readfd,F_SETFL,arg);

    // Create a read and a write FILE structure 
    // https://ycpcs.github.io/cs365-spring2017/lectures/lecture15.html
    // this is the same as was used for previous assignments
    server->writefd = dup(server->readfd);

    // Make one FILE * for reading, one for writing
    server->readfile = fdopen(server->readfd,"r");
    server->writefile = fdopen(server->writefd,"w");

    // The server is connected now.

    server->is_connected = 1;

    return 0;
}

// connect_all_servers - call connect_to_server for all servers.
int connect_all_servers(void)
{
    int i;
    server_t *server;
    int count = 0;

    for (i = 0; i < MAX_SERVERS; i++) {
        server = &servers[i];

        connect_to_server(server);

        if ((debug > 0) || (server->is_connected == 0)) {
            printf("Server %s (%s:%d) : %s\n",server->name,server->host,server->port,
               server->is_connected ? "connected" : "NOT CONNECTED");
        }

        if (server->is_connected) {
            count++;
        }
    }

    return count;
}


// encrypt_decrypt(chunk, size, key)
// very simple XOR cipher
void encrypt_decrypt(uint8_t *chunk, int size, char *key)
{
    char *keyptr;

    keyptr = key;

    // XOR each character with the next character of the password.  If we run
    // off the end of the password, go back to the first character.

    while (size > 0) {
        *chunk = *chunk ^ (uint8_t) *keyptr;
        chunk++;
        size--;
        keyptr++;
        if (*keyptr == '\0') {
            keyptr = key;
        }
    }
}

// construct_filename:  make a file name in the form   .filename.piece#
// (add the dot at the beginning and the .piece# on the end).
void construct_filename(int piece,char *filename, char *serverfilename)
{
    sprintf(serverfilename,".%s.%d",filename,piece);
}


// init_header - fill in the header_t fields.
void init_header(header_t *header,char *filename,int command)
{
    header->command = command;
    header->status = 0;
 
    strcpy(header->username,username);
    strcpy(header->password,password);
    strcpy(header->filename,filename);
}


void display_status(int status)
{
    switch (status) {
        case STATUS_OK:
            break;
        case STATUS_ERR:
            printf("An unknown error occurred.\n");
            break;
        case STATUS_INVPASSWORD:
            printf("Invalid username/password. Please try again.\n");
            break;
        case STATUS_INVCOMMAND:
            printf("Invalid command\n");
            break;
        case STATUS_FILENOTFOUND:
            printf("File not found.\n");
            break;
        case STATUS_INCOMPLETE:
            printf("File is incomplete.\n");
            break;
        default:
            printf("Invalid error code\n");
            break;
    }
}

// put_piece - write a single piece of a file to a remote server.  we pass in 
// the starting position and size and the name of the remote file.
// so if the file is 1000 bytes long the position will be 0, 250, 500, 750 and the length
// will be 250.
int put_piece(server_t *server,int piece, int fd,long pos,int len, char *remotefile)
{
    header_t header;
    char serverfilename[MAX_FILENAME];
    unsigned char chunk[CHUNKSIZE];
    int amtread;
    int res;

    if (server->is_connected == 0) {
        printf("Server %s is not online\n",server->name);
        return -1;
    }

    // construct the piece's filename
    construct_filename(piece,remotefile,serverfilename);

    // init and send the header and command
    init_header(&header,serverfilename,CMD_PUT);
    header.size = len;

    fwrite(&header,sizeof(header_t),1,server->writefile);

    // send the file data
    lseek(fd,pos,SEEK_SET);
    while (len > 0) {

        // read from disk file
        amtread = min(len,sizeof(chunk));
        read(fd,chunk,amtread);

        // "encrypt" using our password.
        encrypt_decrypt(chunk,amtread,password);

        // send to server
        fwrite(chunk,sizeof(char),amtread,server->writefile);
        len = len - amtread;
    }
    fflush(server->writefile);

    // Wait for the response header.
    res = fread(&header,sizeof(header_t),1,server->readfile);

    if (res != 1) {
        printf("dfs_get bad response\n");
        return STATUS_ERR;
    }

    return header.status;
}

// put_file - split a file into pieces and spread out across the servers.

int put_file(char *localfile, char *remotefile)
{
    int pairvalue;
    int fd;
    struct stat statbuf;
    long piecesize;
    int i;
    int res = 0;

    pairvalue = pair_x_value(localfile);
    if (pairvalue < 0) {
        printf("Could not open local file %s\n",localfile);
        return STATUS_FILENOTFOUND;
    }

    stat(localfile,&statbuf);

    piecesize = (statbuf.st_size + 3) / 4;

    fd = open(localfile,O_RDONLY);

    if (fd < 0) {
        printf("Could not open local file %s\n",localfile);
        return STATUS_FILENOTFOUND;
    }

    for (i = 0; i < MAX_SERVERS; i++) {
        int p1,p2;
        server_t *server = &servers[i];

        // If the server is not connected, skip it.
        if (server->is_connected == 0) {
            continue;
        }

        // Figure out which pieces go on this server.
        whichpieces(pairvalue,i,&p1,&p2);

        // Write the pieces to the server.
        res = put_piece(server,p1,fd,piecesize*p1,piecesize,remotefile);
        if (res == 0) {
            res = put_piece(server,p2,fd,piecesize*p2,piecesize,remotefile);
        }
        if (res != 0) { // something went wrong
            break;
        }
    }

    close(fd);

    return res;
}



// retrieve file list from one DFS server.
// store it in the linked list.
int get_filelist(server_t *server)
{
    header_t header;
    char *files;
    char *ptr;
    char *onefile;
    int res;

    // don't do anything if the server is not connected.
    if (server->is_connected == 0) {
        return 0;
    }
    
    init_header(&header,"",CMD_LIST);

    fwrite(&header,sizeof(header_t),1,server->writefile);
    fflush(server->writefile);

    // Wait for the response header.
    res = fread(&header,sizeof(header_t),1,server->readfile);

    if (res != 1) {
        printf("dfs_getfilelist bad response\n");
        return STATUS_ERR;
    }

    if (header.status != 0) {
        return header.status;
    }

    // no files?
    if (header.size == 0) {
        return 0;
    }

    // Read the file list from the server.
    files = malloc(header.size+1);
    res = fread(files,sizeof(char),header.size,server->readfile);
    if (res != header.size) {
        printf("did not get the file list from the server\n");
        return STATUS_ERR;
    }

    // null terminate the string
    files[header.size] = '\0';

    // Walk the file list.
    ptr = files;

    for (;;) {
        onefile = strsep(&ptr,"\n");
        if (onefile == NULL) {
            break;
        }
        // ignore blank lines.
        if (*onefile == '\0') {
            continue;
        }

        // add the file to our file list
        addfile(server,onefile);
    }

    // Free the buffer we allocated.
    free(files);

    return 0;
}

// retrieve the file list from all DFS servers.
int get_all_filelists()
{
    int i;
    int res = 0;

    clearfilelist();

    for (i = 0; i < MAX_SERVERS; i++) {
        res = get_filelist(&servers[i]);
        if (res != 0) {
            break;
        }
    }

    return res;
}


// is_file_complete:  returns 1 if we have at least one server
// for each piece of the file.
int is_file_complete(remotefile_t *rf)
{
    int cnt;
    int i;

    // Make sure we have a server for each piece.
    cnt = 0;
    for (i = 0; i < MAX_SERVERS; i++) {
        if (rf->pieces[i] != NULL) {
            cnt++;
        }
    }


    if (cnt == MAX_SERVERS) {
        return 1;
    } else {
        return 0;
    }

}

// print out the file list.
void print_filelist(void)
{
    remotefile_t *rf;

    rf = filelist;

    while (rf != NULL) {
        if (is_file_complete(rf)) {
            printf("%s\n", rf->filename);
        } else {
            printf("%s  [incomplete]\n", rf->filename);
        }

        rf = rf->next;
    }
}


// get a piece from a DFS server and append it to the currently open file
int get_append_piece(server_t *server, int fd, char *piecefilename)
{
    header_t header;
    unsigned char chunk[CHUNKSIZE];
    int amtleft;
    int amt;
    int res;
    int status;

    // create a GET header and send it to the server.
    init_header(&header,piecefilename,CMD_GET);
    fwrite(&header,sizeof(header_t),1,server->writefile);
    fflush(server->writefile);

    // Wait for the response header.  IF there was an error, return it.
    res = fread(&header,sizeof(header_t),1,server->readfile);

    if (res != 1) {
        printf("dfs_getfile bad response\n");
        return STATUS_ERR;
    }

    if (header.status != 0) {
        return header.status;
    }

    amtleft = header.size;

    if (debug > 2) {
        printf("retrieving %d bytes for %s %s\n",header.size,server->name,piecefilename);
    }

    // read the chunks of the file that follow the header, write to local file

    status = 0;

    while (amtleft > 0) {
        amt = min(amtleft,sizeof(chunk));
        res = fread(chunk,sizeof(char),amt,server->readfile);
        if (res != amt) {
            printf("Didn't get all the data\n");
            status = STATUS_ERR;
            break;
        }

        // "decrypt" using our password.
        encrypt_decrypt(chunk,res,password);

        // write to local file
        write(fd,chunk,res);
        amtleft = amtleft - amt;
    }

    return status;
}


// get all pieces from the DFS and store to a local f ile.
int get_file(char *remotefile,char *localfile)
{
    remotefile_t *rf;
    char filename[MAX_FILENAME];
    int piece;
    int fd;

    // find the file on linked list and error if it's not there
    rf = findfile(remotefile);

    if (rf == NULL) {
        printf("ERROR: file not found: %s\n",remotefile);
        return STATUS_FILENOTFOUND;
    }

    // See if the file is complete, error if it is not.
    if (!is_file_complete(rf)) {
        printf("ERROR: File %s is not complete\n",remotefile);
        return STATUS_INCOMPLETE;
    }

    // Open the local file that we will be fetching the pieces into.
    fd = open(localfile,O_RDWR|O_CREAT|O_TRUNC,S_IREAD|S_IWRITE);

    if (fd < 0) {
        printf("ERROR: Cannot open local file %s\n",localfile);
        return STATUS_FILENOTFOUND;
    }

    // fetch the pieces.

    for (piece = 0; piece < MAX_PIECES; piece++) {
        construct_filename(piece,remotefile,filename);
        get_append_piece(rf->pieces[piece],fd,filename);
    }

    close(fd);

    return 0;

}

// ========================================================================

// command_get : implement the "get" command
int command_get(char *remfile, char *lclfile)
{
    int res;

    if (lclfile == NULL)
	lclfile = remfile;

    connect_all_servers();

    res = get_all_filelists();
    if (res != 0) {
        display_status(res);
        close_all_connections();
        return res;
    }

    res = get_file(remfile,lclfile);
    display_status(res);

    close_all_connections();

    if (res == 0) {
        printf("SUCCESS. Retrieved %s as %s\n",remfile,lclfile);
    }

    return res;
}

// command_put : implement the "put" command
int command_put(char *lclfile, char *remfile)
{
    int count;
    int res;

    if (remfile == NULL)
	remfile = lclfile;

    // connect to the remote servers.

    count = connect_all_servers();

    // We need at least 3 out of the 4 running to guarantee a write

    if (count < (MAX_SERVERS-1)) {
        printf("Cannot put file, not enough servers to guarantee integrity\n");
        close_all_connections();
        return STATUS_INCOMPLETE;
    }

    // if the remote name not specified it is the same as the local name

    if (remfile == NULL) {
        remfile = lclfile;
    }

    // put the file to the servers

    res = put_file(lclfile,remfile);
    display_status(res);

    close_all_connections();

    if (res == 0) {
        printf("SUCCESS. Stored %s as %s\n",lclfile,remfile);
    }

    return res;
}

// command_list : implement the "list" command
int command_list(void)
{
    int res;
    connect_all_servers();

    res = get_all_filelists();
    display_status(res);

    if (res == 0) {
        printf("-- Remote file list --\n");
        print_filelist();
        printf("----------------------\n");
    }

    close_all_connections();

    return res;
}

// ========================================================================

// do_command: process a command typed in by the user.
int do_command(char *cmdline)
{
    char *ptr;
    char *cmd;
    char *remfile;
    char *lclfile;

    ptr = cmdline;
    cmd = strsep(&ptr," ");

    if (!cmd || *cmd == '\0') {
        return 1;
    }

    if (strcmp(cmd,"get") == 0) {
        remfile = strsep(&ptr," ");
        lclfile = strsep(&ptr," ");
        command_get(remfile, lclfile);
    } else if (strcmp(cmd,"put") == 0) {
        lclfile = strsep(&ptr," ");
        remfile = strsep(&ptr," ");
        command_put(lclfile, remfile);
    } else if (strcmp(cmd,"list") == 0) {
        command_list();
    } else if (strcmp(cmd,"exit") == 0) {
        return 0;
    } else if (strcmp(cmd,"quit") == 0) {
        return 0;
    } else {
        printf("Invalid command: %s\n",cmd);
    }

    return 1;

}

// command_loop : prompt for commands, then process them
void command_loop(void)
{
    char cmdline[500];
    char *x;

    for (;;) {
        printf("dfc> ");
        fgets(cmdline,sizeof(cmdline),stdin);

        // fgets adds a new line to the end, strip it off
        if ((x = strchr(cmdline,'\n'))) {
            *x = '\0';
        }

        // do the command.
        if (do_command(cmdline) == 0) {
            break;
        }
    }
}


//
// Main function
//

int main(int argc, char *argv[])
{
    char *cfgfile;
    char *cmd = NULL;


#if 0
    // this prints out the "table 1" as it is computed by "whichpieces"
    // uncomment this to make sure table is correct.
    int x,s;
    for (x = 0; x < 4; x++) {
        printf ("Xval:%d   ",x);
        for (s = 0; s < 4; s++) {
            int p1,p2;
            whichpieces(x,s,&p1,&p2);
            printf("(%d,%d)   ",p1+1,p2+1);
        }
        printf("\n");
    }
    exit(1);
#endif

    if (argc < 2) {
        printf("Usage: dfc config-file [command]\n");
        exit(1);
    }

    cfgfile = argv[1];          // the first arg is the config file
    if (argc > 1) {
        cmd = argv[2];          // the second arg is a command.  If no command we prompt for one.
    }


    // Load the config file
    if (readconfig(cfgfile) < 0) {
        printf("Could not load configuration file '%s'\n",cfgfile);
        exit(1);
    }

    // interpret commands.  We can pass one command as an argument to make scripting easier.
    if (cmd != NULL) {
        do_command(cmd);
    } else {
        command_loop();
    }


    return 0;
}


