#ifndef DRIVER_H
#define DRIVER_H

class Driver {
public:
    virtual void init() = 0;
    virtual ~Driver() {}
};

#endif