#include "Application.hpp"

int main(int argc, char** argv)
{
    Application app{argc, argv};
    if (!app.initialize())
    {
        return 1;
    }
    
    return app.run();
}
