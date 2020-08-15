// Definition of index list of input features
#pragma once

#include "../../Position.h"
#include "../nnue_architecture.h"

namespace Evaluator::NNUE::Features {

    // Class template used for feature index list
    template<typename T, size_t MaxSize>
    class ValueList {
    
    private:
        T _values[MaxSize];
        size_t _size{ 0 };

    public:
        size_t size() const { return _size; }
        void resize(size_t size) { _size = size; }
        void push_back(T const &value) { _values[_size++] = value; }
        T &operator[](size_t index) { return _values[index]; }
        T *begin() { return _values; }
        T *end() { return _values + _size; }
        T const &operator[](size_t index) const { return _values[index]; }
        T const *begin() const { return _values; }
        T const *end() const { return _values + _size; }

        void swap(ValueList &valueList) {
            size_t const maxSize{ std::max(_size, valueList._size) };
            for (size_t i = 0; i < maxSize; ++i) {
                std::swap(_values[i], valueList._values[i]);
            }
            std::swap(_size, valueList._size);
        }
    };

    //Type of feature index list
    class IndexList
        : public ValueList<IndexType, RawFeatures::kMaxActiveDimensions>
    {};

}  // namespace Evaluator::NNUE::Features
