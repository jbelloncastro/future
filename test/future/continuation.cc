
#include "future.h"

#include <gtest/gtest.h>
 
TEST (IntPromise, Chain_Resolved) {
    promise<int> intp;
    future<int>  intf = intp.get_future();

    int set = 55;
    bool success = true;
    intp.set_value(set);

    int times = 2;
    for( int times = 2; times > 0; times-- ) {
        intf = intf.then([&]( int i ) -> int {
            success &= (set == i);
            return i;
        });
    }
    ASSERT_TRUE(success);
}

