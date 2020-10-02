// Definition of index list of input features
#pragma once

#include "../../position.h"
#include "../architecture.h"

namespace Evaluator::NNUE::Features {

    // Class template used for feature index list
    template<typename T, size_t N>
    class ValueList {

    public:

        size_t size() const {
            return _size;
        }
        void resize(size_t const sz) {
            _size = sz;
        }
        void push_back(T const &value) {
            values[_size++] = value;
        }

        T& operator[](size_t const index) {
            assert(0 <= index || index < _size);
            return values[index];
        }
        //T& operator[](size_t const index) throw (const char*) {
        //    if (0 > index || index >= _size) throw "Invalid array access";
        //    return values[index];
        //}

        T* begin() {
            return values;
        }
        T* end() {
            return values + _size;
        }

        T const& operator[](size_t const index) const {
            return values[index];
        }

        T const* begin() const {
            return values;
        }
        T const* end() const {
            return values + _size;
        }

        void swap(ValueList &valueList) {

            size_t const maxSize{ std::max(_size, valueList._size) };
            for (size_t i = 0; i < maxSize; ++i) {
                std::swap(values[i], valueList.values[i]);
            }
            std::swap(_size, valueList._size);
        }

    private:
        T      values[N];
        size_t _size{ 0 };
    };

    //Type of feature index list
    class IndexList
        : public ValueList<IndexType, RawFeatures::MaxActiveDimensions> {
    public:
    private:
    };

}  // namespace Evaluator::NNUE::Features
