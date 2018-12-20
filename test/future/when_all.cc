
#include "future.h"

#include <gtest/gtest.h>

TEST (WhenAll, Basic) {
    promise<int> intp;
    std::vector<shared_future<int>> futures( 10, intp.get_future() );

    int value = 55;

    auto all_good = when_all(futures.begin(), futures.end());

    EXPECT_FALSE( all_good.is_ready() );

    intp.set_value(value);

    for( auto& f: futures )
        EXPECT_TRUE( f.is_ready() );

    EXPECT_TRUE( all_good.is_ready() );
}

