/**********************************************************
* Copyright 2025 Andrea Sorato.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, 
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
* OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This file is part of iuni-ljus. Official website: iuni-ljus.org . 
***********************************************************/

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <map>
#include <ctime>
#include <thread>
#include <sstream>
#include <chrono>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <functional>
#include <arpa/inet.h>
#include <cstring>
#include <mutex>

using namespace std;

/* VERSION 1.1.0 - 2025.04.25 */
//  // Server
//	TcpServer tcps(8000);
//	
//	tcps.pick([](string req, TcpServer::Response res) -> void {
//		res.send("Hello Cranjis!");
//	});
//
//  tcps.start();
//
//  // Client
//  string response;
//  TcpClient::send("127.0.0.1", 8000, "This is a send test", response);
//  cout << "Response: " << response << endl;


class TcpServer {
public:
	struct Request { 
	    string body;
	    Request(string body) : body(body) {}
	};
	
	class SocketPool {
		private:
			mutex socket_pool_mtx;
			map<int, int> socket_pool;
		public:
			void set(int socket) {
				lock_guard<mutex> lg(socket_pool_mtx);
				socket_pool[socket];
			}
			
			void del(int socket) {
				lock_guard<mutex> lg(socket_pool_mtx);
				socket_pool.erase(socket);
			}
			
			void print() {
				lock_guard<mutex> lg(socket_pool_mtx);
				cout << "> ";
				int count = 0;
				for (auto& i : socket_pool) {
					cout << i.first;
					if (count + 1 != socket_pool.size()) cout << " - ";
					count++;
				}
				cout << endl;
			}
	};
	
	class Response {
		int socket;
	public:
		Response(int socket) : socket(socket) {};

		void send(string content) {
		    const char* c_content = content.c_str();
//			cout << "Sending to " << socket << " {" << c_content << "}\n";
			int bytes_sent = ::send(socket, c_content, strlen(c_content), MSG_NOSIGNAL); //Send the response to the client
//			cout << "byte sent: " << bytes_sent << endl;
			if (bytes_sent == -1) {
				// error or unreachable
			}
		}
	};

private:
	const static int MAX_BUFFER_SIZE = 1024;
	function<void(string, Response)> onPick = [](string a, Response b) -> void {};
	
    int doWork(function<void()>, bool);
    void handle_client(int);
	int PORT = 8080;
	bool nonThreaded = false;

	SocketPool socket_pool;
	int server_fd = -1;
public:
	
	TcpServer(int port) {
		PORT = port;
	}
	
	TcpServer(int port, int nonThreaded)  {
		PORT = port;
		nonThreaded = nonThreaded;
	}
	
	int getPort() {
		return PORT;
	}
	
    void pick(function<void(string, Response)> func) {
//    	auto doAndClose = [func](string a, Response b){
//    		func(a, b);
////    		b.end();
//    	};
		onPick = func;
//		onPick = doAndClose;
    }

	void start (function<void()> onLoad) {
		doWork(onLoad, false);
	}

	void start (function<void()> onLoad, bool waitToConnect) {
		doWork(onLoad, waitToConnect);
	}

    void start() {
        doWork([](){}, false);
    }
    
    int quit() {
    	if (server_fd == -1) return 1;
    	close(this->server_fd);
    	return 0;
    }
};

//bool TcpServer::isDataAvailable(int socket) {
//    fd_set readfds;
//    FD_ZERO(&readfds);
//    FD_SET(socket, &readfds);
//
//    struct timeval timeout;
//    timeout.tv_sec = 0;
//    timeout.tv_usec = 0;
//
//    int result = select(socket + 1, &readfds, NULL, NULL, &timeout);
//    if (result == -1) {
//        cerr << "Error in select" << std::endl;
//        return false;
//    }
//    if (result == 0) {
//        // No data available
//        return false;
//    }
//    // Data is available
//    return true;
//}

void TcpServer::handle_client(int client_socket) {
    char buffer[MAX_BUFFER_SIZE];

	int bytes_read = read(client_socket, buffer, MAX_BUFFER_SIZE); // put recv instead? TODO
//    cout << "Bytes read: " << bytes_read << endl;
	if (bytes_read <= 0) { // Client closed the connection or generic error occurred
		return;
    }
	string str_buffer = string(buffer, bytes_read);
	
//    cout << "Readed buffer: " << "[" << str_buffer << "]" << endl;   
	onPick(str_buffer, Response(client_socket));
}
 
 
int TcpServer::doWork(function<void()> onLoad, bool waittoconnect) {
    cout << "Starting TCP server at port " << this->PORT << endl;

    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the network address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

TRYBINDING:
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        if (errno == EADDRINUSE) {
        	cerr << "Error: Port " << this->PORT << " is already in use." << endl;
    	} else {
			perror("bind failed");
		}
		
	    if (waittoconnect) {
			cout << "Binding failed. Retrying soon...\n";
			this_thread::sleep_for(std::chrono::milliseconds(2000));
			goto TRYBINDING;
		}
		
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 20) < 0) {
        perror("listen"); // use cerr instead TODO
        close(server_fd);
        exit(EXIT_FAILURE);
    }

	onLoad();

	class Counter {
		int max_tolerated = 0;
		int counter = 0;
		mutex mtx;
		
	public:
		Counter(const int max) {
			max_tolerated = max;
		}
		void add() {
		RETRY:
			mtx.lock();
			if (counter == max_tolerated) {
				mtx.unlock();
				this_thread::sleep_for(chrono::milliseconds(100));
				goto RETRY;
			}
			counter++;
			mtx.unlock();
		}
		
		void sub() {
			mtx.lock();
			if (counter > 0) counter--;
			mtx.unlock();
		}
		
		int get() {
			mtx.lock();
			int r = counter;
			mtx.unlock();
			return r;
		}			
	};
	
	const int MAX_THREADS = 20;
	Counter threads_counter(MAX_THREADS);

	auto dealer = [&](int client_socket){ // TODO del pointer
		socket_pool.set(client_socket);
//		socket_pool.print();

		handle_client(client_socket);
		close(client_socket);

		socket_pool.del(client_socket);
//		socket_pool.print();
		
		threads_counter.sub();
	};

	auto flistener = [&](){
		while (true) {
			int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
			if (nonThreaded) {
				dealer(client_socket);	
			} else {
				threads_counter.add();
				thread thr(dealer, client_socket);
				thr.detach();
			}
		}
	};

//	auto flog = [&](){
//		while (true) {
//			this_thread::sleep_for(chrono::milliseconds(500));
//			socket_pool.print();
//		}
//	};

	thread tlistener(flistener);
//	thread tlog(flog);

	tlistener.join();
//	tlog.join();
	
    // Close the server socket
    close(server_fd);
    return 0;
}

class TcpClient {
	static bool isDataAvailable(int socket) {
	    fd_set readfds;
	    FD_ZERO(&readfds);
	    FD_SET(socket, &readfds);
	
	    struct timeval timeout;
	    timeout.tv_sec = 0;
	    timeout.tv_usec = 0;
	
	    int result = select(socket + 1, &readfds, NULL, NULL, &timeout);
	    if (result == -1) {
	        cerr << "Error in select" << std::endl;
	        return false;
	    }
	    if (result == 0) {
	        // No data available
	        return false;
	    }
	    // Data is available
	    return true;
	}
	
public:
	static int send(const std::string& serverIP, int serverPort, const std::string& data, std::string& response) {
	    // Create a TCP socket
	    int socketFd = socket(AF_INET, SOCK_STREAM, 0);
		if (socketFd == -1) {
	        std::cerr << "Error creating socket" << std::endl;
	        return -1;
	    }
	
	    // Set up the server address
	    struct sockaddr_in serverAddr;
	    std::memset(&serverAddr, 0, sizeof(serverAddr));
	    serverAddr.sin_family = AF_INET;
	    serverAddr.sin_port = htons(serverPort);
	    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) != 1) {
	        std::cerr << "Invalid server IP address" << std::endl;
	        close(socketFd);
	        return -1;
	    }
	
	    // Connect to the server
	    if (connect(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
	        std::cerr << "Error connecting to server" << std::endl;
	        close(socketFd);
	        return -1;
	    }
	
	    // Send the data
	    ssize_t bytesSent = ::send(socketFd, data.c_str(), data.length(), 0);
	    if (bytesSent == -1) {
	        std::cerr << "Error sending data" << std::endl;
	        close(socketFd);
	        return -1;
	    }
//	    cout << "{" << data.c_str() << "}" << endl;
//		cout << "Bytes sent " << bytesSent << endl;

	    // Receive the response
	    char buffer[1024];
	    ssize_t bytesReceived = 0;
	    int iter = 0;
	    int MAX_ITER = 1024 * 50; // aka 50 MB
	    do {
			bytesReceived = recv(socketFd, buffer, sizeof(buffer) - 1, 0); // put timeout TODO
			if (bytesReceived <= -1) {
		        std::cerr << "Error receiving data" << std::endl;
		        goto END;
		    }

			if (bytesReceived < 1024)
				buffer[bytesReceived] = '\0';
				
		    response += string(buffer);
		    iter++;
		} while (iter < MAX_ITER and isDataAvailable(socketFd));
	    
END:
		close(socketFd); // Close the socket
	
	    return static_cast<int>(bytesSent);
	}
	
	static int send(const std::string& serverIP, int serverPort, const std::string& data) {
		string hidden_response;
		return send(serverIP, serverPort, data, hidden_response);
	}
};
