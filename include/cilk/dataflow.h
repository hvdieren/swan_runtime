/*  dataflow.h                 -*- C++ -*-
 *
 *  Copyright (C) 2015, Hans Vandierendonck, Queen's University Belfast
 *  All rights reserved.
 */
#ifndef DATAFLOW_H_INCLUDED
#define DATAFLOW_H_INCLUDED

#include <stdio.h>

// Includes stdint.h
#include "cilk/common.h"
#include "cilk/../../runtime/spin_mutex.h"

#ifdef __cplusplus
#include <new>
#include <utility>
// #include <type_traits>
#endif

__CILKRTS_BEGIN_EXTERN_C

enum {
    CILK_OBJ_GROUP_EMPTY = 1,
    CILK_OBJ_GROUP_READ = 2,
    CILK_OBJ_GROUP_WRITE = 4,
    CILK_OBJ_GROUP_COMMUT = 8,
    CILK_OBJ_GROUP_NOT_WRITE = 15 - (int)CILK_OBJ_GROUP_WRITE
};

struct __cilkrts_pending_frame;

// This is a repeated definition from include/internal/abi.h visible only
// when building the runtime.
// typedef struct __cilkrts_pending_frame __cilkrts_pending_frame;

struct __cilkrts_task_list_node {
    struct __cilkrts_task_list_node * it_next;
    struct __cilkrts_pending_frame * st_task_and_last; // TODO: uintptr_t?
};

typedef struct __cilkrts_task_list_node __cilkrts_task_list_node;

struct __cilkrts_task_list {
    __cilkrts_task_list_node head;
    __cilkrts_task_list_node *tail;
};

typedef struct __cilkrts_task_list __cilkrts_task_list;

typedef struct __cilkrts_obj_metadata __cilkrts_obj_metadata;
typedef struct __cilkrts_obj_traits __cilkrts_obj_traits;
typedef struct __cilkrts_obj_payload __cilkrts_obj_payload;
typedef struct __cilkrts_obj_version __cilkrts_obj_version;

typedef void  (*cilk_c_dataflow_initialize_fn_t)(void *payload, void *view);
typedef void  (*cilk_c_dataflow_destroy_fn_t)(void *payload, void *view);
typedef void *(*cilk_c_dataflow_allocate_fn_t)(size_t size);
typedef void  (*cilk_c_dataflow_deallocate_fn_t)(void *p);

struct __cilkrts_obj_traits {
    cilk_c_dataflow_initialize_fn_t    initialize_fn;
    cilk_c_dataflow_destroy_fn_t       destroy_fn;
    cilk_c_dataflow_allocate_fn_t      allocate_fn;
    cilk_c_dataflow_deallocate_fn_t    deallocate_fn;
    size_t                             size;
};

struct __cilkrts_obj_metadata {
    uint64_t oldest_num_tasks;
    uint32_t youngest_group; // enum
    uint32_t num_gens;
    __cilkrts_task_list tasks;
    spin_mutex mutex;
};

struct __cilkrts_obj_payload {
    __cilkrts_obj_traits    traits; ///< Function pointers
    uint32_t                refcnt; ///< Reference count
    uint32_t                offset; ///< Offset of data from start of struct
};

struct __cilkrts_obj_version {
    __cilkrts_obj_metadata  meta;
    uint32_t                refcnt;
    __cilkrts_obj_payload  *payload;
};

// From compiler:
uint32_t __cilkrts_obj_metadata_ini_ready(
    __cilkrts_obj_metadata * meta, uint32_t g);
void __cilkrts_obj_metadata_add_task_read(
    struct __cilkrts_pending_frame *, __cilkrts_obj_metadata *,
    __cilkrts_task_list_node *);
void __cilkrts_obj_metadata_add_task_write(
    struct __cilkrts_pending_frame *, __cilkrts_obj_metadata *,
    __cilkrts_task_list_node *);
void __cilkrts_obj_metadata_add_task(
    struct __cilkrts_pending_frame *, __cilkrts_obj_metadata *,
    __cilkrts_task_list_node *, int group);

void __cilkrts_obj_version_init( __cilkrts_obj_version *v,
				 const __cilkrts_obj_traits *traits );
// __CILKRTS_INLINE
void __cilkrts_obj_version_add_ref( __cilkrts_obj_version *v );
// __CILKRTS_INLINE
void __cilkrts_obj_version_del_ref( __cilkrts_obj_version *v );
void __cilkrts_obj_version_destroy( __cilkrts_obj_version *v );

__CILKRTS_INLINE
void *__cilkrts_obj_payload_get_ptr( __cilkrts_obj_payload *pl ) {
    return (void *)(((char *)pl) + pl->offset);
}


__CILKRTS_END_EXTERN_C

#ifdef __cplusplus

namespace cilk {

template<typename Value>
class versioned;

template<typename Value>
class unversioned;

template<typename Value>
class indep;

template<typename Value>
class outdep;

template<typename Value>
class inoutdep;

template<typename Value>
class object_instance;

template<typename Value>
struct object_payload {
    /** Value type of the object
     */
    typedef Value value_type;

    static value_type & get_value( __cilkrts_obj_payload *pl ) {
	return *get_ptr( pl );
    }
    static const value_type & get_value( const __cilkrts_obj_payload *pl ) {
	return *get_ptr( pl );
    }

private:
    static value_type *get_ptr( __cilkrts_obj_payload *pl ) {
	return reinterpret_cast<value_type *>(
	    __cilkrts_obj_payload_get_ptr( pl ) );
    }
};

template<typename Value, typename = void>
struct object_traits {
    /** Value type of the object
     */
    typedef Value value_type;

    static const /*constexpr*/ /*__STDNS*/ size_t size = sizeof(value_type);

    static void initialize_wrapper( void *payload, void *v ) {
	new (reinterpret_cast<value_type *>(v)) value_type();
    }
    static void destroy_wrapper( void *payload, void *v ) {
	// TODO: reinterpret_cast<value_type *>(v)->~value_type();
    }
    static void* allocate_wrapper( /*__STDNS*/ size_t size ) {
	return operator new(size);
    }
    static void deallocate_wrapper( void *payload ) {
	// In std::allocator terms, this is underlying free()
	// operator delete(payload);
    }
};

#if 0
template<typename Value,
	 typename std::enable_if<std::is_array<Value>::value>::type>
struct object_traits {
    /** Value type of the object
     */
    typedef Value value_type;

    static const /*constexpr*/ /*__STDNS*/ size_t size = sizeof(value_type);

    static void initialize_wrapper( void *payload, void *v ) {
	new (reinterpret_cast<value_type *>(v)) value_type();
    }
    static void destroy_wrapper( void *payload, void *v ) {
	// reinterpret_cast<value_type *>(v)->~value_type();
    }
    static void* allocate_wrapper( /*__STDNS*/ size_t size ) {
	return operator new(size);
    }
    static void deallocate_wrapper( void *payload ) {
	operator delete(payload);
    }
};
#endif


template<typename Value>
class object_version {
public:
    /** Value type of the object
     */
    typedef Value value_type;

    /** Version type of the object
     */
    typedef object_version<value_type> version_type;

private:
    /** Instance type of the object
     */
    typedef object_instance<value_type> instance_type;

    friend class versioned<value_type>;

    __cilkrts_obj_version v;

private:
    // First-create constructor
    object_version( instance_type * obj_, __cilkrts_obj_traits *traits,
		    const value_type &val ) {
	__cilkrts_obj_version_init( &v, traits );
	traits->initialize_fn(
	    v.payload,
	    reinterpret_cast<void *>( const_cast<value_type *>( &val ) ) );
	// printf( "object_version construct: %p meta %p 1\n", this, &v.meta );
    }
    object_version( instance_type * obj_, const value_type &val ) {
	static const __cilkrts_obj_traits traits = {
	    object_traits<value_type>::initialize_wrapper,
	    object_traits<value_type>::destroy_wrapper,
	    object_traits<value_type>::allocate_wrapper,
	    object_traits<value_type>::deallocate_wrapper,
	    object_traits<value_type>::size
	};
	__cilkrts_obj_version_init( &v, &traits );
	traits.initialize_fn(
	    v.payload,
	    reinterpret_cast<void *>( const_cast<value_type *>( &val ) ) );
	// printf( "object_version construct: %p meta %p 2\n", this, &v.meta );
    }
    object_version( instance_type * obj_ ) {
	static const __cilkrts_obj_traits traits = {
	    object_traits<value_type>::initialize_wrapper,
	    object_traits<value_type>::destroy_wrapper,
	    object_traits<value_type>::allocate_wrapper,
	    object_traits<value_type>::deallocate_wrapper,
	    object_traits<value_type>::size
	};
	value_type val;
	__cilkrts_obj_version_init( &v, &traits );
	traits.initialize_fn(
	    v.payload,
	    reinterpret_cast<void *>( const_cast<value_type *>( &val ) ) );
	// printf( "object_version construct: %p meta %p 3\n", this, &v.meta );
    }


    // First-create constructor for unversioned objects
    // obj_version( size_t sz, char * payload_ptr, typeinfo tinfo )
	// : refcnt( 1 ), size( sz ), obj( (obj_instance<object_metadata> *)0 ) {
	// data = std::make_shared<value_type>();
    // }
    // Constructor for nesting
    // obj_version( size_t sz, obj_instance<object_metadata> * obj_,
		 // obj_payload * payload_ )
	// : refcnt( 1 ), size( sz ), payload( payload_ ), obj( obj_ ) {
	// payload->add_ref();
    // }
    inline ~object_version() {
	__cilkrts_obj_version_del_ref( &v );
    }

public:
    // void * get_ptr() { return get_payload()->get_ptr(); }
    // const void * get_ptr() const { return get_payload()->get_ptr(); }

private:
    // obj_instance<object_metadata> * get_instance() const { return obj; }

    // tmp
public:
    __cilkrts_obj_metadata * get_metadata() { return &v.meta; }
    const __cilkrts_obj_metadata * get_metadata() const { return &v.meta; }

private:
    void add_ref() { __sync_fetch_and_add( &v.refcnt, 1 ); } // atomic!
    void del_ref() {
	__CILKRTS_ASSERT( v.refcnt > 0 );
	// Check equality to 1 because we check value before decrement.
	if( __sync_fetch_and_add( &v.refcnt, -1 ) == 1 ) { // atomic!
	    del_ref_delete();
	}
    }
private:
    // Setting noinline helps performance on AMD (Opteron 6100)
    // but not on Intel (Core i7).
    void del_ref_delete(); // __attribute__((noinline));

#if 0
// protected:
public:
    // Optimized del_ref() call for unversioned objects.
    void nonfreeing_del_ref() {
	assert( refcnt > 0 );
	__sync_fetch_and_add( &refcnt, -1 ); // atomic!
	// One may desire to call get_payload()->destruct() when the unversioned
	// goes out of scope (in which case refcnt drops to 0). However, it is
	// equally good to do this unconditionally in the obj_unv_instance
	// destructor. That saves us a conditional here.

	// TODO: with split allocation of metadata and payload, free payload
	// or allocate payload inline in special case also?
    }

public:
    template<typename T>
    obj_version<object_metadata> * rename() {
	OBJ_PROF( rename );
	size_t osize = size;      // Save in case del_ref() frees this
	del_ref();                // The renamed instance no longer points here
	return create<T>( osize, obj );   // Create a clone of ourselves
    }
    // bool is_renamed() const { return obj && obj->get_version() != this; }

    bool is_versionable() const { return obj != 0; }

    // For inout renaming
    void copy_to( obj_version<object_metadata> * dst ) {
	assert( size == dst->size && "Copy of obj_version with != size" );
	memcpy( dst->get_ptr(), get_ptr(), dst->size );
    }
#endif

public:
    const value_type &get_value() const throw() {
	return object_payload<value_type>::get_value( v.payload );
    }
    value_type &get_value() throw() {
	return object_payload<value_type>::get_value( v.payload );
    }

    void set_value(const value_type &val) {
	object_payload<value_type>::get_value( v.payload ) = val;
    }
/*
    void set_value(value_type &&val) {
	object_payload<value_type>::get_value( v.payload ) = std::forward(val);
    }
*/

public:
    void __Cilk_is_dataflow_type(void);
};

template<typename Value>
class object_instance {
    /** Value type of the object
     */
    typedef Value value_type;

    /** Version type of the object
     */
    typedef object_version<value_type> version_type;

    /** Friend classes
     */
    friend class indep<value_type>;
    friend class outdep<value_type>;
    friend class inoutdep<value_type>;

protected:
    version_type *v;

    object_instance( version_type *v ) : v( v ) { }

public: // tmp
    version_type * get_version() { return v; }
    version_type * get_version() const { return v; }

public:
    const value_type &get_value() const throw() { return v->get_value(); }
    value_type &get_value() throw() { return v->get_value(); }

    void set_value(const value_type &v) { v->set_value( v ); }
/*
    void set_value(value_type &&v) { v->set_value( std::forward(v) ); }
*/
};

template<typename Value>
class versioned : public object_instance<Value> {
public:
    /** Value type of the object
     */
    typedef Value value_type;

    /** Version type of the object
     */
    typedef object_version<value_type> version_type;

    /** Instance type of the object
     */
    typedef object_instance<value_type> instance_type;

private:
    versioned( version_type * _v ) throw() : instance_type( _v ) { }

public:
    versioned( const value_type & _v )
	: instance_type( new version_type(this, _v) ) { }

    versioned()
	: instance_type( new version_type(this) ) { }
    // versioned( value_type && _v )
	// : instance_type( new version_type(std::forward<value_type>(_v)) ) { }
/*
    template<typename ValueTy,
	     typename = typename std::enable_if<std::is_integral<ValueTy>::value
						&& std::is_same<ValueTy, Value>::value>::type>
    versioned()
	: instance_type( new version_type(this, ValueTy()) ) { }
*/

    /** Obtain an indep specifier for the versioned object.
     */
    indep<value_type> get_indep() const { return indep<value_type>(*this); }
    operator indep<value_type> () const { return get_indep(); }

    /** Obtain an outdep specifier for the versioned object.
     */
    outdep<value_type> get_outdep() const { return outdep<value_type>(*this); }
    operator outdep<value_type>  () const { return get_outdep(); }

    /** Obtain an inoutdep specifier for the versioned object.
     */
    inoutdep<value_type> get_inoutdep() const { return inoutdep<value_type>(*this); }
    operator inoutdep<value_type>    () const { return get_inoutdep(); }

    /** Signature function to assertain this is a versioned object.
     */
    void __Cilk_is_dataflow_type(void);

    /** Signature function to check for properties of versioned object.
     */
    void __Cilk_is_dataflow_indep_type(void);
};


template<typename Value>
class indep : public object_instance<Value> {
public:
    /** Value type of the object
     */
    typedef Value value_type;

    /** Version type of the object
     */
    typedef object_version<value_type> version_type;

    /** Instance type of the object
     */
    typedef object_instance<value_type> instance_type;

private:
    friend class object_version<value_type>; // version_type;

private:
    indep( version_type * v ) throw() : instance_type( v ) { }

public:
    indep( const versioned<value_type> & v_ ) throw()
	: indep( v_.get_version() ) { }

    /** Signature function to assertain this is a versioned object.
     */
    void __Cilk_is_dataflow_type(void);

    /** Signature function to check for properties of versioned object.
     */
    void __Cilk_is_dataflow_indep_type(void);
};

template<typename Value>
class outdep : public object_instance<Value> {
public:
    /** Value type of the object
     */
    typedef Value value_type;

    /** Version type of the object
     */
    typedef object_version<value_type> version_type;

    /** Instance type of the object
     */
    typedef object_instance<value_type> instance_type;

private:
    friend class object_version<value_type>; // version_type;

private:
    outdep( version_type * _v ) throw() : instance_type( _v ) { }

public:
    outdep( const versioned<value_type> & v ) throw()
	: outdep( v.get_version() ) { }

    /** Signature function to assertain this is a versioned object.
     */
    void __Cilk_is_dataflow_type(void);

    /** Signature function to check for properties of versioned object.
     */
    void __Cilk_is_dataflow_outdep_type(void);
};

template<typename Value>
class inoutdep : public object_instance<Value> {
public:
    /** Value type of the object
     */
    typedef Value value_type;

    /** Version type of the object
     */
    typedef object_version<value_type> version_type;

    /** Instance type of the object
     */
    typedef object_instance<value_type> instance_type;

private:
    friend class object_version<value_type>; // version_type;

private:
    inoutdep( version_type * v ) throw() : instance_type( v ) { }

public:
    inoutdep( const versioned<value_type> & v_ ) throw()
	: inoutdep( v_.get_version() ) {
	// printf( "inoutdep create this=%p w/ v=%p from %p\n", this, v_.get_version(), &v_ );
	assert( (intptr_t(instance_type::get_version()) & intptr_t(1)) == 0 );
    }

    /** Signature function to assertain this is a versioned object.
     */
    void __Cilk_is_dataflow_type(void);

    /** Signature function to check for properties of versioned object.
     */
    void __Cilk_is_dataflow_inoutdep_type(void);
};

}

#endif // __cplusplus

#endif // DATAFLOW_H_INCLUDED
