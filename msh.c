#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>   // For dynamic loading of shared libraries
#include <unistd.h>  // For fork(), exec(), etc.
#include <sys/wait.h>  // For wait()
#include <signal.h>

#define MAX_COMMAND_LENGTH 200
#define MAX_ARG_LENGTH 20
#define MAX_ARGS 20
#define MAX_PLUGINS 10

// Structure to hold plugin information
typedef struct {
    void *handle; // Handle to the dynamically loaded plugin
    int (*initialize)(void); // Pointer to initialize function
    int (*run)(char **argv); // Pointer to run function
    char name[MAX_ARG_LENGTH]; // Plugin name
} Plugin;

Plugin plugins[MAX_PLUGINS];
int plugin_count = 0;
int should_suppress_prompt = 0;


// Function to parse input
void parse_input(char* input, char*** parsed) {
    int size = 10;  // Initial size of the array
    *parsed = malloc(size * sizeof(char*));
    if (*parsed == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    
    int index = 0;
    char* token = strtok(input, " \n");

    while (token != NULL) {
        if (index >= size) {
            size *= 2;
            *parsed = realloc(*parsed, size * sizeof(char*));
            if (*parsed == NULL) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }

        (*parsed)[index++] = strdup(token); // Allocate memory for each token
        token = strtok(NULL, " \n");
    }

    (*parsed)[index] = NULL; // Null-terminate the array
}

// Function to load plugin
int load_plugin(const char* plugin_name) {
    if (plugin_count >= MAX_PLUGINS) {
        printf("Maximum number of plugins already loaded.\n");
        return -1;
    }

    char plugin_path[256];
    snprintf(plugin_path, sizeof(plugin_path), "./%s.so", plugin_name);

    for (int i = 0; i < plugin_count; i++) {
        if (strcmp(plugins[i].name, plugin_name) == 0) {
            fprintf(stderr, "Error: Plugin %s initialization failed!\n", plugin_name);
            should_suppress_prompt = 1;
            return -1;
        }
    }

    void *handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error: Plugin %s initialization failed!\n", plugin_name);
        should_suppress_prompt = 1;
        return -2;
    }

    int (*initialize)(void) = dlsym(handle, "initialize");
    char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "Error finding initialize function: %s\n", error);
        dlclose(handle);
        return -3;
    }

    if (initialize() != 0) {
        fprintf(stderr, "Error: Plugin %s initialization failed!\n", plugin_name);
        dlclose(handle);
        should_suppress_prompt = 1; // Suppress prompt on failure
        return -4;
    }

    int (*run)(char **) = dlsym(handle, "run");
    error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "Error finding run function: %s\n", error);
        dlclose(handle);
        return -5;
    }

    Plugin new_plugin = {handle, initialize, run, ""};
    strncpy(new_plugin.name, plugin_name, MAX_ARG_LENGTH - 1);
    plugins[plugin_count++] = new_plugin;
    should_suppress_prompt = 1;
    return 0;
}

// Function to handle built-in commands
int handle_builtin(char** parsed) {
    if (strcmp(parsed[0], "exit") == 0) {
        // If the command is 'exit', terminate the program
        exit(0);
    } else if (strcmp(parsed[0], "load") == 0) {
            if (parsed[1] == NULL) {
                fprintf(stderr, "Error: Plugin 123 initialization failed!\n");
                should_suppress_prompt = 1;
                return 1;  // Indicate that it was a built-in command
            }
            if (load_plugin(parsed[1]) != 0) {
                should_suppress_prompt = 1; // Suppress prompt on failure
            }
            return 1; 
        }

    // If the command is not a built-in, return 0
    return 0;
}

// Function to execute other commands
void execute_command(char** parsed) {

    for (int i = 0; i < plugin_count; i++) {
        if (strcmp(plugins[i].name, parsed[0]) == 0) {
            // Run the plugin
            plugins[i].run(parsed);
            return;
        }
    }
    
    pid_t pid = fork();

    if (pid == -1) {
        // Fork failed
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process
        if (execvp(parsed[0], parsed) == -1) {
            perror(parsed[0]);
            exit(EXIT_FAILURE);
        }
    } else {
        // Parent process
        int status;
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

void sigint_handler(int sig_num) {
    // Optionally, handle cleanup or print a new prompt
    printf("\n> ");
    fflush(stdout);
}

int main() {
    signal(SIGINT, sigint_handler);
    char input[MAX_COMMAND_LENGTH];
    char* parsed[MAX_ARGS];

    while (1) {
        if (!should_suppress_prompt) {
            printf("> ");
        }
        should_suppress_prompt = 0;
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) {
                // End of file (Ctrl+D)
                printf("\n");
                break;  // Exit the loop and terminate the shell
            }
            if (ferror(stdin)) {
                perror("fgets");
                continue;  // Continue to the next iteration
            }
        }

        char** parsed;
        parse_input(input, &parsed);

        if (plugin_count > 0 && strcmp(parsed[0], plugins[plugin_count - 1].name) == 0) {
            plugins[plugin_count - 1].run(parsed);
            should_suppress_prompt = 1;
        } else {
            // Handle other inputs
            if (handle_builtin(parsed) == 0) {
                execute_command(parsed);
            }
        }

        for (int i = 0; parsed[i] != NULL; i++) {
            free(parsed[i]);
        }
        should_suppress_prompt = 0;
        free(parsed);
    }
    
    return 0;
}