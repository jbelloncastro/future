
#ifndef FUTURE_H
#define FUTURE_H

#include <exception>
#include <memory>
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
     * Forward declaration for continuation_chan
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
            using dispatch_fn = void(*)(continuation&, Args...);

            explicit continuation( dispatch_fn func ) :
                _f(func)
            {
            }

            virtual ~continuation() = default;

            void operator()( Args... args ) {
                _f(*this, args...);
            };

            void hook_after( std::shared_ptr<continuation<Args...>> node ) {
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
            friend class continuation_chain<Args...>;

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
                    iterator() = default;

                    iterator( continuation<Args...>* n ) :
                        node(n)
                    {
                    }

                    iterator( const iterator& other ) = default;

                    continuation<Args...>* operator*() {
                        return node;
                    }

                    const continuation<Args...>* operator*() const {
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
                    continuation<Args...>* node; // maybe use weak_ptr here
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
                    (*continuation)(std::forward<Args>(args)...);
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

            bool is_pending()  const { return _state == status::pending; }
            bool is_shared()   const { return _state == status::pending_shared; }
            bool is_resolved() const { return _state == status::resolved; }
            bool is_rejected() const { return _state == status::rejected; }
            bool is_consumed() const { return _state == status::consumed; }

            void resolved() {
                assert( is_pending() );
                _state = status::resolved;
            }

            void rejected() {
                assert( is_pending() );
                _state = status::rejected;
            }

            void consumed() {
                assert( is_resolved() || is_rejected() );
                _state = status::consumed;
            }

        private:
            status                    _state;
    };

} // namespace generic

template < class T >
class shared_state : public generic::shared_state,
                     public generic::continuable<T>
{
    public:
        shared_state()                    = default;
        shared_state(const shared_state&) = delete;

        // TODO: Implement move constructor
        shared_state(shared_state&&)      = delete;

        virtual ~shared_state() {
            // TODO: Make destructor trivial if T is trivially destructible
            if( is_resolved() ) {
                value(std::true_type()).~T();
            }
        }

        template < class... Args >
        void emplace( Args&&... args ) {
            new (&_storage) T(std::forward<Args>(args)...);
            // XXX: How can we better differentiate if we have
            // shared_futures or not at this point?
            bool shared = this->is_shared();
            this->resolved();

            if( this->has_continuation() ) {
                if( shared ) {
                    // Shared continuables don't consume the value, so further
                    // then() and get() are allowed
                    this->propagate(value(std::true_type()));
                } else {
                    // Non-shared continuable moves the value and consumes the
                    // shared_state instance (no further then() or get()
                    // allowed)
                    this->propagate(std::move(value(std::false_type())));
                }
            }
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
            if( this->is_consumed() ) {
                throw future_error( future_errc::future_already_retrieved);
            }
            this->consumed();
            return reinterpret_cast<T&>(_storage);
        }

        /* Retrieves a shared value */
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
        static constexpr size_t storage_size  = std::max(sizeof(T),sizeof(std::exception_ptr));
        static constexpr size_t storage_align = std::max(alignof(T),alignof(std::exception_ptr));

        using buffer_t = typename std::aligned_storage<storage_size,storage_align>::type;

        buffer_t           _storage;
};

template <>
class shared_state<void> : public generic::shared_state, public generic::continuable<> {
    public:
        shared_state()                    = default;
        shared_state(const shared_state&) = delete;
        shared_state(shared_state&&)      = delete;
        virtual ~shared_state()           = default;

        void emplace() {
            this->resolved();
            this->propagate();
        }

        void emplace( std::exception_ptr exception ) {
            _eptr = exception;
            this->rejected();
        }

        template < bool shared >
        void value();

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

template < class Continuation, class R, class F, class... Args >
struct dispatcher {
    static constexpr void invoke( generic::continuation<Args...>& base, Args... args ) {
        auto& c = static_cast<Continuation&>(base);
        c.emplace( c._func(args...) );
    }
};

template < class Continuation, class F, class... Args >
struct dispatcher<Continuation,void,F,Args...> {
    static constexpr void invoke( generic::continuation<Args...>& base, Args... args ) {
        auto& c = static_cast<Continuation&>(base);
        c._func(args...);
        c.emplace();
    }
};

template < class F, class... Args >
class continuation : public shared_state<typename std::result_of<F(Args...)>::type>,
                     public generic::continuation<Args...>
{
    public:
        using value_type = typename std::result_of<F(Args...)>::type;

        continuation( F&& func ) :
            generic::continuation<Args...>( &dispatcher<continuation,value_type,F,Args...>::invoke ),
            _func(std::forward<F>(func))
        {
        }

    private:
        template < class _Continuation, class  _R, class _F, class... _Args >
        friend class dispatcher;

        F _func;
};

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
            // FIXME: Should block until shared state is not pending
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
    assert( !_state->is_consumed() );

    using value_type  = typename std::result_of<F(T)>::type;
    using shared_type = shared_state<value_type>;
    std::shared_ptr<shared_type> new_state;

    if( _state->is_pending() ) {
        // Create and attach a continuation
        auto tmp = std::make_shared<continuation<F,T>>( std::forward<F>(f) );
        _state->attach(tmp);
        new_state = std::move(tmp);
    } else {
        new_state = std::make_shared<shared_type>();
        try {
            new_state->emplace( std::forward<F>(f)(std::move(get())) );
        } catch (...) {
            // Also works if current state is rejected. However, it
            // could be more efficient to emplace the current exception straight
            // away, instead of throwing and catching the exception every time.
            new_state->emplace( std::current_exception() );
        }
    }
    return future<value_type>(std::move(new_state));
}

template < bool shared >
template < class F >
inline auto future_impl<void,shared>::then( F&& f ) {
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
    return future<value_type>(std::move(new_state));
}

#endif // FUTURE_H

