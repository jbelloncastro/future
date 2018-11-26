
#include "future.h"

#include <gtest/gtest.h>

TEST (IntPromise, One2Many_NoShared) {
    promise<int> intp;
    future<int>  intf = intp.get_future();
    std::vector<future<bool>> continuations;

    int value = 55;
    bool success = false;

    // After future creation, both promise and future should be valid
    EXPECT_TRUE ( intf.valid() );

    // First then() should go all right
    ASSERT_NO_THROW(
        continuations.push_back(
            intf.then([=](int i) -> bool {
                return i == value;
            })
        )
    );

    // After first then(), first future // should not be valid any more
    EXPECT_FALSE( intf.valid() );

    // Since intf is not valid, queueing more continuations should fail with
    // future_error
    ASSERT_THROW(
        continuations.push_back(
            intf.then([=](int i) -> bool {
                return i == value;
            })
        ),
        future_error
    );
}

TEST (IntPromise, One2Many_Shared) {
    promise<int> intp;
    shared_future<int> intf = intp.get_future().share();
    std::vector<future<bool>> continuations;

    int value = 55;
    bool success = false;

    // After future creation, future should be valid
    EXPECT_TRUE ( intf.valid() );

    // First then() should go all right
    ASSERT_NO_THROW(
        continuations.push_back(
            intf.then([=](int i) -> bool {
                return i == value;
            })
        )
    );

    // After first then(), since future is shared, it should still be valid.
    // Newly created future should be valid as well.
    EXPECT_TRUE ( intf.valid() );
    EXPECT_TRUE ( continuations.back().valid() );

    // Since intf is valid, queueing more continuations should not fail with
    // any future_error
    ASSERT_NO_THROW(
        continuations.push_back(
            intf.then([=](int i) -> bool {
                return i == value;
            })
        )
    );

    // After second then(), future should still be valid.
    // Newly created future should be valid as well.
    EXPECT_TRUE ( intf.valid() );
    EXPECT_TRUE ( continuations.back().valid() );
}
 
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

