#ifndef COMMAND_INTERPRETER_HPP
#define COMMAND_INTERPRETER_HPP

#include <string>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>

/**
 * @brief Defines the function signature for command callbacks.
 * The function will receive a single string containing all parameters passed to the command.
 */
using CommandCallback = std::function<void(const std::string&)>;

/**
 * @brief A thread-safe, non-blocking command line interpreter.
 */
class CommandInterpreter {
   public:
    CommandInterpreter();
    ~CommandInterpreter();

    /**
     * @brief Subscribes a command string to a callback function.
     * @param command The command to listen for (e.g., "help").
     * @param callback The function to execute when the command is entered.
     */
    void subscribe(const std::string& command, CommandCallback callback);

    /**
     * @brief Starts the command interpreter in a new thread.
     * It will begin listening for user input from the terminal.
     */
    void start();

    /**
     * @brief Stops the command interpreter and cleans up its thread.
     */
    void stop();

   private:
    /**
     * @brief The main loop that runs in a separate thread to read user input.
     */
    void run();

    /**
     * @brief Parses a line of input into a command and its parameters, then executes the callback.
     * @param line The full line of text entered by the user.
     */
    void processLine(const std::string& line);

    std::unordered_map<std::string, CommandCallback> commands;
    std::thread interpreter_thread;
    bool running;
};

/**
 * @brief A thread-safe printf replacement.
 *
 * This function ensures that log messages do not get mixed with the user's
 * command line input. It clears the current input line, prints the log message,
 * and then redraws the input prompt and the user's current text.
 *
 * @param format The format string, just like regular printf.
 * @param ... The variable arguments for the format string.
 */
void safe_printf(const char* format, ...);

#endif  // COMMAND_INTERPRETER_HPP
