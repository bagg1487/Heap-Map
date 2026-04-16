#pragma once

#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <cstdint>

#include <GL/glew.h>

struct TileKey {
    int z, x, y;

    bool operator<(const TileKey& o) const {
        if (z != o.z) return z < o.z;
        if (x != o.x) return x < o.x;
        return y < o.y;
    }
};

struct Tile {
    GLuint tex = 0;
    bool ready = false;
    bool loading = false;

    int w = 256;
    int h = 256;

    std::vector<unsigned char> rgba;
};

struct Job {
    int z, x, y;
};

class TileManager {
public:
    TileManager();
    ~TileManager();

    Tile* get(int z, int x, int y);
    void updateGL();

private:
    void worker();
    void request(int z, int x, int y);

    std::map<TileKey, Tile> tiles;
    std::mutex mtx;

    std::queue<Job> jobs;
    std::mutex job_mtx;
    std::condition_variable cv;

    bool running = true;
    std::thread th;
};