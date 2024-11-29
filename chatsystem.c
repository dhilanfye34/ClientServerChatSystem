#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

# define PORT 5017
# define SERVER_IP "127.0.0.1"
#define MSG_SIZE 1024

typedef struct {
    int fd;
    char name[50];
    char room[50];
    bool logged_in;
} Client;

Client clients[1000];

struct pollfd fds[1000];

//connection to server and returns fd
int conn_to_server() {
    int sockfd;
    printf("Creating socket...\n");
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("Socket error\n");
        exit(1);
    }
    printf("Socket created\n");
    
    printf("Setting up server\n");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    int add = inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);
    if (add <= 0) {
        printf("Invalid address");
        exit(1);
    }

    printf("Connecting to server at %s: %d\n", SERVER_IP, PORT);
    int conn_ret = connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if(conn_ret < 0) {
        printf("Connect error\n");
        exit(1);
    }
    printf("Connected to server\n");
    return sockfd;
}

// makes timestamp for msgs
void time_stamp(char *msg, int size) {
    time_t curr_time = time(NULL);
    struct tm *t = localtime(&curr_time);
    strftime(msg, size, "[%H:%M:%S]", t);
}

// checks if user/pw are valid from file
bool verify_login(char *unm, char *pw) {
    FILE *file = fopen("users.txt", "r");
    if(!file) {
        printf("Error opening users.txt\n");
        exit(1);
    }
    char file_nm[50] = "";
    char file_pw[50] = "";
    while(fscanf(file, "%s %s", file_nm, file_pw) == 2) {
        if(strcmp(file_nm, unm) == 0 && strcmp(file_pw, pw) == 0) {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

// reads pw input, shows stars instead of chars
void read_pw(char *pw, int size){
    struct termios oldp;
    struct termios newp;
    int index = 0;
    char c;

    tcgetattr(STDIN_FILENO, &oldp);
    newp = oldp;

    newp.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newp);
  
    printf("Enter password: ");
    fflush(stdout);

    while (index < size - 1){
        int b_read = read(STDIN_FILENO, &c, 1);
        if(b_read == 1){
            if(c == '\n'){
                break;
            }
            else if(c == 127 || c == '\b'){
                if(index > 0){
                    index--;
                    printf("\b \b");
                    fflush(stdout);
                }
            }
            else {
                pw[index++] = c;
                printf("*");
                fflush(stdout);
            }
        }
    }
    pw[index] = '\0';
    printf("\n");

    tcsetattr(STDIN_FILENO, TCSANOW, &oldp);
}

int sig_num = 0; // number of times sigint has been called

// shuts server down, closes all conns
void shut_down(int sig) {
    sig_num++;
    if(sig_num > 1){
        printf("Server shutting down\n");
        exit(1);
    }

    printf("Server shutting down\n");
    for(int i = 0; i < 1000; i++) {
        if(fds[i].fd != -1) {
            char msg[] = "Server shutting down\n";
            write(fds[i].fd, msg, strlen(msg));
            close(fds[i].fd);
            fds[i].fd = -1;
        }
    }
    
    if(fds[0].fd != -1) {
        close(fds[0].fd);
        fds[0].fd = -1;
    }
    printf("All connections closed\n");
    exit(0);
}

// handles client input and server msgs
void client_poll(int sockfd) {
    struct pollfd fds[2]; // 0 for input, 1 for server
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    char message[MSG_SIZE] = {0};
    char unm[50] = "";
    char pw[50] = "";
    char rm[50] = "";   

    while(1) {
        memset(message, 0, sizeof(message));
        int ret = poll(fds, 2, -1);
        if(ret < 0) {
            printf("poll error\n");
            exit(1);
        }
        if(fds[0].revents & POLLIN) { // user input
            int b_read = read(STDIN_FILENO, message, sizeof(message));
            if(b_read > 0) {
                message[b_read] = '\0';
                if(strncmp(message, "login", 5) == 0) {
                    printf("Enter username: ");
                    fgets(unm, sizeof(unm), stdin);
                    unm[strcspn(unm, "\n")] = '\0';
                    read_pw(pw, sizeof(pw));
                    char login_msg[MSG_SIZE] = "";
                    snprintf(login_msg, sizeof(login_msg), "login %s %s", unm, pw);
                    write(sockfd, login_msg, strlen(login_msg));
                }
                else if(strncmp(message, "enter", 5) == 0) {
                    sscanf(message, "enter %s", rm);
                    snprintf(message, sizeof(message), "enter %s\n", rm);
                    write(sockfd, message, strlen(message));
                }
                else {
                    write(sockfd, message, strlen(message));
                }
            }
        }
        if(fds[1].revents & POLLIN) { // server message
            int b_read = read(sockfd, message, sizeof(message));
            if(b_read > 0) {
                message[b_read] = '\0';
                printf("%s", message);
            }
            else {
                printf("Server disconnected, trying to reconnect...\n");
                close(sockfd);
                int attempts = 0;
                while(attempts < 3){
                    sockfd = conn_to_server();

                    if (sockfd > 0) {
                        printf("Reconnected to server.\n");
                        fds[1].fd = sockfd;

                        if (strlen(unm) > 0) {
                            char login_msg[MSG_SIZE];
                            snprintf(login_msg, sizeof(login_msg), "login %s %s", unm, pw);
                            write(sockfd, login_msg, strlen(login_msg));
                        }
                        if (strlen(rm) > 0) {
                            char enter_msg[MSG_SIZE];
                            snprintf(enter_msg, sizeof(enter_msg), "enter %s", rm);
                            write(sockfd, enter_msg, strlen(enter_msg));
                        }
                        break;
                    }
                    attempts++;
                }
                if (attempts == 3) {
                    printf("Failed to reconnect.\n");
                    exit(1);
                }
            }
        }
    }
}

// sets up server socket, ready to listen
int mk_server() {
    int serv_fd;
    struct sockaddr_in serv_addr;

    printf("Creating socket...\n");
    serv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_fd < 0) {
        printf("Socket error\n");
        exit(1);
    }

    printf("Socket created\n");
      
    int opt = 1;
    if (setsockopt(serv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Set socket option failed");
        exit(1);
    }
      
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    printf("Binding server socket to port %d...\n", PORT);
    if (bind(serv_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1){
        printf("Bind failed\n");
        close(serv_fd);
        exit(1);
    }

    printf("Socket bound to port %d.\n", PORT);

    if(listen(serv_fd, 50) == -1) {
        printf("Listen failed\n");
        exit(1);
    }

    printf("Server active on port %d\n", PORT);
    return serv_fd;
}

// handles new client conns
void handle_conns(int serv_fd, struct pollfd *fds, Client *clients) {
    struct sockaddr_in cl_addr;
    socklen_t cl_len = sizeof(cl_addr);
    int cl_fd = accept(serv_fd, (struct sockaddr *) &cl_addr, &cl_len);
    if (cl_fd < 0) {
        printf("Accept failed\n");
        exit(1);
    }

    printf("New connection from %s:%d\n", inet_ntoa(cl_addr.sin_addr), ntohs(cl_addr.sin_port));
    
    for(int i = 1; i < 1000; i++) {
        if(fds[i].fd == -1) {
            fds[i].fd = cl_fd;
            fds[i].events = POLLIN;
            clients[i].fd = cl_fd;
            strcpy(clients[i].name, "");
            strcpy(clients[i].room, "");
            return;
        }
    }
    printf("No new connection.\n");
    close(cl_fd);
}

// processes msgs from clients
void handle_msgs(int i, struct pollfd *fds, Client *clients) {
    char message[MSG_SIZE] = {0};
    int b_read = read(fds[i].fd, message, sizeof(message));
    if (b_read > 0) {
        message[b_read] = '\0';

        if(!clients[i].logged_in && strncmp(message, "login", 5) != 0) {
            char msg[] = "Login before sending messages\n";
            write(fds[i].fd, msg, strlen(msg));
            return;
        }

        if (strncmp(message, "login", 5) == 0) {
            char *content = message + 6;
            char *nm = strtok(content, " \n");
            char *pw = strtok(NULL, " \n");

            if (!nm || !pw) {
                char msg[] = "Invalid username and/or password, try again.\n";
                write(fds[i].fd, msg, strlen(msg));
                return;
            }

            if (verify_login(nm, pw)) {
                strcpy(clients[i].name, nm);
                clients[i].logged_in = true;
                char msg[MSG_SIZE];
                snprintf(msg, sizeof(msg), "You are logged in %s. Welcome.\n", nm);
                write(fds[i].fd, msg, strlen(msg));
            } 
            else {
                char msg[] = "User does not exist, try again.\n";
                write(fds[i].fd, msg, strlen(msg));
            }
        } 
        else if (strncmp(message, "create", 6) == 0) {
            char *rm = strtok(message + 7, "\n");
            if (!rm) {
                char msg[] = "Invalid create message\n";
                write(fds[i].fd, msg, strlen(msg));
                return;
            } 
            else {
                bool room_exists = false;
                for (int j = 0; j < 1000; j++) {
                    if (clients[j].fd != -1 && strcmp(clients[j].room, rm) == 0) {
                        char msg[] = "Room already exists\n";
                        write(fds[i].fd, msg, strlen(msg));
                        room_exists = true;
                        break;
                    }
                }
                if (!room_exists) {
                    strcpy(clients[i].room, rm);
                    char msg[] = "Room created\n";
                    write(fds[i].fd, msg, strlen(msg));
                }
            }
        } 
        else if (strncmp(message, "enter", 5) == 0) {
            char *rm = strtok(message + 6, "\n");
            if (!rm) {
                char msg[] = "Invalid enter message\n";
                write(fds[i].fd, msg, strlen(msg));
                return;
            }

            bool room_exists = false;
            for (int j = 0; j < 1000; j++) {
                if (clients[j].fd != -1 && strcmp(clients[j].room, rm) == 0) { //if room exists, enter
                    char msg[] = "Entering room\n";
                    write(fds[i].fd, msg, strlen(msg));
                    room_exists = true;
                    break;
                }
            }
            if (!room_exists) {
                char msg[] = "Room does not exist\n";
                write(fds[i].fd, msg, strlen(msg));
                return;
            }
            
            if(strcmp(clients[i].room, "") != 0) {
                char msg[MSG_SIZE];
                snprintf(msg, sizeof(msg), "*: User %s leaving %s\n", clients[i].name, clients[i].room);
                char temp_room[50];
                strncpy(temp_room, clients[i].room, sizeof(temp_room));
                temp_room[sizeof(temp_room) - 1] = '\0';

                for(int j = 0; j < 1000; j++) {
                    if(j != i && clients[j].fd != -1 && strcmp(clients[j].room, temp_room) == 0) {
                        write(clients[j].fd, msg, strlen(msg));
                    }
                }
                memset(clients[i].room, 0, sizeof(clients[i].room));
            }
            strncpy(clients[i].room, rm, sizeof(clients[i].room));
            clients[i].room[sizeof(clients[i].room) - 1] = '\0';

            char msg[MSG_SIZE];
            snprintf(msg, sizeof(msg), "*: User %s entering %s\n", clients[i].name, clients[i].room);
            for(int j = 0; j < 1000; j++) {
                if(j != i && clients[j].fd != -1 && strcmp(clients[j].room, clients[i].room) == 0) {
                    write(clients[j].fd, msg, strlen(msg));
                }
            }
            char msg2[MSG_SIZE];
            snprintf(msg2, sizeof(msg2), "You are in room %s\n", clients[i].room);
            write(fds[i].fd, msg2, strlen(msg2));
        } 
        else if (strncmp(message, "who", 3) == 0) {
            for (int j = 0; j < 1000; j++) {
                if (clients[j].fd != -1 && strcmp(clients[j].room, "") != 0) {
                    char msg[MSG_SIZE] = "";
                    snprintf(msg, sizeof(msg), "%s in room %s\n", clients[j].name, clients[j].room);
                    write(fds[i].fd, msg, strlen(msg));
                }
            }
        } 
        else if (strncmp(message, "logout", 6) == 0) {
            if(strcmp(clients[i].room, "") != 0) {
                char msg[MSG_SIZE];
                snprintf(msg, sizeof(msg), "User %s has left room %s\n", clients[i].name, clients[i].room);
                for(int j = 0; j < 1000; j++) {
                    if(j != i && clients[j].fd != -1 && strcmp(clients[j].room, clients[i].room) == 0) {
                        write(clients[j].fd, msg, strlen(msg));
                    }
                }
                memset(clients[i].room, 0, sizeof(clients[i].room));
            }

            char msg[] = "You are logged out\n";
            write(fds[i].fd, msg, strlen(msg));
            memset(clients[i].name, 0, sizeof(clients[i].name));
            clients[i].logged_in = false;
        }
        else if(strncmp(message, "broadcast", 9) == 0) {
            char *msg = strtok(message + 10, "\n");
            if(!msg || strlen(msg) == 0) {
                char err_msg[] = "Invalid broadcast message\n";
                write(fds[i].fd,err_msg, strlen(err_msg));
                return;
            }

            char time[50] = {0};
            time_stamp(time, sizeof(time)); 
            
            char bc_message[MSG_SIZE];
            snprintf(bc_message, sizeof(bc_message), "* [%s]: %s\n", time, msg);
            
            for(int j = 0; j < 1000; j++) {
                if(clients[j].fd != -1 && clients[j].logged_in) {
                    write(clients[j].fd, bc_message, strlen(bc_message));
                }
            }
        }
        else {
            // chat messages
            char sender_nm[50] = "";
            char sender_rm[50] = "";

            strncpy(sender_nm, clients[i].name, sizeof(sender_nm)-1);
            sender_nm[sizeof(sender_nm) - 1] = '\0';
            strncpy(sender_rm, clients[i].room, sizeof(sender_rm)-1);
            sender_rm[sizeof(sender_rm) - 1] = '\0';

            if (strcmp(sender_rm, "") == 0) {
                char msg[] = "Not in a room.\n";
                write(fds[i].fd, msg, strlen(msg));
            } 
            else {
                char time[50] = {0};
                time_stamp(time, sizeof(time));
                char msg[MSG_SIZE] = {0};
                snprintf(msg, sizeof(msg), "%s [%s], sending message to room %s\n", sender_nm, time, sender_rm);
                write(fds[i].fd, msg, strlen(msg));
                char bc_message[MSG_SIZE] = "";
                snprintf(bc_message, sizeof(bc_message), "%s [%s]: %s", sender_nm, time, message);
                for (int j = 0; j < 1000; j++) {
                    if (j != i && clients[j].fd != -1 && strcmp(clients[j].room, sender_rm) == 0 && clients[j].fd != fds[i].fd) {
                        write(clients[j].fd, bc_message, strlen(bc_message));
                    }
                }
            }
        }
    } 
    else {
        printf("Client %s disconnected\n", clients[i].name);
        
        if(strcmp(clients[i].room, "") != 0) {
            char msg[MSG_SIZE];
            snprintf(msg, sizeof(msg), "User %s has left room %s\n", clients[i].name, clients[i].room);
            char temp_room[50];
            strncpy(temp_room, clients[i].room, sizeof(temp_room));
            
            for(int j = 0; j < 1000; j++) {
                if(j != i && clients[j].fd != -1 && strcmp(clients[j].room, temp_room) == 0) {
                    write(clients[j].fd, msg, strlen(msg));
                }
            }
        }

        close(fds[i].fd); // close socket
        fds[i].fd = -1; // reset socket
        clients[i].fd = -1; // reset client
        memset(clients[i].name, 0, sizeof(clients[i].name));
        memset(clients[i].room, 0, sizeof(clients[i].room));
        clients[i].logged_in = false;
    }
}

// keeps server running, checks events
void server_loop(int serv_fd, struct pollfd *fds, Client *clients) {
    int ret = poll(fds, 1000, -1);
    if(ret < 0) {
        printf("Poll failed\n");
        exit(1);
    }

    if(fds[0].revents & POLLIN) {
        handle_conns(serv_fd, fds, clients);
    }

    for(int i = 1; i < 1000; i++) {
        if(fds[i].revents & POLLIN) {
            handle_msgs(i, fds, clients);
        }
    }
}

// starts server or client based on args
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Either a.out server or a.out client\n");
        exit(1);
    }

    if(strcmp(argv[1], "server") == 0) {
        printf("Starting server...\n");
     
        struct sigaction signal;
        signal.sa_handler = shut_down;
        signal.sa_flags = 0;
        sigemptyset(&signal.sa_mask);
        int s = sigaction(SIGINT, &signal, NULL);

        if(s == -1) {
            printf("Sigaction failed\n");
            exit(1);
        }
    
        int serv_fd = mk_server();

        for(int i = 0; i < 1000; i++) {
            fds[i].fd = -1; // all fds unused
        }

        fds[0].fd = serv_fd;
        fds[0].events = POLLIN;

        while(1) {
            server_loop(serv_fd, fds, clients);
        }
    }
    else if(strcmp(argv[1], "client") == 0) {
        printf("Starting Client...\n");
        int sockfd = conn_to_server();
        client_poll(sockfd);
    }
    else {
        printf("Invalid, 'server' or 'client'.\n");
        exit(1);
    }    
    return 0;
}
