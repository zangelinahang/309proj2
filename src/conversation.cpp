#include "core/conversation.h"

#include <limits>
#include <stdexcept>
#include <utility>

Conversation::Conversation() = default;

Conversation::~Conversation() {
    delete[] data_;
}

Conversation::Conversation(const Conversation& other)
    : data_(nullptr), size_(0), capacity_(0) {
    if (other.capacity_ == 0) {
        return;
    }

    Message* copied = new Message[other.capacity_];
    try {
        for (std::size_t i = 0; i < other.size_; ++i) {
            copied[i] = other.data_[i];
        }
    } catch (...) {
        delete[] copied;
        throw;
    }

    data_ = copied;
    size_ = other.size_;
    capacity_ = other.capacity_;
}

Conversation& Conversation::operator=(const Conversation& other) {
    if (this == &other) {
        return *this;
    }

    Conversation copy(other);
    swap(copy);
    return *this;
}

Conversation::Conversation(Conversation&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

Conversation& Conversation::operator=(Conversation&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    delete[] data_;

    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;

    return *this;
}

void Conversation::append(Message m) {
    if (size_ == capacity_) {
        if (capacity_ > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("Conversation capacity overflow");
        }

        const std::size_t new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;
        Message* grown = new Message[new_capacity];

        for (std::size_t i = 0; i < size_; ++i) {
            grown[i] = std::move(data_[i]);
        }

        delete[] data_;
        data_ = grown;
        capacity_ = new_capacity;
    }

    data_[size_] = std::move(m);
    ++size_;
}

std::size_t Conversation::size() const noexcept {
    return size_;
}

const Message& Conversation::at(std::size_t i) const {
    if (i >= size_) {
        throw std::out_of_range("Conversation::at index out of range");
    }
    return data_[i];
}

const Message* Conversation::begin() const noexcept {
    return data_;
}

const Message* Conversation::end() const noexcept {
    return data_ == nullptr ? nullptr : data_ + size_;
}

void Conversation::swap(Conversation& other) noexcept {
    using std::swap;
    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
}
