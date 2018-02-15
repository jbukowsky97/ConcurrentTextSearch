#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>
#include <string>
#include <regex>

/******************************************
 * child signal handler, exits successfully
 * because there is nothing to clean up
 * 
 * @param sig_num signal sent to child
 ******************************************/
void childSignalHandler(int sig_num)
{
    exit(0);
}

/******************************************
 * CIS 452 Concurrent Text Search
 * 
 * Jonah Bukowsky
 * 
 * Spawns a child process for each file
 * found inside of folder, searches text
 * files with strings provided by user
 * 
 * @param argc number of args
 * @param argv args
 * 
 * @return int exit code of program
 ******************************************/
int main(int argc, char** argv) {
    int numFiles = 0;
    DIR* searchFilesDir;
    struct dirent* file;

    /* get directory from args, make sure / is at end */
    if (argc != 2) {
        std::cout << "Usage:\n\t./binary.out <directory of files>" << std::endl;
        return -1;
    }
    std::string directory = argv[1];
    if (directory[directory.size() - 1] != '/') {
        directory = directory + "/";
    }

    /* find out how many non-directory files
       are in the folder */
    searchFilesDir = opendir(directory.c_str());
    if (searchFilesDir == NULL) {
        std::cout << "Directory not found" << std::endl;
        return -1;
    }
    while ((file = readdir(searchFilesDir)) != NULL) {
        if ((*file).d_type != DT_DIR) {
            numFiles++;
        }
    }
    if (closedir(searchFilesDir) < 0) {
        std::cout << "Error closing directory" << std::endl;
        return -1;
    }

    /* check if no files */
    if (numFiles == 0) {
        std::cout << "No files in directory" << std::endl;
        return -1;
    }

    /* create upstream and downstream pipes needed */
    int downstream[numFiles][2];
    int upstream[numFiles][2];
    for (int i = 0; i < numFiles; i++) {
        if (pipe(downstream[i]) < 0 || pipe(upstream[i]) < 0) {
            std::cout << "error creating downstream" << std::endl;
            exit(-1);
        }
    }

    /* get parent pid so beginning value
       of pid is not 0, create array for
       child pids to keep track of */
    pid_t pid = getpid();
    pid_t childPids[numFiles];

    /* loop through numFiles, forking as
       long as current process is the
       parent process */
    int childIndex = -1;
    for (int i = 0; i < numFiles; i++) {
        if (pid != 0) {
            if ((pid = fork()) < 0) {
                std::cout << "Fork failed" << std::endl;
                return -1;
            }
            if (pid == 0) {
                childIndex = i;
            }else {
                /* capture childs pid */
                childPids[i] = pid;
            }
        }
    }

    /* child process code */
    if (pid == 0) {
        /*setup signal handler */
        if (signal(SIGUSR1, childSignalHandler) == SIG_ERR) {
            std::cout << "Error setting up signal handler" << std::endl;
        }

        /* close all unused pipes for this child processs */
        int results = 0;
        for (int i = 0; i < numFiles; i++) {
            if (i != childIndex) {
                results += close(downstream[i][1]);
                results += close(downstream[i][0]);
                results += close(upstream[i][0]);
                results += close(upstream[i][1]);
            }
        }
        results += close(downstream[childIndex][1]);
        results += close(upstream[childIndex][0]);

        if (results != 0) {
            std::cout << "Error closing pipes" << std::endl;
            return -1;
        }

        /* read filename designated by parent */
        char filename[100];
        if (read(downstream[childIndex][0], filename, sizeof(filename)) < 0) {
            std::cout << "Error reading" << std::endl;
            return -1;
        }

        /* read in contents of file into a string */
        std::string contents;
        std::string line;
        std::ifstream childFile (filename);
        if (childFile.is_open()) {
            while (std::getline(childFile, line)) {
                contents += line;
            }
            childFile.close();
        }

        /* continuously read from parent, search for string in file contents,
           then report back results to parent */
        char searchCStr[1000];
        while (true) {
            if (read(downstream[childIndex][0], searchCStr, sizeof(searchCStr)) < 0) {
                std::cout << "Error reading" << std::endl;
                return -1;
            }
            std::string searchString = searchCStr;
            std::string contentsCopy = contents;

            std::smatch stringMatch;
            int numMatches = 0;
            while (std::regex_search(contentsCopy, stringMatch, std::regex(searchString))) {
                numMatches++;
                contentsCopy = stringMatch.suffix().str();
            }
            if (write(upstream[childIndex][1], &numMatches, sizeof(int)) < 0) {
                std::cout << "Error writing" << std::endl;
                return -1;
            }
        }

        /* should never happen */
        exit(0);
    }

    /* parent process code */

    /* create array of filenames to pass to children */
    std::string filenames[numFiles];
    
    searchFilesDir = opendir(directory.c_str());
    int index = 0;
    while ((file = readdir(searchFilesDir)) != NULL) {
        if ((*file).d_type != DT_DIR) {
            filenames[index++] = directory + (std::string) (*file).d_name;
        }
    }
    if (closedir(searchFilesDir) < 0) {
        std::cout << "Error closing directory" << std::endl;
        return -1;
    }

    /* close unneeded pipes and print assign details */
    for (int i = 0; i < numFiles; i++) {
        if (close(downstream[i][0]) < 0 || close(upstream[i][1]) < 0) {
            std::cout << "Error closing pipes" << std::endl;
            return -1;
        }
        std::cout << "Assigning child with PID " + std::to_string(childPids[i]) + " to file \"" + filenames[i] + "\"" << std::endl;
        if (write(downstream[i][1], filenames[i].c_str(), strlen(filenames[i].c_str()) + 1) < 0) {
            std::cout << "Error writing" << std::endl;
            return -1;
        }
    }

    /* start loop to read in from user */
    std::string searchString;
    while (true) {
        std::cout << "Enter string to search for:\t";
        /* get input and check if it is alphabetical or not */
        getline(std::cin, searchString);
        if (!std::regex_match(searchString, std::regex("[A-Za-z ]+"))) {
            /* tell children to exit */
            for (int i = 0; i < numFiles; i++) {
                if (kill(childPids[i], SIGUSR1) < 0) {
                    std::cout << "Error sending signal to child" << std::endl;
                    return -1;
                }
            }
            break;
        }
        /* write search strings to pipes */
        for (int i = 0; i < numFiles; i++) {
            if (write(downstream[i][1], searchString.c_str(), strlen(searchString.c_str()) + 1) < 0) {
                std::cout << "Error writing" << std::endl;
                return -1;
            }
        }

        /* wait for results from children and report summary */
        std::string summary = "Summary:\n";
        int totalMatches = 0;
        for (int i = 0; i < numFiles; i++) {
            int numMatches;
            if (read(upstream[i][0], &numMatches, sizeof(int)) < 0) {
                std::cout << "Error reading" << std::endl;
                return -1;
            }
            totalMatches += numMatches;
            summary += "\tChild with PID " + std::to_string((long) childPids[i]) + " found " +
                std::to_string(numMatches) + " occurences of \"" + searchString +
                "\" in file \"" + filenames[i] + "\"\n";
        }
        summary += "Total matches:\t" + std::to_string(totalMatches) + "\n";
        std::cout << summary;
    }

    /* wait for every child to exit, verify that number exited equals number created */
    pid_t who;
    int status;
    int exitCount = 0;
    for (int i = 0; i < numFiles; i++) {
        who = wait(&status);
        std::cout << "Child with PID " + std::to_string((long) who) + " exited with status " + std::to_string(status) << std::endl;
        exitCount++;
    }
    if (exitCount == numFiles) {
        std::cout << "All children successfully exited" << std::endl;
    }

    /* exit successfully */
    return 0;
}