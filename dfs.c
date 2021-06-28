
// Zachariah Lichtenberg
// A very simple distributed file server
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


#include "dfs.h"

int debug = 1;

// This structure keeps track of a connection (it is created for each
// thread).  It is largely based on the webserver assignment.

typedef struct connection_s {
    int readfd;
    int writefd;
    FILE *readfile;
    FILE *writefile;
    char *base_directory;
} connection_t;

// the userinfo structure is a list of our valid user names and passwords.

typedef struct userinfo_s {
    struct userinfo_s *next;
    char *username;
    char *password;
} userinfo_t;

// the linked list of users.
userinfo_t *userlist = NULL;

char *cmdnames[] = {"GET","PUT","LIST","MKDIR","DELETE","RMDIR"};

static int min(int a,int b)
{
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

// finduser - locate a user on the userlist linked list. 

userinfo_t *finduser(char *uname)
{
    userinfo_t *uinfo;

    uinfo = userlist;

    while (uinfo != NULL) {
        if (strcmp(uname,uinfo->username) == 0) {
            return uinfo;
        }
       
        uinfo = uinfo->next;
    }

    return NULL;
}

// checkpassword - given a user name and a password return 1 if it is valid, and 0 if invalid
int checkpassword(char *uname,char *pword)
{
    userinfo_t *uinfo = finduser(uname);

    // If the user is not found, the password is invalid
    if (uinfo == NULL) {
        return 0;
    }

    // If the user was found and the password does not match, it's invalid.
    if (strcmp(uinfo->password,pword) != 0) {
        return 0;
    }

    // otherwise good to go
    return 1;
}

// adduser - add a user name and password to the list (from the config file)
void adduser(char *uname, char *pword)
{
    userinfo_t *uinfo;

    // Create a new userinfo structure
    uinfo = (userinfo_t *) calloc(1,sizeof(userinfo_t));

    uinfo->username = strdup(uname);
    uinfo->password = strdup(pword);

    // Add to the linked list
    uinfo->next = userlist;
    userlist = uinfo;

}

// readconfig - read the config file and store the users.
int readconfig(char *filename)
{
    FILE *file;
    char *x;
    char line[1000];
    char *uname;
    char *pword;

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

        // Break off two words from the line, one is the username, the other is the password
        x = line;
        uname = strsep(&x," ");
        pword = strsep(&x," ");

        if (uname && pword) {
            printf("Added user '%s' with password '%s'\n",uname,pword);
            adduser(uname, pword);
        }
    }


    fclose(file);

    return 0;
}

//
// close_connection - close all sockets and terminate the current thread.
// (from the webserver assignemnt)
//
void close_connection(connection_t *conn)
{
    if (debug > 0) {
        printf("%s: --- Closing thread for FD %d\n",conn->base_directory,conn->readfd);
    }

    // Close the streams
    fclose(conn->readfile);
    fclose(conn->writefile);

    // Free the 'connection' structure
    free(conn);          

    // Cause this thread to exit
    pthread_exit(NULL);
}

// Send_status sends a header structure with a status value.
// common subroutine
void send_status(connection_t *conn, header_t *header, int status)
{
    header->size = 0;
    header->status = status;

    fwrite(header,sizeof(header_t),1,conn->writefile);
    fflush(conn->writefile);
}

// construct_filename builds a filename from the haeder, the current DFS server home
// directory, the user's name, and the file name.

static void construct_filename(connection_t *conn,header_t *header,char *filename)
{
    // make a file name from the connection info and the header.
    // if our base_directory is "DFS1" and the username is "Alice", and the file is "foo.txt.1",
    // we will return   DFS1/Alice/foo.txt.1

    snprintf(filename,MAX_FILENAME,"%s/%s/%s",conn->base_directory,header->username,header->filename);
 }

// construct_filedir is like the above but only builds the DFS server home and user directory.
static void construct_filedir(connection_t *conn,header_t *header,char *filedir)
{
    // this is like construct_filename but only makes a directory name (no filename appended)

    snprintf(filedir,MAX_FILENAME,"%s/%s",conn->base_directory,header->username);
}


// check_user_directory sees if a user's directory has already been created and
// creates it if the directory is not there yet.
void check_user_directory(connection_t *conn, char *username)
{
    char filename[MAX_FILENAME];
    struct stat statbuf;

    snprintf(filename,MAX_FILENAME,"%s/%s",conn->base_directory,username);

    // If we could not "stat" the directory, make it.
    if (stat(filename,&statbuf) < 0) {
        mkdir(filename,0777);
    }
}



// cmd_get - process the GET command from the client.
void cmd_get(connection_t *conn,header_t *header)
{
    char filename[MAX_FILENAME];
    struct stat statbuf;
    int fd;
    uint8_t chunk[1024];
    int res;

    // See if the local file is there, get its size, and send
    // a header back.

    construct_filename(conn,header,filename);

    // is the file there
    if (stat(filename,&statbuf) < 0) {
        send_status(conn, header, STATUS_FILENOTFOUND);
        return;
    }

    // yes, open it
    fd = open(filename,O_RDONLY);
    if (fd < 0) {
        send_status(conn, header, STATUS_FILENOTFOUND);
        return;
    }

    // send back the size
    header->size = statbuf.st_size;
    header->status = STATUS_OK;
    
    fwrite(header,1,sizeof(header_t),conn->writefile);

    // send chunks of the file until we are done
    for (;;) {
        res = read(fd,chunk,sizeof(chunk));
        if (res <= 0) {
            break;
        }
        fwrite(chunk,res,sizeof(uint8_t),conn->writefile);
    }

    fflush(conn->writefile);

    // close the local file
    close(fd);

}

// cmd_put - process the PUT command on the client
void cmd_put(connection_t *conn,header_t *header)
{
    int fd;
    int res;
    char filename[MAX_FILENAME];
    int remsize;
    int amtread;
    uint8_t chunk[1024];

    construct_filename(conn,header,filename);

    // Make sure the user's directory exists
    check_user_directory(conn, header->username);

    // open the file
    fd = open(filename,O_RDWR|O_TRUNC|O_CREAT,S_IREAD|S_IWRITE);

    if (fd < 0) {
        printf("Could not open %s: %s\n",filename,strerror(errno));
    }

    // read chunks from the client, write to the file
    remsize = header->size;

    while (remsize > 0) {
        amtread = min(sizeof(chunk),remsize);
        res = fread(chunk,sizeof(char),amtread,conn->readfile);

        if (res != amtread) {
            printf("Didn't get all we expected on cmd_put\n");
            break;
        }

        if (fd > 0) {
            write(fd,chunk,amtread);
        }

        remsize = remsize - amtread;
    }

    if (debug > 2) {
        printf("cmd_put Sending status\n");
    }

    if (fd > 0) {
        close(fd);
        send_status(conn,header,STATUS_OK);
    } else {
        send_status(conn,header,STATUS_ERR);
    }
    
}

/* fill_in_directory: get list of files in a directory 
   most of this routine was found on the manual page for 'readdir'
   code taken from PA1. */

int fill_in_directory(char *dirname, char *buffer, int maxlen)
{
    DIR *dirp;
    struct dirent *dp;

    buffer[0] = '\0';

    dirp = opendir(dirname);
    if (dirp == NULL)
        return 0;

    while ((dp = readdir(dirp)) != NULL) {

        // do not send the special "." and ".." files
        if ((strcmp(dp->d_name,".") == 0) || (strcmp(dp->d_name,"..") == 0)) {
            continue;
        }

        // Stop if we would overflow our buffer
        if ((strlen(buffer) + strlen(dp->d_name) + 2) > maxlen) {
            break;
        }

        // Skip files not starting with a dot
        if (dp->d_name[0] != '.') {
            continue;
        }

        strcat(buffer,dp->d_name);
        strcat(buffer,"\n");
    }
    closedir(dirp);

    return strlen(buffer);
}

// cmd_list - handle the LIST command - send back a list of files in the directory.
void cmd_list(connection_t *conn,header_t *header)
{
    char *filelist;
    char filedir[MAX_FILENAME];
    int len;

    construct_filedir(conn,header,filedir);

    // this is a hack.  If you specify a file name in the 'list' command
    // you can peek inside a directory.

    if (header->filename[0]) {
        strcat(filedir,"/");
        strcat(filedir,header->filename);
    }

    // Allocate a big-enough buffer for the file list
    filelist = malloc(50000);

    // Use the directory reader from PA1 
    len = fill_in_directory(filedir,filelist,50000);

    // Write out the header to the client
    header->size = len;
    header->status = STATUS_OK;
    fwrite(header,sizeof(header_t),1,conn->writefile);

    // If there is any files in the file list, send those back.
    if (len > 0) {
        fwrite(filelist,sizeof(char),len,conn->writefile);
    }

    fflush(conn->writefile);

    free(filelist);

}

// cmd_mkdir, cmd_rmdir, cmd_remove - the server supports them
// but client does not yet

void cmd_mkdir(connection_t *conn,header_t *header)
{
    int res;
    char filename[MAX_FILENAME];

    // Try to create the directory
    construct_filename(conn,header,filename);
    res = mkdir(header->filename,0777);

    // return status result
    if (res < 0) {
        send_status(conn,header,STATUS_ERR);
    } else {
        send_status(conn,header,STATUS_OK);
    }
}


void cmd_rmdir(connection_t *conn,header_t *header)
{
    int res;
    char filename[MAX_FILENAME];

    // Try to remove the directory
    construct_filename(conn,header,filename);
    res = rmdir(header->filename);

    // return status result
    if (res < 0) {
        send_status(conn,header,STATUS_ERR);
    } else {
        send_status(conn,header,STATUS_OK);
    }
}

void cmd_delete(connection_t *conn,header_t *header)
{
    int res;
    char filename[MAX_FILENAME];

    // Try to remove the file
    construct_filename(conn,header,filename);
    res = remove(filename);

    // return status result
    if (res < 0) {
        send_status(conn,header,STATUS_ERR);
    } else {
        send_status(conn,header,STATUS_OK);
    }
}


// ------------------------------------------------------------------------

// dfsthread - this thread is spawned for each inbound connection.
// read a command from the client and call one of the cmd_ routines .
// borrowed from the web server assignment.

void *dfsthread(void *info)
{
    connection_t *conn = (connection_t *) info;

    if (debug > 0) {
        printf("%s: +++ Creating new thread for FD %d\n",conn->base_directory,conn->readfd);
    }

    // Create a read and a write FILE structure 
    // https://ycpcs.github.io/cs365-spring2017/lectures/lecture15.html
    conn->writefd = dup(conn->readfd);

    // 'fdopen' lets us turn a file descriptor into a FILE *, like we get 
    // when you call fopen() on a file name.
    // Make one FILE * for reading, one for writing
    conn->readfile = fdopen(conn->readfd,"r");
    conn->writefile = fdopen(conn->writefd,"w");

    // Drop into the loop here waiting for commands from the server.

    for (;;) {
        header_t header;

        if (fread(&header,sizeof(header),1,conn->readfile) != 1) {
            // we did not get all of the data, the connection must be gone
            break;
        }

        // Check username and password right away.
        // If the password is wrong zero out the size and send back just the header
        // with an "invalid password" status

        if (checkpassword(header.username,header.password) == 0) {
            send_status(conn,&header,STATUS_INVPASSWORD);
            printf("Invalid username/password: %s %s\n",header.username,header.password);
            // go back to next command.
            continue;
        }

        if (debug > 0) {
            if (header.command <= CMD_MAX) {
                printf("%s: [%s] Cmd:%s File:'%s'\n",conn->base_directory,header.username,cmdnames[header.command],header.filename);
            } else {
                printf("%s: [%s] Bad command %d\n",conn->base_directory,header.username,header.command);
            }
        }

        // Call a subroutine for each command
        switch (header.command) {
            case CMD_GET:
                cmd_get(conn,&header);
                break;
            case CMD_PUT:
                cmd_put(conn,&header);
                break;
            case CMD_LIST:
                cmd_list(conn,&header);
                break;
            case CMD_MKDIR:
                cmd_mkdir(conn,&header);
                break;
            case CMD_RMDIR:
                cmd_rmdir(conn,&header);
                break;
            case CMD_DELETE:
                cmd_delete(conn,&header);
                break;
            default:
                send_status(conn,&header,STATUS_INVCOMMAND);
                break;
        }
    }


    // we only do one connection at a time
    close_connection(conn);

    return NULL;

}

// main loop of the file server.  Wait for a connection, accept it,
// and spawn a thread.

void dfsserver(char *directory, int portno)
{
    struct sockaddr_in serveraddr;
    int listen_fd;
    int res;
    int set = 1;

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    // Create a listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    res = setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&set,sizeof(set));
    if (res < 0) {
        perror("setsockopt");
        close(listen_fd);
        return;
    }

    // Bind to the port we will listen on.
    res = bind(listen_fd,(struct sockaddr *) &serveraddr,sizeof(serveraddr));
    if (res < 0) {
        perror("bind");
        close(listen_fd);
        return;
    }

    // Start listening for connections.
    listen(listen_fd, 10);              // max # of connections we can accept


    printf("Waiting for connections\n");

    // Here is the main loop.

    for (;;) {

        // this sockaddr will get filled in with the IP of the client that connects to us.
        struct sockaddr_in newaddr;
        socklen_t newaddrlen  = sizeof(newaddr);
        pthread_attr_t threadattr;
        pthread_t thread;
        int nfd;
        connection_t *conn;

        // Accept next connection

        nfd = accept(listen_fd, (struct sockaddr *) &newaddr, &newaddrlen);

        if (nfd < 0) {
            perror("accept");
            continue;
        }

        if (debug > 1) {
            printf("%s: Got a connection from %s\n",directory,inet_ntoa(newaddr.sin_addr));
        }

        // Create a connection structure to hold information about the connection
        conn = (connection_t *) calloc(1,sizeof(connection_t));
        conn->readfd = nfd;
        conn->base_directory = directory;

        // Create a new thread
        pthread_attr_init(&threadattr);
        pthread_create(&thread, &threadattr, dfsthread, conn);

        // The new thread is responsible for closing the connection when done.
        // Go back and wait for another one.
    }
}

//
// Main function
//

int main(int argc, char *argv[])
{
    int port;
    char *directory;
    struct stat statbuf;

    if (argc < 3) {
        printf("Usage: dfs root-directory port-number\n");
        printf("\n");
        printf("For example,  dfs DFS1 10001\n");
        exit(1);
    }

    directory = argv[1];
    port = atoi(argv[2]);

    printf("Starting DFS server with home directory '%s' on port %d\n",directory,port);

    // Load the user list

    if (readconfig("dfs.conf") < 0) {
        printf("Could not load configuration file 'dfs.conf'\n");
        exit(1);
    }


    // Create DFS directory if needed

    if (stat(directory,&statbuf) < 0) {
        if (mkdir(directory,0777) < 0) {
            printf("Error, could not create DFS directory '%s'\n",directory);
        } else {
            printf("Created DFS directory: %s\n",directory);
        }
    } else {
        printf("DFS directory %s already exists\n",directory);
    }


    // Start the server
    dfsserver(directory,port);

    return 0;
}


