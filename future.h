
#ifndef FUTURE_H
#define FUTURE_H

#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

/** Error codes for future and promise errors
 */
enum class future_errc {
    broken_promise = 0,
    future_already_retrieved,
    promise_already_satisfied,
    no_state
};

/** Exception for future and promise errors
 */
class future_error : public std::exception {
    public:
        future_error( future_errc code ) :
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
    template < class Arg >
    class continuation_chain;

    /**
     * Continuations are function objects to be called when a shared
     * data is resolved.
     */
    template < class Arg >
    class continuation : public std::enable_shared_from_this<continuation<Arg>> {
        public:
            using dispatch_fn = void(*)(continuation&, Arg);

            continuation( dispatch_fn func ) :
                _f(func)
            {
            }

            void operator()( Arg arg ) {
                _f(*this, arg);
            };

            void hook_after( continuation<Arg>& node ) {
                node._next = _next;
                _next = &node;
            }

            continuation* next() {
                return _next;
            }

        private:
            friend class continuation_chain<Arg>;

            continuation* _next; // Intrusive list hook
            dispatch_fn   _f;    // Dispatch function
    };

    template <>
    class continuation<void> : public std::enable_shared_from_this<continuation<void>> {
        public:
            using dispatch_fn = void(*)(continuation&);

            continuation( dispatch_fn func ) :
                _f(func)
            {
            }

            void operator()() {
                _f(*this);
            };

            void hook_after( continuation<void>& node ) {
                node._next = _next;
                _next = &node;
            }

            continuation* next() {
                return _next;
            }

        private:
            friend class continuation_chain<void>;

            continuation* _next; // Intrusive list hook
            dispatch_fn   _f;    // Dispatch function
    };

    /** Single intrusive linked list that connects all the continuations with the
     * same predecessor.
     */
    template < class Arg >
    class continuation_chain {
        public:
            // A single linked list iterator
            class iterator {
                public:
                    iterator() = default;

                    iterator( continuation<Arg>* n ) :
                        node(n)
                    {
                    }

                    iterator( const iterator& other ) = default;

                    continuation<Arg>* operator*() {
                        return node;
                    }

                    const continuation<Arg>* operator*() const {
                        return node;
                    }


                    // Pre increment
                    void operator++() {
                        node = node->next();
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
                    continuation<Arg>* node;
            };

            continuation_chain() = default;

            template < class InputIt >
            continuation_chain( InputIt first, InputIt last ) :
                _head(nullptr)
            {
                while( first != last ) {
                    attach( *first++ );
                }
            }

            void insert( std::shared_ptr<continuation<Arg>> listener ) {
                if( _head )
                    listener->hook_after(*_head);
                _head = listener;
            }

            iterator begin() { return iterator(_head.get()); }
            iterator end()   { return iterator(); }

        private:
            std::shared_ptr<continuation<Arg>> _head; // replace with shared_ptr
    };

    template < class Arg >
    struct continuable {
        public:
            continuable() = default;

            template < class InputIt >
            continuable( InputIt begin, InputIt end ) :
                _chain(begin,end)
            {
            }

            template < class Container >
            continuable( Container& elements ) :
                _chain( std::begin(elements), std::end(elements) )
            {
            }

            void propagate( Arg arg ) {
                // Move semantics when function argument is r-value reference
                for( auto continuation : _chain ) {
                    (*continuation)(std::forward<Arg>(arg));
                }
            }

            void attach( std::shared_ptr<generic::continuation<Arg>> listener ) {
                _chain.insert(std::move(listener));
            }

        private:
            continuation_chain<Arg> _chain;
    };

    template <>
    struct continuable<void> {
        public:
            continuable() = default;

            template < class InputIt >
            continuable( InputIt begin, InputIt end ) :
                _chain(begin,end)
            {
            }

            template < class Container >
            continuable( Container& elements ) :
                _chain( std::begin(elements), std::end(elements) )
            {
            }

            void propagate() {
                // Move semantics when function argument is r-value reference
                for( auto continuation : _chain ) {
                    (*continuation)();
                }
            }

            void attach( std::shared_ptr<generic::continuation<void>> listener ) {
                _chain.insert(std::move(listener));
            }

        private:
            continuation_chain<void> _chain;
    };

    class shared_state {
        public:
            enum class status {
                pending, resolved, rejected
            };

            bool is_pending()  const { return _state == status::pending; }
            bool is_resolved() const { return _state == status::pending; }
            bool is_rejected() const { return _state == status::pending; }

            void resolve() {
                _state = status::resolved;
            }

            void reject() {
                _state = status::rejected;
            }

        private:
            status                    _state;
    };

} // namespace generic

template < class T >
class shared_state : public generic::shared_state, public generic::continuable<T> {
    public:
        shared_state()                    = default;
        shared_state(const shared_state&) = delete;
        shared_state(shared_state&&)      = delete;

        ~shared_state() {
            // TODO: Make dstructor trivial if T is trivially destructible
            if( is_resolved() ) {
                value().~T();
            }
        }

        template < class... Args >
        void emplace( Args&&... args ) {
            new (&_storage) T(std::forward<Args>(args)...);
            this->resolve();
            this->propagate(value());
        }


        void emplace( std::exception_ptr exception ) {
            new (&_storage) std::exception_ptr(exception);
            this->reject();
        }

        T& value() {
            if( this->is_rejected() ) {
                std::rethrow_exception(exception());
            }
            return reinterpret_cast<T&>(_storage);
        }

        const T& value() const {
            if( this->is_rejected() ) {
                std::rethrow_exception(exception());
            }
            return reinterpret_cast<const T&>(_storage);
        }

        std::exception_ptr exception() {
            return reinterpret_cast<std::exception_ptr&>(_storage);
        }

        template < class F >
        auto then( F&& function );

    private:
        static constexpr size_t storage_size  = std::max(sizeof(T),sizeof(std::exception_ptr));
        static constexpr size_t storage_align = std::max(alignof(T),alignof(std::exception_ptr));

        using buffer_t = typename std::aligned_storage<storage_size,storage_align>::type;

        buffer_t           _storage;
};

template <>
class shared_state<void> : public generic::shared_state, public generic::continuable<void> {
    public:
        shared_state()                    = default;
        shared_state(const shared_state&) = delete;
        shared_state(shared_state&&)      = delete;
        ~shared_state()                   = default;

        void emplace() {
            this->resolve();
            this->propagate();
        }

        void emplace( std::exception_ptr exception ) {
            _eptr = exception;
            this->reject();
        }

        std::exception_ptr exception() {
            return _eptr;
        }

        template < class F >
        auto then( F&& function );
    public:
        std::exception_ptr _eptr;
};

template < class Continuation, class R, class F, class Arg >
struct dispatcher {
    static constexpr void invoke( generic::continuation<Arg>& base, Arg arg ) {
        auto& c = static_cast<Continuation&>(base);
        c.emplace( c._func(arg) );
    }
};

template < class Continuation, class F, class Arg >
struct dispatcher<Continuation,void,F,Arg> {
    static constexpr void invoke( generic::continuation<Arg>& base, Arg arg ) {
        auto& c = static_cast<Continuation&>(base);
        c._func(arg);
        c.emplace();
    }
};

template < class Continuation, class R, class F >
struct dispatcher<Continuation,R,F,void> {
    static constexpr void invoke( generic::continuation<void>& base ) {
        auto& c = static_cast<Continuation&>(base);
        c.emplace( c._func() );
    }
};

template < class Continuation, class F >
struct dispatcher<Continuation,void,F,void> {
    static constexpr void invoke( generic::continuation<void>& base ) {
        auto& c = static_cast<Continuation&>(base);
        c._func();
        c.emplace();
    }
};

template < class F, class Arg >
class continuation : public shared_state<typename std::result_of<F(Arg)>::type>,
                     public generic::continuation<Arg>
{
    public:
        using value_type = typename std::result_of<F(Arg)>::type;

        continuation( F&& func ) :
            generic::continuation<Arg>( &dispatcher<continuation,value_type,F,Arg>::invoke ),
            _func(std::forward<F>(func))
        {
        }

    private:
        template < class _Continuation, class  _R, class _F, class _Arg >
        friend class dispatcher;

        F _func;
};

template < class F >
class continuation<F,void> : public shared_state<typename std::result_of<F()>::type>,
                             public generic::continuation<void>
{
    public:
        using value_type = typename std::result_of<F()>::type;

        continuation( F&& func ) :
            generic::continuation<void>( &dispatcher<continuation,value_type,F,void>::invoke ),
            _func(std::forward<F>(func))
        {
        }

    private:
        template < class _Continuation, class  _R, class _F, class _Arg >
        friend class dispatcher;

        F _func;
};

template < class T >
class promise;

template < class T >
class future {
    public:
        future()                = default;
        future( const future& ) = delete;
        future( future&& )      = default;
        ~future()               = default;

        bool valid() const {
            return _state;
        }

        bool is_ready() const {
            return !_state->is_pending();
        }

        T get() {
            return _state->value();
        }

        template < class F >
        auto then( F&& function );

    private:
        friend class promise<T>;

        template < class U >
        friend class future;

        future( std::shared_ptr<shared_state<T>> state ) :
            _state(state)
        {
        }

        template < class F, class Arg >
        future( std::shared_ptr<continuation<F,Arg>> state ):
            _state(state)
        {
        }

        std::shared_ptr<shared_state<T>> _state;
};

template < class T >
class promise {
    public:
        promise() = default;

        promise( const promise& ) = delete;
        promise( promise&& )      = default;
        ~promise()                = default;

        future<T> get_future() {
            if( _state ) {
                throw future_error(future_errc::future_already_retrieved);
            }
            _state = std::make_shared<shared_state<T>>();
            return future<T>(_state);
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

template < class F >
auto shared_state<void>::then( F&& f ) {
    auto ptr = std::make_shared<continuation<F,void>>( std::forward<F>(f) );
    this->attach(ptr->shared_from_this());
    return std::move(ptr);
}

template < class T >
template < class F >
auto shared_state<T>::then( F&& f ) {
    auto ptr = std::make_shared<continuation<F,T>>( std::forward<F>(f) );
    this->attach(ptr->shared_from_this());
    return std::move(ptr);
}

template <>
template < class F >
auto future<void>::then( F&& function ) {
    using value_type = typename std::result_of<F()>::type;

    auto cont = _state->then( std::forward<F>(function) );
    future<value_type> result(std::move(cont));
    return result;
}

template < class T >
template < class F >
auto future<T>::then( F&& function ) {
    using value_type = typename std::result_of<F(T)>::type;

    auto cont = _state->then( std::forward<F>(function) );
    future<value_type> result(std::move(cont));
    return result;
}

#endif // FUTURE_H

