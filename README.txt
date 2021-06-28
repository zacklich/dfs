Zachariah Lichtenberg
This is PA4, a very simple distributed file system.

The server consists of:    
    dfs.c     Main Progran
    dfs.h     Header file

The client consists of:
    dfc.c     Main program
    dfs.h     Same header file as server
    md5.c     Open-source MD5 code
    md5.h     Open-source MD5 code

There are several test scripts:  
    dfcstress.sh      Read and write files in a loop with a user's config file
    passwordtest.sh   Try to access the server with a bad user name or password
    incomplete.sh     Test incomplete files by running with only 3 servers
    maketestfiles.sh  Create a bunch of test files of random data

Sample configuration files:
    alice.conf                  Bob's friend Alice.
    bob.conf                    Alice's friend Bob.
    badusername.conf            contains a bad username
    badpassword.conf            Contains a valid username but bad password

There are also some makefile commands:
    make clear               Erase all of the DFS directories
    make stop                Kill all the DFS servers
    make start               Start all the DFS servers
    make start123            Start servers 1,2,3
    make start234            Start servers 2,3,4


Extra credit:
- Subdirectory support is partially implemented (in the server only) but wasn't done in time. (not done)
- A simple XOR encryption is implemented in the client to store the files "securely" on the server (done)

A lot of code was taken from the web server assignment and the "uftp" assignment.

To compile the program, just type 'make'

Usage (Server)

There are makefile targets to start and stop servers:

      make start           Start all 4 servers
      make start123        Start servers 1,2,3
      make start234        Start servers 2,3,4
      make stop            Stop all servers
      make clear           Delete all of the DFS directories

** Usage (client)

The client may be run as follows:

    $ ./dfc configfile.conf
    dfc>

Starting with just a config file, the DFC program will prompt for commands until "exit" is typed.
As an alternative, you can supply a command when you start DFC.

    $ ./dfc configfile.conf "command"

For example

    $ ./dfc bob.conf "list"
    $ ./dfc bob.conf "get remotefile.txt"

This makes it easier to run in a shell script.


------------------------------------------------------------------------

** Stress Test Script

The stress test script writes several dozen files to the DFS, reads them back,
and compares them with the originals, many times in a loop.

Before running tests create the test files

   ./maketestfiles.sh

In the server window type:

   make clear
   make start

In the client windows type:

   ./dfcstress.sh bob.conf
   ./dfcstress.sh alice.conf

You can kill server processes while this test is running.


------------------------------------------------------------------------

** Invalid password script

The invalid password script uses different config files to try commands
against the server with either an invalid username or an invalid password.

In the server window type:

   make start

In the client window type:

   ./passwordtest.sh

------------------------------------------------------------------------

** incomplete file test:

The incomplete file test first uploads some files with only three operational
servers (1,2,3), then we start three different servers (2,3,4) and attempt
to list the files. Since the dead server (4) wasn't there to get the redundant 
copy and the previously dead server (1) won't have it, the files will be incomplete.

In the server window type:

   make clear

In the client window type:

   ./incomplete.sh

Follow the prompts in the script to start subsets of servers.


------------------------------------------------------------------------



Design Decisions

This server is muilti-threaded, not multi-process.  No specific thread management 
is used.  a new thread is spawned for each inbound connection from the client and
the thread terminates once the connection is closed by the client.

Much of this code was taken from the webserver assignment.

In the server, like previous assignments, there is a "connection_t" structure that
contains the information about a connection that is in progress.  As each
thread is spawned a new connection_t is allocated.

Like the TFTP server, there is a "header" at the start of each message sent to the 
server.  The header contains a command, a file size, a status code, and the username,
password, and file name from the client.    The commands are defined in dfs.h.

#define CMD_GET         0
#define CMD_PUT         1
#define CMD_LIST        2

typedef struct header_s {
    int command;                        // packet type
    int size;                           // size of stuff after  header
    int status;                         // status for returned packets
    char filename[MAX_FILENAME];
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} header_t;


One limitation of this design is that the username, password, and file name have fixed 
maximum lengths.

Each command to the server starts by sending the header, and then the data for a PUT
command.   The response from the server also starts with a header followed by the
data for a GET or LIST command.

for a GET command:
    Client sends header with no other data
    Server sends header and contents of the file (or error status)

For a PUT command:
    Client sends header with the file data
    Server sends a header with error status

For a LIST command:
    Client sends a header with no other data
    Server sends a header with a list of file names separated by newlines

The table of the upload options is easy to compute, the whichpieces() function does this.
By changing the piece numbers and server numbers to be 0-based, a clear pattern 
emerges that can be computed by the modulo (%) operator.

For the MD5 function, some open-source code was found online that is simple and self-contained.

The pieces are stored numbered from 0, not 1.   So, the four pieces end in .0, .1, .2, and .3.

In the client, the GET and PUT commands let you specify the remote file name.  So you can say
"put localfile remote-file" to store the local file with a different remote name.  Likewise
the GET command lets you store a remote file with a different local name.  This makes test 
scripts easier to write.


------------------------------------------------------------------------







