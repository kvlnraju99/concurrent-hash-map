# Concurrent Hash Map --- Day Plan README

## Goal

Implement a **basic concurrent hash map in C++** with correct
functionality and thread safety using bucket-level locking.

------------------------------------------------------------------------

## Scope

### Included

-   Buckets
-   Hashing using modulo
-   Chaining (vector of key-value pairs)
-   Operations:
    -   put
    -   get
    -   remove
-   One mutex per bucket
-   Multi-thread testing

### Excluded

-   Resizing
-   shared_mutex
-   Lock-free techniques
-   Optimizations

------------------------------------------------------------------------

## Step-by-Step Plan

### 1. Build the Base Structure

-   Define `Bucket` struct
-   Define `ConcurrentHashMap` class
-   Create `vector<Bucket>` to store buckets

------------------------------------------------------------------------

### 2. Implement Basic Logic

-   Compute hash of key
-   Map key to bucket using:
    -   `index = hash(key) % bucket_count`
-   Store key-value pairs in bucket list

------------------------------------------------------------------------

### 3. Implement Operations

Implement: - `put(key, value)` - `get(key)` - `remove(key)`

Focus only on correctness (no concurrency yet).

------------------------------------------------------------------------

### 4. Add Concurrency

-   Add `std::mutex` inside each bucket
-   Lock the bucket before accessing/modifying data
-   Use:
    -   `std::lock_guard<std::mutex>` for automatic unlocking

------------------------------------------------------------------------

### 5. Testing

#### Single-thread tests

-   Insert values
-   Retrieve values
-   Update values
-   Remove values

#### Multi-thread tests

-   Multiple `put` operations
-   Multiple `get` operations
-   Multiple `remove` operations
-   Mixed operations across threads

------------------------------------------------------------------------

### 6. Final Cleanup

-   Ensure no crashes
-   Verify correctness of outputs
-   Keep code simple and readable

------------------------------------------------------------------------

## Expected Outcome

By the end: - A working concurrent hash map - Thread-safe operations -
Multi-thread test validation - Clean and understandable implementation

------------------------------------------------------------------------

## One-Line Summary

Build a concurrent hash map using bucket-level locking to allow safe
parallel access by multiple threads.
