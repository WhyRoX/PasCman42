#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "utils_v3.h"
#include "game.h"
#include "pascman.h"

// Define default values
#define DEFAULT_PORT 9999
#define DEFAULT_MAP "./resources/map.txt"
#define DEFAULT_SCENARIO_P1 "./scenarios/joueur1s2.txt"
#define DEFAULT_SCENARIO_P2 "./scenarios/joueur2s2.txt"
#define MAX_PROCESSES 10
#define MAX_LINE_LENGTH 256
#define PLAYER_DELAY 100000 // Microseconds between player commands

// Process tracking structure
typedef struct {
    pid_t pid;
    char* name;
} Process;

Process processes[MAX_PROCESSES]; // Allow for more processes
int process_count = 0;

// Global variables for pipes
int pipe_p1[2]; // Pipe for player 1
int pipe_p2[2]; // Pipe for player 2

// Forward declarations
void cleanup(void);
void signal_handler(int sig);
int file_exists(const char* filename);
void play_scenarios(const char* scenario_p1, const char* scenario_p2);
void send_command_to_client(int client_num, enum Direction command);

int main(int argc, char *argv[]) {
    // Set up signal handling to ensure cleanup on termination
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Default values
    char* map_path = DEFAULT_MAP;
    char* scenario_p1 = NULL;
    char* scenario_p2 = NULL;
    int port = DEFAULT_PORT;
    
    // Parse command line arguments
    // Format: ./pas_labo [port] [map_file] [player1_script] [player2_script]
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0) {
            fprintf(stderr, "Invalid port number: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }
    
    if (argc >= 3) {
        map_path = argv[2];
    }
    
    if (argc >= 4) {
        scenario_p1 = argv[3];
    }
    
    if (argc >= 5) {
        scenario_p2 = argv[4];
    }
    
    printf("Starting PasCman laboratory environment\n");
    
    // Check if map file exists
    if (!file_exists(map_path)) {
        fprintf(stderr, "Error: Map file '%s' not found\n", map_path);
        return EXIT_FAILURE;
    }
    
    printf("Using map: %s\n", map_path);
    printf("Using port: %d\n", port);
    
    // Check if scenario files exist
    if (scenario_p1 != NULL) {
        if (!file_exists(scenario_p1)) {
            fprintf(stderr, "Warning: Player 1 scenario file '%s' not found\n", scenario_p1);
            scenario_p1 = NULL;
        } else {
            printf("Using Player 1 scenario: %s\n", scenario_p1);
        }
    }
    
    if (scenario_p2 != NULL) {
        if (!file_exists(scenario_p2)) {
            fprintf(stderr, "Warning: Player 2 scenario file '%s' not found\n", scenario_p2);
            scenario_p2 = NULL;
        } else {
            printf("Using Player 2 scenario: %s\n", scenario_p2);
        }
    }
    
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    // Create pipes for command injection
    spipe(pipe_p1);
    spipe(pipe_p2);
    
    // Start the server
    pid_t server_pid = sfork();
    if (server_pid == 0) {
        // Child process - execute server
        // Close pipes as server doesn't need them
        sclose(pipe_p1[0]);
        sclose(pipe_p1[1]);
        sclose(pipe_p2[0]);
        sclose(pipe_p2[1]);
        
        printf("Starting server...\n");
        sexecl("./pas_server", "pas_server", port_str, map_path, NULL);
        perror("Failed to execute server");
        exit(EXIT_FAILURE);
    } else {
        // Parent process - record server process
        processes[process_count].pid = server_pid;
        processes[process_count].name = "server";
        process_count++;
    }
    
    // Wait a bit for server to initialize
    sleep(2);
    
    // Start first client
    pid_t client1_pid = sfork();
    if (client1_pid == 0) {
        // Child process - execute first client
        printf("Starting client 1...\n");
        
        // Close the write end of pipe 1, we don't need it in the client
        sclose(pipe_p1[1]);
        // Close both ends of pipe 2, client 1 doesn't need them
        sclose(pipe_p2[0]);
        sclose(pipe_p2[1]);
        
        // Duplicate the read end of pipe 1 to stdin
        sdup2(pipe_p1[0], STDIN_FILENO);
        sclose(pipe_p1[0]);
        
        sexecl("./pas_client", "pas_client", "127.0.0.1", port_str, "-test", NULL);
        perror("Failed to execute client 1");
        exit(EXIT_FAILURE);
    } else {
        // Parent process - record client process
        processes[process_count].pid = client1_pid;
        processes[process_count].name = "client1";
        process_count++;
        
        // Close the read end of pipe 1, we only need to write to it
        sclose(pipe_p1[0]);
    }
    
    // Wait a bit for first client to register
    sleep(1);
    
    // Start second client
    pid_t client2_pid = sfork();
    if (client2_pid == 0) {
        // Child process - execute second client
        printf("Starting client 2...\n");
        
        // Close the write end of pipe 2, we don't need it in the client
        sclose(pipe_p2[1]);
        // Close pipe 1 completely, client 2 doesn't need it
        sclose(pipe_p1[1]);
        
        // Duplicate the read end of pipe 2 to stdin
        sdup2(pipe_p2[0], STDIN_FILENO);
        sclose(pipe_p2[0]);
        
        sexecl("./pas_client", "pas_client", "127.0.0.1", port_str, "-test", NULL);
        perror("Failed to execute client 2");
        exit(EXIT_FAILURE);
    } else {
        // Parent process - record client process
        processes[process_count].pid = client2_pid;
        processes[process_count].name = "client2";
        process_count++;
        
        // Close the read end of pipe 2, we only need to write to it
        sclose(pipe_p2[0]);
    }
    
    // Give some time for the clients to connect properly
    sleep(2);
    
    // Check if all processes are still running
    int all_running = 1;
    for (int i = 0; i < process_count; i++) {
        if (kill(processes[i].pid, 0) != 0) {
            fprintf(stderr, "Process %s has terminated unexpectedly\n", processes[i].name);
            all_running = 0;
        }
    }
    
    if (!all_running) {
        fprintf(stderr, "Not all processes are running, cleaning up...\n");
        cleanup();
        return EXIT_FAILURE;
    }
    
    printf("PasCman laboratory environment is running\n");
    
    // Play the scenarios if provided
    if (scenario_p1 != NULL || scenario_p2 != NULL) {
        printf("Starting scenario playback\n");
        play_scenarios(scenario_p1, scenario_p2);
    } else {
        printf("No scenario files specified, waiting for manual input\n");
        printf("Press Ctrl+C to terminate all processes and exit\n");
        
        // Without scenarios, wait for user to press Ctrl+C
        // This will trigger the signal handler
        while(1) {
            sleep(1);
        }
    }
    
    // We only reach here if scenarios were played
    printf("Scenarios completed, terminating all processes\n");
    
    // Clean up any remaining processes
    cleanup();
    
    return EXIT_SUCCESS;
}

// Check if file exists
int file_exists(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    return 1;
}

// Structure to hold command data
typedef struct {
    int player;
    enum Direction direction;
    int delay_ms; // Delay before executing this command (milliseconds)
} Command;

// Function to read commands from a scenario file
int read_commands_from_file(const char* filename, Command** commands) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        return 0;
    }
    
    // Count the number of valid command lines
    int count = 0;
    char line[MAX_LINE_LENGTH];
    
    while (fgets(line, sizeof(line), file) != NULL) {
        // Skip empty lines and comments
        if (strlen(line) <= 1 || line[0] == '#' || line[0] == '/') {
            continue;
        }
        count++;
    }
    
    // Allocate memory for commands
    *commands = (Command*)malloc(count * sizeof(Command));
    if (*commands == NULL) {
        fclose(file);
        return 0;
    }
    
    // Reset file position to beginning
    rewind(file);
    
    // Read commands
    int i = 0;
    int line_number = 0;
    
    while (fgets(line, sizeof(line), file) != NULL) {
        line_number++;
        
        // Remove newline character if present
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        
        // Skip empty lines and comments
        if (len == 0 || line[0] == '#' || line[0] == '/') {
            continue;
        }
        
        // Parse line: format should be "COMMAND,DELAY_MS" or just symbol like ">", "<", "^", "v"
        char command_str[32];
        int delay = 0;
        
        // First try the COMMAND,DELAY format
        if (sscanf(line, "%[^,],%d", command_str, &delay) != 2) {
            // Try without delay
            if (sscanf(line, "%s", command_str) != 1) {
                fprintf(stderr, "Invalid format at line %d: %s\n", line_number, line);
                continue;
            }
            delay = 0; // Default to no delay
        }
        
        // Convert command string to direction enum
        enum Direction direction;
        
        // Check for symbols first (>, <, ^, v)
        if (strcmp(command_str, ">") == 0) {
            direction = RIGHT;
        } else if (strcmp(command_str, "<") == 0) {
            direction = LEFT;
        } else if (strcmp(command_str, "^") == 0) {
            direction = UP;
        } else if (strcmp(command_str, "v") == 0) {
            direction = DOWN;
        } 
        // Then check for text commands
        else if (strcasecmp(command_str, "UP") == 0) {
            direction = UP;
        } else if (strcasecmp(command_str, "DOWN") == 0) {
            direction = DOWN;
        } else if (strcasecmp(command_str, "LEFT") == 0) {
            direction = LEFT;
        } else if (strcasecmp(command_str, "RIGHT") == 0) {
            direction = RIGHT;
        } else {
            fprintf(stderr, "Unknown command at line %d: %s\n", line_number, command_str);
            continue;
        }
        
        // Store the command
        (*commands)[i].direction = direction;
        (*commands)[i].delay_ms = delay;
        i++;
    }
    
    fclose(file);
    return i; // Return the actual number of commands read
}

// Play scenarios for both players simultaneously
void play_scenarios(const char* scenario_p1, const char* scenario_p2) {
    Command* commands_p1 = NULL;
    Command* commands_p2 = NULL;
    int count_p1 = 0;
    int count_p2 = 0;
    
    // Read commands for player 1
    if (scenario_p1 != NULL) {
        count_p1 = read_commands_from_file(scenario_p1, &commands_p1);
        printf("Read %d commands for Player 1\n", count_p1);
    }
    
    // Read commands for player 2
    if (scenario_p2 != NULL) {
        count_p2 = read_commands_from_file(scenario_p2, &commands_p2);
        printf("Read %d commands for Player 2\n", count_p2);
    }
    
    // Execute commands from both scenarios in chronological order
    int index_p1 = 0;
    int index_p2 = 0;
    int total_elapsed_ms = 0;
    
    printf("Starting scenario playback...\n");
    
    while ((index_p1 < count_p1) || (index_p2 < count_p2)) {
        // Check if we should execute a command for player 1
        if (index_p1 < count_p1 && commands_p1[index_p1].delay_ms <= total_elapsed_ms) {
            printf("Player 1: Executing command %d (direction %d)\n", 
                   index_p1, commands_p1[index_p1].direction);
            send_command_to_client(1, commands_p1[index_p1].direction);
            index_p1++;
        }
        
        // Check if we should execute a command for player 2
        if (index_p2 < count_p2 && commands_p2[index_p2].delay_ms <= total_elapsed_ms) {
            printf("Player 2: Executing command %d (direction %d)\n", 
                   index_p2, commands_p2[index_p2].direction);
            send_command_to_client(2, commands_p2[index_p2].direction);
            index_p2++;
        }
        
        // Wait a bit between checks
        usleep(PLAYER_DELAY);
        total_elapsed_ms += PLAYER_DELAY / 1000;
    }
    
    // Free allocated memory
    if (commands_p1 != NULL) {
        free(commands_p1);
    }
    if (commands_p2 != NULL) {
        free(commands_p2);
    }
    
    printf("Scenario playback complete\n");
    printf("Waiting 5 seconds before cleanup...\n");
    sleep(5);  // Wait 5 seconds
}

// Send command to appropriate client process
void send_command_to_client(int client_num, enum Direction command) {
    // Convert direction enum to string for display
    const char* direction_str;
    switch (command) {
        case UP:    direction_str = "UP";    break;
        case DOWN:  direction_str = "DOWN";  break;
        case LEFT:  direction_str = "LEFT";  break;
        case RIGHT: direction_str = "RIGHT"; break;
        default:    direction_str = "UNKNOWN"; break;
    }
    
    printf("Sending %s command to player %d\n", direction_str, client_num);
    
    // Send the actual direction enum value that pas_client expects
    int dir_value = (int)command;
    
    // Actually send the command to the appropriate pipe
    if (client_num == 1) {
        swrite(pipe_p1[1], &dir_value, sizeof(int));
    } else if (client_num == 2) {
        swrite(pipe_p2[1], &dir_value, sizeof(int));
    }
    
    // Sleep briefly to allow command processing time
    usleep(PLAYER_DELAY);
}

void cleanup(void) {
    printf("Cleaning up processes...\n");
    
    // Close pipes if they're open
    if (pipe_p1[1] >= 0) {
        sclose(pipe_p1[1]);
    }
    if (pipe_p2[1] >= 0) {
        sclose(pipe_p2[1]);
    }
    
    // Send SIGTERM to all child processes
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid > 0) {
            printf("Terminating %s (PID %d)...\n", processes[i].name, processes[i].pid);
            kill(processes[i].pid, SIGTERM);
            // Could also use skill(processes[i].pid, SIGTERM) from utils_v3.h
        }
    }
    
    // Wait for all processes to terminate
    int status;
    while (wait(&status) > 0) {
        // Just wait for all children to finish
    }
    
    printf("All processes terminated\n");
}

void signal_handler(int sig) {
    printf("\nReceived signal %d, terminating...\n", sig);
    cleanup();
    exit(EXIT_SUCCESS);
}