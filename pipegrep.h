#ifndef PIPEGREP_H
#define PIPEGREP_H

#include <string>
#include <mutex>
#include <condition_variable>
#include <deque>

// Define the Buffer class to handle synchronization
template<typename T>
class Buffer {
public:
    explicit Buffer(int maxSize);
    void add(T item);
    T remove();
    bool isEmpty();

private:
    std::deque<T> buffer;
    const int maxSize;
    std::mutex mtx;
    std::condition_variable cond_full, cond_empty;
};

// Stage functions
void stage1(Buffer<std::string>& buff1);
void stage2(Buffer<std::string>& buff1, Buffer<std::string>& buff2, int filesize, int uid, int gid);
void stage3(Buffer<std::string>& buff2, Buffer<std::string>& buff3);
void stage4(Buffer<std::string>& buff3, Buffer<std::string>& buff4, const std::string& searchString);
void stage5(Buffer<std::string>& buff4);

#endif // PIPEGREP_H
