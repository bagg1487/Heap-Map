import zmq

filename = "android_data.txt"
counter = 0

context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind("tcp://*:8080")

print("Сервер запущен на порту 8080")

try:
    while True:
        message = socket.recv_string()
        print(f"Получено: {message}")

        if message == "show":
            print("ВСЕ СОХРАНЕННЫЕ ДАННЫЕ:")
            try:
                with open(filename, "r", encoding="utf-8") as f:
                    all_data = f.read()
                    if all_data:
                        print(all_data)
                    else:
                        print("Файл пуст")
                        all_data = "Файл пуст"
            except FileNotFoundError:
                print("Файл не существует")
                all_data = "Файл не существует"

            socket.send_string(all_data)
            print("Данные отправлены клиенту")

        else:
            counter += 1

            with open(filename, "a", encoding="utf-8") as f:
                f.write(f"Пакет #{counter}: {message}\n")


            response = f"hello from server (пакет #{counter})"
            socket.send_string(response)
            print(f"Отправлено: {response}")

except KeyboardInterrupt:
    print("\nСервер остановлен")
finally:
    socket.close()
    context.term()
