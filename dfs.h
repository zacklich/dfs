
// Use a fixed-length header for now, it's easier to code. 
#define MAX_FILENAME    256
#define MAX_USERNAME    64
#define MAX_PASSWORD    64

typedef struct header_s {
    int command;                        // packet type
    int size;                           // size of stuff after  header
    int status;                         // status for returned packets
    char filename[MAX_FILENAME];
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} header_t;


#define CMD_GET         0
#define CMD_PUT         1
#define CMD_LIST        2
#define CMD_MKDIR       3
#define CMD_DELETE      4
#define CMD_RMDIR       5
#define CMD_MAX         5


#define STATUS_OK       0
#define STATUS_ERR      -1
#define STATUS_INVPASSWORD -2
#define STATUS_INVCOMMAND -3
#define STATUS_FILENOTFOUND -4
#define STATUS_INCOMPLETE -5
