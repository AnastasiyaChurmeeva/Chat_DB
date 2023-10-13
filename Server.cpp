#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <winsock2.h>
#include <vector>
#include <thread>

#include <mysql_driver.h>
#include <mysql_connection.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int BUFFER_SIZE = 1024;

struct Client
{
    SOCKET socket;
    string username;
};

vector<Client> clients;
vector<string> messageHistory;

void error(const char* msg)
{
    cerr << msg << endl;
    exit(1);
}

void broadcastMessage(const string& message, const string& sender)
{
    messageHistory.push_back(sender + ": " + message);

    try
    {
        sql::mysql::MySQL_Driver* driver;
        sql::Connection* con;
        sql::Statement* stmt;

        driver = sql::mysql::get_mysql_driver_instance();
        con = driver->connect("tcp://127.0.0.1:3306", "root", "root"); // Замените "username" и "password" на ваши учетные данные для доступа к MySQL

        stmt = con->createStatement();
        stmt->execute("USE chat"); // Замените "chat" на имя вашей базы данных

        // Сохраняем сообщение в базе данных
        stmt->execute("INSERT INTO messages (sender, message) VALUES ('" + sender + "', '" + message + "')");

        delete stmt;
        delete con;
    }
    catch (sql::SQLException& e)
    {
        cerr << "Oshibka chteniya MySQL: " << e.what() << endl;
    }

    // Отправляем сообщение клиентам
    for (const Client& client : clients)
    {
        if (client.username != sender)
        {
            send(client.socket, (sender + ": " + message).c_str(), message.size() + sender.size() + 2, 0);
        }
    }
}

void clientHandler(Client client)
{
    char clientMessage[BUFFER_SIZE];
    while (true)
    {
        int bytesRead = recv(client.socket, clientMessage, BUFFER_SIZE, 0);
        if (bytesRead <= 0)
        {
            cout << "Klient " << client.username << " otklyuchilsya." << endl;
            closesocket(client.socket);
            break;
        }

        clientMessage[bytesRead] = '\0';
        string message = clientMessage;
        cout << "Klient " << client.username << ": " << message << endl;

        if (message == "exit")
        {
            cout << "Klient " << client.username << " vushel iz chata." << endl;
            closesocket(client.socket);
            break;
        }

        broadcastMessage(message, client.username);
    }

    // Удаление клиента из списка
    for (auto it = clients.begin(); it != clients.end(); ++it)
    {
        if (it->socket == client.socket)
        {
            clients.erase(it);
            break;
        }
    }
}

int main() {
    try
    {
        sql::mysql::MySQL_Driver* driver;
        sql::Connection* con;

        driver = sql::mysql::get_mysql_driver_instance();
        con = driver->connect("tcp://127.0.0.1:3306", "root", "root"); // Замените "username" и "password" на ваши учетные данные для доступа к MySQL

        sql::Statement* stmt = con->createStatement();

        // Создаем базу данных и таблицу, если их не существует
        stmt->execute("CREATE DATABASE IF NOT EXISTS chat");
        stmt->execute("USE chat");
        stmt->execute("CREATE TABLE IF NOT EXISTS messages (id INT PRIMARY KEY AUTO_INCREMENT, sender VARCHAR(255), message TEXT)");

        delete stmt;
        delete con;
    }
    catch (sql::SQLException& e)
    {
        cerr << "Oshibka chteniya MySQL: " << e.what() << endl;
        WSACleanup();
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        error("Oshibka pri inicializacii Winsock");
    }

    SOCKET serverSocket;
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET)
    {
        error("Oshibka pri sozdanii soketa");
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); // Порт сервера
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        error("Oshibka pri privyazke soketa k portu");
    }

    if (listen(serverSocket, 5) == SOCKET_ERROR)
    {
        error("Oshibka pri proslushivanii porta");
    }

    cout << "Server zapushen. Ozhidanie klientov..." << endl;

    while (true)
    {
        SOCKET clientSocket;
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);

        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET)
        {
            cerr << "Oshibka pri prinyatii soedineniya s klientom" << endl;
            continue;
        }

        char username[BUFFER_SIZE];
        int bytesRead = recv(clientSocket, username, BUFFER_SIZE, 0);
        if (bytesRead <= 0)
        {
            cerr << "Oshibka pri chtenii imeni klienta" << endl;
            closesocket(clientSocket);
            continue;
        }
        username[bytesRead] = '\0';

        Client client;
        client.socket = clientSocket;
        client.username = username;

        cout << "Klient " << client.username << " podklyuchilsya." << endl;
        clients.push_back(client);

        thread clientThread(clientHandler, client);
        clientThread.detach(); // Отсоединяем поток клиента
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
