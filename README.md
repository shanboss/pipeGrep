#pipegrep

# Written by Manu Shanbhog 

`pipegrep` is a multi-threaded command-line application designed to simulate a simplified version of the UNIX `grep` command with a pipelined architecture. The application recursively searches through directories for text files that match specific user-defined criteria (such as file size, owner UID, group GID) and extracts lines containing a specified search string.

## Features

- **Multi-threaded File Processing**: Utilizes multiple threads to enhance performance by processing files in parallel through different stages of the pipeline.
- **Customizable File Filtering**: Allows filtering files by size, owner (UID), and group (GID).
- **Search String Filtering**: Extracts lines that contain a specified search term.
- **Pipeline Architecture**: Implements a producer-consumer model with bounded buffers between each stage to manage data flow.

## Critical Sections in 'pipegrep'

The Buffer class manages shared resources (a buffer implemented as a std::deque) between different pipeline stages in the pipegrep application.

Justification:

Mutex Lock (mtx): Ensures that only one thread can add an item to the buffer at any one time, preventing concurrent modifications which can corrupt the buffer state.
Condition Variable (cond_full): Prevents overfilling the buffer. If the buffer is full (buffer.size() >= maxSize), the adding thread must wait. This synchronization ensures that producer threads do not overrun the buffer capacity, which would either lead to lost data (if the buffer overwrites old data) or crash the program (if buffer overflows are unchecked).

Protected Race Condition:

Concurrent Access and Modification: Without this lock, multiple threads could simultaneously modify the buffer, leading to inconsistent or corrupt data states (e.g., pushing items into a full buffer).

## Prerequisites

Before you begin, ensure you have the following installed on your system:

- C++11 compliant compiler (e.g., GCC, Clang)
- CMake (optional, if using CMake to build the project)

## Installation

To compile `pipegrep`, you can use the provided Makefile to get started

## Usage

To run pipegrep, use the following command format:

./pipegrep <buffsize> <filesize> <uid> <gid> <string>

Where:

<buffsize>: Size of the buffers between pipeline stages.
<filesize>: Maximum size (in bytes) of files to ignore, or -1 to ignore this filter.
<uid>: User ID to filter files by, or -1 to ignore this filter.
<gid>: Group ID to filter files by, or -1 to ignore this filter.
<string>: The search string to look for within the files.

# Optimal Performance

I found that setting the buffer size to a number greater than 10^6 (1 million) allows you to comb through a typical directory containing 30 or more files. The program is designed to ignore binary files.

# An extra thread?

If I had to add an extra thread in one of these stages, I would choose Stage 3, line generation. This is primarily because combing through so many files can be a huge bottleneck, especially for large files. Maybe adding a thread here could help parallelize reading different files, significantly improving throughput, especially if files are stored on an SSD where concurrent access can lead to better utilization of the disk bandwidth.

# Any bugs?

Yes and no. I personally would not consider this a 'bug' but the fact that there could be any number of matches not found due to the buffer size is a huge issue (in my opinion). The solution to this would be to remove the buffer size argument entirely and have the program look through your entire directory. After all, the purpose of `grep` is to find a specific keyword, and the fact that you may not find something you are looking for simply because your buffer size may be too small is a huge bottleneck. I say this is a bug because often times during testing I could not find something as there were not enough 0s in my buffer size.

