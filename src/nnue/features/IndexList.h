// Definition of index list of input features
#pragma once

#include "../../Position.h"
#include "../Architecture.h"

namespace Evaluator::NNUE::Features {

    // Class template used for feature index list
    template<typename T, size_t N>
    class ValueList {

    public:

        size_t size() const {
            return size_;
        }
        void resize(size_t size) {
            size_ = size;
        }
        void push_back(T const &value) {
            values[size_++] = value;
        }

        T &operator[](size_t index) {
            return values[index];
        }

        T* begin() {
            return values;
        }
        T* end() {
            return values + size_;
        }

        T const& operator[](size_t index) const {
            return values[index];
        }

        T const* begin() const {
            return values;
        }
        T const* end() const {
            return values + size_;
        }

        void swap(ValueList &valueList) {

            size_t const maxSize{ std::max(size_, valueList.size_) };
            for (size_t i = 0; i < maxSize; ++i) {
                std::swap(values[i], valueList.values[i]);
            }
            std::swap(size_, valueList.size_);
        }

    private:
        T      values[N];
        size_t size_{ 0 };

    };

    //Type of feature index list
    class IndexList
        : public ValueList<IndexType, RawFeatures::MaxActiveDimensions>
    {
    public:

    private:
    };

}  // namespace Evaluator::NNUE::Features
