#pragma once

#include <variant>
#include <coroutine>
#include <type_traits>
#include <chrono>
#include <stop_token>
#include <functional>

namespace Promise {
    struct Void {
        std::variant<std::monostate, std::exception_ptr> internal;

        void return_void() const {
            if (std::holds_alternative<std::exception_ptr>(this->internal)) {
                throw std::get<std::exception_ptr>(this->internal);
            }
        }

        void unhandled_exception() noexcept {
            this->internal = std::current_exception();
        }
    };


    template<typename U>
    struct Type {
        U &&value() {
            if (std::holds_alternative<U>(this->internal)) {
                return std::move(std::get<U>(this->internal));
            }
            if (std::holds_alternative<std::exception_ptr>(this->internal)) {
                throw std::get<std::exception_ptr>(this->internal);
            }
            throw std::exception("not return value");

        }

        void return_value(U val) {
            this->internal = std::move(val);
        }

        void unhandled_exception() noexcept {
            this->internal = std::current_exception();
        }

        std::variant<std::monostate, U, std::exception_ptr> internal;
    };
}
namespace Task {
    template<typename T = void>
    struct Task {
        struct promise_type : std::conditional_t<std::is_same_v<T, void>, Promise::Void, Promise::Type<T>> {
            auto get_return_object() {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this), *this};
            }

            static std::suspend_always initial_suspend() noexcept {
                return {};
            }

            static std::suspend_always final_suspend() noexcept {
                return {};
            }

            Task<T> *pTask{nullptr};
        };

        bool resume() {
            // Already done
            if (this->handler.done()) return true;

            if (!this->cb) { // No callback, simply resume
                this->handler.resume();
            } else { // What we are waiting on
                if (!this->cb()) return false;
                this->cb = nullptr;
            }

            return this->handler.done();
        }

        bool await_ready() {
            return this->handler.done();
        }

        void await_suspend(auto h) {
            h.promise().pTask->cb = [t = this->handler.promise().pTask]() {
                return t->resume();
            };
        }

        auto await_resume() {
            // Deal with void task's
            if constexpr (std::is_same_v<T, void>) {
                return;
            } else {
                return std::move(handler.promise().value());
            }
        }

        Task(std::coroutine_handle<promise_type> &&handle, promise_type &promise) :
                handler{std::move(handle)},
                pPromise{&promise} {
            pPromise->pTask = this;
        }

        Task(Task &&t) noexcept:
                cb{std::move(t.cb)} {
            std::swap(this->handler, t.handler);
            std::swap(this->pPromise, t.pPromise);
            this->pPromise->pTask = this;
        }

        Task &operator=(Task &&t) {
            std::swap(this->handler, t.handler);
            std::swap(this->pPromise, t.pPromise);
            this->cb = std::move(t.cb);
            this->pPromise->pTask = this;
        }

        Task(const Task &) = delete;

        Task &operator=(const Task &) = delete;

        ~Task() {
            if (this->handler) this->handler.destroy();
        }

        std::coroutine_handle<promise_type> handler;
        std::function<bool()> cb{nullptr};
        promise_type *pPromise;
    };

    static Task<> delay(const std::chrono::milliseconds &time, std::stop_token token = {}) {
        auto end_time = time + std::chrono::system_clock::now();
        while (!token.stop_requested() && end_time > std::chrono::system_clock::now()) {
            co_await std::suspend_always{};
        }
    }

    static Task<> skipFrames(const uint32_t frames = 0, std::stop_token token = {}) {
        // Just wait a single frame, if its given 0, 1
        if (frames < 2) {
            co_await std::suspend_always{};
            co_return ;
        }

        uint32_t count = 0;
        while (!token.stop_requested() && count++ < frames) {
            co_await std::suspend_always{};
        }
    }

    static inline Task<> skipFrame(std::stop_token token = {}) {
        return skipFrames(1);
    }
}