#pragma once

// Forward declaration — avoids pulling SDL2 headers into every file that includes Engine.h.
// Only Engine.cpp needs to know the internals of SDL_Window.
struct SDL_Window;

class Engine
{
  public:
    Engine();
    ~Engine();

    bool init(const char* title, int width, int height);
    void run();
    void shutdown();

  private:
    void processEvents();
    void update(double dt);
    void render();

    SDL_Window* window = nullptr;
    void* glContext = nullptr;
    bool running = false;
};
