#pragma once

#include <QtGlobal>

#include <functional>
#include <utility>

// A lazy holder — think std::shared_ptr that constructs its T only on the FIRST dereference.
// Call sites read like a pointer (`*capsule`, `capsule->x`, `capsule.get()`), but the object
// behind it is built on demand, so a backend whose consumer never appears is never created.
//
// This is what lets the bar pay only for the applets in the config while keeping the QML side
// none the wiser: whether a model is created eagerly (`new T`) or lazily (`Capsule<T>`), the
// QML that consumes it is identical — it just receives the object once it's handed over.
//
// On first construction it logs (qInfo, so `-v`) with its label, so you can watch exactly
// which applet/popup backends come alive and when.
//
// Construction is single-shot and cached. GUI-thread use only (not synchronised).
template <typename T>
class Capsule {
public:
    using Factory = std::function<T *()>;

    Capsule(const char *label, Factory factory)
        : m_label(label)
        , m_factory(std::move(factory))
    {
    }

    T *get()
    {
        if (m_ptr == nullptr) {
            qInfo("Capsule: instantiating %s (first use)", m_label);
            m_ptr = m_factory();
        }
        return m_ptr;
    }

    T *operator->() { return get(); }
    T &operator*() { return *get(); }

    // Whether the object has actually been built yet (i.e. someone dereferenced the capsule).
    bool constructed() const { return m_ptr != nullptr; }

private:
    const char *m_label;
    Factory m_factory;
    T *m_ptr = nullptr;
};
