
#ifndef WHEN_H
#define WHEN_H

#include <algorithm>
#include <tuple>
#include <vector>

template < class Sequence >
struct when_any_result {
    size_t   index;
    Sequence futures;
};

template < class Future >
using when_all_result = std::vector<Future>;

template < class Future >
class when_all_continuation;

template < class Future >
struct future_traits;

template < class T >
struct future_traits<shared_future<T>> {
    typedef T               value_type;
    typedef shared_state<T> state_type;
};

template < class T >
struct future_traits<future<T>> {
    typedef T               value_type;
    typedef shared_state<T> state_type;
};

template < class Future >
class when_all_continuation : public shared_state<std::vector<Future>>,
                              public generic::continuation<typename future_traits<Future>::state_type&>,
                              public std::enable_shared_from_this<when_all_continuation<Future>>
{
    using continuation_type = generic::continuation<typename future_traits<Future>::state_type&>;
    public:
        template < class InputIt >
        when_all_continuation( InputIt first, InputIt last ) :
            when_all_continuation(first, last, std::integral_constant<bool,is_shared_future<Future>::value>())
        {
        }

        void attach_to_futures() {
            // Attach all the futures
            for( Future& f: _futures ) {
                f.then(this->shared_from_this());
            }
        }

        void notify_one() {
            _pending--;
            if( _pending == 0 ) {
                this->emplace(std::move(_futures));
            }
        }

    private:
        // Shared futures are copied into the vector
        template < class InputIt >
        when_all_continuation( InputIt first, InputIt last, std::integral_constant<bool,true> ) :
            continuation_type( dispatcher<when_all_continuation>::get_dispatch() ),
            _futures(first, last),
            _pending(_futures.size())
        {
        }

        // Non-shared futures are moved into the vector
        template < class InputIt >
        when_all_continuation( InputIt first, InputIt last, std::integral_constant<bool,false> ) :
            continuation_type( dispatcher<when_all_continuation>::get_dispatch() ),
            _futures(),
            _pending(_futures.size())
        {
            _futures.reserve( std::distance(first,last) );
            std::for_each( first, last, [&]( Future& f ) {
                    _futures.emplace_back( std::move(f) );
                    });
        }

        std::vector<Future> _futures;
        size_t              _pending;
};

template < class Future >
struct dispatcher<when_all_continuation<Future>> {
    using value_type   = typename future_traits<Future>::value_type;
    using GenericCont  = generic::continuation<shared_state<value_type>&>;;
    using Argument     = shared_state<value_type>;
    using Continuation = when_all_continuation<Future>;
    using DispatchFunc = void(GenericCont&, Argument&);

    static void invoke( GenericCont& cont_base, Argument& ) {
        Continuation& cont = static_cast<Continuation&>(cont_base);
        cont.notify_one();
    }

    static constexpr DispatchFunc* get_dispatch() { return &invoke; }
};

template < class InputIt >
auto when_all( InputIt first, InputIt last ) -> future<std::vector<typename std::iterator_traits<InputIt>::value_type>> {
    using Future      = typename std::iterator_traits<InputIt>::value_type;
    using SharedState = shared_state<std::vector<Future>>;

    auto ptr = std::make_shared<when_all_continuation<Future>>(first,last);
    ptr->attach_to_futures();
    return SharedState::get_future_from(std::move(ptr));
}

#endif // WHEN_H

