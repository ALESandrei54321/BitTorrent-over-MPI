#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_CLIENTS 10


typedef struct {
    char filename[MAX_FILENAME];
    int num_chunks;
    char chunks[MAX_CHUNKS][HASH_SIZE + 1];
} FileSegmentInfo;

typedef struct {
    char client_id;
    int is_seed;
    int has_segment[MAX_CHUNKS];
} ClientSegmentInfo;

typedef struct {
    FileSegmentInfo file;
    int num_clients;
    ClientSegmentInfo client_info[MAX_CLIENTS];
} Swarm;

typedef struct {
    int rank;
    int num_wanted_files;
    int num_owned_files;
    FileSegmentInfo *wanted_files;
    FileSegmentInfo *owned_files;
} ThreadInfo;


int randomPeer(Swarm *swarm_info, int segment) {
    int num_clients_with_segment = 0;
    int clients_with_segment[MAX_CLIENTS];

    // Find clients that have the desired segment
    for (int i = 0; i < swarm_info->num_clients; i++) {
        if (swarm_info->client_info[i].has_segment[segment]) {
            clients_with_segment[num_clients_with_segment] = swarm_info->client_info[i].client_id;
            num_clients_with_segment++;
        }
    }

    // If no client has the segment, return -1
    if (num_clients_with_segment == 0) {
        return -1;
    }

    // Randomly select a client from those that have the segment
    int random_index = rand() % num_clients_with_segment;
    return clients_with_segment[random_index];
}


void *download_thread_func(void *arg) {
    MPI_Status status;
    ThreadInfo *thread_info = (ThreadInfo *)arg;
    int rank = thread_info->rank;

    FileSegmentInfo *wanted_files = thread_info->wanted_files;
    FileSegmentInfo *owned_files = thread_info->owned_files;

    int total_downloaded_segments = 0;

    for (int i = 0; i < thread_info->num_wanted_files; i++) {
        
        // Send "REQUEST" message to the tracker
        MPI_Send("REQUEST", 7, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // Send the filename to the tracker
        MPI_Send(wanted_files[i].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        Swarm swarm_info;
        MPI_Recv(&swarm_info, sizeof(Swarm), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &status);

        int new_file_idx = thread_info->num_owned_files;
        thread_info->num_owned_files++;
        strncpy(owned_files[new_file_idx].filename, wanted_files[i].filename, FILENAME_MAX);
        
        owned_files[new_file_idx].num_chunks = swarm_info.file.num_chunks;


        ClientSegmentInfo new_client_info;
        int needed_segments = swarm_info.file.num_chunks;
        int collected_segments = 0;


        for (int j = 0; j < needed_segments; j++) {
            new_client_info.has_segment[j] = 0;
        }
        new_client_info.is_seed = 0;
            

        while (collected_segments < needed_segments) {

            int peer_rank = randomPeer(&swarm_info, collected_segments);
            if (peer_rank < 0) {
                printf("No viable peer found\n");
            }

            char send_buffer[256];
            sprintf(send_buffer, "%s %s %d", "REQUEST", wanted_files[i].filename, collected_segments);
            MPI_Send(send_buffer, sizeof(send_buffer), MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD);

            char received_chunk[HASH_SIZE + 1];
            MPI_Recv(received_chunk, HASH_SIZE, MPI_CHAR, peer_rank, 0, MPI_COMM_WORLD, &status);
            received_chunk[HASH_SIZE] = '\0';

            //Add hash to the new file chunks
            strncpy(owned_files[new_file_idx].chunks[collected_segments], received_chunk, HASH_SIZE);
            new_client_info.has_segment[collected_segments] = 1;

            MPI_Send("ACK", 4, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD);

            collected_segments++;
            total_downloaded_segments++;
            
            if (total_downloaded_segments % 10 == 0) {
                // Create a message to inform the tracker about the owned segments
                MPI_Send("UPDATE", 7, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

                // Send the filename to the tracker
                MPI_Send(wanted_files[i].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

                // Send the client's segment information to the tracker
                MPI_Send(&new_client_info, sizeof(ClientSegmentInfo), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);              

                MPI_Recv(&swarm_info, sizeof(Swarm), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &status);

            }

        }

        MPI_Send("UPDATE", 7, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // Send the filename to the tracker
        MPI_Send(wanted_files[i].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // Send the client's segment information to the tracker
        MPI_Send(&new_client_info, sizeof(ClientSegmentInfo), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        MPI_Recv(&swarm_info, sizeof(Swarm), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &status);

        // Save the chunks to a file
        char output_filename[30];
        sprintf(output_filename, "client%d_%s", rank, wanted_files[i].filename);

        FILE *output_file = fopen(output_filename, "w");

        if (output_file == NULL) {
            printf("Error opening output file %s\n", output_filename);
            exit(EXIT_FAILURE);
        }

        for (int j = 0; j < owned_files[new_file_idx].num_chunks; j++) {
            fprintf(output_file, "%s\n", owned_files[new_file_idx].chunks[j]);
        }

        fclose(output_file);

        printf("%d: Chunks saved to file %s\n", thread_info->rank, output_filename);

        MPI_Send("FIN_FILE", 12, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(owned_files[new_file_idx].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);


    }

    MPI_Send("FINISH", 7, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

    return NULL;
}


void *upload_thread_func(void *arg) {
    ThreadInfo *thread_info = (ThreadInfo *)arg;
    int rank = thread_info->rank;

    FileSegmentInfo *owned_files = thread_info->owned_files;

    while (1) {
        MPI_Status status;
        char received_buffer[256];
        memset(received_buffer, 0, sizeof(received_buffer));
        MPI_Recv(received_buffer, sizeof(received_buffer), MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        int sender_rank = status.MPI_SOURCE;

        char request_message[10];
        char filename[MAX_FILENAME];
        int requested_segment;

        sscanf(received_buffer, "%s %s %d", request_message, filename, &requested_segment);
        
        if (strncmp(request_message, "REQUEST", 7) == 0) {

            int file_index = -1;
            for (int j = 0; j < thread_info->num_owned_files; j++) {
                if (strcmp(owned_files[j].filename, filename) == 0) {
                    file_index = j;
                    break;
                }
            }
            if (file_index == -1) {
                //printf("-=-------------------no file ----------------------");
            }
            //printf("%d_upload: %s to send to %d\n", rank ,owned_files[file_index].chunks[requested_segment], sender_rank);
            MPI_Send(owned_files[file_index].chunks[requested_segment], HASH_SIZE, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);

            char ack_message[4];
            MPI_Recv(ack_message, sizeof(ack_message), MPI_CHAR, sender_rank, 1, MPI_COMM_WORLD, &status);
            if (strncmp(ack_message, "ACK", 3) == 0) {
                //printf("%d: Confirmed.\n", rank);
            }
        } else if (strncmp(received_buffer, "CLOSE", 5) == 0) {
            // Handle finish message and exit the upload thread
            printf("%d: CLOSING UPLOAD THREAD\n", rank);
            return NULL;
        }

    }
    return NULL;
}


void tracker(int numtasks, int rank) {
    MPI_Status status;
    int finished = numtasks - 1;
    int num_files = 0;

    // Array to store swarm information for each file
    Swarm swarm[MAX_FILES];

    while (1) {
        char filename[MAX_FILENAME];
        int num_segments;
        char hash[HASH_SIZE + 1];

        // Receive initialization message from clients with a specific tag
        MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        int sender_rank = status.MPI_SOURCE;

        // Check for the end of initialization
        if (strlen(filename) == 0) {
            MPI_Send("ACK\n", 4, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);

            finished--;
            if (finished <= 0) {
                // Inform peers to start processing

                for (int i = 0; i < 3; i++) {
                    printf("%s\n", swarm[i].file.filename);
                    for (int j = 0; j < swarm[i].num_clients; j++) {
                        printf("%d\n", swarm[i].client_info[j].client_id);
                        for (int k = 0; k < swarm[i].file.num_chunks; k++) {
                            printf("%d ", swarm[i].client_info[j].has_segment[k]);
                        }
                        printf("\n");
                    }
                    printf("\n\n\n");
                }

                //sleep(1);
                for (int i = 1; i < numtasks; i++)
                    MPI_Send("START\n", 6, MPI_CHAR, i, 0, MPI_COMM_WORLD);

                break;
            }
        } else {
            // Receive the number of segments and client ID with a specific tag
            MPI_Recv(&num_segments, 1, MPI_INT, sender_rank, 1, MPI_COMM_WORLD, &status);

            // Check if the file already exists in the swarm
            int file_index = -1;
            for (int i = 0; i < num_files; ++i) {
                if (strcmp(swarm[i].file.filename, filename) == 0) {
                    file_index = i;
                    break;
                }
            }

            if (file_index == -1) {
                // File doesn't exist, create a new file in the swarm
                strcpy(swarm[num_files].file.filename, filename);
                swarm[num_files].file.num_chunks = num_segments;
                swarm[num_files].num_clients = 1;
                swarm[num_files].client_info[0].client_id = sender_rank;
                swarm[num_files].client_info[0].is_seed = 1;
                for (int i = 0; i < num_segments; i++) {
                    // Receive hash with a specific tag
                    MPI_Recv(hash, HASH_SIZE, MPI_CHAR, sender_rank, 2, MPI_COMM_WORLD, &status);
                    hash[HASH_SIZE] = '\0';

                    // Update client's segment information
                    swarm[num_files].client_info[0].has_segment[i] = 1;

                    // Copy hash to the swarm's file information
                    strncpy(swarm[num_files].file.chunks[i], hash, HASH_SIZE);
                }

                num_files++;
            } else {
                // File already exists, find client index or add new client
                int client_index = -1;
                for (int i = 0; i < swarm[file_index].num_clients; ++i) {
                    if (swarm[file_index].client_info[i].client_id == sender_rank) {
                        client_index = i;
                        break;
                    }
                }

                if (client_index == -1) {
                    // New client for the existing file
                    int aux = swarm[file_index].num_clients;
                    swarm[file_index].client_info[aux].client_id = sender_rank;
                    swarm[file_index].client_info[aux].is_seed = 1;
                    for (int i = 0; i < num_segments; i++) {
                        // Update client's segment information
                        swarm[file_index].client_info[aux].has_segment[i] = 1;
                    }
                    

                    swarm[file_index].num_clients++;
                }
            }
        }
    }

    finished = numtasks - 1;

    // Handle messages from clients
    while (1) {
        char message[MAX_FILENAME];
        MPI_Recv(message, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        int sender_rank = status.MPI_SOURCE;

        // Check the type of message
        if (strncmp(message, "REQUEST", 7) == 0) {
            // Handle client request for file information
            // Send the swarm information back to the client

            MPI_Recv(message, MAX_FILENAME, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);

            int file_index = -1;
            for (int i = 0; i < MAX_FILES; ++i) {
                if (strcmp(swarm[i].file.filename, message) == 0) {
                    file_index = i;
                    break;
                }
            }
            
            if (file_index != -1) {
                MPI_Send(&swarm[file_index], sizeof(Swarm), MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);
            } else {
                printf("No such file: %s, received from %d\n", message, sender_rank);
            }

        } else if (strncmp(message, "UPDATE", 6) == 0) {
            // Handle client update on file segments
            // Update swarm information and send ACK back to the client
            char update_filename[MAX_FILENAME];
            MPI_Recv(update_filename, MAX_FILENAME, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);

            int update_file_index = -1;
            for (int i = 0; i < num_files; ++i) {
                if (strcmp(swarm[i].file.filename, update_filename) == 0) {
                    update_file_index = i;
                    break;
                }
            }

            if (update_file_index != -1) {
                // Receive the client's segment information for the update
                ClientSegmentInfo update_client_info;
                MPI_Recv(&update_client_info, sizeof(ClientSegmentInfo), MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);

                // Update the swarm with the client's new segment information
                int update_client_index = -1;
                for (int i = 0; i < swarm[update_file_index].num_clients; ++i) {
                    if (swarm[update_file_index].client_info[i].client_id == sender_rank) {
                        update_client_index = i;
                        break;
                    }
                }

                if (update_client_index == -1) {
                    int cl_idx = swarm[update_file_index].num_clients;
                    swarm[update_file_index].client_info[cl_idx].client_id = sender_rank;
                    for(int j = 0; j < swarm[update_file_index].file.num_chunks; j++) {
                        swarm[update_file_index].client_info[cl_idx].has_segment[j] = update_client_info.has_segment[j];
                    }
                    swarm[update_file_index].num_clients++;
                } else {
                    for(int j = 0; j < swarm[update_file_index].file.num_chunks; j++) {
                        swarm[update_file_index].client_info[update_client_index].has_segment[j] = update_client_info.has_segment[j];
                    }
                }

                MPI_Send(&swarm[update_file_index], sizeof(Swarm), MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);

            }



        } else if (strncmp(message, "FIN_FILE", 8) == 0) {
            // Handle client finish download message
            char finished_filename[MAX_FILENAME];
            MPI_Recv(finished_filename, MAX_FILENAME, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);

            // Find the file in the swarm
            int finished_file_index = -1;
            for (int i = 0; i < num_files; ++i) {
                if (strcmp(swarm[i].file.filename, finished_filename) == 0) {
                    finished_file_index = i;
                    break;
                }
            }

            // Update is_seed for the client who finished downloading
            int finished_client_index = -1;
            for (int i = 0; i < swarm[finished_file_index].num_clients; ++i) {
                if (swarm[finished_file_index].client_info[i].client_id == sender_rank) {
                    finished_client_index = i;
                    break;
                }
            }

            if (finished_client_index != -1) {
                swarm[finished_file_index].client_info[finished_client_index].is_seed = 1;
                printf("%d: Client %d finished downloading %s\n", rank, sender_rank, finished_filename);
            }

        } else if (strncmp(message, "FINISH", 6) == 0) {
          // Handle client finish all downloads
        
            finished--;
            if (finished <= 0) {
                for (int i = 1; i < numtasks; i++) {
                    MPI_Send("CLOSE", 6, MPI_CHAR, i, 1, MPI_COMM_WORLD);
                }
            }

        }
        if (finished <=0) {
            printf("STOPPING TRACKER\n");
            break;
        }
            
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    char filename[20];
    sprintf(filename, "in%d.txt", rank);

    char line[100];
    FILE *file = fopen(filename, "r");

    if (file == NULL) {
        printf("Error opening input file");
        exit(EXIT_FAILURE);
    }

    // Read the number of files owned by the client
    fgets(line, sizeof(line), file);
    int num_owned_files;
    sscanf(line, "%d", &num_owned_files);

    FileSegmentInfo *owned_files = malloc(num_owned_files * sizeof(FileSegmentInfo));

    for (int i = 0; i < num_owned_files; ++i) {
        char file_name[MAX_FILENAME];
        int num_segments;
        fgets(line, sizeof(line), file);

        sscanf(line, "%s %d", file_name, &num_segments);

        strcpy(owned_files[i].filename, file_name);
        owned_files[i].num_chunks = num_segments;

        // Send the file name and number of segments to the tracker with specific tags
        MPI_Send(file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&num_segments, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD);

        // Send the hash of each segment to the tracker with specific tags
        for (int j = 0; j < num_segments; ++j) {
            fgets(line, sizeof(line), file);
            strncpy(owned_files[i].chunks[j], line, HASH_SIZE);
            MPI_Send(&line, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);
        }
    }

    // Signal end of file registration with specific tag
    MPI_Send("", 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

    // Wait for the tracker
    MPI_Recv(line, 4, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, NULL);
    if (strncmp(line, "ACK", 3) == 0) {
        printf("%d: Finished init.\n", rank);
    }

    MPI_Recv(line, 6, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, NULL);
    if (strncmp(line, "START", 5) == 0) {
        printf("%d: STARTING DOWNLOAD\n", rank);
    }

    // Read the number of files to download
    fgets(line, sizeof(line), file);
    int num_files_to_download;
    sscanf(line, "%d", &num_files_to_download);

    // Allocate memory for wanted_files array
    FileSegmentInfo *wanted_files = malloc(num_files_to_download * sizeof(FileSegmentInfo));
    owned_files = realloc(owned_files, (num_files_to_download + num_owned_files + 1) * sizeof(FileSegmentInfo));

    // Process each file to download
    for (int i = 0; i < num_files_to_download; ++i) {
        char file_name[MAX_FILENAME];
        fgets(line, sizeof(line), file);
        sscanf(line, "%s", file_name);

        // Copy file name
        strncpy(wanted_files[i].filename, file_name, MAX_FILENAME);
        wanted_files[i].filename[MAX_FILENAME] = '\0';
    }

    fclose(file);

    // Create thread info structure
    ThreadInfo thread_info;
    thread_info.rank = rank;
    thread_info.num_wanted_files = num_files_to_download;
    thread_info.wanted_files = wanted_files;
    thread_info.num_owned_files = num_owned_files;
    thread_info.owned_files = owned_files;

    // Pass thread_info to download and upload threads
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&thread_info);
    if (r) {
        printf("Error creating download thread\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&thread_info);
    if (r) {
        printf("Error creating upload thread\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Error joining download thread\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Error joining upload thread\n");
        exit(-1);
    }

    // Free allocated memory
    free(wanted_files);
    free(owned_files);
    
}

int main(int argc, char *argv[]) {
    int numtasks, rank;
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        printf("MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
