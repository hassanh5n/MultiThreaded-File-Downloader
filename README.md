# Multi-threaded File Downloader
A high-performance, multi-threaded file downloader written in C that supports resumable downloads and automatic retry mechanisms.

Features
Multi-threaded downloading: Uses up to 16 threads for faster downloads
Resumable downloads: Automatically resumes interrupted downloads
Progress tracking: Real-time progress bar with speed and ETA information
Automatic retry: Retries failed chunks up to 3 times
Smart thread allocation: Automatically determines optimal thread count based on file size
Range request support: Downloads files in 1MB chunks for better reliability
Requirements
Dependencies
libcurl - for HTTP requests
pthread - for multi-threading (usually included with GCC)
Installation of Dependencies
Ubuntu/Debian:

bash
sudo apt-get update
sudo apt-get install libcurl4-openssl-dev build-essential
CentOS/RHEL/Fedora:

bash
# For CentOS/RHEL
sudo yum install libcurl-devel gcc pthread-devel
# For Fedora
sudo dnf install libcurl-devel gcc pthread-devel
macOS:

bash
# Using Homebrew
brew install curl
# Xcode command line tools (includes GCC and pthread)
xcode-select --install
Compilation
bash
gcc -o downloader downloader.c -lcurl -lpthread
Or with additional optimization flags:

bash
gcc -O2 -o downloader downloader.c -lcurl -lpthread -Wall -Wextra
Usage
Basic Usage
bash
./downloader <URL>
This will download the file and save it as output.mkv by default.

Specify Output Filename
bash
./downloader <URL> <filename>
Examples
bash
# Download to default filename
./downloader https://example.com/largefile.zip

# Download with custom filename
./downloader https://example.com/video.mp4 my_video.mp4

# Download and resume if interrupted
./downloader https://example.com/largefile.iso ubuntu.iso
How It Works
File Size Detection: The program first makes a HEAD request to determine the file size
Thread Allocation: Automatically determines optimal thread count based on file size:
Files < 10MB: 2 threads
Files < 50MB: 4 threads
Files < 200MB: 8 threads
Files < 500MB: 12 threads
Files ≥ 500MB: 16 threads
Chunk Distribution: Divides the file into 1MB chunks and distributes them among worker threads
Progress Tracking: A separate thread displays real-time download progress
Resume Support: Saves progress to a .meta file for resumable downloads
Resume Functionality
If a download is interrupted, simply run the same command again. The program will:

Load the existing .meta file
Skip already downloaded chunks
Resume downloading from where it left off
The .meta file is automatically created alongside your download and can be safely deleted after the download completes.

Progress Display
The program shows a real-time progress bar with:

Progress percentage
Visual progress bar
Download speed (MB/s)
Estimated time remaining (ETA)
Example output:

Progress: [=============-------------] 45% | Speed: 2.34 MB/s | ETA: 02:15
Configuration
You can modify these constants in the source code to customize behavior:

MAX_THREADS: Maximum number of worker threads (default: 16)
CHUNK_SIZE: Size of each download chunk (default: 1MB)
MAX_RETRIES: Number of retry attempts for failed chunks (default: 3)
MAX_CHUNKS: Maximum number of chunks supported (default: 2048)
Error Handling
The program includes comprehensive error handling for:

Network connectivity issues
Server errors
File system errors
Memory allocation failures
Failed chunks are automatically retried up to 3 times before being reported as failed.

Limitations
Maximum file size: Limited by MAX_CHUNKS * CHUNK_SIZE (2GB with default settings)
Server must support HTTP range requests for multi-threaded downloading
Some servers may rate-limit or block multi-threaded requests
Troubleshooting
Compilation errors:

Ensure libcurl development headers are installed
On some systems, you may need to link with -lcurl and -lpthread explicitly
Download failures:

Check if the server supports range requests
Some servers block requests without proper User-Agent headers
Try reducing thread count by modifying MAX_THREADS
Permission errors:

Ensure you have write permissions in the target directory
Check available disk space
Contributing
Feel free to submit issues, fork the repository, and create pull requests for any improvements.

Author
Created by Hassan Nafees

