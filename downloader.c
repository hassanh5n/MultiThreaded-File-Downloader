#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#define MAX_THREADS 16
#define MAX_CHUNKS 2048
#define CHUNK_SIZE (1024 * 1024) // 1 MB
#define MAX_RETRIES 3

typedef struct {
    char url[2048];
    char filename[1024];
} downloadcontext;

typedef struct {
    long start;
    long end;
} Chunk;

Chunk chunkQueue[MAX_CHUNKS];
int queueFront = 0, queueRear = 0;
pthread_mutex_t queueMutex;
long totalDownloaded = 0;
long totalSize = 0;
pthread_mutex_t progressMutex;
char completed_chunks[MAX_CHUNKS] = {0};


size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    FILE* fp = (FILE*)stream;
    size_t written = fwrite(ptr, size, nmemb, fp);
    pthread_mutex_lock(&progressMutex);
    totalDownloaded += written;
    pthread_mutex_unlock(&progressMutex);
    return written;
}

int enqueue_chunk(long start, long end) {
    pthread_mutex_lock(&queueMutex);
    if ((queueRear + 1) % MAX_CHUNKS == queueFront) {
        pthread_mutex_unlock(&queueMutex);
        return 0;
    }
    chunkQueue[queueRear].start = start;
    chunkQueue[queueRear].end = end;
    queueRear = (queueRear + 1) % MAX_CHUNKS;
    pthread_mutex_unlock(&queueMutex);
    return 1;
}

int dequeue_chunk(Chunk *chunk) {
    pthread_mutex_lock(&queueMutex);
    if (queueFront == queueRear) {
        pthread_mutex_unlock(&queueMutex);
        return 0;
    }
    *chunk = chunkQueue[queueFront];
    queueFront = (queueFront + 1) % MAX_CHUNKS;
    pthread_mutex_unlock(&queueMutex);
    return 1;
}

long get_file_size(const char* url) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Downloader/1.0");

    if (curl_easy_perform(curl) != CURLE_OK) {
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_off_t size = 0;
    if (curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &size) != CURLE_OK || size < 0) {
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);
    return (long)size;
}

void save_progress(const char* filename) {
    char metafile[1060];
    snprintf(metafile, sizeof(metafile), "%s.meta", filename); // e.g., "output.mkv.meta"

    FILE* f = fopen(metafile, "wb");
    if (!f) {
        perror("Failed to save progress");
        return;
    }

    fwrite(completed_chunks, sizeof(char), MAX_CHUNKS, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

void* download_worker(void* arg) {
    downloadcontext* ctx = (downloadcontext*)arg;

    while (1) {
        Chunk chunk;
        if (!dequeue_chunk(&chunk)) break;

        int retries = MAX_RETRIES;
        while (retries-- > 0) {
            CURL* curl = curl_easy_init();
            if (!curl) continue;

            char range[64];
            snprintf(range, sizeof(range), "%ld-%ld", chunk.start, chunk.end);

            FILE* fp = fopen(ctx->filename, "rb+");
            if (!fp) {
                curl_easy_cleanup(curl);
                continue;
            }
            fseek(fp, chunk.start, SEEK_SET);

            curl_easy_setopt(curl, CURLOPT_URL, ctx->url);
            curl_easy_setopt(curl, CURLOPT_RANGE, range);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            CURLcode res = curl_easy_perform(curl);

            if (res == CURLE_OK) {
                fflush(fp);
                fsync(fileno(fp));
                fclose(fp);
                curl_easy_cleanup(curl);

                usleep(1000);

                int chunk_index = chunk.start / CHUNK_SIZE;
                completed_chunks[chunk_index] = 1;
                save_progress(ctx->filename);
                break;
            }
            else {
                fclose(fp);
                curl_easy_cleanup(curl);
                if (retries == 0)
                fprintf(stderr, "Chunk %ld-%ld failed after retries.\n", chunk.start, chunk.end);
            }

        }
    }

    pthread_exit(NULL);
}

void* show_progress(void* arg) {
    const int bar_width = 30;
    long prev_downloaded = 0;
    time_t start_time = time(NULL);

    while (1) {
        pthread_mutex_lock(&progressMutex);
        long downloaded = totalDownloaded;
        pthread_mutex_unlock(&progressMutex);

        if (totalSize <= 0) {
            printf("\rWaiting for file size to initialize...\n");
            sleep(1);
            continue;
        }

        long elapsed = time(NULL) - start_time;
        if (elapsed == 0) elapsed = 1;

        long speed = downloaded / elapsed;
        long remaining = totalSize - downloaded;
        if (remaining < 0) remaining = 0;

        long eta = (speed > 0) ? (remaining / speed) : 0;

        int percent = (int)((downloaded * 100) / totalSize);
        int filled = (bar_width * percent) / 100;

        // Draw bar
        printf("\rProgress: [");
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) printf("=");
            else printf("-");
        }
        printf("] %d%%", percent);
        printf(" | Speed: %.2f MB/s", speed / (1024.0 * 1024.0));
        printf(" | ETA: %02ld:%02ld", eta / 60, eta % 60);

        fflush(stdout);

        if (downloaded >= totalSize) break;
        sleep(1);
    }

    printf("\nDownload complete!\n");
    pthread_exit(NULL);
}



int determine_thread_count(long filesize_bytes) {
    if (filesize_bytes < 10 * 1024 * 1024) return 2;
    else if (filesize_bytes < 50 * 1024 * 1024) return 4;
    else if (filesize_bytes < 200 * 1024 * 1024) return 8;
    else if (filesize_bytes < 500 * 1024 * 1024) return 12;
    else return 16;
}

void load_progress(const char* filename) {
    char metafile[1060];
    snprintf(metafile, sizeof(metafile), "%s.meta", filename);

    FILE* f = fopen(metafile, "rb");
    if (!f) {
        printf("No existing .meta file found. Starting fresh.\n");
        return;
    }

    fread(completed_chunks, sizeof(char), MAX_CHUNKS, f);
    fclose(f);
    printf("Resuming from saved progress.\n");
}



int main(int argc, char* argv[]) {
    char url[2048];
    char filename[1024] = "output.mkv";

    if (argc < 2) {
        printf("Usage: %s <url> [filename]\n", argv[0]);
        return 1;
    }

    strcpy(url, argv[1]);

    if (argc >= 3) {
        strcpy(filename, argv[2]);
    }

    totalSize = get_file_size(url);
    if (totalSize <= 0) {
        fprintf(stderr, "Failed to get file size.\n");
        return 1;
    }

    load_progress(filename);

    totalDownloaded = 0;
    for (long i = 0; i < totalSize; i += CHUNK_SIZE) {
        int chunk_index = i / CHUNK_SIZE;
        if (completed_chunks[chunk_index] == 1) {
            long end = (i + CHUNK_SIZE > totalSize) ? totalSize - 1 : i + CHUNK_SIZE - 1;
            totalDownloaded += (end - i + 1);
        }
    }

    int thread_count = determine_thread_count(totalSize);
    printf("Total size: %ld bytes\n", totalSize);
    printf("Using %d threads\n", thread_count);

    FILE* f = fopen(filename, "wb");    
    if (!f) {
        perror("Failed to create file");
        return 1;
    }
    fseek(f, totalSize - 1, SEEK_SET);
    fputc('\0', f);
    fclose(f);

    pthread_mutex_init(&queueMutex, NULL);
    pthread_mutex_init(&progressMutex, NULL);

    for (long i = 0; i < totalSize; i += CHUNK_SIZE) {
    int chunk_index = i / CHUNK_SIZE;
    if (completed_chunks[chunk_index] == 1) {
        continue;
    }

    long end = (i + CHUNK_SIZE > totalSize) ? totalSize - 1 : i + CHUNK_SIZE - 1;
    enqueue_chunk(i, end);
}


    pthread_t threads[MAX_THREADS], progressThread;
    downloadcontext ctx;
    strcpy(ctx.url, url);
    strcpy(ctx.filename, filename);

    pthread_create(&progressThread, NULL, show_progress, NULL);

    for (int i = 0; i < thread_count; ++i) {
        pthread_create(&threads[i], NULL, download_worker, &ctx);
    }

    for (int i = 0; i < thread_count; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_join(progressThread, NULL);

    save_progress(filename);

    pthread_mutex_destroy(&queueMutex);
    pthread_mutex_destroy(&progressMutex);

    printf("File saved as: %s\n", filename);
    return 0;
}
