// Definition of index list of input features
#pragma once

#include "../../Position.h"
#include "../nnue_architecture.h"

namespace Evaluator::NNUE::Features {

    // Class template used for feature index list
    template <typename T, size_t MaxSize>
    class ValueList {

    public:
        size_t size() const { return size_; }
        void resize(size_t size) { size_ = size; }
        void push_back(T const &value) { values_[size_++] = value; }
        T &operator[](size_t index) { return values_[index]; }
        T *begin() { return values_; }
        T *end() { return values_ + size_; }
        T const &operator[](size_t index) const { return values_[index]; }
        T const *begin() const { return values_; }
        T const *end() const { return values_ + size_; }

        void swap(ValueList &other) {
            const size_t max_size = std::max(size_, other.size_);
            for (size_t i = 0; i < max_size; ++i) {
                std::swap(values_[i], other.values_[i]);
            }
            std::swap(size_, other.size_);
        }

    private:
        T values_[MaxSize];
        size_t size_ = 0;
    };

    //Type of feature index list
    class IndexList
        : public ValueList<IndexType, RawFeatures::kMaxActiveDimensions>
    {};

}  // namespace Evaluator::NNUE::Features
