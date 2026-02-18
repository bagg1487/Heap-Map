#include "server.hpp"
#include <thread>

using namespace std;

int main() {
    SharedData shared;
    
    thread gui_thread(run_gui, &shared);
    thread server_thread(run_server, &shared);
    
    gui_thread.join();
    server_thread.join();
    
    return 0;
}