/*
 ***************************************************************************
 * Clarkson University                                                     *
 * CS 444/544: Operating Systems, Spring 2024                              *
 * Project: Prototyping a Web Server/Browser                               *
 * Created by Daqing Hou, dhou@clarkson.edu                                *
 *            Xinchao Song, xisong@clarkson.edu                            *
 * April 10, 2022                                                          *
 * Copyright © 2022-2024 CS 444/544 Instructor Team. All rights reserved.  *
 * Unauthorized use is strictly prohibited.                                *
 ***************************************************************************
 */

#include "net_util.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unordered_map>
#include <random>  





#define BUFFER_LEN 1024


#define NUM_VARIABLES 26
#define NUM_SESSIONS 128
#define NUM_BROWSER 128
#define DATA_DIR "./sessions"
#define SESSION_PATH_LEN 128

typedef struct browser_struct {
    bool in_use;
    int socket_fd;
    int session_id;
} browser_t;

typedef struct session_struct {
    bool in_use;
    bool variables[NUM_VARIABLES];
    double values[NUM_VARIABLES];
} session_t;

static browser_t browser_list[NUM_BROWSER];                             // Stores the information of all browsers.
static std::unordered_map<int, session_t> session_map;  // Stores the information of all sessions.
// static session_t session_list[NUM_SESSIONS];                            // Stores the information of all sessions.
static pthread_mutex_t browser_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the browser list.
static pthread_mutex_t session_map_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the session list.


// Returns the string format of the given session.
// There will be always 9 digits in the output string.
void session_to_str(int session_id, char result[]);

// Determines if the given string represents a number.
bool is_str_numeric(const char str[]);

// Process the given message and update the given session if it is valid.
bool process_message(int session_id, const char message[]);

// Broadcasts the given message to all browsers with the same session ID.
void broadcast(int session_id, const char message[]);

// Gets the path for the given session.
void get_session_file_path(int session_id, char path[]);

// Loads every session from the disk one by one if it exists.
void load_all_sessions();

// Saves the given sessions to the disk.
void save_session(int session_id);

// Assigns a browser ID to the new browser.
// Determines the correct session ID for the new browser
// through the interaction with it.
int register_browser(int browser_socket_fd);

// Handles the given browser by listening to it,
// processing the message received,
// broadcasting the update to all browsers with the same session ID,
// and backing up the session on the disk.
void browser_handler(int browser_socket_fd);

// Starts the server.
// Sets up the connection,
// keeps accepting new browsers,
// and creates handlers for them.
void start_server(int port);

/**
 * Returns the string format of the given session.
 * There will be always 9 digits in the output string.
 *
 * @param session_id the session ID
 * @param result an array to store the string format of the given session;
 *               any data already in the array will be erased
 */
void session_to_str(int session_id, char result[]) {
    memset(result, 0, BUFFER_LEN);
    if (session_map.find(session_id) == session_map.end()) {
        return; // Session ID not found
    }
    session_t& session = session_map[session_id]; // Access the session by ID

    for (int i = 0; i < NUM_VARIABLES; ++i) {
        if (session.variables[i]) {
            char line[32];
            if (session.values[i] < 1000) {
                snprintf(line, sizeof(line), "%c = %.6f\n", 'a' + i, session.values[i]); // Using snprintf for safety
            } else {
                snprintf(line, sizeof(line), "%c = %.8e\n", 'a' + i, session.values[i]); // Using snprintf for safety
            }
            strcat(result, line);
        }
    }
}


/**
 * Determines if the given string represents a number.
 *
 * @param str the string to determine if it represents a number
 * @return a boolean that determines if the given string represents a number
 */


bool is_str_numeric(const char str[]) {
    if (str == NULL || str[0] == '\0' || 
        !(isdigit(str[0]) || str[0] == '-' || str[0] == '.')) {
        return false;
    }

    char* ptr;
    strtod(str, &ptr);  // Convert string to double, ptr points to part not used in conversion
    return *ptr == '\0';  // Check if the entire string was consumed
}

/**
 * Process the given message and update the given session if it is valid.
 * If the message is valid, the function will return true; otherwise, it will return false.
 *
 * @param session_id the session ID
 * @param message the message to be processed
 * @return a boolean that determines if the given message is valid
 */


extern std::unordered_map<int, session_t> session_map; 


/**
 * Checks if a variable (identified by a single character) has been initialized.
 */
bool is_variable_initialized(int session_id, char variable) {
    int idx = variable - 'a';
    if (idx < 0 || idx >= NUM_VARIABLES) return false; // Check range
    return session_map[session_id].variables[idx];
}

/**
 * Process the given message and update the given session if it is valid.
 */



bool process_message(int session_id, const char message[]) {
    if (session_map.find(session_id) == session_map.end()) {
        return false;  // Session does not exist
    }

    char *token, *tokens[5];
    int num_tokens = 0, result_idx;
    double first_value, second_value;
    char data[BUFFER_LEN], symbol;

    strcpy(data, message);  // Makes a copy of the message.

    // Tokenization of the input
    token = strtok(data, " ");
    while (token != NULL && num_tokens < 5) {
        tokens[num_tokens++] = token;
        token = strtok(NULL, " ");
    }

    // Validate format: Must have at least three tokens (var, "=", value)
    if (num_tokens < 3 || strcmp(tokens[1], "=") != 0) return false;

    if (!isalpha(tokens[0][0]) || strlen(tokens[0]) != 1) return false;  // Ensure variable is a single letter
    result_idx = tokens[0][0] - 'a';

    session_t& session = session_map[session_id]; // Get the session reference

    // First value or variable handling
    if (is_str_numeric(tokens[2])) {
        first_value = strtod(tokens[2], NULL);
    } else if (isalpha(tokens[2][0]) && strlen(tokens[2]) == 1 && is_variable_initialized(session_id, tokens[2][0])) {
        first_value = session.values[tokens[2][0] - 'a'];
    } else {
        return false;  // First value is not numeric or an uninitialized variable
    }

    if (num_tokens == 3) {  // Simple assignment, no operation
        session.variables[result_idx] = true;
        session.values[result_idx] = first_value;
        return true;
    }

    // Operation handling
    if (num_tokens != 5) return false;  // Complete expression must have exactly five tokens
    symbol = tokens[3][0];
    if (strchr("+-*/", symbol) == NULL) return false;  // Validate operation symbol

    // Second value or variable handling
    if (is_str_numeric(tokens[4])) {
        second_value = strtod(tokens[4], NULL);
    } else if (isalpha(tokens[4][0]) && strlen(tokens[4]) == 1 && is_variable_initialized(session_id, tokens[4][0])) {
        second_value = session.values[tokens[4][0] - 'a'];
    } else {
        return false;  // Second value is not numeric or an uninitialized variable
    }

    // Perform the operation
    session.variables[result_idx] = true;
    switch (symbol) {
        case '+':
            session.values[result_idx] = first_value + second_value;
            break;
        case '-':
            session.values[result_idx] = first_value - second_value;
            break;
        case '*':
            session.values[result_idx] = first_value * second_value;
            break;
        case '/':
            if (second_value == 0) return false;  // Avoid division by zero
            session.values[result_idx] = first_value / second_value;
            break;
        default:
            return false;  // Unrecognized operation
    }

    return true;
}



/**
 * Broadcasts the given message to all browsers with the same session ID.
 *
 * @param session_id the session ID
 * @param message the message to be broadcasted
 */



void broadcast(int session_id, const char message[]) {
    // First check if the session_id exists in the session_map
    pthread_mutex_lock(&session_map_mutex);  // Lock the session map mutex
    bool session_exists = (session_map.find(session_id) != session_map.end());
    pthread_mutex_unlock(&session_map_mutex);  // Unlock the mutex

    if (!session_exists) {
        return;  // If the session does not exist, do not proceed with broadcasting
    }

    // If the session exists, proceed to broadcast to all browsers in this session
    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (browser_list[i].in_use && browser_list[i].session_id == session_id) {
            send_message(browser_list[i].socket_fd, message);
        }
    }
}

/**
 * Gets the path for the given session.
 *
 * @param session_id the session ID
 * @param path the path to the session file associated with the given session ID
 */
void get_session_file_path(int session_id, char path[]) {
    sprintf(path, "%s/session%d.dat", DATA_DIR, session_id);
}

/**
 * Loads every session from the disk one by one if it exists.
 * Use get_session_file_path() to get the file path for each session.
 */
void load_all_sessions() {

    // TODO
   for (int i = 0; i < NUM_SESSIONS; ++i) {
        char path[SESSION_PATH_LEN];
        get_session_file_path(i, path);
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            fread(&session_map[i], sizeof(session_t), 1, file);
            fclose(file);
        }
    }
}

/**
 * Saves the given sessions to the disk.
 * Use get_session_file_path() to get the file path for each session.
 *
 * @param session_id the session ID
 */
void save_session(int session_id) {
    // TODO
    char path[SESSION_PATH_LEN];
    get_session_file_path(session_id, path);
    FILE *file = fopen(path, "w");
    if (file != NULL) {
        fwrite(&session_map[session_id], sizeof(session_t), 1, file);
        fclose(file);
    }
   
}

/**
 * Assigns a browser ID to the new browser.
 * Determines the correct session ID for the new browser through the interaction with it.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 * @return the ID for the browser
 */
/**
int register_browser(int browser_socket_fd) {
    int browser_id;

    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (!browser_list[i].in_use) {
            browser_id = i;
            browser_list[browser_id].in_use = true;
            browser_list[browser_id].socket_fd = browser_socket_fd;
            break;
        }
    }

    char message[BUFFER_LEN];
    receive_message(browser_socket_fd, message);

    int session_id = strtol(message, NULL, 10);
    if (session_id == -1) {
        for (int i = 0; i < NUM_SESSIONS; ++i) {
            if (!session_list[i].in_use) {
                session_id = i;
                session_list[session_id].in_use = true;
                break;
            }
        }
    }
    browser_list[browser_id].session_id = session_id;

    sprintf(message, "%d", session_id);
    send_message(browser_socket_fd, message);

    return browser_id;
}


int register_browser(int browser_socket_fd) {
    int browser_id;

    pthread_mutex_lock(&browser_list_mutex); // Locking the mutex before accessing browser_list
    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (!browser_list[i].in_use) {
            browser_id = i;
            browser_list[browser_id].in_use = true;
            browser_list[browser_id].socket_fd = browser_socket_fd;
            break;
        }
    }
    pthread_mutex_unlock(&browser_list_mutex); // Unlocking the mutex after accessing browser_list

    char message[BUFFER_LEN];
    receive_message(browser_socket_fd, message);

    int session_id = strtol(message, NULL, 10);
    if (session_id == -1) {
        pthread_mutex_lock(&session_list_mutex); // Locking the mutex before accessing session_list
        for (int i = 0; i < NUM_SESSIONS; ++i) {
            if (!session_list[i].in_use) {
                session_id = i;
                session_list[session_id].in_use = true;
                break;
            }
        }
        pthread_mutex_unlock(&session_list_mutex); // Unlocking  the mutex after accessing session_list
    }
    browser_list[browser_id].session_id = session_id;

    sprintf(message, "%d", session_id);
    pthread_mutex_lock(&browser_list_mutex); // Locking the mutex before accessing browser_list
    send_message(browser_list[browser_id].socket_fd, message);
    pthread_mutex_unlock(&browser_list_mutex); // Unlocking the mutex after accessing browser_list

    return browser_id;
}
*/



int register_browser(int browser_socket_fd) {
    pthread_mutex_lock(&browser_list_mutex); // Locking the mutex before accessing browser_list
    int browser_id = -1;
    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (!browser_list[i].in_use) {
            browser_id = i;
            browser_list[browser_id].in_use = true;
            browser_list[browser_id].socket_fd = browser_socket_fd;
            break;
        }
    }
    pthread_mutex_unlock(&browser_list_mutex); // Unlocking the mutex after accessing browser_list

    if (browser_id == -1) {
        return -1; // No available browser slots
    }

    // Generate a unique session ID
    std::random_device rd; // Obtain a random number from hardware
    std::mt19937 eng(rd()); // Seed the generator
    std::uniform_int_distribution<> distr(10000, 99999); // Define the range

    int session_id = distr(eng);
    pthread_mutex_lock(&session_map_mutex); // Lock the session map mutex
    while (session_map.find(session_id) != session_map.end()) { // Ensure uniqueness
        session_id = distr(eng);
    }

    // Create a new session and store it in the map
    session_t new_session;
    new_session.in_use = true;
    memset(new_session.variables, 0, sizeof(new_session.variables));
    memset(new_session.values, 0, sizeof(new_session.values));
    session_map[session_id] = new_session;

    pthread_mutex_unlock(&session_map_mutex); // Unlock the session map mutex

    browser_list[browser_id].session_id = session_id;

    // Send the session ID back to the browser
    char message[BUFFER_LEN];
    snprintf(message, sizeof(message), "%d", session_id); // Safer with snprintf
    send_message(browser_list[browser_id].socket_fd, message);

    return browser_id;
}


/**
 * Handles the given browser by listening to it, processing the message received,
 * broadcasting the update to all browsers with the same session ID, and backing up
 * the session on the disk.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 */

void* browser_handler(void* arg) {
    int browser_socket_fd = *((int*)arg); // Cast the argument back to int
    free(arg);  // Free the argument right after using it

    int browser_id = register_browser(browser_socket_fd);
    int socket_fd = browser_list[browser_id].socket_fd;
    int session_id = browser_list[browser_id].session_id;

    printf("Successfully accepted Browser #%d for Session #%d.\n", browser_id, session_id);

    while (true) {
        char message[BUFFER_LEN];
        char response[BUFFER_LEN];

        receive_message(socket_fd, message);
        printf("Received message from Browser #%d for Session #%d: %s\n", browser_id, session_id, message);

        if ((strcmp(message, "EXIT") == 0) || (strcmp(message, "exit") == 0)) {
            pthread_mutex_lock(&browser_list_mutex);
            browser_list[browser_id].in_use = false;
            pthread_mutex_unlock(&browser_list_mutex);
            close(socket_fd);
            printf("Browser #%d exited.\n", browser_id);
            break; // Exit the thread
        }

        if (message[0] == '\0') {
            continue;
        }

        bool data_valid = process_message(session_id, message);
        if (!data_valid) {
            strcpy(response, "ERROR");
            send_message(socket_fd, response); // Send error message to the browser
            continue;
        }

        session_to_str(session_id, response);
        broadcast(session_id, response);
        save_session(session_id);
    }

    pthread_exit(NULL); // Exit the thread
}

/**
 * Starts the server. Sets up the connection, keeps accepting new browsers,
 * and creates handlers for them.
 *
 * @param port the port that the server is running on
 */



void start_server(int port) {
    // Loads every session if there exists one on the disk.
    load_all_sessions();

    // Creates the socket.
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Binds the socket.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    if (bind(server_socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    // Listens to the socket.
    if (listen(server_socket_fd, SOMAXCONN) < 0) {
        perror("Socket listen failed");
        exit(EXIT_FAILURE);
    }
    printf("The server is now listening on port %d.\n", port);

    // Main loop to accept new browsers and creates handlers for them.
    while (true) {
        struct sockaddr_in browser_address;
        socklen_t browser_address_len = sizeof(browser_address);
        int* browser_socket_fd = (int*)malloc(sizeof(int)); // Allocate memory for the browser socket fd
        *browser_socket_fd = accept(server_socket_fd, (struct sockaddr *)&browser_address, &browser_address_len);
        if (*browser_socket_fd < 0) {
            perror("Socket accept failed");
            free(browser_socket_fd); // Free memory if accept failed
            continue;
        }

        // Creating a thread to run browser_handler and pass browser_socket_fd as argument
        pthread_t browser_thread;
        if (pthread_create(&browser_thread, NULL, browser_handler, (void*)browser_socket_fd) != 0) {
            perror("Thread creation failed");
            close(*browser_socket_fd);
            free(browser_socket_fd); // Free memory if thread creation failed
            continue;
        }

        // Detach the thread so its resources are automatically released when it exits
        pthread_detach(browser_thread);
    }

    // Closes the server socket (This part is never reached in an infinite loop)
    close(server_socket_fd);
}

/**
 * The main function for the server.
 *
 * @param argc the number of command-line arguments passed by the user
 * @param argv the array that contains all the arguments
 * @return exit code
 */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc == 1) {
    } else if ((argc == 3)
               && ((strcmp(argv[1], "--port") == 0) || (strcmp(argv[1], "-p") == 0))) {
        port = strtol(argv[2], NULL, 10);

    } else {
        puts("Invalid arguments.");
        exit(EXIT_FAILURE);
    }

    if (port < 1024) {
        puts("Invalid port.");
        exit(EXIT_FAILURE);
    }

    start_server(port);

    exit(EXIT_SUCCESS);
}
