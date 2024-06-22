#include <iostream>
#include <string>
#include <map>
#include <rocksdb/db.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <thread>

#define BUFFER_SIZE 1024

void handle_client(int client_socket, rocksdb::DB* db) {
    bool stop = false;
    char buffer[BUFFER_SIZE] = {0};
    std::string response;

    while (!stop) {
        int valread = read(client_socket, buffer, BUFFER_SIZE);
        if (valread <= 0) break;

        std::string command(buffer, valread);
        std::istringstream iss(command);
        std::string operation;
        iss >> operation;

        if (operation == "SET") {
            std::string key, value;
            iss >> key >> value;
            rocksdb::Status status = db->Put(rocksdb::WriteOptions(), key, std::move(value));
            response = status.ok() ? "OK\n" : "FAIL\n";
        } else if (operation == "GET") {
            std::string key, value;
            iss >> key;
            rocksdb::Status status = db->Get(rocksdb::ReadOptions(), key, &value);
            response = status.ok() ? std::move(value) + "\n" : "NOT_FOUND\n";
        } else if (operation == "SCAN") {
            std::string start_key, end_key;
            iss >> start_key >> end_key;
            std::ostringstream oss;
            auto it = db->NewIterator(rocksdb::ReadOptions());
            for (it->Seek(start_key); it->Valid() && it->key().ToString() <= end_key; it->Next()) {
                oss << it->key().ToString() << " : " << it->value().ToString() << "\n";
            }
            delete it;
            response = oss.str();
        } else if (operation == "QUIT") {
            response = "Server shutting down\n";
            stop = true;
        } else {
            response = "UNKNOWN COMMAND\n";
        }

        send(client_socket, response.c_str(), response.size(), 0);
    }

    close(client_socket);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <path_to_db>" << std::endl;
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string db_path = argv[2];
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db);

    if (!status.ok()) {
        std::cerr << "Unable to open/create database: " << status.ToString() << std::endl;
        return 1;
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket failed" << std::endl;
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "Setsockopt failed" << std::endl;
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }

    std::cout << "Server started on port " << port << std::endl;

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            std::cerr << "Accept failed" << std::endl;
            return 1;
        }

        std::cout << "New connection established" << std::endl;
        std::thread(handle_client, new_socket, db).detach();
    }

    delete db;
    return 0;
}
