
#include "future.h"

int main( int argc, char* argv[] ) {
    promise<int> p;
    future<int>  f = p.get_future();

    p.set_value(5);

    return (f.get() == 5)? 0 : 1;
}
