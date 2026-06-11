#pragma once

// 通用单例模板 — 任何类继承即可获得单例能力（Meyer's Singleton）
//
// 用法:
//   class Logger : public Singleton<Logger> {
//       friend class Singleton<Logger>;  // 允许 Singleton 调私有构造
//   private:
//       Logger() = default;
//   };
//   auto& log = Logger::GetInstance();
//
template <typename T>
class Singleton {
protected:
    Singleton()  = default;
    ~Singleton() = default;

    Singleton(const Singleton&)            = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&)                 = delete;
    Singleton& operator=(Singleton&&)      = delete;

public:
    static T& GetInstance() {
        static T instance;  // C++11 保证 static 局部变量初始化的线程安全
        return instance;
    }
};
