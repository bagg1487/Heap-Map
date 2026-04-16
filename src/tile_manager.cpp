#include "tile_manager.hpp"
#include <stb_image.h>
#include <curl/curl.h>
#include <iostream>

static size_t write_cb(void* data, size_t size, size_t nmemb, void* userp) {
    auto* vec = (std::vector<unsigned char>*)userp;
    size_t total = size * nmemb;
    vec->insert(vec->end(), (unsigned char*)data, (unsigned char*)data + total);
    return total;
}

static std::string url(int z, int x, int y) {
    return "https://tile.openstreetmap.org/" +
           std::to_string(z) + "/" +
           std::to_string(x) + "/" +
           std::to_string(y) + ".png";
}

TileManager::TileManager() {
    th = std::thread(&TileManager::worker, this);
}

TileManager::~TileManager() {
    running = false;
    cv.notify_all();
    if (th.joinable()) th.join();

    for (auto& [k, t] : tiles) {
        if (t.tex) glDeleteTextures(1, &t.tex);
    }
}

Tile* TileManager::get(int z, int x, int y) {
    TileKey k{z, x, y};

    {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = tiles.find(k);
        if (it != tiles.end())
            return &it->second;

        tiles.emplace(k, Tile{});
    }

    request(z, x, y);

    std::lock_guard<std::mutex> lock(mtx);
    return &tiles[k];
}

void TileManager::request(int z, int x, int y) {
    std::lock_guard<std::mutex> lock(mtx);

    TileKey k{z, x, y};
    if (tiles[k].loading || tiles[k].ready)
        return;

    tiles[k].loading = true;

    {
        std::lock_guard<std::mutex> qlock(job_mtx);
        jobs.push({z, x, y});
    }

    cv.notify_one();
}

void TileManager::worker() {
    CURL* curl = curl_easy_init();

    while (running) {
        Job j;

        {
            std::unique_lock<std::mutex> lock(job_mtx);
            cv.wait(lock, [&] { return !jobs.empty() || !running; });

            if (!running) break;

            j = jobs.front();
            jobs.pop();
        }

        std::vector<unsigned char> data;

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url(j.z, j.x, j.y).c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "MyGpsMonitorApp/1.0 (kutenand2@gmail.com)");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_perform(curl);

        int w, h, c;
        unsigned char* img = stbi_load_from_memory(
            data.data(), data.size(), &w, &h, &c, 4
        );

        if (!img) continue;

        std::lock_guard<std::mutex> lock(mtx);

        TileKey k{j.z, j.x, j.y};
        auto& t = tiles[k];

        t.w = w;
        t.h = h;
        t.rgba.assign(img, img + w * h * 4);
        t.ready = true;
        t.loading = false;

        stbi_image_free(img);
    }

    curl_easy_cleanup(curl);
}

void TileManager::updateGL() {
    std::lock_guard<std::mutex> lock(mtx);

    for (auto& [k, t] : tiles) {
        if (!t.ready || t.tex || t.rgba.empty())
            continue;

        glGenTextures(1, &t.tex);
        glBindTexture(GL_TEXTURE_2D, t.tex);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     t.w, t.h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     t.rgba.data());

        t.rgba.clear();
    }
}