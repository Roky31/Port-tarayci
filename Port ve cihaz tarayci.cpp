#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <regex>
#include <map>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

std::mutex mtx;
std::condition_variable cv;
std::queue<std::string> ipQueue;
bool done = false;
std::ofstream dosya("sonuc.txt");
std::map<std::string, std::string> macAdlari;

bool portAcikMi(const std::string& ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(port);

    DWORD timeout = 300;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    bool result = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0);
    closesocket(sock);
    return result;
}

void arpTaramasi() {
    system("arp -a > maclist.txt");
    std::ifstream macFile("maclist.txt");
    std::string line;
    std::regex rgx("(\\d+\\.\\d+\\.\\d+\\.\\d+)\\s+([a-fA-F0-9:-]+)");
    std::smatch match;

    while (std::getline(macFile, line)) {
        if (std::regex_search(line, match, rgx)) {
            macAdlari[match[1]] = match[2];
        }
    }
    macFile.close();
}

void worker() {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] { return !ipQueue.empty() || done; });

        if (ipQueue.empty() && done)
            return;

        std::string ip = ipQueue.front();
        ipQueue.pop();
        lock.unlock();

        bool aktif = portAcikMi(ip, 80) || portAcikMi(ip, 443);

        if (aktif) {
            std::lock_guard<std::mutex> yazKilidi(mtx);
            dosya << "Aktif IP: " << ip;
            if (macAdlari.count(ip)) {
                dosya << " | MAC: " << macAdlari[ip];
            }
            dosya << "\n";

            for (int port : {22, 23, 53, 80, 443, 8080, 3389}) {
                if (portAcikMi(ip, port)) {
                    dosya << "    Acik Port: " << port << "\n";
                }
            }
            dosya << "\n";
            std::cout << "[+] Cihaz bulundu: " << ip << "\n";
        }
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "ARP taramasi yapiliyor...\n";
    arpTaramasi();
    std::cout << "Tarama basliyor...\n";

    for (int i = 1; i <= 254; ++i) {
        ipQueue.push("192.168.1." + std::to_string(i));
    }

    const int threadSayisi = 30;
    std::vector<std::thread> threadler;

    for (int i = 0; i < threadSayisi; ++i) {
        threadler.emplace_back(worker);
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
    }
    cv.notify_all();

    for (auto& th : threadler) {
        th.join();
    }

    dosya.close();
    WSACleanup();

    std::cout << "\nTarama tamamlandi. 'sonuc.txt' dosyasina yazildi.\n";
    return 0;
}