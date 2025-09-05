#include "CommandInterpreter.hpp"
#include <iostream>
#include <cstdarg>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <vector>

// --- Global state for managing synchronized terminal I/O ---
// Mutex to protect stdout and the current_input_line
static std::mutex g_stdout_mutex;
// Buffer to store the user's current command line input
static std::string g_current_input_line;
// Store original terminal settings to restore them on exit
static struct termios g_orig_termios;

/**
 * @brief Restores the terminal to its original settings.
 */
static void restore_terminal_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

/**
 * @brief Sets the terminal to non-canonical (raw) mode.
 * This allows reading characters as they are typed without waiting for the Enter key.
 * It also disables echoing characters automatically.
 */
static void set_raw_mode() {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    // Register the cleanup function to be called on normal program exit
    atexit(restore_terminal_mode);

    struct termios raw = g_orig_termios;
    // Turn off canonical mode (line buffering) and character echoing
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// --- safe_printf implementation ---
void safe_printf(const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_stdout_mutex);

    // 1. Clear the current user input line from the screen
    printf("\r");                                                     // Move cursor to the beginning of the line
    for (size_t i = 0; i < g_current_input_line.length() + 2; ++i) {  // +2 for "> " prompt
        printf(" ");
    }
    printf("\r");  // Move cursor back to the beginning

    // 2. Print the actual log message
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    // 3. Redraw the user input prompt and the line they were typing
    printf("> %s", g_current_input_line.c_str());
    fflush(stdout);
}

// --- CommandInterpreter implementation ---
CommandInterpreter::CommandInterpreter() : running(false) {}

CommandInterpreter::~CommandInterpreter() {
    stop();
}

void CommandInterpreter::subscribe(const std::string& command, CommandCallback callback) {
    commands[command] = callback;
}

void CommandInterpreter::start() {
    if (running) return;
    running = true;
    set_raw_mode();
    interpreter_thread = std::thread(&CommandInterpreter::run, this);
}

void CommandInterpreter::stop() {
    if (!running) return;
    running = false;
    if (interpreter_thread.joinable()) {
        interpreter_thread.join();
    }
    // The atexit handler will call restore_terminal_mode, but we call it here
    // as well to ensure it's clean if stop() is called before main exits.
    restore_terminal_mode();
    printf("\n");  // Newline after stopping for a clean exit
}

void CommandInterpreter::run() {
    // Set stdin to be non-blocking
    int old_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);

    {  // Print initial prompt
        std::lock_guard<std::mutex> lock(g_stdout_mutex);
        printf("> ");
        fflush(stdout);
    }

    while (running) {
        char c;
        // Try to read a character
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '\n' || c == '\r') {  // Enter key - Special handling to avoid deadlock
                std::string line_to_process;
                {  // Use a narrow scope to lock, copy the line, and unlock
                    std::lock_guard<std::mutex> lock(g_stdout_mutex);
                    printf("\r\n");  // Move to a new line before processing
                    line_to_process = g_current_input_line;
                    g_current_input_line.clear();
                }

                // Process the command *after* the lock has been released
                processLine(line_to_process);

                // Re-acquire lock just to print the new prompt
                {
                    std::lock_guard<std::mutex> lock(g_stdout_mutex);
                    printf("> ");  // Show new prompt
                    fflush(stdout);
                }
            } else {  // Handle all other characters (backspace, printable, etc.)
                std::lock_guard<std::mutex> lock(g_stdout_mutex);
                if (c == 127 || c == 8) {  // Backspace
                    if (!g_current_input_line.empty()) {
                        g_current_input_line.pop_back();
                        printf("\b \b");  // Erase character from screen
                    }
                } else if (c >= 32 && c < 127) {  // Printable character
                    g_current_input_line += c;
                    printf("%c", c);  // Echo the character
                }
                fflush(stdout);
            }
        } else {
            // No input available, sleep briefly to prevent busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // Restore original stdin flags
    fcntl(STDIN_FILENO, F_SETFL, old_flags);
}

void CommandInterpreter::processLine(const std::string& line) {
    if (line.empty()) {
        return;
    }

    std::string command;
    std::string params;
    size_t first_space = line.find(' ');

    if (first_space == std::string::npos) {
        command = line;
    } else {
        command = line.substr(0, first_space);
        params = line.substr(first_space + 1);
    }

    auto it = commands.find(command);
    if (it != commands.end()) {
        try {
            it->second(params);  // Execute the associated callback
        } catch (const std::exception& e) {
            safe_printf("Error executing command '%s': %s\n", command.c_str(), e.what());
        }
    } else {
        safe_printf("Unknown command: '%s'. Type 'help' for a list of commands.\n", command.c_str());
    }
}
