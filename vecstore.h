// A quick and dirty unified vector serialization/deserialization (C) Dmitry 'sciloaf' Solovyev, 2024
#pragma once

#include <vector>
#include <string>
#include <deque>
#include <stdint.h>
#include <stdio.h>

template <class T> class vector_storage {
public:
    static size_t store(const std::vector<T> & vec, FILE* f) {
        uint64_t sz = vec.size();
        size_t r = fwrite(&sz,sizeof(sz),1,f) * sizeof(uint64_t);
        r += fwrite(vec.data(),sizeof(T),sz,f) * sizeof(T);
        return r;
    }

    static void* store(const std::vector<T> & vec, void* mem) {
        if (!mem) return nullptr;
        uint64_t* sptr = reinterpret_cast<uint64_t*> (mem);
        *sptr = vec.size();
        T* ptr = reinterpret_cast<T*> (sptr+1);
        for (auto & i: vec) *ptr++ = i;
        return ptr;
    }

    static std::vector<T> load(FILE* f) {
        std::vector<T> vec;
        uint64_t sz;
        if (fread(&sz,sizeof(sz),1,f)) {
            vec.resize(sz);
            if (fread(vec.data(),sizeof(T),sz,f) != sz)
                vec.clear();
        }
        return vec;
    }

    static std::vector<T> load(void** mem) {
        std::vector<T> vec;
        if ((!mem) || !(*mem)) return vec;
        uint64_t* sptr = reinterpret_cast<uint64_t*> (*mem);
        vec.resize(*sptr);
        T* ptr = reinterpret_cast<T*> (sptr+1);
        memcpy(vec.data(),ptr,sizeof(T) * (*sptr));
        *mem = ptr + (*sptr);
        return vec;
    }

    static std::vector<T> from_string(const std::string & str) {
        std::vector<T> r;
        for (auto & i: str) r.push_back(i); // implicitly casted, hopefully ok ;)
        return r;
    }

    static std::string to_string(const std::vector<T> & vec) {
        std::string r;
        for (auto & i: vec) r.push_back(i);
        return r;
    }

    static std::vector<T> from_deque(const std::deque<T> & deq) {
        std::vector<T> r;
        for (auto & i: deq) r.push_back(i);
        return r;
    }

    static std::deque<T> to_deque(const std::vector<T> & vec) {
        std::deque<T> r;
        for (auto & i: vec) r.push_back(i);
        return r;
    }

    static size_t size(const std::vector<T> & vec) {
        return vec.size() * sizeof(T) + sizeof(uint64_t);
    }

    static size_t size(const std::string & str) {
        return str.size() * sizeof(T) + sizeof(uint64_t);
    }

    static size_t size(const std::deque<T> & deq) {
        return deq.size() * sizeof(T) + sizeof(uint64_t);
    }
};
