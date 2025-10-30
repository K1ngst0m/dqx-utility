#include "app/Application.hpp"

int main(int argc, char** argv)
{
    // Normal application mode
    return Application{argc, argv}.run();
}
