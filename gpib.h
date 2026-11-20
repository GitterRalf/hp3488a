#pragma once

#include <QByteArray>
#include <memory>

struct GpibTarget
{
    int board = 0;
    int address = 7;
};

class GpibSession
{
public:
    virtual ~GpibSession() = default;
    virtual void local() = 0;
    virtual void write(const QByteArray &command) = 0;
    virtual QByteArray read(bool waitForReply = true) = 0;
    virtual unsigned char status() = 0;
};

std::unique_ptr<GpibSession> openGpib(GpibTarget target);
