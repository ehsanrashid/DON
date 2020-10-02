#pragma once
// Definition of index list of input features

#include "../../position.h"
#include "../architecture.h"

namespace Evaluator::NNUE::Features {

    // Class template used for feature index list
    template<typename T, size_t N>
    class ValueList {

    public:

        size_t size() const { return size_; }

        T const* begin() const { return values; }
        T const* end()   const { return values + size(); }
        T const &operator[](size_t const index) const { return values[index]; }

        T*       begin() { return values; }
        T*       end()   { return values + size(); }
        T&       operator[](size_t const index) { assert(0 <= index || index < size()); return values[index]; }

        //T&       operator[](size_t const index) throw (char const*) { 
        //    if (0 > index || index >= size()) {
        //        throw "Invalid array access";
        //    }
        //    return values[index];
        //}

        void resize(size_t const sz) { size_ = sz; }
        void push_back(T const &value) { values[size_++] = value; }

        void swap(ValueList &valueList) {

            size_t const maxSize{ std::max(size(), valueList.size()) };
            for (size_t i = 0; i < maxSize; ++i) {
                std::swap(values[i], valueList.values[i]);
            }
            std::swap(size(), valueList.size());
        }

    private:

        T      values[N];
        size_t size_{ 0 };
    };

    //Type of feature index list
    class IndexList
        : public ValueList<IndexType, RawFeatures::MaxActiveDimensions> {

    };

}
