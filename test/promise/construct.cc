
#include "future.h"

#include <gtest/gtest.h>
 
TEST (VoidPromise, Default_SetValue) { 
    promise<void> vp;

    bool success = false;
    try {
        vp.set_value();
    } catch ( const future_error& ) {
        success = true;
    } catch (...) {
        // Unexpected exception type
    }

    EXPECT_TRUE (success);
}
 
TEST (VoidPromise, Default_SetException) { 
    promise<void> vp;

    bool success = false;
    try {
        vp.set_exception(std::make_exception_ptr(0));
    } catch ( const future_error& ) {
        success = true;
    } catch (...) {
        // Unexpected exception type
    }

    EXPECT_TRUE (success);
}
 
TEST (VoidPromise, Future_SetValue) {
    promise<void> vp;
    future<void>  fp = vp.get_future();

    bool success = false;
    try {
        vp.set_value();
        fp.get();
        success = true;
    } catch (...) {
    }

    EXPECT_TRUE (success);
}
 
TEST (VoidPromise, Future_SetException) {
    promise<void> vp;
    future<void>  fp = vp.get_future();

    bool success = false;
    try {
        vp.set_exception(std::make_exception_ptr<int>(0));
        fp.get(); // should throw int
    } catch ( int ) {
        success = true;
    } catch (...) {
        // Unexpected exception type
    }

    EXPECT_TRUE (success);
}

TEST (IntPromise, Future_SetValue) {
    promise<int> intp;
    future<int> intf = intp.get_future();

    try {
        int set = 55;
        intp.set_value(set);
        ASSERT_EQ( set, intf.get() );
    } catch (...) {
        // Unreachable code
        ASSERT_TRUE(false);
    }
}

