#include <iostream>
#include <fstream>
#include <string>
#include <csignal>
#include <atomic>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno> // *** NEW: for errno

using namespace std;

atomic<bool> aborted(false);

// Escape string safely for JSON
string jsonEscape(const string &input) {
    string output;
    for (char c : input) {
        switch (c) {
            case '\"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    output += buf;
                } else {
                    output += c;
                }
        }
    }
    return output;
}

// Get clean timestamp (no newlines)
string currentTime() {
    time_t t = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return string(buf);
}

// Handle SIGINT (Ctrl+C)
void signalHandler(int) {
    aborted.store(true);
    cerr << "\nAborting wipe safely..." << endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <device> <method>\n";
        cerr << "method 1 = Zero-fill, 2 = Random overwrite\n";
        return 1;
    }

    string device = argv[1];
    int method = stoi(argv[2]);

    signal(SIGINT, signalHandler);

    int fd = open(device.c_str(), O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    const size_t bufsize = 4096;
    char *buffer = new char[bufsize];
    if (method == 1) memset(buffer, 0, bufsize);

    cerr << "Starting wipe of " << device
         << " using " << (method == 1 ? "Zero-fill" : "Random overwrite") << endl;

    // *** NEW: wipe first and last MB to remove partition table (MBR/GPT)
    const size_t header_size = 1024 * 1024; // 1 MB
    char *header = new char[header_size];
    memset(header, 0, header_size);

    if (pwrite(fd, header, header_size, 0) < 0) {
        perror("Failed to wipe beginning of disk");
    }

    off_t disk_size = lseek(fd, 0, SEEK_END);
    if (disk_size > header_size) {
        if (pwrite(fd, header, header_size, disk_size - header_size) < 0) {
            perror("Failed to wipe end of disk");
        }
    }
    delete[] header;

    lseek(fd, header_size, SEEK_SET); // start main wipe after first MB

    ssize_t written;
    while (!aborted.load()) {
        if (method == 2) {
            for (size_t i = 0; i < bufsize; i++) buffer[i] = rand() % 256;
        }
        written = write(fd, buffer, bufsize);
        if (written < 0) {
            if (errno == ENOSPC) { // *** NEW: disk full = success
                cerr << "Reached end of disk." << endl;
                break;
            } else {
                perror("Write error");
                ofstream ofs("wipe_log.json");
                if (ofs.is_open()) {
                    ofs << "{\n";
                    ofs << "  \"device\": \"" << jsonEscape(device) << "\",\n";
                    ofs << "  \"method\": \"" << (method==1 ? "Zero-fill" : "Random") << "\",\n";
                    ofs << "  \"status\": \"FAILED\",\n";
                    ofs << "  \"end_time\": \"" << currentTime() << "\"\n";
                    ofs << "}\n";
                }
                ofs.close();
                delete[] buffer;
                close(fd);
                return 1;
            }
        }
    }

    delete[] buffer;
    close(fd);

    // Write final JSON log
    ofstream ofs("wipe_log.json");
    if (ofs.is_open()) {
        ofs << "{\n";
        ofs << "  \"device\": \"" << jsonEscape(device) << "\",\n";
        ofs << "  \"method\": \"" << (method==1 ? "Zero-fill (Quick)" : "Random overwrite (1 pass)") << "\",\n";
        if (aborted.load()) ofs << "  \"status\": \"ABORTED\",\n";
        else ofs << "  \"status\": \"SUCCESS\",\n";
        ofs << "  \"end_time\": \"" << currentTime() << "\"\n";
        ofs << "}\n";
        ofs.close();
    }

    cerr << "Wipe complete for " << device << endl;
    return 0;
}
