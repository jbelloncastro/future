
#include "future.h"
#include <iostream>

int main( int argc, char* argv[] ) {
    promise<int> p;
    future<int>  f = p.get_future();

    f.then([](int v) {
        std::cout << "Value is " << v << "\n";
    })
    .then([]() {
        std::cout << "I'm done here!\n";
    });

    p.set_value(5);

    return (f.get() == 5)? 0 : 1;
}
