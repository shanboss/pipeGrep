#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>

std::mutex mtx;
std::condition_variable cv;
bool stage3Finished = false;

// Define the Buffer class to handle synchronization
template<typename T>
class Buffer {
public:
    Buffer(int maxSize) : maxSize(maxSize) {}

    void add(T item) {
        std::unique_lock<std::mutex> lock(mtx);
        cond_full.wait(lock, [this]() { return buffer.size() < maxSize; });
        buffer.push_back(item);
        cond_empty.notify_one();
    }

    T remove() {
        std::unique_lock<std::mutex> lock(mtx);
        cond_empty.wait(lock, [this]() { return !buffer.empty(); });
        T item = buffer.front();
        buffer.pop_front();
        cond_full.notify_one();
        return item;
    }

    bool isEmpty() {
        std::lock_guard<std::mutex> lock(mtx);
        return buffer.empty();
    }

private:
    std::deque<T> buffer;
    int maxSize;
    std::mutex mtx;
    std::condition_variable cond_full, cond_empty;
};

bool is_binary_file(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file to check if binary: " << filePath << std::endl;
        return false;
    }

    char ch;
    while (file.read(&ch, sizeof(ch))) {
        if (!std::isprint(ch) && !std::isspace(ch)) {
            return true;
        }
    }
    return false;
}

// Function to recursively traverse directories
void recurseDirectory(const std::string& directory, Buffer<std::string>& buff1) {
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        std::cerr << "Failed to open directory: " << directory << std::endl;
        return;
    }

    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string entryPath = directory + "/" + entry->d_name;

        // Check if entry is a directory or a symbolic link to a directory
        if (entry->d_type == DT_DIR) {
            recurseDirectory(entryPath, buff1);  // Recursively search in the found directory
        } else if (entry->d_type == DT_LNK) {
            struct stat entryStat;
            if (stat(entryPath.c_str(), &entryStat) == 0) { // Follow the symlink
                if (S_ISDIR(entryStat.st_mode)) {  // Check if it's a directory
                    recurseDirectory(entryPath, buff1);
                }
            } else {
                std::cerr << "Failed to access symlink target: " << entryPath << std::endl;
            }
        } else if (entry->d_type == DT_REG) {  // Check if entry is a regular file
            buff1.add(entryPath);  // Add the file path to the buffer
        }
    }
    closedir(dir);
}

void stage1(Buffer<std::string>& buff1) {
    std::string rootDirectory = "."; // Start directory traversal from current directory
    recurseDirectory(rootDirectory, buff1);
    buff1.add(""); // Signal that there are no more filenames to add
}

void stage2(Buffer<std::string>& buff1, Buffer<std::string>& buff2, int filesize, int uid, int gid) {
    while (true) {
        std::string filePath = buff1.remove();
        if (filePath.empty()) {
            buff2.add(""); // Signal end of data
            break;
        }

        if (is_binary_file(filePath)) {
            continue;
        }

        struct stat fileInfo;
        if (stat(filePath.c_str(), &fileInfo) == 0) {
            bool passSize = (filesize == -1 || fileInfo.st_size > filesize);
            bool passUid = (uid == -1 || fileInfo.st_uid == uid);
            bool passGid = (gid == -1 || fileInfo.st_gid == gid);

            if (passSize && passUid && passGid) {
                buff2.add(filePath);
            }
        } else {
            std::cerr << "Error accessing file information for " << filePath << std::endl;
        }
    }
}


// Stage 3: Adding lines from the acquired files
void stage3(Buffer<std::string>& buff2, Buffer<std::string>& buff3) {
    while (true) {
        std::string filePath = buff2.remove();
        if (filePath.empty()) {  // End-of-data signal from Stage 2
            {
                std::lock_guard<std::mutex> lock(mtx);
                stage3Finished = true; // Set the completion flag under lock protection
            }
            cv.notify_all(); // Notify other threads that Stage 3 has finished
            buff3.add(""); // Send end-of-data signal to the next stage
            break;
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            continue;
        }

        std::string line;
        while (std::getline(file, line)) {
            buff3.add(line);
        }
    }
}




// Stage 4: Line Filter
void stage4(Buffer<std::string>& buff3, Buffer<std::string>& buff4, const std::string& searchString) {
    while (true) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return !buff3.isEmpty() || stage3Finished; }); // Adjusted to use isEmpty()
            if (buff3.isEmpty() && stage3Finished) {
                buff4.add(""); // Send end-of-data signal to the next stage
                break;
            }
            line = buff3.remove(); // Safely remove the line
        }

        if (!line.empty() && line.find(searchString) != std::string::npos) {
            buff4.add(line);

        }
    }
}

// Stage 5: Output
void stage5(Buffer<std::string>& buff4) {
    int matchCount = 0; // Counter for the number of matches

    while (true) {
        std::string line = buff4.remove(); // Get the next filtered line from the buffer
        if (line.empty()) { // Check for the end-of-data signal from stage4
            break; // If received, stop processing
        }

        std::cout << line << std::endl; // Print the line
        matchCount++; // Increment the match counter
    }

    // After all lines have been printed, output the total number of matches
    std::cout << "***** You found " << matchCount << " matches *****" << std::endl;
}



int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cout << "Usage: pipegrep ⟨buffsize⟩ ⟨filesize⟩ ⟨uid⟩ ⟨gid⟩ ⟨string⟩" << std::endl;
        return 1;
    }

    int buffSize = std::stoi(argv[1]);
    int fileSize = std::stoi(argv[2]);
    int uid = std::stoi(argv[3]);
    int gid = std::stoi(argv[4]);
    std::string searchString = argv[5];

    Buffer<std::string> buff1(buffSize), buff2(buffSize), buff3(buffSize), buff4(buffSize);

    std::thread t1(stage1, std::ref(buff1));
    std::thread t2(stage2, std::ref(buff1), std::ref(buff2), fileSize, uid, gid);
    std::thread t3(stage3, std::ref(buff2), std::ref(buff3));
    std::thread t4(stage4, std::ref(buff3), std::ref(buff4), searchString);
    std::thread t5(stage5, std::ref(buff4));

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();

    
    return 0;
}
