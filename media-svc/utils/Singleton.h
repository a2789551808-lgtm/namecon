#pragma once
#include <memory>

// 通用单例模板 — 任何类继承即可获得单例能力
//
// 用法:
//   class Logger : public Singleton<Logger> {
//       friend class Singleton<Logger>;  // 允许 Singleton 调私有构造
//   private:
//       Logger() = default;
//   };
//   auto log = Logger::GetInstance();
//
template <typename T>
class Singleton {
protected:
    Singleton()  = default;
    ~Singleton() = default;

    // 禁止拷贝
    Singleton(const Singleton&)            = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&)                 = delete;
    Singleton& operator=(Singleton&&)      = delete;

public:
    static std::shared_ptr<T> GetInstance() {
        //C++11 保证线程安全
        static std::shared_ptr<T> instance(new T);
        return instance;
    }
};
