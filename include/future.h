
#ifndef FUTURE_H
#define FUTURE_H

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>

#include <cassert>

/** Error codes for future and promise errors
 */
enum class future_errc {
    broken_promise = 0,
    future_already_retrieved,
    promise_already_satisfied,
    no_state
};

namespace traits {

    template < bool cond >
    struct copyable {};

    template <>
    struct copyable<false> {
        copyable() = default;
        copyable(copyable&&) = default;
        copyable(const copyable&) = delete;
    };

} // namespace traits

/** Exception for future and promise errors
 */
class future_error : public std::exception {
    public:
        explicit future_error( future_errc code ) :
            _code(code)
        {
        }

        future_error( const future_error& ) = default;
        ~future_error()                     = default;

        future_errc code() const {
            return _code;
        }

        virtual const char* what() const noexcept {
            static constexpr const char* messages[] = {
                "The asynchronous task abandoned its shared state",
                "The contents of shared state were already accessed through future",
                "Attempt to store a value in the shared state twice",
                "Attempt to access a promise or future without associated shared state"
            };

            return messages[static_cast<int>(_code)];
        }

    private:
        future_errc _code;
};

namespace generic {

    /*
     * Forward declaration for continuation_chain
     */
    template < class... Args >
    class continuation_chain;

    /**
     * Continuations are function objects to be called when a shared
     * data is resolved.
     */
    template < class... Args >
    class continuation {
        public:
            using dispatch_fn = void(*)(continuation&, Args&&...);

            explicit continuation( dispatch_fn func ) :
                _f(func)
            {
            }

            virtual ~continuation() = default;

            void operator()( Args&&... args ) {
                _f(*this, std::forward<Args>(args)...);
            };

            void hook_after( std::shared_ptr<continuation> node ) {
                node->_next = std::move(_next);
                _next = std::move(node);
            }

            std::shared_ptr<continuation>& next() {
                return _next;
            }

            bool has_next() const {
                return _next != nullptr;
            }

        private:
            template < class... Args_ >
            friend class continuation_chain;

            std::shared_ptr<continuation> _next; // Intrusive list hook
            dispatch_fn                   _f;    // Dispatch function
    };

    /** Single intrusive linked list that connects all the continuations with the
     * same predecessor.
     */
    template < class... Args >
    class continuation_chain {
        public:
            // A single linked list iterator
            class iterator {
                public:
                    typedef continuation<Args...>* pointer;
                    typedef continuation<Args...>& reference;

                    iterator() = default;

                    iterator( pointer n ) :
                        node(n)
                    {
                    }

                    iterator( const iterator& other ) = default;

                    reference operator*() {
                        return *node;
                    }

                    reference operator->() {
                        return node;
                    }

                    const reference operator*() const {
                        return node;
                    }

                    // Pre increment
                    void operator++() {
                        node = node->next().get();
                    }

                    // Post increment
                    iterator operator++(int) {
                        iterator copy(*this);
                        ++(*this);
                        return copy;
                    }

                    bool operator!=( const iterator& other ) const {
                        return node != other.node;
                    }

                private:
                    pointer node; // maybe use weak_ptr here
            };

            continuation_chain()                            = default;
            continuation_chain( const continuation_chain& ) = delete;
            continuation_chain( continuation_chain&& )      = default;
            ~continuation_chain()                           = default;

            template < class InputIt >
            continuation_chain( InputIt first, InputIt last ) :
                _head(nullptr)
            {
                while( first != last ) {
                    attach( *first++ );
                }
            }

            void insert( std::shared_ptr<continuation<Args...>> listener ) {
                if( _head )
                    listener->hook_after(_head);
                _head = listener;
            }

            bool empty() const {
                return _head == nullptr;
            }

            iterator begin() { return iterator(_head.get()); }
            iterator end()   { return iterator(); }

        private:
            std::shared_ptr<continuation<Args...>> _head; // replace with shared_ptr
    };

    template < class... Args >
    struct continuable {
        public:
            continuable() = default;

            template < class InputIt >
            continuable( InputIt begin, InputIt end ) :
                _chain(begin,end)
            {
            }

            template < class Container >
            explicit continuable( Container& elements ) :
                _chain( std::begin(elements), std::end(elements) )
            {
            }

            void propagate( Args... args ) {
                for( auto continuation : _chain ) {
                    continuation( std::forward<Args>(args)...);
                }
            }

            void attach( std::shared_ptr<generic::continuation<Args...>> listener ) {
                _chain.insert(std::move(listener));
            }

            bool has_continuation() const {
                return !_chain.empty();
            }

        private:
            continuation_chain<Args...> _chain;
    };

    class shared_state {
        public:
            enum class status {
                pending,         // value not set
                pending_shared,  // value not set, multiple listeners
                resolved, // value set but not consumed (may be shared)
                rejected, // exception set
                consumed // value already consumed (not shared)
            };

            bool is_pending()  const { return _state == status::pending || _state == status::pending_shared; }
            bool is_shared()   const { return _state == status::pending_shared; }
            bool is_resolved() const { return _state == status::resolved; }
            bool is_rejected() const { return _state == status::rejected; }
            bool is_consumed() const { return _state == status::consumed; }

            void resolved() {
                assert( is_pending() );
                _state = status::resolved;
                notify();
            }

            void rejected() {
                assert( is_pending() );
                _state = status::rejected;
                notify();
            }

            void consumed() {
                assert( !is_pending() );
                if( this->is_consumed() ) {
                    throw future_error( future_errc::future_already_retrieved);
                }
                _state = status::consumed;
            }

            // Blocks the current thread until the state is satisfied
            void wait() {
                std::unique_lock<std::mutex> lock(_mutex);
                if( is_pending() ) {
                    _resolved_cond.wait(lock);
                }
            }

        private:
            // Wakes up all waiting threads
            void notify() {
                std::unique_lock<std::mutex> lock(_mutex);
                _resolved_cond.notify_all();
            }

            status                    _state;
            std::mutex                _mutex;
            std::condition_variable   _resolved_cond;
    };

} // namespace generic

template < class T >
class shared_state : public generic::shared_state,
                     public generic::continuable<shared_state<T>&>
{
    public:
        typedef T value_type;

        shared_state()                    = default;
        shared_state(const shared_state&) = delete;

        // TODO: Implement move constructor
        shared_state(shared_state&&)      = delete;

        virtual ~shared_state() {
            // TODO: Make destructor trivial if T is trivially destructible
            if( is_resolved() ) {
                // Move the value away
                std::move(value(std::false_type()));
            }
        }

        template < class... Args >
        void emplace( Args&&... args ) {
            new (&_storage) T(std::forward<Args>(args)...);
            this->resolved();
            this->propagate(*this);
        }

        void emplace( std::exception_ptr exception ) {
            new (&_storage) std::exception_ptr(exception);
            this->rejected();
        }

        /* Retrieves a non-shared value */
        T& value( std::false_type ) {
            if( this->is_rejected() ) {
                std::rethrow_exception(exception());
            }
            this->consumed();
            return reinterpret_cast<T&>(_storage);
        }

        /* Retrieves a shared value: shared state remains valid */
        const T& value( std::true_type ) const {
            if( this->is_rejected() ) {
                std::rethrow_exception(exception());
            }
            if( this->is_consumed() ) {
                throw future_error( future_errc::future_already_retrieved);
            }
            return reinterpret_cast<const T&>(_storage);
        }

        std::exception_ptr exception() const {
            return reinterpret_cast<const std::exception_ptr&>(_storage);
        }

    private:
        using buffer_t = typename std::aligned_union<0,std::exception_ptr,T>::type;

        buffer_t _storage;
};

template <>
class shared_state<void> : public generic::shared_state,
                           public generic::continuable<shared_state<void>&>
{
    public:
        shared_state()                    = default;
        shared_state(const shared_state&) = delete;
        shared_state(shared_state&&)      = delete;
        virtual ~shared_state()           = default;

        void emplace() {
            this->resolved();
            this->propagate(*this);
        }

        void emplace( std::exception_ptr exception ) {
            _eptr = exception;
            this->rejected();
        }

        // Overload for unique value. Does consume value.
        void value( std::false_type ) {
            if( this->is_rejected() ) {
                std::rethrow_exception(exception());
            }
            if( this->is_consumed() ) {
                throw future_error( future_errc::future_already_retrieved);
            }
            this->consumed();
        }

        // Overload for shared value. Does not consume value.
        void value( std::true_type ) {
            if( this->is_rejected() ) {
                std::rethrow_exception(exception());
            }
            if( this->is_consumed() ) {
                throw future_error( future_errc::future_already_retrieved);
            }
            // Does not invalidate the shared state's value
        }

        std::exception_ptr exception() const {
            return _eptr;
        }

    private:
        std::exception_ptr _eptr;
};

template < class Continuation >
struct dispatcher;

template < class F, class... Args >
struct shared_state_for {
    typedef shared_state<typename std::result_of<F(Args...)>::type> type;
};

template < class F, class T >
struct shared_state_for< F, shared_state<T>& > {
    typedef shared_state<typename std::result_of<F(T)>::type> type;
};

template < class T >
struct remove_shared_state {
    typedef T type;
};

template < class T >
struct remove_shared_state<shared_state<T>> {
    typedef typename shared_state<T>::value_type type;
};

template < class F, class... Args >
class continuation : public shared_state_for<F,Args...>::type,
                     public generic::continuation<Args...>
{
    public:
        using value_type = typename shared_state_for<F,Args...>::type::value_type;

        continuation( F&& func ) :
            generic::continuation<Args...>( dispatcher<continuation>::get_dispatch() ),
            _func(std::forward<F>(func))
        {
        }

        template < class... FArgs >
        void operator()( FArgs&&... args ) {
            try {
                resolve( *this, std::forward<FArgs>(args)... );
            } catch(...) {
                this->emplace( std::current_exception() );
            }
        }

        template < class... FArgs >
        auto invoke( FArgs&&... args ) {
            return _func(std::forward<FArgs>(args)...);
        }

    private:
        F _func;
};

// Tag that indicates a continuation to acquire ownership of the value
// contained in its predecesor shared state
template < bool shared >
struct consume_state_value {};

template < class F, class T >
class continuation<F,shared_state<T>&> : public shared_state_for<F,T>::type,
                                         public generic::continuation<shared_state<T>&>
{
    public:
        using value_type  = typename shared_state_for<F,T>::type::value_type;

        template < bool consume_state >
        continuation( F&& func, consume_state_value<consume_state> tag ) :
            generic::continuation<shared_state<T>&>( dispatcher<continuation>::get_dispatch(tag) ),
            _func(std::forward<F>(func))
        {
        }

        template < class... FArgs >
        void operator()( FArgs&&... args ) {
            try {
                resolve( *this, std::forward<FArgs>(args)... );
            } catch(...) {
                this->emplace( std::current_exception() );
            }
        }

        template < class... FArgs >
        auto invoke( FArgs&&... args ) {
            return _func(std::forward<FArgs>(args)...);
        }

    private:
        F _func;
};

// Either emplaces F's return value (result is not void)
template < class Continuation, class... Args >
typename std::enable_if<std::is_same<void,typename Continuation::value_type>::value>::type
resolve( Continuation& cont, Args&&... args ) {
    cont.emplace( cont.invoke(std::forward<Args>(args)...) );
}

// or ignores it and emplaces nothing
template < class Continuation, class... Args >
typename std::enable_if<!std::is_same<void,typename Continuation::value_type>::value>::type
resolve( Continuation& cont, Args&&... args ) {
    cont.invoke(std::forward<Args>(args)...);
    cont.emplace();
}

template < class F, class... Args >
struct dispatcher<continuation<F,Args...>> {
    using Continuation = continuation<F,Args...>;

    static void invoke( generic::continuation<Args...>& cont_base, Args&&... args ) {
        Continuation& cont = static_cast<Continuation&>(cont_base);
        cont(std::forward<Args>(args)...);
    }

    static constexpr auto get_dispatch() { return &invoke; }
};

template < class F, class T >
struct dispatcher<continuation<F,shared_state<T>&>> {
    using Continuation = continuation<F,shared_state<T>&>;

    static void invoke_consume( generic::continuation<shared_state<T>&>& cont_base, shared_state<T>& state );
    static void invoke_share  ( generic::continuation<shared_state<T>&>& cont_base, shared_state<T>& state );

    static constexpr auto get_dispatch( consume_state_value<true>  ) { return &invoke_consume; }
    static constexpr auto get_dispatch( consume_state_value<false> ) { return &invoke_share; }
};

// Non-shared state value access. Consumes state value and forwards to continuation.
template < class F, class T >
void dispatcher<continuation<F,shared_state<T>&>>::invoke_consume( generic::continuation<shared_state<T>&>& cont_base, shared_state<T>& state ) {
    auto& cont = static_cast<Continuation&>(cont_base);
    cont( std::move(state.value(std::false_type())) );
}

// Shared state value access. Does not consume state value and passes reference to continuation.
template < class F, class T >
void dispatcher<continuation<F,shared_state<T>&>>::invoke_share( generic::continuation<shared_state<T>&>& cont_base, shared_state<T>& state ) {
    auto& cont = static_cast<Continuation&>(cont_base);
    cont( state.value(std::true_type()) );
}

template < class T >
class promise;

template < class T, bool shared >
class future_impl : traits::copyable<shared> {
    public:
        future_impl()                                    = default;
        ~future_impl()                                   = default;

        future_impl( const future_impl& )                = default;
        future_impl& operator=( const future_impl& )     = default;

        future_impl( future_impl&& )                     = default;
        future_impl& operator=( future_impl&& ) noexcept = default;

        template < typename = std::enable_if<shared> >
        future_impl( future_impl<T,false>&& other ) :
            _state( std::move(other._state) )
        {
        }

        template < typename = std::enable_if<shared> >
        future_impl& operator=( future_impl<T,false>&& other ) {
            _state = std::move(other._state);
            return *this;
        }

        bool valid() const {
            return static_cast<bool>(_state);
        }

        bool is_ready() const {
            return !_state->is_pending();
        }

        auto get() {
            return _state->value(std::integral_constant<bool,shared>());
        }

        template < class F >
        auto then( F&& f );

    private:
        template < class, bool >
        friend class future_impl;

        template < class >
        friend class promise;

        template < class >
        friend class packaged_task;

        explicit future_impl( std::shared_ptr<shared_state<T>> state ) :
            _state(state)
        {
        }

        std::shared_ptr<shared_state<T>> _state;
};

template < bool shared >
class future_impl<void,shared> : traits::copyable<shared> {
    public:
        future_impl()                                    = default;
        ~future_impl()                                   = default;

        future_impl( const future_impl& )                = default;
        future_impl& operator=( const future_impl& )     = default;

        future_impl( future_impl&& ) noexcept            = default;
        future_impl& operator=( future_impl&& ) noexcept = default;

        template < typename = std::enable_if<shared> >
        future_impl( future_impl<void,false>&& other ) :
            _state( std::move(other._state) )
        {
        }

        template < typename = std::enable_if<shared> >
        future_impl& operator=( future_impl<void,false>&& other ) {
            _state = std::move(other._state);
            return *this;
        }

        bool valid() const {
            return _state != nullptr;
        }

        bool is_ready() const {
            return !_state->is_pending();
        }

        void get() {
            // FIXME: Should block until shared state is not pending
            _state->value(std::integral_constant<bool,shared>());
            return;
        }

        template < class F >
        auto then( F&& f );

    private:
        template < class, bool >
        friend class future_impl;

        template < class >
        friend class promise;
    
        template < class >
        friend class packaged_task;

        explicit future_impl( std::shared_ptr<shared_state<void>> state ) :
            _state(state)
        {
        }

        std::shared_ptr<shared_state<void>> _state;
};

template < class T >
class shared_future : public future_impl<T,true> {
    using impl = future_impl<T,true>;

    public:
        using impl::future_impl;
        using impl::valid;
        using impl::is_ready;
        using impl::get;
        using impl::then;
};

template < class T >
class future : public future_impl<T,false> {
    using impl = future_impl<T,false>;

    public:
        using future_impl<T,false>::future_impl;
        using future_impl<T,false>::valid;
        using future_impl<T,false>::is_ready;
        using future_impl<T,false>::get;
        using future_impl<T,false>::then;

        shared_future<T> share() {
            return shared_future<T>( std::move(*this) );
        }
};

template < class T >
class promise {
    public:
        promise() = default;

        promise( const promise& ) = delete;
        promise( promise&& )      = default;
        ~promise()                = default;

        promise& operator=( const promise& ) = delete;
        promise& operator=( promise&& ) noexcept = default;

        future<T> get_future() {
            if( _state ) {
                throw future_error(future_errc::future_already_retrieved);
            }
            _state = std::make_shared<shared_state<T>>();
            return future<T>(_state);
        }

        void set_value( const T& value ) {
            if( !_state ) {
                throw future_error( future_errc::no_state );
            }
            _state->emplace(value);
        }

        void set_value( T&& value ) {
            if( !_state ) {
                throw future_error( future_errc::no_state );
            }
            _state->emplace( std::forward<T>(value) );
        }

        void set_exception( std::exception_ptr exception ) {
            if( !_state ) {
                throw future_error( future_errc::no_state );
            }
            _state->emplace( exception );
        }

    private:
        std::shared_ptr<shared_state<T>> _state;
};

template <>
struct promise<void> {
    public:
        promise() = default;

        promise( const promise& ) = delete;
        promise( promise&& )      = default;
        ~promise()                = default;

        promise& operator=( const promise& ) = delete;
        promise& operator=( promise&& ) noexcept = default;

        future<void> get_future() {
            if( _state ) {
                throw future_error(future_errc::future_already_retrieved);
            }
            _state = std::make_shared<shared_state<void>>();
            return std::move(future<void>(_state));
        }

        void set_value() {
            if( !_state ) {
                throw future_error( future_errc::no_state );
            }
            _state->emplace();
        }

        void set_exception( std::exception_ptr exception ) {
            if( !_state ) {
                throw future_error( future_errc::no_state );
            }
            _state->emplace( exception );
        }

    private:
        std::shared_ptr<shared_state<void>> _state;
};

template < class >
class packaged_task;

/** Defers a call to a type-erased function.
 * It is similar to a promise + future::then, but with a single shared state
 * (result), rather than two (one for the argument, i.e. promise::set_value, and
 * another for the result).
 */
template < class R, class... Args >
class packaged_task<R(Args...)> : public generic::continuable<Args...> {
    public:
        packaged_task() noexcept = default;

        template < class F >
        packaged_task( F&& f ) :
            packaged_task()
        {
            // This is similar to a 'shared_state::then', the difference being
            // that the argument comes from a function call rather than another
            // state.
            auto ptr = std::make_shared<continuation<F,Args...>>( std::forward<F>(f) );
            this->attach(ptr);
            _state = std::move(ptr);
        }

        packaged_task( const packaged_task& )     = delete;
        packaged_task( packaged_task&& ) noexcept = default;
        ~packaged_task()                          = default;

        bool valid() const {
            return _state != nullptr;
        }

        void swap( packaged_task& other ) {
            // Delegate to move constructor: just swaps the pointers.
            std::swap( *this, other );
        }

        future<R> get_future() {
            if( !_state ) {
                throw future_error( future_errc::no_state );
            }
            return future<R>(_state);
        }

        void operator()( Args... args ) {
            this->propagate(std::forward<Args>(args)...);
        }

    private:
        // Would this rather be a regular pointer?
        // Continuable::continuation_chain already has a shared pointer to the
        // same object.
        std::shared_ptr<shared_state<R>> _state;
};

template < class T, bool shared >
template < class F >
inline auto future_impl<T,shared>::then( F&& f ) {
    assert( valid() ); // Behavior is undefined if future is not valid
    assert( !_state->is_consumed() );

    using value_type  = typename std::result_of<F(T)>::type;
    using shared_type = shared_state<value_type>;
    std::shared_ptr<shared_type> new_state;

    if( _state->is_pending() ) {
        // Create and attach a continuation
        auto tmp = std::make_shared<continuation<F,shared_state<T>&>>( std::forward<F>(f), consume_state_value<!shared>() );
        _state->attach(std::shared_ptr<generic::continuation<shared_state<T>&>>(tmp));
        new_state = std::move(tmp);
    } else {
        new_state = std::make_shared<shared_type>();
        try {
            // Don't move the value if it's not shared or it's a reference!
            if( !shared && !std::is_reference<T>::value ) {
                new_state->emplace( std::forward<F>(f)(std::move(get())) );
            } else {
                new_state->emplace( std::forward<F>(f)(get()) );
            }
        } catch (...) {
            // Also works if current state is rejected. However, it
            // could be more efficient to emplace the current exception straight
            // away, instead of throwing and catching the exception every time.
            new_state->emplace( std::current_exception() );
        }
    }

    // Invalidate if the future is not shared
    if( !shared )
        _state.reset();

    return future<value_type>(std::move(new_state));
}

template < bool shared >
template < class F >
inline auto future_impl<void,shared>::then( F&& f ) {
    assert( valid() ); // Behavior is undefined if future is not valid
    assert( !_state->is_consumed() );

    using value_type  = typename std::result_of<F()>::type;
    using shared_type = shared_state<value_type>;
    std::shared_ptr<shared_type> new_state;

    if( _state->is_pending() ) {
        // Create and attach a continuation
        auto tmp = std::make_shared<continuation<F,void>>( std::forward<F>(f) );
        _state->attach(tmp);
        new_state = std::move(tmp);
    } else {
        new_state = std::make_shared<shared_type>();
        try {
            get();
            new_state->emplace( std::forward<F>(f)() );
        } catch (...) {
            // Also works if current state is rejected. However, it
            // could be more efficient to emplace the current exception straight
            // away, instead of throwing and catching the exception every time.
            new_state->emplace( std::current_exception() );
        }
    }

    // Invalidate if the future is not shared
    if( !shared )
        _state.reset();

    return future<value_type>(std::move(new_state));
}

#endif // FUTURE_H

