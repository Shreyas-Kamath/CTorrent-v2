#include "Client.hpp"

int main() {
    try {
        Client c;
        c.run();
    }

    catch (const std::exception& ex) {
        std::println("{}", ex.what());
    }
}