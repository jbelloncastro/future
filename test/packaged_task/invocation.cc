
#include "future.h"

#include <gtest/gtest.h>
 
TEST (PackagedTask, Default) {
    packaged_task<int(int)> itask( [](int i ) -> int {
        return i;
    });

    future<int> ifut = itask.get_future();

    int set = 55;
    bool success = false;
    
    itask(set);

    ASSERT_EQ(ifut.get(), set);
}

