/* Swarthmore College, CS 43, Lab 4
 * Copyright (c) 2019 Swarthmore College Computer Science Department,
 * Swarthmore PA
 * Professor Vasanta Chaganti */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG (10)
#define MAX_CLIENTS (1024)
#define NUM_SONGS (13)

/* Store per-client and per-song state here.
 * You may use globals for these. */

/* Per-client struct. */
typedef struct {
    int connected;
    char* song_data;
    char* list_data;
    char* info_data;
    int data_offset;
    int info_offset;
    int list_offset;
    int is_music_playing;
    int song_id;
    int needs_data;
    char client_request[1024];
    int num_processed;
    int num_received;
} client;

client clients[MAX_CLIENTS];

/* : Store song data / info in a struct here too, if you want. You may also
 * want to build the list string once and send it back to any client that asks
 * for it. */

 typedef struct {
   char* song_names[NUM_SONGS];
   char* song_info[NUM_SONGS];
   char* song_data[NUM_SONGS];
   int song_length[NUM_SONGS];
 } song_list;

 song_list global_songdata;


/* Use fcntl (file control) to set the given socket to non-blocking mode.
 * Setting your sockets to non-blocking mode is not required, but it might help
 * with your debugging.  By setting each socket you get from accept() to
 * non-blocking, you can be sure that normally blocking calls like send, recv,
 * and accept will instead return an error condition and set errno to
 * EWOULDBLOCK/EAGAIN.  I would recommend that you set your sockets for
 * non-blocking and then explicitly check each call to send, recv, and accept
 * for this errno.  If you see it happening, you know that you're attempting to
 * call one of those functions when you shouldn't be. */
void set_non_blocking(int sock) {
    /* Get the current flags. We want to add O_NONBLOCK to this set. */
    int socket_flags = fcntl(sock, F_GETFD);
    if (socket_flags < 0) {
        perror("fcntl");
        exit(1);
    }

    /* Add in the O_NONBLOCK flag by bitwise ORing it to the old flags. */
    socket_flags = socket_flags | O_NONBLOCK;

    /* Set the new flags, including O_NONBLOCK. */
    int result = fcntl(sock, F_SETFD, socket_flags);
    if (result < 0) {
        perror("fcntl");
        exit(1);
    }

    /* The socket is now in non-blocking mode. */
}

/* Returns true (non-zero) for files whose names end in '.mp3'.
 * This is used below in read_mp3_files.  You probably don't need to use
 * it anywhere else. */
int filter(const struct dirent *ent) {
    int len = strlen(ent->d_name);

    return !strncasecmp(ent->d_name + len - 4, ".mp3", 4);
}

int play(int client_num){

  client playing_client = clients[client_num];
  printf("OLD OFFSET: %d\n", playing_client.data_offset);
  int song_num = playing_client.song_id;
  printf("Song_num: %d\n",song_num);
  char* playing_song_stream = playing_client.song_data;
  /* Make sure the song we got from the client is in range. */
  if(song_num < 0 || song_num > 12){
    printf("Bad song number!\n");
    clients[client_num].needs_data = 0;
    return -1;

  }

  char* song_to_play = global_songdata.song_data[song_num];


  //memset(playing_client.song_data,0,sizeof(clients[client_num].song_data));

  short type = 0;
  memcpy(playing_song_stream, &type, 2);
  //long song_length = strlen(song_to_play);
  long song_length = global_songdata.song_length[song_num];
  printf("Song length: %ld\n",song_length);
  memcpy(playing_song_stream+2, &song_length, 8);

  int bytes_sent;
  /* Gotta redo this, and pack a struct to send instead. Maybe add the flag as well.
     I'm thinking we should follow Vasanta's piazza format - alternating
     4k chunks of song data (if playing) and text, with a header Being
     sent at the beginning of each request. */
  //clients[client_num].song_data += *global_songdata.song_data[song_num];

  //To the position after the header, copies 2048 bytes of the song to play beginning at data offset.
  memcpy(playing_song_stream+10, song_to_play+playing_client.data_offset, 2048);
  bytes_sent = send(client_num,playing_song_stream,2048,MSG_NOSIGNAL);
  printf("We just sent: %d\n",bytes_sent);
  clients[client_num].data_offset+= bytes_sent;
  printf("NEW OFFSET: %d\n", clients[client_num].data_offset);

  playing_client.is_music_playing = 1;
  if (bytes_sent == 0){
    playing_client.needs_data = 0;
  }

  if(clients[client_num].data_offset +2048 > song_length){
    printf("We've (just about) reached the end of the song\n");
    clients[client_num].needs_data = 0;
  }
  return 0;
}

int send_info(int sockfd){
  int retval;
  int song_number = clients[sockfd].song_id;

  short type = 1;
  memcpy(clients[sockfd].info_data, &type, 2);
  long info_length = strlen(global_songdata.song_info[song_number]);
  memcpy(clients[sockfd].info_data+2, &info_length, 4);
  memcpy(clients[sockfd].info_data+6, global_songdata.song_info[song_number], info_length);

  retval = send(sockfd, clients[sockfd].info_data+clients[sockfd].info_offset, 2048, MSG_NOSIGNAL);
  if (retval<0){
    perror("send() failed");
    exit(1);
  }
  clients[sockfd].list_offset += retval;

  if (retval == 0){
    clients[sockfd].needs_data = 0;
  }

  return retval;
}

int send_list(int sockfd){

  char response[2048];

  for (int i=0; i < NUM_SONGS; i++){
    snprintf(response, sizeof(response), "(%d) : ", i);
    strcat(response, global_songdata.song_names[i]);
  }

  short type = 2;
  memcpy(clients[sockfd].list_data, &type, 2);
  long list_length = strlen(*global_songdata.song_names);
  memcpy(clients[sockfd].list_data+2, &list_length, 4);
  memcpy(clients[sockfd].list_data+6, response, strlen(response));

  int retval = send(sockfd, clients[sockfd].list_data+clients[sockfd].list_offset, 2048, MSG_NOSIGNAL);
  if (retval<0){
    perror("send() failed");
    exit(1);
  }
  clients[sockfd].list_offset += retval;

  if (retval == 0){
    clients[sockfd].needs_data = 0;
  }

  return retval;
}

void send_data(int sockfd){
  char *request = clients[sockfd].client_request;
  int retval;

  if (request[0] == 2){
    retval = send_list(sockfd);
  }
  else if (request[0] == 1){
    retval = send_info(sockfd);
    if (retval<0){
      printf("send_info() error.\n");
    }
  }
  else if (request[0] == 0){
    retval = play(sockfd);
    if (retval<0){
      printf("Please try again.\n");
    }
  }
  else{
    printf("Unknown request type from %d: %s\n",sockfd, request);
  }

  return;
}


/* Given a path to a directory, this function scans that directory (using the
 * handy scandir library function) to produce an alphabetized list of files
 * whose names end in ".mp3".  For each one, it then also checks for a
 * corresponding ".info" file and reads that in its entirety. */
int read_mp3_files(char *dir) {
    struct dirent **namelist;
    int i,n;

    n = scandir(dir, &namelist, filter, alphasort);
    if (n < 0) {
        perror("scandir");
        exit(1);
    }

    for (i = 0; i < n; ++i) {
        int bytes_read = 0;
        int total_read = 0;
        char path[1024];
        struct stat fileinfo;

        FILE *infofile = NULL;

        FILE *song = NULL;

        char *infostring = NULL;

        /* namelist[i]->d_name now contains the name of an mp3 file. */
        //printf("(%d) %s\n", i, namelist[i]->d_name);

        /* Build a path to a possible input file. */
        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, namelist[i]->d_name);

        if (stat(path, &fileinfo)) {
            perror("stat");
            exit(1);
        }

        //printf("  --file's size is %ld bytes.\n", (long) fileinfo.st_size);

        /*

         * It's fine for you to read the entire file here and keep the contents
         * in the server's memory. */


        /* Using fopen to get the song data and read into the global struct. */
        char* data_string = NULL;
        song = fopen(path,"r");
        if (song == NULL) {
          data_string = "No information available";
        }

        else{
          int song_data_size = 1024;
          data_string = malloc(song_data_size);

          do {
              song_data_size *= 2;
              data_string = realloc(data_string, song_data_size);

              bytes_read = fread(data_string + total_read, 1, song_data_size - total_read, song);
              total_read += bytes_read;
          } while (bytes_read > 0);

        }
        global_songdata.song_length[i] = total_read;
        /* Reset total_read and bytes_read for the next loop. */
        total_read = 0;
        bytes_read = 0;

        /* Build a path to the info file by appending ".info" to the path. */
        strcat(path, ".info");

        infofile = fopen(path, "r");
        if (infofile == NULL) {
            /* It wasn't there (or failed to open for some other reason). */
            infostring = "No information available.";
        } else {
            /* We found and opened the info file. */
            int infosize = 1024;
            infostring = malloc(infosize);

            do {
                infosize *= 2;
                infostring = realloc(infostring, infosize);

                bytes_read = fread(infostring + total_read, 1, infosize - total_read, infofile);
                total_read += bytes_read;
            } while (bytes_read > 0);


            fclose(infofile);

            /* Zero-out the unused space at the end of the buffer. */
            memset(infostring + total_read, 0, infosize - total_read);
        }

        /* infostring now contains the info data for this song. */

        /* TODO: Use these info strings when clients send info commands.
         * You can store them along with the song data if you want. */


          global_songdata.song_info[i] = infostring;
          global_songdata.song_names[i] = namelist[i]->d_name;
          global_songdata.song_data[i] = data_string;






        //printf("Info:%s\n\n", infostring);

        free(namelist[i]);
    }
    free(namelist);

    /* Return the number of files we found. */
    return n;
}

int parse_buffer(int sockfd){

  short type = clients[sockfd].client_request[0];
  printf("Type: %d\n",type);
  if(type == 0){
    int song_id = clients[sockfd].client_request[2];
    printf("Song id: %d\n",song_id);
    clients[sockfd].client_request[2] = song_id;
    clients[sockfd].song_id = song_id;
    clients[sockfd].data_offset = 0;
    printf("ZEROED OFFSET IN parse_buffer\n");
    }
  else if(type == 1){
    int song_id = clients[sockfd].client_request[2];
    printf("Song id: %d\n",song_id);
    clients[sockfd].client_request[2] = song_id;
    clients[sockfd].song_id = song_id;
    clients[sockfd].info_offset = 0;
  }
    clients[sockfd].client_request[0] = type;
    return type;
}

int main(int argc, char **argv) {
    int serv_sock;
    int i;
    int val = 1;
    int song_count;
    uint16_t port;
    struct sockaddr_in addr;
    fd_set rfds, wfds;

    if (argc < 3) {
        printf("Usage:\n%s <port> <music directory>\n", argv[0]);
        exit(0);
    }

    /* Get the port number from the arguments. */
    port = (uint16_t) atoi(argv[1]);

    /* Create the socket that we'll listen on. */
    serv_sock = socket(AF_INET, SOCK_STREAM, 0);

    /* Set SO_REUSEADDR so that we don't waste time in TIME_WAIT. */
    val = setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &val,
            sizeof(val));
    if (val < 0) {
        perror("Setting socket option failed");
        exit(1);
    }

    /* Set our server socket to non-blocking mode.  This way, if we
     * accidentally accept() when we shouldn't have, we won't block
     * indefinitely. */
    set_non_blocking(serv_sock);

    /* Read the other argument (mp3 directory).  See the notes for this
     * function above. */
    song_count = read_mp3_files(argv[2]);
    printf("Found %d songs.\n", song_count);
/*
    for(int i = 0; i < NUM_SONGS; i++){
      printf("Song %d name: %s\n",i,global_songdata.song_names[i]);
      printf("Song %d info:\n\n%s\n",i,global_songdata.song_info[i]);
    }
*/
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* Bind our socket and start listening for connections. */
    val = bind(serv_sock, (struct sockaddr*)&addr, sizeof(addr));
    if(val < 0) {
        perror("bind");
        exit(1);
    }

    val = listen(serv_sock, BACKLOG);
    if(val < 0) {
        perror("listen");
        exit(1);
    }

    while (1) {
        /* With select, we need to keep track of the largest descriptor value
         * we give it.  Since we're always going to be checking the server
         * socket for reading (to see if there are new connections to accept),
         * we'll start by assigning the max to that.  Later on, when we add
         * other descriptors to the set, we need to increase this if those are
         * numerically larger. */
        int max_fd = serv_sock;

        /* Clear out the file descriptor sets for this iteration. */
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        /* Always tell select that we're interested in checking the server
         * socket for reading.  Being told that this socket is ready for
         * reading means it's safe to accept() a connection. */
        FD_SET(serv_sock, &rfds);

        /* Check each client to see if 1) we want to see if it has data for
         * reading, and 2) to see if we want to write data to it.  If so, set
         * the clients socket descriptor in the appropriate descriptor set. */
        for (i = 0; i < MAX_CLIENTS; ++i) {
            /* If we're interested in recving from this client, set its socket
             * in the FD_SET for reading.  For now, we'll assume that we always
             * want to check a client for reading if that client is connected.
             * */

             if (clients[i].connected) {
                FD_SET(i, &rfds);
                if (i > max_fd)
                    max_fd = i;
             }

            /* Do something similar here for wfds, but only if we have data
             * to send the client. */

             if (clients[i].connected && clients[i].needs_data) {
                FD_SET(i, &wfds);
                if (i > max_fd)
                    max_fd = i;
             }

        }

        /* Increment the max descriptor we've seen by one.  The first argument
         * to select needs to be one larger than any of the descriptors in the
         * sets.  There's a note about this in the tips section of the lab
         * website. */
        max_fd += 1;

        /* Finally, we call select.  This will block until one of the sockets
         * we asked about is ready for recving/sending. */
        val = select(max_fd, &rfds, &wfds, NULL, NULL);

        if (val < 1) {
            perror("select");
            continue;
        }

        /* Check the FD_SETs after select returns.  These will tell us
         * which sockets are ready for recving/sending. */
        for (i = 0; i < max_fd; ++i) {
            if (FD_ISSET(i, &rfds)) {
                /* Socket descriptor i is ready for "reading".
                 * If this is the server socket, it means we can accept()
                 * on it.  Otherwise, it's a regular client, and we can
                 * recv() on it. */

                 if(i == serv_sock){
                   struct sockaddr_in remoteaddr;

                   unsigned int socklen = sizeof(remoteaddr);
                   int sock = accept(serv_sock,(struct sockaddr *) &remoteaddr, &socklen);
                   if(sock<0){
                     perror("Error accepting connection");
                     exit(1);
                   }
                   printf("New connection (%d)!\n", sock);
                   client new_client;
                   new_client.info_offset = 0;
                   new_client.data_offset = 0;
                   printf("ZEROED OFFSET AFTER NEW CONNECTION\n");
                   new_client.is_music_playing = 0;
                   new_client.needs_data = 0;
                   new_client.connected = 1;
                   clients[sock] = new_client;
                 }

                 else{
                   printf("We're about to recv\n");
                   /* Since we haven't written client yet, recv length is 256 by default, but we might go back and change it. */
                   int n = recv(i,clients[i].client_request,6,0);
                   if (n < 0){
                     perror("recv error");
                     exit(1);
                   }
                   printf("What we got: %s\n",clients[i].client_request);
                   printf("We're about to call parse_buffer\n");
                   int request_type = parse_buffer(i);
                   printf("We just called parse_buffer\n");
                   //Stop request: set needs_data to 0.
                   printf("Parse Buffer returned: %d\n",request_type);
                   if(request_type == 3){

                     clients[i].song_id = 13;
                     clients[i].is_music_playing = 0;
                     clients[i].needs_data = 0;
                     clients[i].data_offset = 0;
                     printf("ZEROED OFFSET AFTER STOP REQUEST\n");
                   }

                   else{
                     clients[i].needs_data = 1;
                     printf("We just set needs_data to 1\n");
                   }

                 }

                 /* I would recommend calling another function here rather
                  * than cluttering up main and this select loop. */
            }
            if (FD_ISSET(i, &wfds)) {
                /* Socket descriptor i is ready for sending. */
                send_data(i);
                memset(clients[i].client_request,0,sizeof(clients[i].client_request));
                 /* I would recommend calling another function here rather
                  * than cluttering up main and this select loop. */
            }
        }
    }
}
