#include <zmq.hpp>
#include <iostream>
#include <string>
using namespace std;
using namespace zmq;

int main() {
    string server_ip = "172.25.100.191";
    int port = 8080;
    
    cout << "Пытаюсь подключиться к " << server_ip << ":" << port << endl;
    
    try {
        context_t context(1);
        socket_t socket(context, socket_type::req);
        
        cout << "Создан сокет, коннекчусь..." << endl;
        socket.connect("tcp://" + server_ip + ":" + to_string(port));
        
        cout << "Отправляю ping..." << endl;
        string request = "ping";
        socket.send(request.c_str(), request.size());
        
        cout << "Жду ответ..." << endl;
        message_t reply;
        socket.recv(&reply);
        
        string response(static_cast<char*>(reply.data()), reply.size());
        cout << "Получен ответ: " << response << endl;
        
        if (response == "pong") {
            cout << "✅ УСПЕШНО! Сервер отвечает!" << endl;
        } else {
            cout << "❌ Неожиданный ответ: " << response << endl;
        }
        
    } catch (const exception& e) {
        cout << "❌ Ошибка: " << e.what() << endl;
    }
    
    return 0;
}