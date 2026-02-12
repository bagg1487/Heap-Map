import zmq

# Создаем контекст и REQ-сокет (как в сервере)
context = zmq.Context()
client_socket = context.socket(zmq.REQ)  # REQ, а не обычный socket!

# Подключаемся к серверу
client_socket.connect('tcp://localhost:8080')

# Отправляем сообщение
client_socket.send_string('Hello, server!')

# Получаем ответ (REQ-REP требует получения ответа!)
reply = client_socket.recv_string()
print(f"Ответ сервера: {reply}")

client_socket.close()
context.term()