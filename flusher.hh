/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef FLUSHER_H
#define FLUSHER_H 1

#include "common.hh"
#include "ep.hh"

class Flusher {
public:
    Flusher(EventuallyPersistentStore *st):
        store(st), running(true), hasInitialized(false) {
    }
    ~Flusher() {
        stop();
    }

    void stop();

    void initialize();

    void run(void);
private:
    EventuallyPersistentStore *store;
    volatile bool running;
    bool hasInitialized;

    int doFlush(bool shouldWait);

    DISALLOW_COPY_AND_ASSIGN(Flusher);
};

#endif /* FLUSHER_H */
