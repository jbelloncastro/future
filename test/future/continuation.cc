
#include "future.h"

#include <gtest/gtest.h>

TEST (IntPromise, One2Many_NoShared) {
    promise<int> intp;
    future<int>  intf = intp.get_future();
    std::vector<future<bool>> continuations;

    int value = 55;

    // After future creation, future should be valid or then() has undefined behavior
    ASSERT_TRUE ( intf.valid() );

    // First then() should go all right
    future<bool> continued;
    ASSERT_NO_THROW(
        continued =
            intf.then([=](const int& i) -> bool {
                return i == value;
            })
    );
    continuations.push_back(std::move(continued));

    // After first then(), first future // should not be valid any more
    EXPECT_FALSE( intf.valid() );

    // Since intf is not valid, queueing more continuations should fail with
    // future_error
    //ASSERT_DEATH(
    //    continued =
    //        intf.then([=](int i) -> bool {
    //            return i == value;
    //        })
    //    ,"Assertion .* failed."
    //);
    continuations.push_back(std::move(continued));
}

TEST (IntPromise, One2Many_Shared) {
    promise<int> intp;
    shared_future<int> intf = intp.get_future().share();
    std::vector<future<bool>> continuations;

    int value = 55;

    // After future creation, future should be valid or then() has undefined behavior
    ASSERT_TRUE ( intf.valid() );

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

    for( int times = 2; times > 0; times-- ) {
        intf = intf.then([&]( int i ) -> int {
            success &= (set == i);
            return i;
        });
    }
    ASSERT_TRUE(success);
}

